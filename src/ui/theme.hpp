#pragma once
#include "platform/windows.hpp"
#include <array>
#include <commctrl.h>
#include <objidl.h>
#include <algorithm>
// The Windows 10.0.15063 GDI+ headers use unqualified min/max and shadow members.
namespace Gdiplus {
using std::max;
using std::min;
} // namespace Gdiplus
#pragma warning(push)
#pragma warning(disable : 4458)
#include <gdiplus.h>
#pragma warning(pop)
namespace nm::ui {
struct Palette {
    COLORREF background, sidebar, panel, field, border, text, muted, accent, blue, green, amber,
        red;
};
enum class Font { body, caption, heading, strong, mono, title };
enum class Button { normal, primary, danger, navigation, quiet };
class Theme {
    Palette colors_{};
    bool dark_ = true;
    UINT dpi_ = 96;
    std::array<HFONT, 6> fonts_{};
    HBRUSH panel_ = nullptr, field_ = nullptr, background_ = nullptr;
    ULONG_PTR graphics_ = 0;
    void resources();
    static LRESULT CALLBACK button_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    static LRESULT CALLBACK surface_proc(HWND, UINT, WPARAM, LPARAM, UINT_PTR, DWORD_PTR);
    void surface(HWND, HDC);
    void button(HWND, HDC);

  public:
    Theme();
    ~Theme();
    Theme(const Theme &) = delete;
    Theme &operator=(const Theme &) = delete;
    void initialize(HWND);
    void set_dark(bool);
    bool dark() const { return dark_; }
    bool high_contrast() const;
    int px(int value) const { return MulDiv(value, static_cast<int>(dpi_), 96); }
    const Palette &colors() const { return colors_; }
    HFONT font(Font kind = Font::body) const { return fonts_[static_cast<std::size_t>(kind)]; }
    HBRUSH background_brush() const { return background_; }
    void apply(HWND root);
    void control(HWND, Button role = Button::normal, int icon = -1);
    void active(HWND, bool);
    LRESULT color(UINT message, HDC, HWND) const;
    bool notify(NMHDR *, HWND list, LRESULT &result);
    COLORREF severity(int value) const;
};
Gdiplus::Color color(COLORREF value, BYTE alpha = 255);
void fill(HDC, RECT, COLORREF);
void round_rect(HDC, RECT, COLORREF fill, COLORREF stroke, int radius = 8);
void label(HDC, const std::wstring &, RECT, COLORREF, HFONT,
           UINT flags = DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
void icon(HDC, int index, RECT, COLORREF);
} // namespace nm::ui
