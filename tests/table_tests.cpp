#include "ui/table.hpp"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
int checks = 0, selection_notifications = 0, redraw_changes = 0, count_changes = 0;
LRESULT CALLBACK observe_list(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR, DWORD_PTR) {
    if (message == WM_SETREDRAW)
        ++redraw_changes;
    if (message == LVM_SETITEMCOUNT)
        ++count_changes;
    return DefSubclassProc(hwnd, message, w, l);
}
void check(bool value, const char *message) {
    ++checks;
    if (!value)
        throw std::runtime_error(message);
}
LRESULT CALLBACK proc(HWND window, UINT message, WPARAM w, LPARAM l) {
    auto *table = reinterpret_cast<nm::ui::TableView *>(GetWindowLongPtrW(window, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        table =
            static_cast<nm::ui::TableView *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(table));
    }
    if (message == WM_NOTIFY && reinterpret_cast<NMHDR *>(l)->code == LVN_ITEMCHANGED)
        ++selection_notifications;
    if (message == WM_NOTIFY && table) {
        LRESULT result = 0;
        if (table->on_notify(reinterpret_cast<NMHDR *>(l), result))
            return result;
    }
    return DefWindowProcW(window, message, w, l);
}
nm::Json record(int i) {
    nm::Event event;
    event.id = "row-" + std::to_string(i);
    event.pid = static_cast<std::uint32_t>(i);
    event.app = "C:\\Test\\client.exe";
    event.time = 1700000000000;
    event.remote_ip = "2001:db8::1";
    event.remote_port = 443;
    event.classification = "unknown";
    event.reason = "Review test";
    nm::Json result = event;
    result["country"] = "Documentation";
    return result;
}
} // namespace
int main() {
    HWND parent = nullptr;
    try {
        INITCOMMONCONTROLSEX controls{sizeof(controls),
                                      ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
        check(InitCommonControlsEx(&controls) != FALSE, "Common controls initialization");
        nm::ui::TableView table;
        WNDCLASSW cls{};
        cls.hInstance = GetModuleHandleW(nullptr);
        cls.lpfnWndProc = proc;
        cls.lpszClassName = L"NetMonTableTests";
        check(RegisterClassW(&cls) != 0, "Register test window");
        parent = CreateWindowW(cls.lpszClassName, L"NetMon table integration test", WS_OVERLAPPED,
                               0, 0, 900, 600, nullptr, nullptr, cls.hInstance, &table);
        check(parent != nullptr, "Create hidden test host");
        // These controls exist only inside this test process. No user app is automated.
        HWND list = CreateWindowW(
            WC_LISTVIEWW, L"", WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL,
            0, 0, 880, 300, parent, nullptr, cls.hInstance, nullptr);
        HWND details = CreateWindowW(L"EDIT", L"", WS_CHILD | ES_MULTILINE, 0, 310, 880, 250,
                                     parent, nullptr, cls.hInstance, nullptr);
        check(list && details, "Create table controls");
        check(SetWindowSubclass(list, observe_list, 1, 0) != FALSE,
              "Observe native list mutations");
        for (int i = 0; i < 7; ++i) {
            LVCOLUMNW col{};
            col.mask = LVCF_TEXT | LVCF_WIDTH;
            col.pszText = const_cast<wchar_t *>(L"Column");
            col.cx = 140;
            ListView_InsertColumn(list, i, &col);
        }
        table.attach(list, details);
        nm::Json data = nm::Json::array();
        for (int i = 0; i < 200; ++i)
            data.push_back(record(i));
        auto snapshot = nm::ui::prepare_table(data, false);
        check(snapshot.display[0].cells[2] == L"[2001:db8::1]:443", "IPv6 endpoint formatting");
        check(table.set_snapshot(std::move(snapshot)), "Initial snapshot not applied");
        check(ListView_GetItemCount(list) == 200, "Virtual row count");
        ListView_SetItemState(list, 4, LVIS_SELECTED | LVIS_FOCUSED, LVIS_SELECTED | LVIS_FOCUSED);
        check(table.selected() == 4, "Selection unavailable");
        wchar_t detail_text[2048]{};
        GetWindowTextW(details, detail_text, 2048);
        check(std::wstring(detail_text).find(L"PID: 4") != std::wstring::npos,
              "Selection details not updated");
        const auto unchanged_writes = table.detail_writes(), unchanged_updates = table.updates();
        check(!table.set_snapshot(nm::ui::prepare_table(data, false)),
              "Unchanged rows were repainted");
        check(table.detail_writes() == unchanged_writes && table.updates() == unchanged_updates,
              "Unchanged refresh triggered UI work");
        const auto before_selection_events = selection_notifications;
        data[50]["reason"] = "Changed unrelated row";
        table.set_snapshot(nm::ui::prepare_table(data, false));
        check(selection_notifications == before_selection_events && table.selected() == 4,
              "A cell-only update reset the native selection");
        check(table.detail_writes() == unchanged_writes,
              "An unrelated row update rewrote selected details");
        ListView_Scroll(list, 0, 600);
        const auto old_top = ListView_GetTopIndex(list);
        check(old_top > 0, "Native table did not scroll");
        const auto top_id = table.records()[static_cast<std::size_t>(old_top)].at("id");
        const auto counts_before = count_changes, redraws_before = redraw_changes;
        data.insert(data.begin(), record(200));
        data.erase(data.end() - 1);
        table.set_snapshot(nm::ui::prepare_table(data, false));
        check(count_changes == counts_before,
              "A row replacement reset the unchanged native item count");
        check(redraw_changes == redraws_before,
              "A refresh toggled native visibility via WM_SETREDRAW");
        check(table.selected() == 5 && table.records()[5].at("id") == "row-4",
              "Refresh did not preserve selected identity");
        check(table.detail_writes() == unchanged_writes,
              "Refresh unnecessarily rewrote unchanged details");
        check(table.records()[static_cast<std::size_t>(ListView_GetTopIndex(list))].at("id") ==
                  top_id,
              "Refresh did not preserve scroll anchor");
        NMLVDISPINFOW info{};
        info.hdr.hwndFrom = list;
        info.hdr.code = LVN_GETDISPINFOW;
        info.item.mask = LVIF_TEXT;
        info.item.iItem = 5;
        info.item.iSubItem = 3;
        LRESULT result = 0;
        check(table.on_notify(&info.hdr, result) &&
                  std::wstring(info.item.pszText) == L"Documentation",
              "Virtual display callback");
        const auto before = table.detail_writes();
        std::vector<double> samples;
        for (int i = 0; i < 1000; ++i) {
            const auto start = std::chrono::steady_clock::now();
            ListView_SetItemState(list, i % 200, LVIS_SELECTED | LVIS_FOCUSED,
                                  LVIS_SELECTED | LVIS_FOCUSED);
            samples.push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start)
                    .count());
        }
        check(table.detail_writes() - before == 1000, "Selection caused duplicate detail updates");
        std::sort(samples.begin(), samples.end());
        const auto selected_id =
            table.records()[static_cast<std::size_t>(table.selected())].at("id");
        data.erase(std::remove_if(data.begin(), data.end(),
                                  [&](const auto &row) { return row.at("id") == selected_id; }),
                   data.end());
        table.set_snapshot(nm::ui::prepare_table(data, false));
        check(table.selected() == -1, "Expired selected row left an incorrect selection");
        GetWindowTextW(details, detail_text, 2048);
        check(std::wstring(detail_text).find(L"Select a connection") != std::wstring::npos,
              "Expired selection kept stale details");
        table.set_snapshot(nm::ui::prepare_table(nm::Json::array(), true));
        check(ListView_GetItemCount(list) == 0, "Changing to empty rules did not clear rows");
        DestroyWindow(parent);
        parent = nullptr;
        std::cout
            << nm::Json{{"passed", true},
                        {"checks", checks},
                        {"selection_operations", 1000},
                        {"selection_p95_ms", samples[949]},
                        {"selection_max_ms", samples.back()},
                        {"benchmark_scope",
                         "Hidden native control; excludes desktop painting and input delivery"}}
                   .dump(2)
            << "\n";
        return 0;
    } catch (const std::exception &ex) {
        if (parent)
            DestroyWindow(parent);
        std::cerr << "FAILED after " << checks << " checks: " << ex.what() << "\n";
        return 1;
    }
}
