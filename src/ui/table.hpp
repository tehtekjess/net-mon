#pragma once
#include "platform/windows.hpp"
#include "platform/world.hpp"
#include <array>
#include <memory>
#include <commctrl.h>
namespace nm::ui {
struct DisplayRow {
    std::string id;
    std::array<std::wstring, 7> cells;
    std::wstring details;
    bool operator==(const DisplayRow &other) const {
        return id == other.id && cells == other.cells && details == other.details;
    }
};
struct TableSnapshot {
    Json records = Json::array();
    std::vector<DisplayRow> display;
    bool rules = false;
    MapSnapshot map;
    std::size_t needs_review = 0;
};
// Formatting belongs on the IPC worker, not in selection/paint callbacks.
TableSnapshot prepare_table(Json records, bool rules, std::shared_ptr<const WorldMap> world = {});
class TableView {
    HWND list_ = nullptr, details_ = nullptr;
    std::shared_ptr<const TableSnapshot> snapshot_ = std::make_shared<const TableSnapshot>();
    bool updating_ = false;
    std::wstring detail_text_;
    std::uint64_t detail_writes_ = 0, updates_ = 0;
    void update_details();

  public:
    void attach(HWND list, HWND details);
    bool set_snapshot(std::shared_ptr<const TableSnapshot> snapshot);
    bool set_snapshot(TableSnapshot snapshot) {
        return set_snapshot(std::make_shared<const TableSnapshot>(std::move(snapshot)));
    }
    bool on_notify(NMHDR *notification, LRESULT &result);
    int selected() const;
    const Json &records() const { return snapshot_->records; }
    std::shared_ptr<const TableSnapshot> snapshot() const { return snapshot_; }
    std::uint64_t detail_writes() const { return detail_writes_; }
    std::uint64_t updates() const { return updates_; }
};
} // namespace nm::ui
