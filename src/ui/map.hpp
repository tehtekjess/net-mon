#pragma once
#include "ui/table.hpp"
#include "ui/theme.hpp"
namespace nm::ui {
class MapView {
    HWND window_ = nullptr, countries_ = nullptr, endpoints_ = nullptr, info_ = nullptr;
    Theme *theme_ = nullptr;
    std::shared_ptr<const TableSnapshot> snapshot_ = std::make_shared<const TableSnapshot>();
    std::shared_ptr<const TableSnapshot> pending_snapshot_;
    std::string selected_;
    std::vector<std::string> country_codes_;
    std::vector<std::wstring> endpoint_keys_;
    std::wstring info_text_;
    HBITMAP cache_ = nullptr;
    SIZE cache_size_{};
    double zoom_ = 1, pan_x_ = 0, pan_y_ = 0;
    POINT drag_start_{};
    double drag_x_ = 0, drag_y_ = 0;
    bool dragging_ = false, moved_ = false;
    static LRESULT CALLBACK proc(HWND, UINT, WPARAM, LPARAM);
    const MapGroup *group() const;
    RECT map_rect() const;
    POINT project(MapPoint) const;
    int hit(POINT) const;
    void select(const std::string &);
    void controls();
    void layout();
    void paint(HDC);
    void invalidate_cache();
    void set_zoom(double);
    void details();

  public:
    ~MapView();
    HWND create(HWND parent, HINSTANCE instance, Theme &theme);
    HWND window() const { return window_; }
    void set_snapshot(std::shared_ptr<const TableSnapshot>);
    void theme_changed();
};
} // namespace nm::ui
