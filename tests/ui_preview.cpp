// Render only this test process's own production controls, using synthetic records.
#define wWinMain netmon_unused_entry
#include "../src/ui/main.cpp"
#undef wWinMain
#include <iostream>
namespace {
bool ready = false;
std::filesystem::path output;
int stage = 0, checks = 0;
bool failed = false;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
    ++checks;
}
void exercise_map() {
    const auto map = world_view.window();
    const auto countries = GetDlgItem(map, 1), endpoints = GetDlgItem(map, 2);
    check(SendMessageW(countries, CB_GETCOUNT, 0, 0) == 5, "Five country groups expected");
    SendMessageW(countries, CB_SHOWDROPDOWN, TRUE, 0);
    check(SendMessageW(countries, CB_GETDROPPEDSTATE, 0, 0) != 0, "Country dropdown opens");
    auto changed = std::make_shared<nm::ui::TableSnapshot>(*table.snapshot());
    std::reverse(changed->map.groups.begin(), changed->map.groups.end());
    world_view.set_snapshot(changed);
    check(SendMessageW(countries, CB_GETDROPPEDSTATE, 0, 0) != 0 &&
              SendMessageW(countries, CB_GETCURSEL, 0, 0) == 0,
          "Live refresh leaves the open country chooser stable");
    SendMessageW(countries, CB_SHOWDROPDOWN, FALSE, 0);
    SendMessageW(map, WM_APP + 1, 0, 0);
    world_view.set_snapshot(table.snapshot());
    SendMessageW(countries, CB_SETCURSEL, 1, 0);
    SendMessageW(map, WM_COMMAND, MAKEWPARAM(1, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(countries));
    check(SendMessageW(endpoints, LB_GETCOUNT, 0, 0) == 2, "Country selection updates IPs");
    SendMessageW(endpoints, LB_SETCURSEL, 1, 0);
    SendMessageW(map, WM_COMMAND, MAKEWPARAM(2, LBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(endpoints));
    world_view.set_snapshot(table.snapshot());
    check(SendMessageW(endpoints, LB_GETCURSEL, 0, 0) == 1, "IP selection survives refresh");
    SendMessageW(map, WM_KEYDOWN, VK_RIGHT, 0);
    check(SendMessageW(countries, CB_GETCURSEL, 0, 0) == 2, "Keyboard country selection");
    for (int i = 0; i < 8; ++i)
        SendMessageW(GetDlgItem(map, 4), BM_CLICK, 0, 0);
    SendMessageW(map, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(200, 180));
    SendMessageW(map, WM_MOUSEMOVE, MK_LBUTTON, MAKELPARAM(250, 200));
    SendMessageW(map, WM_LBUTTONUP, 0, MAKELPARAM(250, 200));
    SendMessageW(GetDlgItem(map, 3), BM_CLICK, 0, 0);
    SendMessageW(GetDlgItem(map, 5), BM_CLICK, 0, 0);
    SendMessageW(map, WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), 0);
    SendMessageW(GetDlgItem(map, 5), BM_CLICK, 0, 0);
    SendMessageW(countries, CB_SETCURSEL, 0, 0);
    SendMessageW(map, WM_COMMAND, MAKEWPARAM(1, CBN_SELCHANGE),
                 reinterpret_cast<LPARAM>(countries));
    check(SendMessageW(endpoints, LB_GETCOUNT, 0, 0) == 5, "Reset retains country data");
}
SettingsDialog settings_data{};
HWND settings_window = nullptr;
void capture(HWND hwnd, const std::wstring &name) {
    RECT r{};
    GetClientRect(hwnd, &r);
    auto dc = GetDC(hwnd), memory = CreateCompatibleDC(dc);
    auto bitmap = CreateCompatibleBitmap(dc, r.right, r.bottom);
    auto old = SelectObject(memory, bitmap);
    SendMessageW(hwnd, WM_PRINTCLIENT, reinterpret_cast<WPARAM>(memory), PRF_CLIENT);
    // Top-level WM_PRINT offsets native children from the nonclient frame. Render
    // this test window's children at their actual client positions instead.
    for (auto child = GetWindow(hwnd, GW_CHILD); child; child = GetWindow(child, GW_HWNDNEXT)) {
        if (!(GetWindowLongPtrW(child, GWL_STYLE) & WS_VISIBLE))
            continue;
        RECT child_rect{};
        GetWindowRect(child, &child_rect);
        MapWindowPoints(nullptr, hwnd, reinterpret_cast<POINT *>(&child_rect), 2);
        const auto saved = SaveDC(memory);
        SetViewportOrgEx(memory, child_rect.left, child_rect.top, nullptr);
        IntersectClipRect(memory, 0, 0, child_rect.right - child_rect.left,
                          child_rect.bottom - child_rect.top);
        SendMessageW(child, WM_PRINT, reinterpret_cast<WPARAM>(memory),
                     PRF_CLIENT | PRF_NONCLIENT | PRF_CHILDREN | PRF_ERASEBKGND);
        RestoreDC(memory, saved);
    }
    UINT count = 0, size = 0;
    Gdiplus::GetImageEncodersSize(&count, &size);
    std::vector<BYTE> storage(size);
    auto encoders = reinterpret_cast<Gdiplus::ImageCodecInfo *>(storage.data());
    Gdiplus::GetImageEncoders(count, size, encoders);
    bool saved = false;
    {
        Gdiplus::Bitmap image(bitmap, nullptr);
        for (UINT i = 0; i < count; ++i)
            if (std::wstring(encoders[i].MimeType) == L"image/png")
                saved =
                    image.Save((output / name).c_str(), &encoders[i].Clsid, nullptr) == Gdiplus::Ok;
    }
    SelectObject(memory, old);
    DeleteObject(bitmap);
    DeleteDC(memory);
    ReleaseDC(hwnd, dc);
    if (!saved)
        throw std::runtime_error("Preview PNG could not be saved");
}
LRESULT CALLBACK preview_proc(HWND h, UINT m, WPARAM w, LPARAM l) {
    if (m == WM_TIMER && w == 72) {
        if (!ready || displayed_generation != view_generation)
            return 0;
        try {
            if (stage == 0) {
                ListView_SetItemState(list, 0, LVIS_SELECTED | LVIS_FOCUSED,
                                      LVIS_SELECTED | LVIS_FOCUSED);
                capture(h, L"connections-dark.png");
                select_view(2);
            } else if (stage == 1) {
                exercise_map();
                capture(h, L"map-dark.png");
                theme.set_dark(false);
                appearance();
            } else if (stage == 2) {
                check(SendMessageW(GetDlgItem(world_view.window(), 1), CB_GETCURSEL, 0, 0) == 0,
                      "Theme preserves country selection");
                check(!theme.dark(), "Light theme applied");
                capture(h, L"map-light.png");
                select_view(0);
            } else if (stage == 3) {
                capture(h, L"connections-light.png");
                theme.set_dark(true);
                appearance();
                SetWindowPos(h, nullptr, 0, 0, 1060, 740,
                             SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
            } else if (stage == 4) {
                capture(h, L"connections-compact.png");
                select_view(2);
            } else if (stage == 5) {
                capture(h, L"map-compact.png");
                settings_data = {h, nm::Settings{}, false};
                WNDCLASSW cls{};
                cls.hInstance = instance;
                cls.lpfnWndProc = settings_proc;
                cls.lpszClassName = L"NetMonSettingsPreview";
                RegisterClassW(&cls);
                settings_window =
                    CreateWindowExW(0, cls.lpszClassName, L"Settings preview",
                                    WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, 0, 0, theme.px(620),
                                    theme.px(620), h, nullptr, instance, &settings_data);
                theme.apply(settings_window);
                ShowWindow(settings_window, SW_SHOWNOACTIVATE);
            } else {
                const auto mode = GetDlgItem(settings_window, 1001);
                check(SendMessageW(mode, CB_GETCOUNT, 0, 0) == 3, "Three alert modes");
                SendMessageW(mode, CB_SHOWDROPDOWN, TRUE, 0);
                check(IsWindow(settings_window) && SendMessageW(mode, CB_GETDROPPEDSTATE, 0, 0),
                      "Settings stays open with dropdown");
                SendMessageW(mode, CB_SETCURSEL, 2, 0);
                SendMessageW(settings_window, WM_COMMAND, MAKEWPARAM(1001, CBN_SELCHANGE),
                             reinterpret_cast<LPARAM>(mode));
                check(IsWindow(settings_window) && !settings_data.accepted,
                      "Selection does not save/close dialog");
                SendMessageW(mode, CB_SHOWDROPDOWN, FALSE, 0);
                SendMessageW(mode, CB_SETCURSEL, 1, 0);
                capture(settings_window, L"settings-dark.png");
                DestroyWindow(settings_window);
                settings_window = nullptr;
                DestroyWindow(h);
                return 0;
            }
            ++stage;
        } catch (const std::exception &e) {
            failed = true;
            std::cerr << e.what() << "\n";
            DestroyWindow(h);
        }
        return 0;
    }
    const auto result = wndproc(h, m, w, l);
    if (m == reply_message && table.records().size() > 0)
        ready = true;
    return result;
}
} // namespace
int main() {
    read_only_client = true;
    instance = GetModuleHandleW(nullptr);
    pipe_name = L"Preview only";
    output = nm::executable_path().parent_path().parent_path() / L"ui-preview";
    std::filesystem::create_directories(output);
    client_transport = [](const nm::Json &q, HANDLE cancel) -> nm::Json {
        const auto op = q.value("op", "");
        if (op == "watch") {
            WaitForSingleObject(cancel, 200);
            return {{"revision", 1}};
        }
        if (op == "status")
            return {{"demo", true},
                    {"processed", 12408},
                    {"storage_bytes", 14 * 1048576},
                    {"config_revision", 1},
                    {"collector", {{"subscribed", true}, {"message", "Synthetic visual preview"}}}};
        if (op == "config")
            return nm::Config{};
        if (op == "query") {
            const char *ips[] = {"8.8.8.8",      "1.1.1.1",        "9.9.9.9",     "20.42.65.90",
                                 "151.101.1.69", "104.16.132.229", "52.95.110.1", "13.107.42.14",
                                 "133.242.0.1",  "200.160.2.3",    "127.0.0.1",   "192.168.1.24"};
            const char *apps[] = {"browser.exe", "mail.exe", "backup.exe", "demo-agent.exe"};
            nm::Json rows = nm::Json::array();
            for (int i = 0; i < 12; ++i) {
                nm::Event event;
                event.id = std::to_string(i);
                event.app = std::string("C:\\Demo\\") + apps[i % 4];
                event.time = 1793578800000 - i * 95000;
                event.pid = 3100 + static_cast<std::uint32_t>(i);
                event.remote_ip = ips[i];
                event.remote_port = i == 2 ? 53 : 443;
                event.protocol = i == 2 ? 17 : 6;
                event.classification = i == 3 ? "review" : i % 3 ? "good" : "unknown";
                event.reason = i == 3  ? "Synthetic example: needs your review"
                               : i % 3 ? "Matches your Good rule"
                                       : "Previously observed; not explicitly trusted";
                event.state = "policy-permitted";
                event.local_ip = "192.168.1.10";
                event.local_port = 49201;
                event.identity_status = "Synthetic visual preview";
                rows.push_back(event);
            }
            return rows;
        }
        throw std::runtime_error("Unexpected preview request");
    };
    INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES};
    InitCommonControlsEx(&controls);
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = preview_proc;
    cls.lpszClassName = L"NetMonPreview";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    const auto hwnd =
        CreateWindowExW(0, cls.lpszClassName, L"NetMon synthetic visual preview",
                        WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT, 1280,
                        880, nullptr, nullptr, instance, nullptr);
    if (!hwnd)
        return 2;
    ShowWindow(hwnd, SW_SHOWNOACTIVATE);
    SetTimer(hwnd, 72, 500, nullptr);
    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0)
        dispatch(msg);
    diagnostics.reset();
    std::cout << "Rendered " << stage + 1 << " preview states in " << nm::utf8(output.wstring())
              << "\n";
    std::cout << checks << " native map/theme/settings checks passed\n";
    return stage == 6 && !failed ? 0 : 1;
}
