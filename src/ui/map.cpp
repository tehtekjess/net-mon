#include "ui/map.hpp"
#include <windowsx.h>
#include <algorithm>
#include <cmath>
namespace nm::ui {
namespace {
constexpr int country_id = 1, endpoint_id = 2, minus_id = 3, plus_id = 4, reset_id = 5;
}
MapView::~MapView() {
    if (cache_)
        DeleteObject(cache_);
}
const MapGroup *MapView::group() const {
    const auto &groups = snapshot_->map.groups;
    const auto it = std::find_if(groups.begin(), groups.end(),
                                 [&](const auto &item) { return item.code == selected_; });
    return it == groups.end() ? nullptr : &*it;
}
RECT MapView::map_rect() const {
    RECT r{};
    GetClientRect(window_, &r);
    const auto p = [this](int n) { return theme_->px(n); };
    const auto available_width = std::max(p(200), static_cast<int>(r.right) - p(334));
    const auto available = std::max(p(100), static_cast<int>(r.bottom) - p(210));
    const auto width = std::min(available_width, static_cast<int>(available * 360.0 / 145.0));
    const auto height = static_cast<int>(width * 145.0 / 360.0);
    const auto left = p(16) + (available_width - width) / 2;
    const auto top = p(64) + (available - height) / 2;
    return {left, top, left + width, top + height};
}
POINT MapView::project(MapPoint point) const {
    const auto r = map_rect();
    const auto width = r.right - r.left, height = r.bottom - r.top;
    return {static_cast<LONG>(r.left + width / 2.0 +
                              ((point.longitude + 180.0) / 360.0 - 0.5) * width * zoom_ + pan_x_),
            static_cast<LONG>(r.top + height / 2.0 +
                              ((85.0 - point.latitude) / 145.0 - 0.5) * height * zoom_ + pan_y_)};
}
int MapView::hit(POINT point) const {
    double distance = static_cast<double>(theme_->px(19) * theme_->px(19));
    int found = -1;
    const auto area = map_rect();
    if (!PtInRect(&area, point))
        return -1;
    for (std::size_t i = 0; i < snapshot_->map.groups.size(); ++i) {
        const auto center = project(snapshot_->map.groups[i].location);
        const double dx = static_cast<double>(point.x - center.x),
                     dy = static_cast<double>(point.y - center.y);
        if (dx * dx + dy * dy < distance) {
            distance = dx * dx + dy * dy;
            found = static_cast<int>(i);
        }
    }
    return found;
}
void MapView::invalidate_cache() {
    if (cache_) {
        DeleteObject(cache_);
        cache_ = nullptr;
    }
    if (window_)
        InvalidateRect(window_, nullptr, FALSE);
}
void MapView::set_zoom(double value) {
    zoom_ = std::clamp(value, 1.0, 4.0);
    const auto area = map_rect();
    const double max_x = (area.right - area.left) * (zoom_ - 1) / 2.0,
                 max_y = (area.bottom - area.top) * (zoom_ - 1) / 2.0;
    pan_x_ = std::clamp(pan_x_, -max_x, max_x);
    pan_y_ = std::clamp(pan_y_, -max_y, max_y);
    invalidate_cache();
}
HWND MapView::create(HWND parent, HINSTANCE instance, Theme &theme) {
    theme_ = &theme;
    WNDCLASSW cls{};
    cls.hInstance = instance;
    cls.lpfnWndProc = proc;
    cls.lpszClassName = L"NetMonWorldMap";
    cls.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    RegisterClassW(&cls);
    window_ = CreateWindowExW(0, cls.lpszClassName, L"Destination map - estimated countries",
                              WS_CHILD | WS_CLIPCHILDREN | WS_TABSTOP, 0, 0, 100, 100, parent,
                              nullptr, instance, this);
    if (!window_)
        throw std::runtime_error("Cannot create destination map");
    return window_;
}
void MapView::set_snapshot(std::shared_ptr<const TableSnapshot> snapshot) {
    // Freeze an open native popup until the user finishes choosing a country.
    if (countries_ && SendMessageW(countries_, CB_GETDROPPEDSTATE, 0, 0)) {
        pending_snapshot_ = std::move(snapshot);
        return;
    }
    pending_snapshot_.reset();
    const bool geometry = snapshot_->map.geometry != snapshot->map.geometry;
    snapshot_ = std::move(snapshot);
    if (!group())
        selected_ = snapshot_->map.groups.empty() ? "" : snapshot_->map.groups.front().code;
    if (IsWindowVisible(window_))
        controls();
    if (geometry)
        invalidate_cache();
    else
        InvalidateRect(window_, nullptr, FALSE);
}
void MapView::theme_changed() {
    if (!window_)
        return;
    theme_->control(countries_);
    theme_->control(endpoints_);
    theme_->control(info_);
    SendMessageW(countries_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), theme_->px(27));
    SendMessageW(countries_, CB_SETITEMHEIGHT, 0, theme_->px(29));
    SendMessageW(endpoints_, LB_SETITEMHEIGHT, 0, theme_->px(55));
    layout();
}
void MapView::select(const std::string &code) {
    if (code == selected_)
        return;
    selected_ = code;
    endpoint_keys_.clear();
    controls();
    InvalidateRect(window_, nullptr, FALSE);
}
void MapView::details() {
    std::wstring value = L"Select an IP to see its processes and ports.\r\n\r\nLocations are "
                         L"country estimates, not precise host positions.";
    const auto index = SendMessageW(endpoints_, LB_GETCURSEL, 0, 0);
    if (const auto *g = group();
        g && index >= 0 && static_cast<std::size_t>(index) < g->endpoints.size())
        value = g->endpoints[static_cast<std::size_t>(index)].details;
    if (value != info_text_) {
        info_text_ = std::move(value);
        SetWindowTextW(info_, info_text_.c_str());
    }
}
void MapView::controls() {
    if (!countries_)
        return;
    std::vector<std::string> codes;
    for (const auto &g : snapshot_->map.groups)
        codes.push_back(g.code);
    if (codes != country_codes_) {
        SendMessageW(countries_, CB_RESETCONTENT, 0, 0);
        for (const auto &g : snapshot_->map.groups) {
            const auto label = g.name + L" (" + wide(g.code) + L")";
            SendMessageW(countries_, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label.c_str()));
        }
        country_codes_ = std::move(codes);
    }
    int index = -1;
    for (std::size_t i = 0; i < country_codes_.size(); ++i)
        if (country_codes_[i] == selected_)
            index = static_cast<int>(i);
    if (SendMessageW(countries_, CB_GETCURSEL, 0, 0) != index)
        SendMessageW(countries_, CB_SETCURSEL, static_cast<WPARAM>(index), 0);
    std::vector<std::wstring> keys;
    if (const auto *g = group())
        for (const auto &ip : g->endpoints)
            keys.push_back(ip.title + L"\n" + ip.subtitle);
    if (keys != endpoint_keys_) {
        std::wstring selected_ip;
        const auto old = SendMessageW(endpoints_, LB_GETCURSEL, 0, 0);
        if (old >= 0 && static_cast<std::size_t>(old) < endpoint_keys_.size())
            selected_ip = endpoint_keys_[static_cast<std::size_t>(old)];
        SendMessageW(endpoints_, LB_RESETCONTENT, 0, 0);
        if (const auto *g = group())
            for (const auto &ip : g->endpoints)
                SendMessageW(endpoints_, LB_ADDSTRING, 0,
                             reinterpret_cast<LPARAM>(ip.title.c_str()));
        endpoint_keys_ = std::move(keys);
        const auto found = std::find(endpoint_keys_.begin(), endpoint_keys_.end(), selected_ip);
        if (!endpoint_keys_.empty())
            SendMessageW(endpoints_, LB_SETCURSEL,
                         found == endpoint_keys_.end()
                             ? 0
                             : static_cast<WPARAM>(found - endpoint_keys_.begin()),
                         0);
    }
    details();
}
void MapView::layout() {
    if (!countries_)
        return;
    RECT r{};
    GetClientRect(window_, &r);
    const auto p = [this](int n) { return theme_->px(n); };
    const int x = static_cast<int>(r.right) - p(286), width = p(266);
    MoveWindow(countries_, x, p(45), width, p(300), TRUE);
    const int list_height = std::max(p(100), static_cast<int>(r.bottom) - p(290));
    MoveWindow(endpoints_, x, p(90), width, list_height, TRUE);
    MoveWindow(info_, x, p(106) + list_height, width,
               std::max(p(100), static_cast<int>(r.bottom) - list_height - p(125)), TRUE);
    for (int i = 0; i < 3; ++i)
        MoveWindow(GetDlgItem(window_, minus_id + i), p(18 + i * 43), p(14), p(i == 2 ? 68 : 36),
                   p(30), TRUE);
    invalidate_cache();
}
void MapView::paint(HDC dc) {
    RECT client{};
    GetClientRect(window_, &client);
    const auto &c = theme_->colors();
    const auto p = [this](int n) { return theme_->px(n); };
    fill(dc, client, c.panel);
    const int divider = static_cast<int>(client.right) - p(302);
    fill(dc, {divider, 0, divider + 1, client.bottom}, c.border);
    label(dc, L"DESTINATIONS", {divider + p(16), p(13), client.right - p(12), p(38)}, c.muted,
          theme_->font(Font::caption));
    const auto area = map_rect();
    const int width = area.right - area.left, height = area.bottom - area.top;
    if (width > 0 && height > 0 &&
        (!cache_ || cache_size_.cx != width || cache_size_.cy != height)) {
        if (cache_)
            DeleteObject(cache_);
        cache_ = CreateCompatibleBitmap(dc, width, height);
        cache_size_ = {width, height};
        if (cache_) {
            auto memory = CreateCompatibleDC(dc);
            auto old = SelectObject(memory, cache_);
            fill(memory, {0, 0, width, height}, c.panel);
            {
                Gdiplus::Graphics graphics(memory);
                graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
                Gdiplus::Pen grid(color(c.border, 110), 1.0f);
                for (int lon = -180; lon <= 180; lon += 45) {
                    auto a = project({static_cast<double>(lon), 85}),
                         b = project({static_cast<double>(lon), -60});
                    graphics.DrawLine(
                        &grid, static_cast<int>(a.x - area.left), static_cast<int>(a.y - area.top),
                        static_cast<int>(b.x - area.left), static_cast<int>(b.y - area.top));
                }
                for (int lat = -60; lat <= 85; lat += 30) {
                    auto a = project({-180, static_cast<double>(lat)}),
                         b = project({180, static_cast<double>(lat)});
                    graphics.DrawLine(
                        &grid, static_cast<int>(a.x - area.left), static_cast<int>(a.y - area.top),
                        static_cast<int>(b.x - area.left), static_cast<int>(b.y - area.top));
                }
                if (snapshot_->map.geometry) {
                    Gdiplus::SolidBrush land(
                        color(theme_->dark() ? RGB(42, 59, 79) : RGB(205, 218, 230)));
                    Gdiplus::Pen outline(
                        color(theme_->dark() ? RGB(64, 86, 109) : RGB(177, 196, 214)), 0.7f);
                    for (const auto &country : snapshot_->map.geometry->shapes()) {
                        for (const auto &ring : country.rings) {
                            std::vector<Gdiplus::PointF> points;
                            points.reserve(ring.size());
                            for (const auto &v : ring) {
                                const auto q = project(v);
                                points.emplace_back(static_cast<float>(q.x - area.left),
                                                    static_cast<float>(q.y - area.top));
                            }
                            if (points.size() >= 3) {
                                graphics.FillPolygon(&land, points.data(),
                                                     static_cast<int>(points.size()));
                                graphics.DrawPolygon(&outline, points.data(),
                                                     static_cast<int>(points.size()));
                            }
                        }
                    }
                }
            } // GDI+ must release the borrowed DC before it is destroyed.
            SelectObject(memory, old);
            DeleteDC(memory);
        }
    }
    if (cache_) {
        auto memory = CreateCompatibleDC(dc);
        auto old = SelectObject(memory, cache_);
        BitBlt(dc, area.left, area.top, width, height, memory, 0, 0, SRCCOPY);
        SelectObject(memory, old);
        DeleteDC(memory);
    }
    {
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetClip(Gdiplus::Rect(area.left, area.top, width, height));
        // Small groups are painted last, so they remain selectable in dense regions.
        for (const auto &g : snapshot_->map.groups) {
            const auto point = project(g.location);
            const auto ink = theme_->severity(g.severity);
            const auto radius = static_cast<float>(p(g.code == selected_ ? 8 : 5));
            const auto x = static_cast<float>(point.x), y = static_cast<float>(point.y);
            Gdiplus::SolidBrush glow(color(ink, g.code == selected_ ? 48 : 24)), dot(color(ink));
            Gdiplus::Pen rim(color(c.panel), 1.6f);
            graphics.FillEllipse(&glow, x - radius * 2.2f, y - radius * 2.2f, radius * 4.4f,
                                 radius * 4.4f);
            graphics.FillEllipse(&dot, x - radius, y - radius, radius * 2, radius * 2);
            graphics.DrawEllipse(&rim, x - radius, y - radius, radius * 2, radius * 2);
        }
    }
    const auto *selected = group();
    if (selected) {
        const auto title = selected->name + L"  /  " + selected->caption;
        label(dc, title, {p(20), area.bottom + p(18), divider - p(16), area.bottom + p(46)}, c.text,
              theme_->font(Font::strong));
    } else
        label(dc,
              snapshot_->map.geometry ? L"No public destinations located in this view"
                                      : L"World map data unavailable",
              {p(20), area.bottom + p(18), divider - p(16), area.bottom + p(46)}, c.muted,
              theme_->font());
    const auto note = std::to_wstring(snapshot_->map.mapped_ips) + L" mapped IPs  ·  " +
                      std::to_wstring(snapshot_->map.local_ips) + L" local  ·  " +
                      std::to_wstring(snapshot_->map.unlocated_ips) + L" unlocated";
    label(dc, note, {p(20), area.bottom + p(48), divider - p(12), area.bottom + p(70)}, c.muted,
          theme_->font(Font::caption));
    const wchar_t *legend[] = {L"Good", L"Unreviewed", L"Review", L"Bad / threat"};
    for (int i = 0; i < 4; ++i) {
        const auto x = p(20 + i * 116);
        const auto y = area.bottom + p(83);
        auto brush = CreateSolidBrush(theme_->severity(i));
        auto old = SelectObject(dc, brush);
        auto pen = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, x, y, x + p(6), y + p(6));
        SelectObject(dc, old);
        SelectObject(dc, pen);
        DeleteObject(brush);
        label(dc, legend[i], {x + p(12), y - p(6), x + p(112), y + p(12)}, c.muted,
              theme_->font(Font::caption));
    }
    label(dc, L"Country estimates · DB-IP / Natural Earth",
          {p(20), client.bottom - p(30), divider - p(12), client.bottom - p(8)}, c.muted,
          theme_->font(Font::caption));
    label(dc, L"Drag to pan · Scroll to zoom", {p(184), p(17), divider - p(12), p(43)}, c.muted,
          theme_->font(Font::caption));
}
LRESULT CALLBACK MapView::proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
    auto *self = reinterpret_cast<MapView *>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (message == WM_NCCREATE) {
        self = static_cast<MapView *>(reinterpret_cast<CREATESTRUCTW *>(l)->lpCreateParams);
        self->window_ = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }
    if (!self)
        return DefWindowProcW(hwnd, message, w, l);
    const auto p = [self](int n) { return self->theme_->px(n); };
    switch (message) {
    case WM_CREATE: {
        auto make = [&](const wchar_t *type, const wchar_t *name, DWORD style, int id) {
            const auto child =
                CreateWindowExW(0, type, name, WS_CHILD | WS_VISIBLE | WS_TABSTOP | style, 0, 0,
                                100, 30, hwnd, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                GetModuleHandleW(nullptr), nullptr);
            self->theme_->control(child);
            return child;
        };
        self->countries_ = make(WC_COMBOBOXW, L"Country",
                                CBS_DROPDOWNLIST | CBS_OWNERDRAWFIXED | CBS_HASSTRINGS, country_id);
        SendMessageW(self->countries_, CB_SETITEMHEIGHT, static_cast<WPARAM>(-1), p(27));
        SendMessageW(self->countries_, CB_SETITEMHEIGHT, 0, p(29));
        self->endpoints_ = make(L"LISTBOX", L"Destination IP addresses",
                                LBS_OWNERDRAWFIXED | LBS_HASSTRINGS | LBS_NOTIFY |
                                    LBS_NOINTEGRALHEIGHT | WS_VSCROLL,
                                endpoint_id);
        SendMessageW(self->endpoints_, LB_SETITEMHEIGHT, 0, p(55));
        self->info_ =
            make(L"EDIT", L"", ES_MULTILINE | ES_READONLY | ES_AUTOVSCROLL | WS_VSCROLL, 0);
        make(L"BUTTON", L"−", 0, minus_id);
        make(L"BUTTON", L"+", 0, plus_id);
        make(L"BUTTON", L"Reset", 0, reset_id);
        self->layout();
        return 0;
    }
    case WM_APP + 1:
        if (self->pending_snapshot_)
            self->set_snapshot(std::move(self->pending_snapshot_));
        return 0;
    case WM_SHOWWINDOW:
        if (w) {
            self->controls();
            InvalidateRect(hwnd, nullptr, FALSE);
        }
        return 0;
    case WM_SIZE:
        self->layout();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(hwnd, &paint);
        self->paint(dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    case WM_PRINTCLIENT:
        self->paint(reinterpret_cast<HDC>(w));
        return 0;
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORLISTBOX:
        return self->theme_->color(message, reinterpret_cast<HDC>(w), reinterpret_cast<HWND>(l));
    case WM_MEASUREITEM:
        reinterpret_cast<MEASUREITEMSTRUCT *>(l)->itemHeight = static_cast<UINT>(p(29));
        return TRUE;
    case WM_DRAWITEM: {
        const auto *draw = reinterpret_cast<DRAWITEMSTRUCT *>(l);
        const auto &c = self->theme_->colors();
        const bool selected = (draw->itemState & ODS_SELECTED) != 0;
        fill(draw->hDC, draw->rcItem,
             selected ? (self->theme_->dark() ? RGB(32, 54, 69) : RGB(220, 237, 244)) : c.field);
        if (draw->CtlID == country_id) {
            if (draw->itemID < self->snapshot_->map.groups.size()) {
                const auto &group = self->snapshot_->map.groups[draw->itemID];
                RECT r = draw->rcItem;
                r.left += p(8);
                label(draw->hDC, group.name + L" (" + wide(group.code) + L")", r, c.text,
                      self->theme_->font());
            }
        } else if (const auto *g = self->group(); g && draw->itemID < g->endpoints.size()) {
            const auto &ip = g->endpoints[draw->itemID];
            RECT title = draw->rcItem;
            title.left += p(12);
            title.top += p(7);
            title.bottom = title.top + p(22);
            label(draw->hDC, ip.title, title, self->theme_->severity(ip.severity),
                  self->theme_->font(Font::mono));
            title.top += p(23);
            title.bottom = draw->rcItem.bottom - p(3);
            label(draw->hDC, ip.subtitle, title, c.muted, self->theme_->font(Font::caption));
        }
        if (draw->itemState & ODS_FOCUS)
            DrawFocusRect(draw->hDC, &draw->rcItem);
        return TRUE;
    }
    case WM_COMMAND:
        if (LOWORD(w) == country_id && HIWORD(w) == CBN_CLOSEUP)
            PostMessageW(hwnd, WM_APP + 1, 0, 0);
        if (LOWORD(w) == country_id && HIWORD(w) == CBN_SELCHANGE) {
            const auto index = SendMessageW(self->countries_, CB_GETCURSEL, 0, 0);
            if (index >= 0 && static_cast<std::size_t>(index) < self->country_codes_.size())
                self->select(self->country_codes_[static_cast<std::size_t>(index)]);
        } else if (LOWORD(w) == endpoint_id && HIWORD(w) == LBN_SELCHANGE)
            self->details();
        else if (HIWORD(w) == BN_CLICKED) {
            if (LOWORD(w) == minus_id)
                self->set_zoom(self->zoom_ / 1.3);
            if (LOWORD(w) == plus_id)
                self->set_zoom(self->zoom_ * 1.3);
            if (LOWORD(w) == reset_id)
                self->set_zoom(1);
        }
        return 0;
    case WM_MOUSEWHEEL:
        self->set_zoom(self->zoom_ * (GET_WHEEL_DELTA_WPARAM(w) > 0 ? 1.25 : 0.8));
        return 0;
    case WM_LBUTTONDOWN:
        self->drag_start_ = {GET_X_LPARAM(l), GET_Y_LPARAM(l)};
        if (const auto area = self->map_rect(); PtInRect(&area, self->drag_start_)) {
            self->dragging_ = true;
            self->moved_ = false;
            self->drag_x_ = self->pan_x_;
            self->drag_y_ = self->pan_y_;
            SetCapture(hwnd);
            SetFocus(hwnd);
        }
        return 0;
    case WM_MOUSEMOVE:
        if (self->dragging_ && self->zoom_ > 1) {
            const auto dx = GET_X_LPARAM(l) - self->drag_start_.x,
                       dy = GET_Y_LPARAM(l) - self->drag_start_.y;
            if (std::abs(dx) + std::abs(dy) > p(4))
                self->moved_ = true;
            if (self->moved_) {
                const auto area = self->map_rect();
                const double max_x = (area.right - area.left) * (self->zoom_ - 1) / 2.0,
                             max_y = (area.bottom - area.top) * (self->zoom_ - 1) / 2.0;
                self->pan_x_ = std::clamp(self->drag_x_ + dx, -max_x, max_x);
                self->pan_y_ = std::clamp(self->drag_y_ + dy, -max_y, max_y);
                self->invalidate_cache();
            }
        }
        return 0;
    case WM_LBUTTONUP:
        if (self->dragging_) {
            self->dragging_ = false;
            ReleaseCapture();
            if (!self->moved_) {
                const auto index = self->hit({GET_X_LPARAM(l), GET_Y_LPARAM(l)});
                if (index >= 0)
                    self->select(self->snapshot_->map.groups[static_cast<std::size_t>(index)].code);
            }
        }
        return 0;
    case WM_CAPTURECHANGED:
        self->dragging_ = false;
        return 0;
    case WM_GETDLGCODE:
        return DLGC_WANTARROWS;
    case WM_KEYDOWN:
        if ((w == VK_LEFT || w == VK_RIGHT) && !self->country_codes_.empty()) {
            const auto it = std::find(self->country_codes_.begin(), self->country_codes_.end(),
                                      self->selected_);
            const int count = static_cast<int>(self->country_codes_.size());
            int index = it == self->country_codes_.end()
                            ? 0
                            : static_cast<int>(it - self->country_codes_.begin());
            index = (index + (w == VK_RIGHT ? 1 : count - 1)) % count;
            self->select(self->country_codes_[static_cast<std::size_t>(index)]);
            return 0;
        }
        break;
    case WM_NCDESTROY:
        self->window_ = nullptr;
        break;
    }
    return DefWindowProcW(hwnd, message, w, l);
}
} // namespace nm::ui
