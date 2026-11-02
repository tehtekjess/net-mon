#include "ui/table.hpp"
#include <algorithm>
namespace nm::ui {
namespace {
std::wstring when(Millis time) {
    ULARGE_INTEGER u{};
    u.QuadPart = static_cast<ULONGLONG>(time + 11644473600000LL) * 10000;
    FILETIME ft{u.LowPart, u.HighPart}, local{};
    SYSTEMTIME st{};
    FileTimeToLocalFileTime(&ft, &local);
    FileTimeToSystemTime(&local, &st);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u-%02u-%02u %02u:%02u:%02u", st.wYear, st.wMonth, st.wDay, st.wHour,
               st.wMinute, st.wSecond);
    return text;
}
std::wstring basename(const std::string &path) {
    return std::filesystem::path(wide(path)).filename().wstring();
}
std::string endpoint(const std::string &ip, std::uint16_t port) {
    return (ip.find(':') == std::string::npos ? ip : "[" + ip + "]") + ":" + std::to_string(port);
}
} // namespace
TableSnapshot prepare_table(Json records, bool rules, std::shared_ptr<const WorldMap> world) {
    TableSnapshot snapshot;
    snapshot.records = std::move(records);
    snapshot.rules = rules;
    if (!rules)
        snapshot.map = prepare_map(snapshot.records, std::move(world));
    snapshot.display.reserve(snapshot.records.size());
    for (const auto &item : snapshot.records) {
        DisplayRow row;
        row.id = item.value("id", "");
        if (rules) {
            const auto r = item.get<Rule>();
            row.cells = {when(r.created).substr(0, 10),
                         basename(r.app),
                         wide(endpoint(r.remote_ip, r.remote_port)),
                         wide(item.value("country", "-")),
                         wide(r.scope),
                         wide(r.verdict),
                         wide(r.reason.empty() ? "Saved rule" : r.reason)};
            row.details = wide(item.dump(2));
        } else {
            const auto e = item.get<Event>();
            if (e.classification == "unknown" || e.classification == "review")
                ++snapshot.needs_review;
            row.cells = {when(e.time).substr(11),
                         basename(e.app),
                         wide(endpoint(e.remote_ip, e.remote_port)),
                         wide(item.value("country", "Unavailable")),
                         wide(e.state),
                         wide(e.classification),
                         wide(e.reason)};
            row.details = wide(
                "Observed: " + utf8(when(e.time)) + "\r\nProcess: " + e.app +
                "\r\nPID: " + std::to_string(e.pid) +
                "   Instance: " + (e.process_key.empty() ? "unavailable" : e.process_key) +
                "\r\nDestination: " + endpoint(e.remote_ip, e.remote_port) +
                "   Protocol: " + std::to_string(e.protocol) +
                "\r\nLocal: " + endpoint(e.local_ip, e.local_port) + "\r\nObservation: " + e.state +
                "   Source: " + e.source + "\r\nDecision: " + e.classification + " - " + e.reason +
                "\r\nCountry (estimate): " + item.value("country", "Unavailable") +
                "\r\nIP Geolocation by DB-IP (db-ip.com), November 2026. VPN/CDN/anycast locations "
                "can differ." +
                "\r\nIdentity: " +
                (e.identity_status.empty() ? "Unavailable in this record" : e.identity_status) +
                "\r\nSignature: " + e.signature +
                "   SHA-256: " + (e.hash.empty() ? "unavailable" : e.hash) +
                "\r\nUser SID: " + e.user + "\r\nRule: " + e.rule_id +
                "\r\nGood rules match this executable hash; Bad does not block traffic.");
        }
        snapshot.display.push_back(std::move(row));
    }
    return snapshot;
}
void TableView::attach(HWND list, HWND details) {
    list_ = list;
    details_ = details;
    update_details();
}
int TableView::selected() const {
    return list_ ? ListView_GetNextItem(list_, -1, LVNI_SELECTED) : -1;
}
void TableView::update_details() {
    const auto index = selected();
    static const std::wstring empty =
        L"Select a connection to inspect its process, endpoint, and classification.";
    const auto &value = index >= 0 && static_cast<std::size_t>(index) < snapshot_->display.size()
                            ? snapshot_->display[static_cast<std::size_t>(index)].details
                            : empty;
    if (value == detail_text_)
        return;
    detail_text_ = value;
    SetWindowTextW(details_, detail_text_.c_str());
    ++detail_writes_;
}
bool TableView::set_snapshot(std::shared_ptr<const TableSnapshot> next) {
    if (snapshot_ == next ||
        (snapshot_->rules == next->rules && snapshot_->display == next->display))
        return false;
    const bool same_order =
        snapshot_->rules == next->rules && snapshot_->display.size() == next->display.size() &&
        std::equal(snapshot_->display.begin(), snapshot_->display.end(), next->display.begin(),
                   [](const DisplayRow &a, const DisplayRow &b) { return a.id == b.id; });
    if (same_order) {
        const auto previous = std::move(snapshot_);
        snapshot_ = std::move(next);
        if (IsWindowVisible(list_)) {
            const auto top = ListView_GetTopIndex(list_);
            const auto end = std::min(static_cast<int>(snapshot_->display.size()),
                                      top + ListView_GetCountPerPage(list_) + 1);
            for (int i = top; i < end; ++i) {
                const auto row = static_cast<std::size_t>(i);
                if (previous->display[row].cells != snapshot_->display[row].cells) {
                    RECT bounds{};
                    if (ListView_GetItemRect(list_, i, &bounds, LVIR_BOUNDS))
                        InvalidateRect(list_, &bounds, FALSE);
                }
            }
        }
        update_details();
        ++updates_;
        return true;
    }
    const auto selection = selected();
    const auto top = ListView_GetTopIndex(list_);
    auto id_at = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < snapshot_->display.size()
                   ? snapshot_->display[static_cast<std::size_t>(index)].id
                   : std::string{};
    };
    const auto selected_id = id_at(selection);
    const auto top_id = (top > 0 || selection >= 0) ? id_at(top) : std::string{};
    auto find = [&](const std::string &id) {
        if (id.empty())
            return -1;
        auto it = std::find_if(next->display.begin(), next->display.end(),
                               [&](const auto &row) { return row.id == id; });
        return it == next->display.end() ? -1 : static_cast<int>(it - next->display.begin());
    };
    const auto next_selection = find(selected_id), next_top = find(top_id);
    updating_ = true;
    // Do not toggle WM_SETREDRAW: it changes visibility and can cause accessibility
    // clients to rebuild the entire virtual tree on every incoming record.
    const auto previous_count = snapshot_->display.size();
    if (selection >= 0 && selection != next_selection)
        ListView_SetItemState(list_, selection, 0, LVIS_SELECTED | LVIS_FOCUSED);
    snapshot_ = std::move(next);
    if (snapshot_->display.size() != previous_count)
        ListView_SetItemCountEx(list_, static_cast<int>(snapshot_->display.size()),
                                LVSICF_NOINVALIDATEALL | LVSICF_NOSCROLL);
    if (next_selection >= 0 && next_selection != selection)
        ListView_SetItemState(list_, next_selection, LVIS_SELECTED | LVIS_FOCUSED,
                              LVIS_SELECTED | LVIS_FOCUSED);
    if (next_top >= 0) {
        RECT bounds{};
        if (ListView_GetItemRect(list_, next_top, &bounds, LVIR_BOUNDS))
            ListView_Scroll(
                list_, 0, (next_top - ListView_GetTopIndex(list_)) * (bounds.bottom - bounds.top));
    }
    updating_ = false;
    InvalidateRect(list_, nullptr, FALSE);
    update_details();
    ++updates_;
    return true;
}
bool TableView::on_notify(NMHDR *header, LRESULT &result) {
    if (header->hwndFrom != list_)
        return false;
    result = 0;
    if (header->code == LVN_GETDISPINFOW) {
        auto *info = reinterpret_cast<NMLVDISPINFOW *>(header);
        const auto row = info->item.iItem, col = info->item.iSubItem;
        if ((info->item.mask & LVIF_TEXT) && row >= 0 && col >= 0 && col < 7 &&
            static_cast<std::size_t>(row) < snapshot_->display.size())
            info->item.pszText =
                const_cast<wchar_t *>(snapshot_->display[static_cast<std::size_t>(row)]
                                          .cells[static_cast<std::size_t>(col)]
                                          .c_str());
        return true;
    }
    if (updating_)
        return false;
    if (header->code == LVN_ITEMCHANGED) {
        const auto *change = reinterpret_cast<NMLISTVIEW *>(header);
        if ((change->uChanged & LVIF_STATE) && (change->uNewState & LVIS_SELECTED) &&
            ((change->uOldState ^ change->uNewState) & LVIS_SELECTED))
            update_details();
        return true;
    }
    if (header->code == LVN_ODSTATECHANGED || header->code == NM_CLICK) {
        update_details();
        return true;
    }
    return false;
}
} // namespace nm::ui
