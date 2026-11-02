#include "ui/theme.hpp"
#include <dwmapi.h>
#include <uxtheme.h>
#include <windowsx.h>
#include <algorithm>
namespace nm::ui {
Gdiplus::Color color(COLORREF value, BYTE alpha) {
    return {alpha, GetRValue(value), GetGValue(value), GetBValue(value)};
}
void fill(HDC dc, RECT r, COLORREF value) {
    auto brush = CreateSolidBrush(value);
    FillRect(dc, &r, brush);
    DeleteObject(brush);
}
void round_rect(HDC dc, RECT r, COLORREF background, COLORREF border, int radius) {
    auto brush = CreateSolidBrush(background);
    auto pen = CreatePen(PS_SOLID, 1, border);
    auto old_brush = SelectObject(dc, brush), old_pen = SelectObject(dc, pen);
    RoundRect(dc, r.left, r.top, r.right, r.bottom, radius, radius);
    SelectObject(dc, old_brush);
    SelectObject(dc, old_pen);
    DeleteObject(brush);
    DeleteObject(pen);
}
void label(HDC dc, const std::wstring &value, RECT r, COLORREF ink, HFONT font, UINT flags) {
    const auto old_font = SelectObject(dc, font);
    const auto old_mode = SetBkMode(dc, TRANSPARENT);
    const auto old_color = SetTextColor(dc, ink);
    DrawTextW(dc, value.c_str(), static_cast<int>(value.size()), &r, flags);
    SetTextColor(dc, old_color);
    SetBkMode(dc, old_mode);
    SelectObject(dc, old_font);
}
void icon(HDC dc, int index, RECT rect, COLORREF ink) {
    Gdiplus::Graphics graphics(dc);
    graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
    graphics.TranslateTransform(static_cast<float>(rect.left), static_cast<float>(rect.top));
    graphics.ScaleTransform(static_cast<float>(rect.right - rect.left) / 24.0f,
                            static_cast<float>(rect.bottom - rect.top) / 24.0f);
    Gdiplus::Pen pen(color(ink), 1.6f);
    pen.SetStartCap(Gdiplus::LineCapRound);
    pen.SetEndCap(Gdiplus::LineCapRound);
    if (index == 0) {
        const Gdiplus::PointF points[] = {{2, 13}, {7, 13}, {10, 5}, {14, 19}, {17, 11}, {22, 11}};
        graphics.DrawLines(&pen, points, 6);
    } else if (index == 1) {
        const Gdiplus::PointF points[] = {{12, 2}, {21, 6}, {20, 15}, {12, 22},
                                          {4, 15}, {3, 6},  {12, 2}};
        graphics.DrawLines(&pen, points, 7);
        graphics.DrawLine(&pen, 12.0f, 7.0f, 12.0f, 13.0f);
        graphics.DrawEllipse(&pen, 11.4f, 16.0f, 1.2f, 1.2f);
    } else if (index == 2) {
        graphics.DrawEllipse(&pen, 2, 2, 20, 20);
        graphics.DrawEllipse(&pen, 7, 2, 10, 20);
        graphics.DrawLine(&pen, 2, 12, 22, 12);
    } else if (index == 3) {
        graphics.DrawEllipse(&pen, 3, 3, 18, 18);
        graphics.DrawLine(&pen, 12, 6, 12, 12);
        graphics.DrawLine(&pen, 12, 12, 17, 15);
    } else if (index == 4) {
        const Gdiplus::PointF points[] = {{4, 18}, {6, 14},  {6, 8},   {9, 4}, {15, 4},
                                          {18, 8}, {18, 14}, {20, 18}, {4, 18}};
        graphics.DrawLines(&pen, points, 9);
        graphics.DrawArc(&pen, 9, 17, 6, 5, 0.0f, 180.0f);
    } else if (index == 5) {
        for (int y : {6, 12, 18})
            graphics.DrawLine(&pen, 3, y, 21, y);
        graphics.DrawEllipse(&pen, 7, 4, 4, 4);
        graphics.DrawEllipse(&pen, 14, 10, 4, 4);
        graphics.DrawEllipse(&pen, 6, 16, 4, 4);
    } else {
        graphics.DrawEllipse(&pen, 4, 4, 16, 16);
        graphics.DrawEllipse(&pen, 9, 9, 6, 6);
        for (int i = 0; i < 4; ++i) {
            graphics.DrawLine(&pen, 12, 1, 12, 4);
            graphics.RotateTransform(90.0f, Gdiplus::MatrixOrderAppend);
        }
    }
}
Theme::Theme() {
    Gdiplus::GdiplusStartupInput input;
    if (Gdiplus::GdiplusStartup(&graphics_, &input, nullptr) != Gdiplus::Ok)
        throw std::runtime_error("Cannot initialize graphics");
    resources();
}
Theme::~Theme() {
    for (auto font : fonts_)
        DeleteObject(font);
    DeleteObject(panel_);
    DeleteObject(field_);
    DeleteObject(background_);
    if (graphics_)
        Gdiplus::GdiplusShutdown(graphics_);
}
bool Theme::high_contrast() const {
    HIGHCONTRASTW state{sizeof(state), 0, nullptr};
    return SystemParametersInfoW(SPI_GETHIGHCONTRAST, sizeof(state), &state, 0) &&
           (state.dwFlags & HCF_HIGHCONTRASTON);
}
void Theme::resources() {
    colors_ = dark_ ? Palette{RGB(14, 20, 31),    RGB(10, 16, 26),    RGB(21, 30, 44),
                              RGB(17, 25, 38),    RGB(36, 49, 68),    RGB(233, 239, 247),
                              RGB(143, 160, 182), RGB(94, 215, 202),  RGB(123, 176, 246),
                              RGB(100, 213, 162), RGB(245, 193, 102), RGB(248, 126, 139)}
                    : Palette{RGB(242, 245, 249), RGB(232, 238, 245), RGB(255, 255, 255),
                              RGB(246, 248, 251), RGB(215, 224, 234), RGB(26, 40, 60),
                              RGB(88, 107, 130),  RGB(11, 117, 111),  RGB(45, 103, 181),
                              RGB(19, 125, 78),   RGB(154, 98, 7),    RGB(181, 49, 67)};
    if (high_contrast()) {
        colors_.background = colors_.panel = colors_.field = colors_.sidebar =
            GetSysColor(COLOR_WINDOW);
        colors_.text = colors_.muted = GetSysColor(COLOR_WINDOWTEXT);
        colors_.border = colors_.accent = colors_.blue = colors_.green = colors_.amber =
            colors_.red = GetSysColor(COLOR_HIGHLIGHT);
    }
    DeleteObject(panel_);
    DeleteObject(field_);
    DeleteObject(background_);
    panel_ = CreateSolidBrush(colors_.panel);
    field_ = CreateSolidBrush(colors_.field);
    background_ = CreateSolidBrush(colors_.background);
    const int sizes[] = {15, 12, 28, 15, 13, 21};
    for (std::size_t i = 0; i < fonts_.size(); ++i) {
        DeleteObject(fonts_[i]);
        fonts_[i] = CreateFontW(
            -px(sizes[i]), 0, 0, 0, (i == 2 || i == 3 || i == 5) ? FW_SEMIBOLD : FW_NORMAL, FALSE,
            FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, i == 4 ? L"Consolas" : L"Segoe UI");
    }
}
void Theme::initialize(HWND window) {
    const auto dpi = GetDpiForWindow(window);
    if (dpi)
        dpi_ = dpi;
    resources();
}
void Theme::set_dark(bool dark) {
    if (dark_ != dark) {
        dark_ = dark;
        resources();
    }
}
void Theme::control(HWND window, Button role, int glyph) {
    wchar_t type[64]{};
    GetClassNameW(window, type, 64);
    SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(font()), FALSE);
    if (_wcsicmp(type, L"Button") == 0) {
        SetPropW(window, L"NetMon.Role", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(role)));
        SetPropW(window, L"NetMon.Icon", reinterpret_cast<HANDLE>(static_cast<INT_PTR>(glyph + 1)));
        SetWindowSubclass(window, button_proc, 1, reinterpret_cast<DWORD_PTR>(this));
    } else if (_wcsicmp(type, L"Edit") == 0) {
        SetWindowTheme(window, L"", L"");
        SendMessageW(window, EM_SETMARGINS, EC_LEFTMARGIN | EC_RIGHTMARGIN,
                     MAKELPARAM(px(10), px(10)));
    } else if (_wcsicmp(type, L"ComboBox") == 0) {
        SetWindowTheme(window, L"", L"");
        SetWindowSubclass(window, surface_proc, 2, reinterpret_cast<DWORD_PTR>(this));
    } else if (_wcsicmp(type, WC_LISTVIEWW) == 0) {
        SetWindowTheme(window, L"", L"");
        ListView_SetBkColor(window, colors_.panel);
        ListView_SetTextBkColor(window, colors_.panel);
        ListView_SetTextColor(window, colors_.text);
        SetWindowTheme(ListView_GetHeader(window), L"", L"");
        SetWindowSubclass(ListView_GetHeader(window), surface_proc, 2,
                          reinterpret_cast<DWORD_PTR>(this));
    }
}
void Theme::apply(HWND root) {
    BOOL dark = dark_ && !high_contrast();
    // The old SDK does not declare DWMWA_USE_IMMERSIVE_DARK_MODE; 20 is its public value.
    DwmSetWindowAttribute(root, 20, &dark, sizeof(dark));
    EnumChildWindows(
        root,
        [](HWND child, LPARAM data) -> BOOL {
            auto *theme = reinterpret_cast<Theme *>(data);
            const auto role =
                static_cast<Button>(reinterpret_cast<INT_PTR>(GetPropW(child, L"NetMon.Role")));
            const auto glyph =
                static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(child, L"NetMon.Icon"))) - 1;
            theme->control(child, role, glyph);
            InvalidateRect(child, nullptr, TRUE);
            return TRUE;
        },
        reinterpret_cast<LPARAM>(this));
    RedrawWindow(root, nullptr, nullptr, RDW_INVALIDATE | RDW_ALLCHILDREN);
}
void Theme::active(HWND window, bool value) {
    const bool previous = GetPropW(window, L"NetMon.Active") != nullptr;
    if (value == previous)
        return;
    if (value)
        SetPropW(window, L"NetMon.Active", reinterpret_cast<HANDLE>(1));
    else
        RemovePropW(window, L"NetMon.Active");
    InvalidateRect(window, nullptr, FALSE);
}
LRESULT Theme::color(UINT message, HDC dc, HWND child) const {
    wchar_t type[64]{};
    GetClassNameW(child, type, 64);
    const bool field =
        message == WM_CTLCOLOREDIT || message == WM_CTLCOLORLISTBOX || _wcsicmp(type, L"Edit") == 0;
    SetTextColor(dc, IsWindowEnabled(child) ? colors_.text : colors_.muted);
    SetBkColor(dc, field ? colors_.field : colors_.background);
    return reinterpret_cast<LRESULT>(field ? field_ : background_);
}
COLORREF Theme::severity(int value) const {
    return value >= 3   ? colors_.red
           : value == 2 ? colors_.amber
           : value == 1 ? colors_.blue
                        : colors_.green;
}
void Theme::button(HWND hwnd, HDC dc) {
    RECT r{};
    GetClientRect(hwnd, &r);
    const auto role =
        static_cast<Button>(reinterpret_cast<INT_PTR>(GetPropW(hwnd, L"NetMon.Role")));
    const bool hover = GetPropW(hwnd, L"NetMon.Hover") != nullptr;
    const bool selected = GetPropW(hwnd, L"NetMon.Active") != nullptr;
    const bool down = (SendMessageW(hwnd, BM_GETSTATE, 0, 0) & BST_PUSHED) != 0;
    const bool enabled = IsWindowEnabled(hwnd) != FALSE;
    const auto type = GetWindowLongPtrW(hwnd, GWL_STYLE) & BS_TYPEMASK;
    const bool check = type == BS_AUTOCHECKBOX || type == BS_CHECKBOX;
    const auto bg = role == Button::navigation ? colors_.sidebar : colors_.background;
    fill(dc, r, bg);
    COLORREF background = colors_.panel, ink = colors_.text, border = colors_.border;
    if (role == Button::primary) {
        background = dark_ ? RGB(28, 66, 66) : RGB(218, 243, 237);
        ink = colors_.accent;
    }
    if (role == Button::danger) {
        background = dark_ ? RGB(59, 32, 43) : RGB(255, 235, 237);
        ink = colors_.red;
    }
    if (role == Button::quiet || role == Button::navigation) {
        background = bg;
        border = bg;
        ink = colors_.muted;
    }
    if (hover || down || selected) {
        background = dark_ ? RGB(32, 48, 65) : RGB(211, 226, 236);
        border = background;
        ink = selected ? colors_.accent : colors_.text;
    }
    if (!enabled)
        ink = colors_.muted;
    wchar_t text[512]{};
    GetWindowTextW(hwnd, text, 512);
    if (check) {
        RECT box{px(1), (r.bottom - px(17)) / 2, px(18), (r.bottom + px(17)) / 2};
        round_rect(dc, box, colors_.field, colors_.border, px(4));
        if (SendMessageW(hwnd, BM_GETCHECK, 0, 0) == BST_CHECKED) {
            round_rect(dc, box, colors_.accent, colors_.accent, px(4));
            auto pen = CreatePen(PS_SOLID, px(2), colors_.background);
            auto old = SelectObject(dc, pen);
            MoveToEx(dc, box.left + px(4), box.top + px(8), nullptr);
            LineTo(dc, box.left + px(7), box.top + px(12));
            LineTo(dc, box.left + px(14), box.top + px(4));
            SelectObject(dc, old);
            DeleteObject(pen);
        }
        r.left = px(28);
        label(dc, text, r, ink, font());
    } else {
        RECT card{r.left + 1, r.top + 1, r.right - 1, r.bottom - 1};
        round_rect(dc, card, background, border, px(10));
        const int glyph =
            static_cast<int>(reinterpret_cast<INT_PTR>(GetPropW(hwnd, L"NetMon.Icon"))) - 1;
        if (glyph >= 0) {
            icon(dc, glyph, {px(13), (r.bottom - px(20)) / 2, px(33), (r.bottom + px(20)) / 2},
                 ink);
            r.left = px(45);
            r.right -= px(8);
            label(dc, text, r, ink, font(selected ? Font::strong : Font::body));
        } else
            label(dc, text, r, ink, font(Font::strong),
                  DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        if (selected && role == Button::navigation)
            fill(dc, {px(1), px(10), px(3), r.bottom - px(10)}, colors_.accent);
    }
    if (GetFocus() == hwnd && (SendMessageW(hwnd, WM_QUERYUISTATE, 0, 0) & UISF_HIDEFOCUS) == 0) {
        RECT focus = r;
        InflateRect(&focus, -px(4), -px(4));
        DrawFocusRect(dc, &focus);
    }
}
LRESULT CALLBACK Theme::button_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR,
                                    DWORD_PTR data) {
    auto *theme = reinterpret_cast<Theme *>(data);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(hwnd, &paint);
        theme->button(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        theme->button(hwnd, reinterpret_cast<HDC>(w));
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    if (message == WM_MOUSEMOVE && !GetPropW(hwnd, L"NetMon.Hover")) {
        SetPropW(hwnd, L"NetMon.Hover", reinterpret_cast<HANDLE>(1));
        TRACKMOUSEEVENT track{sizeof(track), TME_LEAVE, hwnd, 0};
        TrackMouseEvent(&track);
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    if (message == WM_MOUSELEAVE) {
        RemovePropW(hwnd, L"NetMon.Hover");
        InvalidateRect(hwnd, nullptr, FALSE);
    }
    const auto result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == WM_ENABLE ||
        message == BM_SETCHECK || message == BM_SETSTATE || message == WM_SETTEXT)
        InvalidateRect(hwnd, nullptr, FALSE);
    if (message == WM_NCDESTROY) {
        RemovePropW(hwnd, L"NetMon.Hover");
        RemovePropW(hwnd, L"NetMon.Active");
        RemovePropW(hwnd, L"NetMon.Role");
        RemovePropW(hwnd, L"NetMon.Icon");
        RemoveWindowSubclass(hwnd, button_proc, 1);
    }
    return result;
}
void Theme::surface(HWND hwnd, HDC dc) {
    RECT bounds{};
    GetClientRect(hwnd, &bounds);
    wchar_t type[64]{};
    GetClassNameW(hwnd, type, 64);
    if (_wcsicmp(type, WC_HEADERW) == 0) {
        fill(dc, bounds, colors_.field);
        for (int i = 0; i < Header_GetItemCount(hwnd); ++i) {
            RECT r{};
            Header_GetItemRect(hwnd, i, &r);
            if (r.right - r.left <= 0)
                continue;
            wchar_t text[160]{};
            HDITEMW item{};
            item.mask = HDI_TEXT;
            item.pszText = text;
            item.cchTextMax = 160;
            Header_GetItem(hwnd, i, &item);
            r.left += px(10);
            r.right -= px(5);
            label(dc, text, r, colors_.muted, font(Font::caption));
        }
        fill(dc, {bounds.left, bounds.bottom - 1, bounds.right, bounds.bottom}, colors_.border);
    } else {
        round_rect(dc, bounds, colors_.field, GetFocus() == hwnd ? colors_.accent : colors_.border,
                   px(8));
        std::wstring value;
        const auto index = SendMessageW(hwnd, CB_GETCURSEL, 0, 0);
        const auto length =
            index >= 0 ? SendMessageW(hwnd, CB_GETLBTEXTLEN, static_cast<WPARAM>(index), 0) : 0;
        if (length > 0 && length < 4096) {
            value.resize(static_cast<std::size_t>(length) + 1);
            SendMessageW(hwnd, CB_GETLBTEXT, static_cast<WPARAM>(index),
                         reinterpret_cast<LPARAM>(value.data()));
            value.resize(static_cast<std::size_t>(length));
        }
        RECT r = bounds;
        r.left += px(10);
        r.right -= px(26);
        label(dc, value, r, IsWindowEnabled(hwnd) ? colors_.text : colors_.muted, font());
        Gdiplus::Graphics graphics(dc);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        Gdiplus::Pen pen(nm::ui::color(colors_.muted), 1.5f);
        const float x = static_cast<float>(bounds.right - px(17)),
                    y = static_cast<float>(bounds.bottom / 2 - px(2));
        graphics.DrawLine(&pen, x - 3.0f, y, x, y + 3.0f);
        graphics.DrawLine(&pen, x, y + 3.0f, x + 3.0f, y);
    }
}
LRESULT CALLBACK Theme::surface_proc(HWND hwnd, UINT message, WPARAM w, LPARAM l, UINT_PTR,
                                     DWORD_PTR data) {
    auto *theme = reinterpret_cast<Theme *>(data);
    if (message == WM_PAINT) {
        PAINTSTRUCT paint{};
        auto dc = BeginPaint(hwnd, &paint);
        theme->surface(hwnd, dc);
        EndPaint(hwnd, &paint);
        return 0;
    }
    if (message == WM_PRINTCLIENT) {
        theme->surface(hwnd, reinterpret_cast<HDC>(w));
        return 0;
    }
    if (message == WM_ERASEBKGND)
        return 1;
    const auto result = DefSubclassProc(hwnd, message, w, l);
    if (message == WM_SETFOCUS || message == WM_KILLFOCUS || message == CB_SETCURSEL ||
        message == WM_ENABLE || message == WM_SIZE)
        InvalidateRect(hwnd, nullptr, FALSE);
    if (message == WM_NCDESTROY)
        RemoveWindowSubclass(hwnd, surface_proc, 2);
    return result;
}
bool Theme::notify(NMHDR *header, HWND list, LRESULT &result) {
    if (!list || header->code != NM_CUSTOMDRAW)
        return false;
    if (header->hwndFrom == ListView_GetHeader(list)) {
        auto *draw = reinterpret_cast<NMCUSTOMDRAW *>(header);
        if (draw->dwDrawStage == CDDS_PREPAINT)
            result = CDRF_NOTIFYITEMDRAW;
        else if (draw->dwDrawStage == CDDS_ITEMPREPAINT) {
            fill(draw->hdc, draw->rc, colors_.field);
            wchar_t text[160]{};
            HDITEMW item{};
            item.mask = HDI_TEXT;
            item.pszText = text;
            item.cchTextMax = 160;
            Header_GetItem(header->hwndFrom, static_cast<int>(draw->dwItemSpec), &item);
            RECT r = draw->rc;
            r.left += px(12);
            label(draw->hdc, text, r, colors_.muted, font(Font::caption));
            fill(draw->hdc, {draw->rc.left, draw->rc.bottom - 1, draw->rc.right, draw->rc.bottom},
                 colors_.border);
            result = CDRF_SKIPDEFAULT;
        } else
            result = CDRF_DODEFAULT;
        return true;
    }
    if (header->hwndFrom == list) {
        auto *draw = reinterpret_cast<NMLVCUSTOMDRAW *>(header);
        if (draw->nmcd.dwDrawStage == CDDS_PREPAINT)
            result = CDRF_NOTIFYITEMDRAW;
        else if (draw->nmcd.dwDrawStage == CDDS_ITEMPREPAINT) {
            if (ListView_GetItemState(list, static_cast<int>(draw->nmcd.dwItemSpec),
                                      LVIS_SELECTED) &
                LVIS_SELECTED) {
                const auto r = draw->nmcd.rc;
                fill(draw->nmcd.hdc, r, dark_ ? RGB(31, 57, 71) : RGB(219, 237, 245));
                int x = -GetScrollPos(list, SB_HORZ);
                for (int col = 0; col < Header_GetItemCount(ListView_GetHeader(list)); ++col) {
                    const auto width = ListView_GetColumnWidth(list, col);
                    if (width > 0) {
                        wchar_t value[2048]{};
                        ListView_GetItemText(list, static_cast<int>(draw->nmcd.dwItemSpec), col,
                                             value, 2048);
                        label(draw->nmcd.hdc, value,
                              {x + px(6), r.top, x + width - px(5), r.bottom}, colors_.text,
                              font(col == 1 ? Font::strong : Font::body));
                    }
                    x += width;
                }
                result = CDRF_SKIPDEFAULT;
            } else {
                draw->clrText = colors_.text;
                draw->clrTextBk = colors_.panel;
                result = CDRF_NOTIFYSUBITEMDRAW;
            }
        } else if (draw->nmcd.dwDrawStage == (CDDS_ITEMPREPAINT | CDDS_SUBITEM)) {
            draw->clrText = colors_.text;
            draw->clrTextBk = (draw->nmcd.dwItemSpec % 2) ? colors_.field : colors_.panel;
            if (draw->iSubItem == 5) {
                wchar_t value[64]{};
                ListView_GetItemText(list, static_cast<int>(draw->nmcd.dwItemSpec), 5, value, 64);
                const std::wstring status = value;
                draw->clrText = severity(status == L"bad" || status == L"threat"          ? 3
                                         : status == L"suspicious" || status == L"review" ? 2
                                         : status == L"good"                              ? 0
                                                                                          : 1);
            }
            SelectObject(draw->nmcd.hdc, font(draw->iSubItem == 1 ? Font::strong : Font::body));
            result = CDRF_NEWFONT;
        } else
            result = CDRF_DODEFAULT;
        return true;
    }
    return false;
}
} // namespace nm::ui
