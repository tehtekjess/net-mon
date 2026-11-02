#include "ipc/pipe.hpp"
#include "ui/client.hpp"
#include "ui/diagnostics.hpp"
#include "ui/table.hpp"
#include "ui/tray.hpp"
#include "ui/theme.hpp"
#include "ui/map.hpp"
#include <sstream>
#include <algorithm>
#include <commctrl.h>
#include <commdlg.h>
#include <memory>
#include <shellapi.h>
namespace {
using nm::Json;
constexpr UINT reply_message = WM_APP + 1, tray_message = WM_APP + 2,
               diagnostic_message = WM_APP + 3;
enum : int {
    id_good = 100,
    id_bad,
    id_reset,
    id_settings,
    id_view,
    id_search,
    id_scope,
    id_previous,
    id_next,
    id_hide_app,
    id_export = 200,
    id_export_rules,
    id_import_rules,
    id_feed,
    id_install,
    id_exit,
    id_snooze,
    id_resume,
    id_show,
    id_theme,
    id_more,
    id_status,
    id_navigation = 300
};
HWND window = nullptr, list = nullptr, details = nullptr, summary = nullptr, search_box = nullptr,
     scope_box = nullptr;
nm::ui::Theme theme;
nm::ui::MapView world_view;
HFONT font = nullptr;
HIMAGELIST row_height = nullptr;
std::array<HWND, 6> nav_buttons{};
int view_selection = 0;
bool appearance_changed = false;
nm::ui::StatusText service_status;
const char *view_modes[] = {"live", "review", "map", "history", "alerts", "rules"};
const wchar_t *view_titles[] = {L"Recent activity", L"Needs review", L"Destination map",
                                L"Saved history",   L"Saved alerts", L"Saved rules"};
void layout();
void appearance();
HINSTANCE instance = nullptr;
UINT taskbar_restart = 0;
std::unique_ptr<nm::ui::TrayIcon> tray;
std::wstring pipe_name;
std::unique_ptr<nm::ui::Client> client;
std::shared_ptr<nm::ui::Diagnostics> diagnostics;
bool read_only_client = false, rules_view = false;
nm::ui::Client::Transport client_transport;
int offset = 0;
std::string view = "live", search;
nm::ui::TableView table;
std::uint64_t view_generation = 0, displayed_generation = UINT64_MAX;
constexpr UINT_PTR search_timer = 1, mailbox_timer = 2;
std::shared_ptr<const nm::Config> config;
HWND feedback = nullptr;
std::wstring feedback_text;
void set_feedback(const wchar_t *value) {
    if (feedback_text == value)
        return;
    feedback_text = value;
    SetWindowTextW(feedback, value);
}
void dispatch(MSG &message, HWND dialog = nullptr) {
    const auto observer = diagnostics;
    const auto token = observer ? observer->begin(message) : nm::ui::Diagnostics::Dispatch{};
    const HWND navigation = dialog ? dialog : window;
    if (!navigation || !IsDialogMessageW(navigation, &message)) {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }
    if (observer)
        observer->end(token);
}
void enqueue(Json request) {
    client->submit(std::move(request));
    set_feedback(L"Saving...");
}
void request_view() {
    client->set_view({view, search, offset, view_generation});
    set_feedback(L"Loading...");
}
std::wstring text(HWND control) {
    auto length = GetWindowTextLengthW(control);
    std::wstring s(static_cast<size_t>(length) + 1, L'\0');
    GetWindowTextW(control, s.data(), length + 1);
    s.resize(static_cast<size_t>(length));
    return s;
}
HWND control(const wchar_t *type, const wchar_t *label, DWORD style, int id, int x, int y, int w,
             int h, HWND parent = nullptr) {
    auto handle =
        CreateWindowExW(0, type, label, WS_CHILD | WS_VISIBLE | style, theme.px(x), theme.px(y),
                        theme.px(w), theme.px(h), parent ? parent : window,
                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), instance, nullptr);
    theme.control(handle);
    return handle;
}
void show_error(const std::string &error) {
    MessageBoxW(window, nm::wide(error).c_str(), L"NetMon", MB_OK | MB_ICONWARNING);
}
int selected() { return table.selected(); }
void tray_add() {
    if (tray)
        tray->recreate();
    else
        tray = std::make_unique<nm::ui::TrayIcon>(window, tray_message);
}
std::filesystem::path choose_file(bool save, const wchar_t *title) {
    wchar_t name[32768]{};
    OPENFILENAMEW of{};
    of.lStructSize = sizeof(of);
    of.hwndOwner = window;
    of.lpstrFilter = L"JSON files (*.json)\0*.json\0All files\0*.*\0";
    of.lpstrFile = name;
    of.nMaxFile = 32768;
    of.lpstrTitle = title;
    of.lpstrDefExt = L"json";
    of.Flags =
        OFN_NOCHANGEDIR | OFN_PATHMUSTEXIST | (save ? OFN_OVERWRITEPROMPT : OFN_FILEMUSTEXIST);
    return (save ? GetSaveFileNameW(&of) : GetOpenFileNameW(&of)) ? std::filesystem::path(name)
                                                                  : std::filesystem::path{};
}

void layout() {
    if (!list)
        return;
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto p = [](int value) { return theme.px(value); };
    const int width = bounds.right, height = bounds.bottom, left = p(234),
              content = width - left - p(24);
    for (std::size_t i = 0; i < nav_buttons.size(); ++i)
        MoveWindow(nav_buttons[i], p(14), p(119 + static_cast<int>(i) * 47), p(188), p(40), TRUE);
    MoveWindow(GetDlgItem(window, id_theme), p(14), height - p(151), p(188), p(34), TRUE);
    MoveWindow(GetDlgItem(window, id_settings), p(14), height - p(108), p(188), p(34), TRUE);
    MoveWindow(GetDlgItem(window, id_more), p(14), height - p(65), p(188), p(34), TRUE);
    MoveWindow(search_box, left + p(11), p(194),
               std::max(p(160), std::min(p(390), content - p(310))), p(24), TRUE);
    MoveWindow(GetDlgItem(window, id_previous), width - p(214), p(188), p(86), p(35), TRUE);
    MoveWindow(GetDlgItem(window, id_next), width - p(116), p(188), p(92), p(35), TRUE);
    MoveWindow(feedback, left + std::min(p(430), content - p(270)), p(195), p(150), p(24), TRUE);
    MoveWindow(summary, left, height - p(29), content, p(22), TRUE);
    const bool map = view == "map", paged = view == "history" || view == "alerts";
    for (const auto id : {id_previous, id_next})
        ShowWindow(GetDlgItem(window, id), paged ? SW_SHOW : SW_HIDE);
    ShowWindow(list, map ? SW_HIDE : SW_SHOW);
    ShowWindow(world_view.window(), map ? SW_SHOW : SW_HIDE);
    const int inspector = height - p(227);
    MoveWindow(list, left + 1, p(240), content - 2, std::max(p(120), inspector - p(254)), TRUE);
    MoveWindow(world_view.window(), left, p(238), content, std::max(p(180), height - p(282)), TRUE);
    MoveWindow(scope_box, left + p(14), inspector + p(43), p(251), p(150), TRUE);
    MoveWindow(GetDlgItem(window, id_good), left + p(276), inspector + p(43), p(92), p(32), TRUE);
    MoveWindow(GetDlgItem(window, id_bad), left + p(378), inspector + p(43), p(92), p(32), TRUE);
    MoveWindow(GetDlgItem(window, id_reset), left + p(480), inspector + p(43), p(111), p(32), TRUE);
    MoveWindow(GetDlgItem(window, id_hide_app), left + p(601), inspector + p(43), p(165), p(32),
               TRUE);
    MoveWindow(details, left + p(5), inspector + p(86), content - p(10), p(107), TRUE);
    for (const auto child :
         {scope_box, details, GetDlgItem(window, id_good), GetDlgItem(window, id_bad),
          GetDlgItem(window, id_reset), GetDlgItem(window, id_hide_app)})
        ShowWindow(child, map ? SW_HIDE : SW_SHOW);
    EnableWindow(GetDlgItem(window, id_good), !rules_view);
    EnableWindow(GetDlgItem(window, id_bad), !rules_view);
    const int widths[] = {100, 171, 235, 155, rules_view ? 150 : 0, 112, 345};
    for (int i = 0; i < 7; ++i)
        if (ListView_GetColumnWidth(list, i) != p(widths[i]))
            ListView_SetColumnWidth(list, i, p(widths[i]));
    InvalidateRect(window, nullptr, FALSE);
}
void appearance() {
    font = theme.font();
    theme.apply(window);
    world_view.theme_changed();
    SetWindowTextW(GetDlgItem(window, id_theme),
                   theme.dark() ? L"Use light theme" : L"Use dark theme");
    if (row_height) {
        ListView_SetImageList(list, nullptr, LVSIL_SMALL);
        ImageList_Destroy(row_height);
    }
    row_height = ImageList_Create(1, theme.px(31), ILC_COLOR32, 1, 1);
    ListView_SetImageList(list, row_height, LVSIL_SMALL);
    layout();
}
void select_view(int index) {
    if (index < 0 || index >= 6)
        return;
    view_selection = index;
    view = view_modes[index];
    offset = 0;
    rules_view = view == "rules";
    ++view_generation;
    for (std::size_t i = 0; i < nav_buttons.size(); ++i)
        theme.active(nav_buttons[i], static_cast<int>(i) == index);
    table.set_snapshot(nm::ui::prepare_table(Json::array(), rules_view));
    world_view.set_snapshot(table.snapshot());
    layout();
    request_view();
}
void paint_main(HDC dc) {
    using nm::ui::fill;
    using nm::ui::label;
    using nm::ui::round_rect;
    RECT bounds{};
    GetClientRect(window, &bounds);
    const auto p = [](int value) { return theme.px(value); };
    const auto &c = theme.colors();
    const int width = bounds.right, height = bounds.bottom, left = p(234),
              content = width - left - p(24);
    fill(dc, bounds, c.background);
    fill(dc, {0, 0, p(216), height}, c.sidebar);
    fill(dc, {p(215), 0, p(216), height}, c.border);
    nm::ui::icon(dc, 0, {p(22), p(28), p(50), p(56)}, c.accent);
    label(dc, L"NetMon", {p(60), p(22), p(200), p(60)}, c.text, theme.font(nm::ui::Font::title));
    label(dc, L"CONNECTION MONITOR", {p(22), p(71), p(202), p(92)}, c.muted,
          theme.font(nm::ui::Font::caption));
    label(dc, L"LOCAL · PRIVATE · OFFLINE", {p(20), height - p(25), p(209), height - p(6)}, c.muted,
          theme.font(nm::ui::Font::caption));
    label(dc, view_titles[view_selection], {left, p(24), width - p(190), p(63)}, c.text,
          theme.font(nm::ui::Font::heading));
    const wchar_t *subtitles[] = {
        L"Latest outbound activity. Select a connection to inspect its process and destination.",
        L"Unreviewed connections, with your local-network and application exclusions applied.",
        L"Recent destination IPs grouped by country estimate. These are not exact host locations.",
        L"Retained connection history. Search or move between pages to explore earlier activity.",
        L"Saved suspicious and marked-bad observations, including their original alert reasons.",
        L"Your saved decisions. Good rules match an executable hash; Bad rules always take "
        L"priority."};
    label(dc, subtitles[view_selection], {left, p(64), width - p(24), p(86)}, c.muted,
          theme.font(nm::ui::Font::caption));
    RECT health{width - p(171), p(31), width - p(24), p(64)};
    round_rect(dc, health, c.panel, c.border, p(24));
    const auto healthy = service_status.healthy || service_status.demo;
    auto dot = CreateSolidBrush(healthy ? c.green : c.amber);
    auto old = SelectObject(dc, dot);
    auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
    Ellipse(dc, health.left + p(14), health.top + p(13), health.left + p(21), health.top + p(20));
    SelectObject(dc, old);
    SelectObject(dc, pen);
    DeleteObject(dot);
    health.left += p(30);
    label(dc,
          service_status.demo ? L"Demo"
          : healthy           ? L"Monitoring"
                              : L"Attention",
          health, c.text, theme.font(nm::ui::Font::caption));
    const auto snapshot = table.snapshot();
    const std::wstring labels[] = {rules_view ? L"SAVED RULES" : L"IN THIS VIEW", L"NEEDS REVIEW",
                                   L"COUNTRIES", L"HISTORY USED"};
    const std::wstring values[] = {
        std::to_wstring(snapshot->display.size()), std::to_wstring(snapshot->needs_review),
        std::to_wstring(snapshot->map.groups.size()),
        std::to_wstring(service_status.storage_bytes / 1048576) + L" MiB"};
    const int card = (content - p(36)) / 4;
    for (int i = 0; i < 4; ++i) {
        RECT r{left + i * (card + p(12)), p(102), left + i * (card + p(12)) + card, p(174)};
        round_rect(dc, r, c.panel, c.border, p(12));
        label(dc, labels[i], {r.left + p(16), r.top + p(10), r.right - p(12), r.top + p(28)},
              c.muted, theme.font(nm::ui::Font::caption));
        label(dc, values[i], {r.left + p(16), r.top + p(30), r.right - p(12), r.bottom - p(8)},
              i == 1 ? c.amber : c.text, theme.font(nm::ui::Font::title));
    }
    round_rect(dc, {left, p(188), left + std::min(p(417), content - p(282)), p(224)}, c.field,
               c.border, p(10));
    if (view != "map") {
        const int inspector = height - p(227);
        round_rect(dc, {left, p(238), width - p(24), inspector - p(12)}, c.panel, c.border, p(12));
        round_rect(dc, {left, inspector, width - p(24), height - p(36)}, c.panel, c.border, p(12));
        label(dc, rules_view ? L"RULE DETAILS" : L"CONNECTION DETAILS",
              {left + p(15), inspector + p(10), width - p(36), inspector + p(34)}, c.muted,
              theme.font(nm::ui::Font::caption));
    }
}
struct MenuEntry {
    int id;
    const wchar_t *label;
};
const MenuEntry more_entries[] = {{id_status, L"Monitoring status..."},
                                  {0, L""},
                                  {id_export, L"Export displayed records..."},
                                  {id_export_rules, L"Export saved rules..."},
                                  {id_import_rules, L"Import replacement rules..."},
                                  {id_feed, L"Import threat indicators..."},
                                  {0, L""},
                                  {id_snooze, L"Snooze alerts for one hour"},
                                  {id_resume, L"Resume alerts"},
                                  {0, L""},
                                  {id_install, L"Install / update service..."},
                                  {id_exit, L"Exit tray app"}};
int more_menu(HWND owner) {
    auto menu = CreatePopupMenu();
    for (const auto &entry : more_entries) {
        MENUITEMINFOW item{sizeof(item)};
        item.fMask = MIIM_FTYPE | MIIM_ID | MIIM_DATA | MIIM_STATE;
        item.fType = entry.id ? MFT_OWNERDRAW : MFT_SEPARATOR;
        item.wID = static_cast<UINT>(entry.id);
        item.dwItemData = reinterpret_cast<ULONG_PTR>(&entry);
        item.fState = !config && (entry.id == id_export_rules || entry.id == id_snooze ||
                                  entry.id == id_resume)
                          ? MFS_DISABLED
                          : MFS_ENABLED;
        if (entry.id) {
            // Preserve a native accessible label alongside the custom appearance.
            item.fMask |= MIIM_STRING;
            item.dwTypeData = const_cast<wchar_t *>(entry.label);
        }
        InsertMenuItemW(menu, static_cast<UINT>(GetMenuItemCount(menu)), TRUE, &item);
    }
    RECT r{};
    GetWindowRect(GetDlgItem(owner, id_more), &r);
    SetForegroundWindow(owner);
    const auto id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_BOTTOMALIGN, r.right,
                                   r.bottom, 0, owner, nullptr);
    DestroyMenu(menu);
    return static_cast<int>(id);
}

struct SettingsDialog {
    HWND parent;
    nm::Settings value;
    bool accepted = false;
};
LRESULT CALLBACK settings_proc(HWND dialog, UINT msg, WPARAM wp, LPARAM lp) {
    auto *state = reinterpret_cast<SettingsDialog *>(GetWindowLongPtrW(dialog, GWLP_USERDATA));
    if (msg == WM_CREATE) {
        state =
            static_cast<SettingsDialog *>(reinterpret_cast<CREATESTRUCTW *>(lp)->lpCreateParams);
        SetWindowLongPtrW(dialog, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
        control(L"STATIC", L"Alert mode", 0, 0, 16, 18, 130, 24, dialog);
        auto mode = control(WC_COMBOBOXW, L"", CBS_DROPDOWNLIST | WS_TABSTOP, 1001, 150, 14, 430,
                            120, dialog);
        for (auto label :
             {L"Balanced", L"Ask about new connections", L"Threats and marked Bad only"})
            SendMessageW(mode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        SendMessageW(
            mode, CB_SETCURSEL,
            state->value.alert_mode == "balanced" ? 0 : (state->value.alert_mode == "ask" ? 1 : 2),
            0);
        auto all = control(L"BUTTON", L"Log all connections (otherwise suspicious only)",
                           BS_AUTOCHECKBOX | WS_TABSTOP, 1002, 16, 55, 565, 26, dialog);
        SendMessageW(all, BM_SETCHECK, state->value.log_all ? BST_CHECKED : BST_UNCHECKED, 0);
        const wchar_t *labels[] = {L"History limit (MiB)", L"Retention (days)",
                                   L"Alert cooldown (sec)"};
        const auto values = {state->value.history_mib, state->value.retention_days,
                             state->value.cooldown_seconds};
        int i = 0;
        for (auto n : values) {
            control(L"STATIC", labels[i], 0, 0, 16, 96 + i * 38, 180, 24, dialog);
            control(L"EDIT", std::to_wstring(n).c_str(), WS_BORDER | ES_NUMBER | WS_TABSTOP,
                    1003 + i, 215, 92 + i * 38, 120, 26, dialog);
            ++i;
        }
        auto local = control(L"BUTTON", L"Hide localhost destinations from Needs review",
                             BS_AUTOCHECKBOX | WS_TABSTOP, 1006, 16, 215, 565, 28, dialog);
        SendMessageW(local, BM_SETCHECK,
                     state->value.review_hide_loopback ? BST_CHECKED : BST_UNCHECKED, 0);
        auto network = control(L"BUTTON", L"Hide local-network destinations from Needs review",
                               BS_AUTOCHECKBOX | WS_TABSTOP, 1008, 16, 247, 565, 28, dialog);
        SendMessageW(network, BM_SETCHECK,
                     state->value.review_hide_local_network ? BST_CHECKED : BST_UNCHECKED, 0);
        control(L"STATIC",
                L"Includes private/shared, link-local, multicast, broadcast and localhost.", 0, 0,
                32, 280, 549, 24, dialog);
        control(L"STATIC", L"Hide these applications from Needs review (full path, one per line):",
                0, 0, 16, 311, 565, 24, dialog);
        std::string excluded;
        for (const auto &path : state->value.review_excluded_apps)
            excluded += path + "\r\n";
        auto paths = control(L"EDIT", nm::wide(excluded).c_str(),
                             WS_BORDER | WS_VSCROLL | ES_MULTILINE | ES_AUTOVSCROLL |
                                 ES_WANTRETURN | WS_TABSTOP,
                             1007, 16, 337, 565, 100, dialog);
        SendMessageW(paths, EM_SETLIMITTEXT, 65536, 0);
        control(L"STATIC",
                L"Review exclusions only hide rows; they do not trust an app or silence alerts.\n"
                L"Delete a path above to show that application again.\n"
                L"History: 32-4096 MiB. Country estimates: offline DB-IP, November 2026.",
                0, 0, 16, 451, 565, 70, dialog);
        control(L"BUTTON", L"Save", BS_DEFPUSHBUTTON | WS_TABSTOP, IDOK, 386, 535, 90, 30, dialog);
        control(L"BUTTON", L"Cancel", WS_TABSTOP, IDCANCEL, 486, 535, 90, 30, dialog);
        return 0;
    }
    if (msg == WM_PAINT) {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(dialog, &paint);
        RECT r{};
        GetClientRect(dialog, &r);
        nm::ui::fill(dc, r, theme.colors().background);
        EndPaint(dialog, &paint);
        return 0;
    }
    if (msg == WM_PRINTCLIENT) {
        RECT r{};
        GetClientRect(dialog, &r);
        nm::ui::fill(reinterpret_cast<HDC>(wp), r, theme.colors().background);
        return 0;
    }
    if (msg == WM_ERASEBKGND)
        return 1;
    if (msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORSTATIC || msg == WM_CTLCOLORLISTBOX ||
        msg == WM_CTLCOLORBTN)
        return theme.color(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp));
    if (msg == WM_COMMAND && state) {
        if (LOWORD(wp) == IDOK && HIWORD(wp) == BN_CLICKED) {
            try {
                auto mode = SendDlgItemMessageW(dialog, 1001, CB_GETCURSEL, 0, 0);
                state->value.alert_mode = mode == 0 ? "balanced" : (mode == 1 ? "ask" : "threats");
                state->value.log_all =
                    SendDlgItemMessageW(dialog, 1002, BM_GETCHECK, 0, 0) == BST_CHECKED;
                BOOL valid = FALSE;
                state->value.history_mib = GetDlgItemInt(dialog, 1003, &valid, FALSE);
                if (!valid)
                    throw std::runtime_error("Invalid history limit");
                state->value.retention_days = GetDlgItemInt(dialog, 1004, &valid, FALSE);
                if (!valid)
                    throw std::runtime_error("Invalid retention");
                state->value.cooldown_seconds = GetDlgItemInt(dialog, 1005, &valid, FALSE);
                if (!valid)
                    throw std::runtime_error("Invalid cooldown");
                state->value.review_hide_loopback =
                    SendDlgItemMessageW(dialog, 1006, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->value.review_hide_local_network =
                    SendDlgItemMessageW(dialog, 1008, BM_GETCHECK, 0, 0) == BST_CHECKED;
                state->value.review_excluded_apps.clear();
                std::istringstream paths(nm::utf8(text(GetDlgItem(dialog, 1007))));
                std::string path;
                while (std::getline(paths, path)) {
                    const auto first = path.find_first_not_of(" \t\r");
                    const auto last = path.find_last_not_of(" \t\r");
                    if (first != std::string::npos)
                        state->value.review_excluded_apps.push_back(
                            path.substr(first, last - first + 1));
                }
                nm::validate(state->value);
                state->accepted = true;
                DestroyWindow(dialog);
            } catch (const std::exception &ex) {
                MessageBoxW(dialog, nm::wide(ex.what()).c_str(), L"Settings",
                            MB_OK | MB_ICONWARNING);
            }
            return 0;
        }
        if (LOWORD(wp) == IDCANCEL && HIWORD(wp) == BN_CLICKED) {
            DestroyWindow(dialog);
            return 0;
        }
    }
    if (msg == WM_CLOSE) {
        DestroyWindow(dialog);
        return 0;
    }
    return DefWindowProcW(dialog, msg, wp, lp);
}
void settings() {
    SettingsDialog data{window, config->settings, false};
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = settings_proc;
    cls.lpszClassName = L"NetMonSettings";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    cls.hbrBackground = nullptr;
    RegisterClassW(&cls);
    auto dialog =
        CreateWindowExW(WS_EX_DLGMODALFRAME, L"NetMonSettings", L"NetMon settings",
                        WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                        theme.px(620), theme.px(620), window, nullptr, instance, &data);
    if (!dialog)
        throw std::runtime_error(nm::win_error());
    EnableWindow(window, FALSE);
    theme.apply(dialog);
    ShowWindow(dialog, SW_SHOW);
    SetFocus(GetDlgItem(dialog, 1001));
    MSG msg{};
    while (IsWindow(dialog) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        dispatch(msg, dialog);
    }
    EnableWindow(window, TRUE);
    SetForegroundWindow(window);
    if (data.accepted)
        enqueue({{"op", "settings"}, {"settings", data.value}});
}
void install() {
    const auto script = nm::executable_path().parent_path() / L"Install.ps1";
    if (!std::filesystem::exists(script))
        throw std::runtime_error("Use the packaged build's Install.cmd to install the service.");
    const auto args = L"-NoProfile -ExecutionPolicy Bypass -File \"" + script.wstring() +
                      L"\" -OwnerSid " + nm::wide(nm::current_sid());
    auto result =
        ShellExecuteW(window, L"runas", L"powershell.exe", args.c_str(), nullptr, SW_HIDE);
    if (reinterpret_cast<INT_PTR>(result) <= 32)
        throw std::runtime_error("Installation was cancelled or could not start");
}
void command(int id, int notification) {
    if (id == id_show) {
        ShowWindow(window, SW_SHOW);
        SetForegroundWindow(window);
        return;
    }
    if (id == id_exit) {
        DestroyWindow(window);
        return;
    }
    if (id == id_install) {
        install();
        return;
    }
    if ((id == id_settings || id == id_snooze || id == id_resume || id == id_export_rules) &&
        !config)
        return;
    if (id == id_settings) {
        settings();
        return;
    }
    if (id >= id_navigation && id < id_navigation + 6) {
        select_view(id - id_navigation);
        return;
    }
    if (id == id_theme) {
        appearance_changed = true;
        theme.set_dark(!theme.dark());
        appearance();
        client->save_theme(theme.dark());
        return;
    }
    if (id == id_status) {
        MessageBoxW(window, service_status.summary.c_str(), L"Monitoring status", MB_OK);
        return;
    }
    if (id == id_more) {
        const auto selected = more_menu(window);
        if (selected)
            command(selected, BN_CLICKED);
        return;
    }
    if (id == id_search && notification == EN_CHANGE) {
        search = nm::utf8(text(search_box));
        offset = 0;
        ++view_generation;
        SetTimer(window, search_timer, 150, nullptr);
        return;
    }
    if (id == id_previous || id == id_next) {
        offset = std::max(0, offset + (id == id_next ? 100 : -100));
        ++view_generation;
        request_view();
        return;
    }
    if (id == id_snooze || id == id_resume) {
        auto s = config->settings;
        s.snooze_until = id == id_snooze ? nm::now_ms() + 3600000 : 0;
        enqueue({{"op", "settings"}, {"settings", s}});
        return;
    }
    if (id == id_export || id == id_export_rules) {
        auto path = choose_file(true, id == id_export ? L"Export displayed connection records"
                                                      : L"Export rules");
        if (!path.empty()) {
            if (id == id_export)
                client->export_records(path, table.snapshot());
            else
                client->export_rules(path, config);
            set_feedback(L"Exporting...");
        }
        return;
    }
    if (id == id_import_rules || id == id_feed) {
        auto path = choose_file(false, id == id_feed ? L"Import threat indicators"
                                                     : L"Import replacement rule set");
        if (path.empty())
            return;
        if (id == id_import_rules) {
            if (MessageBoxW(window, L"Replace your existing rules with this file?", L"Import rules",
                            MB_YESNO | MB_ICONQUESTION) != IDYES)
                return;
        }
        client->import_file(path, id == id_feed);
        set_feedback(L"Importing...");
        return;
    }
    const auto &rows = table.records();
    int index = selected();
    if (index < 0 || static_cast<size_t>(index) >= rows.size())
        return;
    const auto &row = rows[static_cast<size_t>(index)];
    if (id == id_hide_app) {
        const auto path = row.value("app", "");
        if (path.empty())
            throw std::runtime_error("Select a connection with a process path");
        enqueue({{"op", "exclude_review_app"}, {"app", path}});
        return;
    }
    if (id == id_reset) {
        std::string rule = rules_view ? row.value("id", "") : row.value("rule_id", "");
        if (rule.empty())
            throw std::runtime_error(
                "This record has no matching saved rule. Open Saved rules to remove a decision.");
        enqueue({{"op", "delete_rule"}, {"id", rule}});
        return;
    }
    if ((id == id_good || id == id_bad) && !rules_view) {
        auto scope = SendMessageW(scope_box, CB_GETCURSEL, 0, 0);
        enqueue({{"op", "mark"},
                 {"id", row.at("id")},
                 {"verdict", id == id_good ? "good" : "bad"},
                 {"scope", scope == 1 ? "app" : (scope == 2 ? "destination" : "connection")}});
    }
}
LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == taskbar_restart && taskbar_restart) {
        tray_add();
        return 0;
    }
    try {
        switch (msg) {
        case WM_CREATE: {
            window = hwnd;
            try {
                diagnostics = std::make_shared<nm::ui::Diagnostics>(
                    hwnd, diagnostic_message,
                    read_only_client
                        ? nm::executable_path().parent_path() / L"ui-test-diagnostics.json"
                        : nm::ui::Diagnostics::default_path());
            } catch (...) {
            }
            theme.initialize(hwnd);
            font = theme.font();
            for (std::size_t i = 0; i < nav_buttons.size(); ++i) {
                nav_buttons[i] = control(L"BUTTON", view_titles[i], WS_TABSTOP,
                                         id_navigation + static_cast<int>(i), 14,
                                         119 + static_cast<int>(i) * 47, 188, 40);
                theme.control(nav_buttons[i], nm::ui::Button::navigation, static_cast<int>(i));
            }
            theme.active(nav_buttons[0], true);
            control(L"BUTTON", L"Use light theme", WS_TABSTOP, id_theme, 14, 580, 188, 34);
            auto settings_button =
                control(L"BUTTON", L"Settings", WS_TABSTOP, id_settings, 14, 622, 188, 34);
            theme.control(settings_button, nm::ui::Button::quiet, 6);
            EnableWindow(settings_button, FALSE);
            control(L"BUTTON", L"More actions", WS_TABSTOP, id_more, 14, 664, 188, 34);
            summary = control(L"STATIC", L"Connecting to the monitoring service...",
                              SS_LEFTNOWORDWRAP, 0, 234, 720, 950, 22);
            search_box =
                control(L"EDIT", L"", ES_AUTOHSCROLL | WS_TABSTOP, id_search, 245, 194, 390, 24);
            SendMessageW(search_box, EM_SETCUEBANNER, TRUE,
                         reinterpret_cast<LPARAM>(L"Search process, IP address or reason"));
            SendMessageW(search_box, EM_SETLIMITTEXT, 256, 0);
            control(L"BUTTON", L"Previous", WS_TABSTOP, id_previous, 970, 188, 86, 35);
            control(L"BUTTON", L"Next page", WS_TABSTOP, id_next, 1068, 188, 92, 35);
            feedback = control(L"STATIC", L"Loading...", SS_LEFTNOWORDWRAP, 0, 690, 195, 150, 24);
            list =
                control(WC_LISTVIEWW, L"Connections",
                        LVS_REPORT | LVS_OWNERDATA | LVS_SINGLESEL | LVS_SHOWSELALWAYS | WS_TABSTOP,
                        0, 234, 240, 950, 300);
            ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_DOUBLEBUFFER |
                                                        LVS_EX_LABELTIP);
            const wchar_t *names[] = {L"Time",  L"Process",  L"Destination", L"Country (est.)",
                                      L"Scope", L"Decision", L"Reason"};
            for (int i = 0; i < 7; ++i) {
                LVCOLUMNW col{};
                col.mask = LVCF_TEXT | LVCF_WIDTH;
                col.pszText = const_cast<wchar_t *>(names[i]);
                col.cx = theme.px(140);
                ListView_InsertColumn(list, i, &col);
            }
            scope_box = control(WC_COMBOBOXW, L"Decision scope", CBS_DROPDOWNLIST | WS_TABSTOP,
                                id_scope, 248, 585, 251, 150);
            for (auto v : {L"This app + destination + port", L"This app (all destinations)",
                           L"This destination + port (all apps)"})
                SendMessageW(scope_box, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(v));
            SendMessageW(scope_box, CB_SETCURSEL, 0, 0);
            auto good = control(L"BUTTON", L"Mark Good", WS_TABSTOP, id_good, 510, 585, 92, 32);
            auto bad = control(L"BUTTON", L"Mark Bad", WS_TABSTOP, id_bad, 612, 585, 92, 32);
            theme.control(good, nm::ui::Button::primary);
            theme.control(bad, nm::ui::Button::danger);
            control(L"BUTTON", L"Remove rule", WS_TABSTOP, id_reset, 714, 585, 111, 32);
            control(L"BUTTON", L"Hide app from review", WS_TABSTOP, id_hide_app, 835, 585, 165, 32);
            details = control(L"EDIT", L"",
                              ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL | WS_TABSTOP,
                              0, 239, 630, 940, 107);
            world_view.create(hwnd, instance, theme);
            table.attach(list, details);
            appearance();
            tray_add();
            client = std::make_unique<nm::ui::Client>(
                pipe_name, [] { PostMessageW(window, reply_message, 0, 0); }, read_only_client,
                client_transport);
            return 0;
        }
        case WM_SHOWWINDOW:
            if (client)
                client->set_visible(wp != FALSE);
            break;
        case WM_SIZE: {
            if (client)
                client->set_visible(wp != SIZE_MINIMIZED && IsWindowVisible(hwnd));
            layout();
            return 0;
        }
        case WM_GETMINMAXINFO: {
            auto *info = reinterpret_cast<MINMAXINFO *>(lp);
            info->ptMinTrackSize = {theme.px(1060), theme.px(740)};
            return 0;
        }
        case WM_PAINT: {
            PAINTSTRUCT paint{};
            auto dc = BeginPaint(hwnd, &paint);
            paint_main(dc);
            EndPaint(hwnd, &paint);
            return 0;
        }
        case WM_PRINTCLIENT:
            paint_main(reinterpret_cast<HDC>(wp));
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_CTLCOLOREDIT:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORLISTBOX:
        case WM_CTLCOLORBTN:
            return theme.color(msg, reinterpret_cast<HDC>(wp), reinterpret_cast<HWND>(lp));
        case WM_DPICHANGED: {
            theme.initialize(hwnd);
            const auto *r = reinterpret_cast<RECT *>(lp);
            SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            appearance();
            return 0;
        }
        case WM_SETTINGCHANGE:
        case WM_THEMECHANGED:
            theme.initialize(hwnd);
            appearance();
            return 0;
        case WM_MEASUREITEM: {
            auto *m = reinterpret_cast<MEASUREITEMSTRUCT *>(lp);
            if (m->CtlType == ODT_MENU) {
                const auto *entry = reinterpret_cast<const MenuEntry *>(m->itemData);
                m->itemWidth = static_cast<UINT>(theme.px(260));
                m->itemHeight = static_cast<UINT>(theme.px(entry && entry->id ? 34 : 10));
                return TRUE;
            }
            break;
        }
        case WM_DRAWITEM: {
            const auto *draw = reinterpret_cast<DRAWITEMSTRUCT *>(lp);
            if (draw->CtlType == ODT_MENU) {
                const auto &c = theme.colors();
                const auto *entry = reinterpret_cast<const MenuEntry *>(draw->itemData);
                nm::ui::fill(draw->hDC, draw->rcItem,
                             (draw->itemState & ODS_SELECTED) ? c.border : c.panel);
                RECT r = draw->rcItem;
                r.left += theme.px(14);
                r.right -= theme.px(12);
                if (entry && entry->id)
                    nm::ui::label(draw->hDC, entry->label, r,
                                  (draw->itemState & ODS_DISABLED) ? c.muted : c.text,
                                  theme.font());
                else
                    nm::ui::fill(
                        draw->hDC,
                        {r.left, (r.top + r.bottom) / 2, r.right, (r.top + r.bottom) / 2 + 1},
                        c.border);
                return TRUE;
            }
            break;
        }
        case diagnostic_message:
            if (diagnostics)
                diagnostics->heartbeat();
            return 0;
        case WM_COMMAND:
            command(LOWORD(wp), HIWORD(wp));
            return 0;
        case WM_TIMER:
            if (wp == search_timer) {
                KillTimer(hwnd, search_timer);
                request_view();
            } else if (wp == mailbox_timer) {
                KillTimer(hwnd, mailbox_timer);
                PostMessageW(hwnd, reply_message, 0, 0);
            }
            return 0;
        case WM_NOTIFY: {
            LRESULT result = 0;
            if (theme.notify(reinterpret_cast<NMHDR *>(lp), list, result))
                return result;
            if (table.on_notify(reinterpret_cast<NMHDR *>(lp), result))
                return result;
            return 0;
        }
        case reply_message: {
            nm::ui::UiUpdate update;
            if (!client->take(update)) {
                SetTimer(hwnd, mailbox_timer, 10, nullptr);
                return 0;
            }
            KillTimer(hwnd, mailbox_timer);
            if (update.config) {
                config = std::move(update.config);
                EnableWindow(GetDlgItem(hwnd, id_settings), TRUE);
            }
            if (update.dark_theme && !appearance_changed) {
                theme.set_dark(*update.dark_theme);
                appearance();
            }
            if (update.status) {
                const bool changed = service_status.healthy != update.status->healthy ||
                                     service_status.demo != update.status->demo ||
                                     service_status.storage_bytes / 1048576 !=
                                         update.status->storage_bytes / 1048576;
                service_status = *update.status;
                auto status =
                    service_status.healthy || service_status.demo
                        ? L"Local processing  ·  " + std::to_wstring(service_status.processed) +
                              L" observed  ·  " + std::to_wstring(service_status.dropped) +
                              L" dropped  ·  Offline country estimates"
                        : service_status.summary;
                if (text(summary) != status)
                    SetWindowTextW(summary, status.c_str());
                if (changed) {
                    RECT area{};
                    GetClientRect(hwnd, &area);
                    area.bottom = theme.px(176);
                    InvalidateRect(hwnd, &area, FALSE);
                }
                tray->set_tip(std::move(update.status->tip));
            }
            if (update.view && update.view->generation == view_generation) {
                if (update.view->snapshot) {
                    world_view.set_snapshot(update.view->snapshot);
                    table.set_snapshot(std::move(update.view->snapshot));
                    RECT cards{};
                    GetClientRect(hwnd, &cards);
                    cards.left = theme.px(234);
                    cards.top = theme.px(100);
                    cards.bottom = theme.px(176);
                    InvalidateRect(hwnd, &cards, FALSE);
                    displayed_generation = view_generation;
                    set_feedback(L"Ready");
                } else if (!update.view->error.empty())
                    set_feedback(update.view->error.c_str());
            }
            if (update.alert)
                tray->notify(std::move(update.alert->title), std::move(update.alert->body));
            for (const auto &done : update.completions) {
                set_feedback(done.error.empty() ? done.label.c_str() : L"Action failed");
                if (!done.error.empty())
                    show_error(nm::utf8(done.error));
            }
            return 0;
        }
        case tray_message:
            if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == NIN_SELECT ||
                LOWORD(lp) == NIN_BALLOONUSERCLICK) {
                command(id_show, 0);
                return 0;
            }
            if (LOWORD(lp) == WM_CONTEXTMENU || LOWORD(lp) == WM_RBUTTONUP) {
                HMENU menu = CreatePopupMenu();
                AppendMenuW(menu, MF_STRING, id_show, L"Open NetMon");
                AppendMenuW(menu, MF_STRING, id_snooze, L"Snooze 1 hour");
                AppendMenuW(menu, MF_STRING, id_resume, L"Resume alerts");
                AppendMenuW(menu, MF_STRING, id_exit, L"Exit tray app (monitoring continues)");
                POINT point{};
                GetCursorPos(&point);
                SetForegroundWindow(hwnd);
                auto id = TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY, point.x, point.y, 0,
                                         hwnd, nullptr);
                DestroyMenu(menu);
                if (id)
                    command(static_cast<int>(id), 0);
                return 0;
            }
            return 0;
        case WM_CLOSE:
            ShowWindow(hwnd, SW_HIDE);
            return 0;
        case WM_DESTROY:
            client.reset();
            tray.reset();
            if (row_height) {
                ImageList_Destroy(row_height);
                row_height = nullptr;
            }
            PostQuitMessage(0);
            return 0;
        }
    } catch (const std::exception &ex) {
        show_error(ex.what());
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}
} // namespace
int WINAPI wWinMain(HINSTANCE h, HINSTANCE, PWSTR command_line, int show) {
    instance = h;
    try {
        bool demo = std::wstring(command_line).find(L"--demo") != std::wstring::npos;
        pipe_name = demo ? nm::demo_pipe() : nm::service_pipe;
        nm::Handle singleton(CreateMutexW(
            nullptr, FALSE,
            (L"Local\\NetMon.Tray." + nm::wide(nm::current_sid()) + (demo ? L".Demo" : L""))
                .c_str()));
        if (GetLastError() == ERROR_ALREADY_EXISTS) {
            auto existing = FindWindowW(L"NetMonMain", nullptr);
            if (existing) {
                ShowWindow(existing, SW_SHOW);
                SetForegroundWindow(existing);
            }
            return 0;
        }
        INITCOMMONCONTROLSEX controls{sizeof(controls), ICC_LISTVIEW_CLASSES |
                                                            ICC_STANDARD_CLASSES | ICC_TAB_CLASSES};
        InitCommonControlsEx(&controls);
        taskbar_restart = RegisterWindowMessageW(L"TaskbarCreated");
        WNDCLASSW cls{};
        cls.hInstance = h;
        cls.lpfnWndProc = wndproc;
        cls.lpszClassName = L"NetMonMain";
        cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        cls.hIcon = LoadIconW(nullptr, IDI_INFORMATION);
        cls.hbrBackground = nullptr;
        RegisterClassW(&cls);
        auto hwnd =
            CreateWindowExW(0, cls.lpszClassName,
                            demo ? L"NetMon - Synthetic demo" : L"NetMon - Outgoing connections",
                            WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN, CW_USEDEFAULT, CW_USEDEFAULT,
                            1280, 880, nullptr, nullptr, h, nullptr);
        if (!hwnd)
            throw std::runtime_error(nm::win_error());
        if (std::wstring(command_line).find(L"--tray") == std::wstring::npos)
            ShowWindow(hwnd, show);
        MSG msg{};
        while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
            dispatch(msg);
        }
        diagnostics.reset();
        return 0;
    } catch (const std::exception &ex) {
        MessageBoxW(nullptr, nm::wide(ex.what()).c_str(), L"NetMon", MB_OK | MB_ICONERROR);
        return 1;
    }
}
