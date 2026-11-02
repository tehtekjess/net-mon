#include "storage/store.hpp"
#include "platform/windows.hpp"
#include <algorithm>
#include <iomanip>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <winsqlite/winsqlite3.h>
namespace nm {
namespace {
constexpr std::uint64_t MiB = 1024 * 1024, segment_limit = 8 * MiB,
                        reserve = 2 * segment_limit + 65536;
struct Db {
    sqlite3 *p = nullptr;
    ~Db() {
        if (p)
            sqlite3_close(p);
    }
};
struct Statement {
    sqlite3_stmt *p = nullptr;
    Statement(sqlite3 *db, const char *sql) {
        if (sqlite3_prepare_v2(db, sql, -1, &p, nullptr) != SQLITE_OK)
            throw std::runtime_error(sqlite3_errmsg(db));
    }
    ~Statement() { sqlite3_finalize(p); }
    void text(int i, const std::string &s) {
        if (sqlite3_bind_text(p, i, s.c_str(), static_cast<int>(s.size()), SQLITE_TRANSIENT) !=
            SQLITE_OK)
            throw std::runtime_error("SQLite bind failed");
    }
};
void exec(sqlite3 *db, const char *sql) {
    if (sqlite3_exec(db, sql, nullptr, nullptr, nullptr) != SQLITE_OK)
        throw std::runtime_error(sqlite3_errmsg(db));
}
void readonly(Db &db, const std::filesystem::path &path) {
    if (sqlite3_open_v2(utf8(path.wstring()).c_str(), &db.p, SQLITE_OPEN_READONLY, nullptr) !=
        SQLITE_OK)
        throw std::runtime_error("Cannot open history segment");
}
std::string column(sqlite3_stmt *p, int n) {
    const auto *v = sqlite3_column_text(p, n);
    return v ? reinterpret_cast<const char *>(v) : "";
}
} // namespace
Store::Store(const std::filesystem::path &root, Settings s)
    : dir_(root / L"history"), settings_(s) {
    validate(settings_);
    std::filesystem::create_directories(dir_);
    // Every run starts a fresh segment; prior segments are never modified.
    open_new();
    maintain();
}
Store::~Store() { close(); }
void Store::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}
std::vector<std::filesystem::path> Store::segments() const {
    std::vector<std::filesystem::path> out;
    const std::regex pattern("history-[0-9]{20}\\.db");
    for (const auto &item : std::filesystem::directory_iterator(dir_))
        if (item.is_regular_file() && !item.is_symlink() &&
            std::regex_match(item.path().filename().string(), pattern))
            out.push_back(item.path());
    std::sort(out.begin(), out.end());
    return out;
}
void Store::open_new() {
    close();
    std::ostringstream name;
    sequence_ = std::max(sequence_ + 1, static_cast<std::uint64_t>(now_ms()) * 1000);
    name << "history-" << std::setw(20) << std::setfill('0') << sequence_ << ".db";
    active_ = dir_ / name.str();
    if (sqlite3_open_v2(utf8(active_.wstring()).c_str(), &db_,
                        SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr) != SQLITE_OK)
        throw std::runtime_error("Cannot create history: " + std::string(sqlite3_errmsg(db_)));
    exec(db_, "PRAGMA page_size=4096; PRAGMA journal_mode=DELETE; PRAGMA synchronous=NORMAL; "
              "PRAGMA temp_store=MEMORY; PRAGMA cache_size=-1024; PRAGMA max_page_count=2048;"
              "CREATE TABLE IF NOT EXISTS events(id TEXT PRIMARY KEY,time INTEGER NOT NULL,search "
              "TEXT NOT NULL,alert INTEGER NOT NULL,payload TEXT NOT NULL);"
              "CREATE INDEX IF NOT EXISTS events_time ON events(time); PRAGMA user_version=1;");
}
std::uint64_t Store::bytes() const {
    std::uint64_t result = 0;
    for (const auto &f : std::filesystem::directory_iterator(dir_))
        if (f.is_regular_file())
            result += f.file_size();
    return result;
}
void Store::configure(const Settings &s) {
    settings_ = s;
    maintain();
}
void Store::maintain() {
    const auto cutoff = now_ms() - static_cast<Millis>(settings_.retention_days) * 86400000;
    auto paths = segments();
    for (const auto &p : paths) {
        if (p == active_)
            continue;
        bool expired = false;
        {
            Db reader;
            readonly(reader, p);
            Statement statement(reader.p, "SELECT coalesce(max(time),0) FROM events");
            if (sqlite3_step(statement.p) == SQLITE_ROW)
                expired = sqlite3_column_int64(statement.p, 0) < cutoff;
        }
        if (expired)
            std::filesystem::remove(p);
    }
    auto used = bytes();
    const auto budget = static_cast<std::uint64_t>(settings_.history_mib) * MiB;
    for (const auto &p : segments()) {
        if (used + reserve + 65536 <= budget)
            break;
        if (p == active_)
            continue;
        auto n = std::filesystem::file_size(p);
        std::filesystem::remove(p);
        used -= n;
    }
    if (used + reserve + 65536 > budget)
        throw std::runtime_error("History storage budget exhausted");
}
void Store::append(const Event &e) { append_batch({e}); }
void Store::append_batch(const std::vector<Event> &events) {
    for (size_t begin = 0; begin < events.size(); begin += 32) {
        const auto end = std::min(begin + 32, events.size());
        std::vector<std::string> payloads;
        std::uint64_t estimate = 65536;
        for (size_t i = begin; i < end; ++i) {
            auto payload = Json(events[i]).dump();
            if (payload.size() > 32768)
                throw std::runtime_error("Event exceeds 32 KiB");
            estimate += payload.size() * 3 + 4096;
            payloads.push_back(std::move(payload));
        }
        if (std::filesystem::file_size(active_) + estimate > segment_limit - 65536)
            open_new();
        maintain();
        exec(db_, "BEGIN IMMEDIATE");
        try {
            Statement statement(db_, "INSERT OR IGNORE INTO events VALUES(?1,?2,?3,?4,?5)");
            for (size_t i = begin; i < end; ++i) {
                const auto &e = events[i];
                sqlite3_reset(statement.p);
                sqlite3_clear_bindings(statement.p);
                statement.text(1, e.id);
                sqlite3_bind_int64(statement.p, 2, e.time);
                statement.text(3, lower_ascii(e.app + " " + e.remote_ip + " " + e.reason + " " +
                                              e.classification + " " + e.state));
                sqlite3_bind_int(statement.p, 4, e.alert ? 1 : 0);
                statement.text(5, payloads[i - begin]);
                if (sqlite3_step(statement.p) != SQLITE_DONE)
                    throw std::runtime_error(sqlite3_errmsg(db_));
            }
            exec(db_, "COMMIT");
        } catch (...) {
            sqlite3_exec(db_, "ROLLBACK", nullptr, nullptr, nullptr);
            throw;
        }
    }
}
Json Store::query(std::string search, int offset, int limit, bool alerts_only,
                  std::atomic<bool> *cancelled) {
    const auto check_cancel = [cancelled] {
        if (cancelled && cancelled->load())
            throw std::runtime_error("Query cancelled");
    };
    check_cancel();
    if (search.size() > 256 || offset < 0 || offset > 100000 || limit < 1 || limit > 500)
        throw std::invalid_argument("Invalid history query");
    std::string escaped;
    for (char c : lower_ascii(search)) {
        if (c == '%' || c == '_' || c == '\\')
            escaped += '\\';
        escaped += c;
    }
    auto paths = segments();
    Json result = Json::array();
    const auto cutoff = now_ms() - static_cast<Millis>(settings_.retention_days) * 86400000;
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        check_cancel();
        Db reader;
        readonly(reader, *it);
        if (cancelled)
            sqlite3_progress_handler(
                reader.p, 1000,
                [](void *context) {
                    return static_cast<std::atomic<bool> *>(context)->load() ? 1 : 0;
                },
                cancelled);
        Statement statement(reader.p,
                            "SELECT payload FROM events WHERE time>=?1 AND search LIKE ?2 ESCAPE "
                            "'\\' AND (?3=0 OR alert=1) ORDER BY time DESC,rowid DESC");
        sqlite3_bind_int64(statement.p, 1, cutoff);
        statement.text(2, "%" + escaped + "%");
        sqlite3_bind_int(statement.p, 3, alerts_only ? 1 : 0);
        int rc = 0;
        while ((rc = sqlite3_step(statement.p)) == SQLITE_ROW) {
            check_cancel();
            if (offset > 0) {
                --offset;
                continue;
            }
            result.push_back(Json::parse(column(statement.p, 0)));
            if (result.size() >= static_cast<size_t>(limit))
                return result;
        }
        check_cancel();
        if (rc != SQLITE_DONE)
            throw std::runtime_error(sqlite3_errmsg(reader.p));
    }
    return result;
}
std::optional<Event> Store::find(const std::string &id) {
    if (id.size() > 256)
        return {};
    auto paths = segments();
    for (auto it = paths.rbegin(); it != paths.rend(); ++it) {
        Db reader;
        readonly(reader, *it);
        Statement s(reader.p, "SELECT payload FROM events WHERE id=?1");
        s.text(1, id);
        if (sqlite3_step(s.p) == SQLITE_ROW)
            return Json::parse(column(s.p, 0)).get<Event>();
    }
    return {};
}
} // namespace nm
