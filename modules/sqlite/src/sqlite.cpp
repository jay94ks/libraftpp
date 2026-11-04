#include <raftpp/sqlite/sqlite.hpp>
#include "database_storage.hpp"
#include <sqlite3.h>

namespace raftpp::detail {
    class sqlite_engine final : public database_engine {
        struct db_deleter {
            void operator()(sqlite3* value) const noexcept {
                sqlite3_close_v2(value);
            }
        };
        struct statement_deleter {
            void operator()(sqlite3_stmt* value) const noexcept {
                sqlite3_finalize(value);
            }
        };
        owned<sqlite3, db_deleter> database;
        error_info diagnostic;
        size_t max_value = 0, max_rows = 0;
        error_code status(int code) noexcept {
            if (code == SQLITE_OK || code == SQLITE_DONE || code == SQLITE_ROW)
                return {};
            const auto primary = code & 255;
            auto error =
                primary == SQLITE_NOMEM                                 ? errc::out_of_memory
                : primary == SQLITE_BUSY || primary == SQLITE_LOCKED    ? errc::busy
                : primary == SQLITE_FULL                                ? errc::no_space
                : primary == SQLITE_CORRUPT || primary == SQLITE_NOTADB ? errc::corrupt
                : primary == SQLITE_TOOBIG                              ? errc::too_big
                : primary == SQLITE_READONLY || primary == SQLITE_PERM || primary == SQLITE_AUTH
                    ? errc::unauthorized
                    : errc::io_error;
            diagnostic = make_error_info(error, "sqlite", sqlite3_errstr(code));
            return error;
        }
        error_code execute(const char* sql) {
            return status(sqlite3_exec(database.get(), sql, nullptr, nullptr, nullptr));
        }
        error_code prepare(const char* sql, owned<sqlite3_stmt, statement_deleter>& output) {
            sqlite3_stmt* raw = nullptr;
            auto code = sqlite3_prepare_v2(database.get(), sql, -1, &raw, nullptr);
            output.reset(raw);
            return status(code);
        }
        error_code bind(sqlite3_stmt* statement, int position, string_view value) {
            if (value.size() > static_cast<size_t>(numeric_limits<int>::max()))
                return errc::too_big;
            return status(sqlite3_bind_blob(statement, position, value.data(),
                                            static_cast<int>(value.size()), SQLITE_TRANSIENT));
        }
        string column(sqlite3_stmt* statement, int index) {
            auto count = sqlite3_column_bytes(statement, index);
            auto bytes = sqlite3_column_blob(statement, index);
            if (!bytes && count)
                throw bad_alloc{};
            return count ? string(static_cast<const char*>(bytes), static_cast<size_t>(count))
                         : string{};
        }

    public:
        error_code open(const fs::path& path, size_t value, size_t rows) override {
            if (database)
                return errc::busy;
            max_value = value;
            max_rows = rows;
            sqlite3* raw = nullptr;
            auto code = sqlite3_open_v2(
                path.string().c_str(), &raw,
                SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX, nullptr);
            database.reset(raw);
            if (auto error = status(code))
                return error;
            sqlite3_extended_result_codes(database.get(), 1);
            sqlite3_busy_timeout(database.get(), 0);
            if (auto error = execute(
                    "PRAGMA locking_mode=EXCLUSIVE; PRAGMA journal_mode=DELETE; PRAGMA synchronous=FULL;"))
                return error;
            return execute(
                "BEGIN EXCLUSIVE; CREATE TABLE IF NOT EXISTS raftpp_kv (key BLOB PRIMARY KEY, value BLOB NOT NULL) WITHOUT ROWID; COMMIT;");
        }
        error_code get(string_view key, string& output) override {
            owned<sqlite3_stmt, statement_deleter> statement;
            if (auto error = prepare("SELECT value FROM raftpp_kv WHERE key=?1", statement))
                return error;
            if (auto error = bind(statement.get(), 1, key))
                return error;
            auto result = sqlite3_step(statement.get());
            if (result == SQLITE_DONE)
                return errc::not_found;
            if (auto error = status(result))
                return error;
            if (sqlite3_column_type(statement.get(), 0) != SQLITE_BLOB)
                return errc::corrupt;
            if (static_cast<size_t>(sqlite3_column_bytes(statement.get(), 0)) > max_value)
                return errc::too_big;
            output = column(statement.get(), 0);
            return {};
        }
        error_code scan(vector<pair<string, string>>& output) override {
            owned<sqlite3_stmt, statement_deleter> statement;
            if (auto error = prepare("SELECT key,value FROM raftpp_kv ORDER BY key", statement))
                return error;
            int result;
            while ((result = sqlite3_step(statement.get())) == SQLITE_ROW) {
                if (sqlite3_column_type(statement.get(), 0) != SQLITE_BLOB ||
                    sqlite3_column_type(statement.get(), 1) != SQLITE_BLOB)
                    return errc::corrupt;
                if (output.size() >= max_rows || sqlite3_column_bytes(statement.get(), 0) > 16 ||
                    static_cast<size_t>(sqlite3_column_bytes(statement.get(), 1)) > max_value)
                    return errc::too_big;
                output.emplace_back(column(statement.get(), 0), column(statement.get(), 1));
            }
            return status(result);
        }
        error_code write(const vector<database_change>& changes) override {
            if (auto error = execute("BEGIN EXCLUSIVE"))
                return error;
            struct rollback {
                sqlite3* database;
                ~rollback() {
                    if (database)
                        sqlite3_exec(database, "ROLLBACK", nullptr, nullptr, nullptr);
                }
            } guard{database.get()};
            owned<sqlite3_stmt, statement_deleter> put, erase;
            if (auto error = prepare(
                    "INSERT INTO raftpp_kv(key,value) VALUES(?1,?2) ON CONFLICT(key) DO UPDATE SET value=excluded.value",
                    put))
                return error;
            if (auto error = prepare("DELETE FROM raftpp_kv WHERE key=?1", erase))
                return error;
            for (const auto& change : changes) {
                auto statement = change.value ? put.get() : erase.get();
                if (auto error = status(sqlite3_reset(statement)))
                    return error;
                if (auto error = bind(statement, 1, change.key))
                    return error;
                if (change.value)
                    if (auto error = bind(statement, 2, *change.value))
                        return error;
                if (auto error = status(sqlite3_step(statement)))
                    return error;
            }
            if (auto error = execute("COMMIT"))
                return error;
            guard.database = nullptr;
            return {};
        }
        void close() noexcept override {
            database.reset();
        }
        error_info error_details() const noexcept override {
            return diagnostic;
        }
    };
}
namespace raftpp {
    sqlite_storage::sqlite_storage(fs::path path, storage_limits limits)
        : state_(std::make_shared<detail::database_state>(std::make_unique<detail::sqlite_engine>(),
                                                          std::move(path), limits)) {}
    sqlite_storage::~sqlite_storage() {
        auto state = state_;
        state->close();
    }
    error_code sqlite_storage::init(server_id id, string_view address) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.init", [&] { return state->init(id, address); });
    }
    error_code sqlite_storage::bootstrap(const configuration& value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.bootstrap",
                          [&] { return state->configuration_entry(value, false); });
    }
    error_code sqlite_storage::recover(const configuration& value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.recover",
                          [&] { return state->configuration_entry(value, true); });
    }
    error_code sqlite_storage::load(io_state& output) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.load", [&] { return state->load(output); });
    }
    error_code sqlite_storage::set_term(term value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.set_term", [&] { return state->set_term(value); });
    }
    error_code sqlite_storage::set_vote(server_id value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.set_vote", [&] { return state->set_vote(value); });
    }
    error_code sqlite_storage::truncate(log_index index) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("sqlite.truncate", [&] { return state->truncate(index); });
    }
    error_code sqlite_storage::append(log_index index, shared<const entry_batch> value,
                                      completion callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        auto error = state->run("sqlite.append", [&] { return state->append(index, value); });
        if (!error) {
            try {
                if (callback)
                    callback({});
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "sqlite.callback");
                state->close();
            }
        }
        callback = {};
        value.reset();
        return error;
    }
    error_code sqlite_storage::snapshot_put(unsigned trailing, shared<const snapshot> value,
                                            completion callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        auto error =
            state->run("sqlite.snapshot_put", [&] { return state->snapshot_put(trailing, value); });
        if (!error) {
            try {
                if (callback)
                    callback({});
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "sqlite.callback");
                state->close();
            }
        }
        callback = {};
        value.reset();
        return error;
    }

    error_code sqlite_storage::snapshot_get(snapshot_callback callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        snapshot value;
        bool found = false;
        auto error =
            state->run("sqlite.snapshot_get", [&] { return state->snapshot_get(value, found); });
        if (!error) {
            try {
                if (callback) {
                    if (found)
                        callback({}, optional<snapshot>{std::move(value)});
                    else
                        callback({}, std::nullopt);
                }
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "sqlite.callback");
                state->close();
            }
        }
        callback = {};
        value = snapshot{};
        return error;
    }
    void sqlite_storage::close(close_callback callback) noexcept {
        auto state = state_;
        state->close(std::move(callback));
    }
    uint16_t sqlite_storage::capacity() const noexcept {
        return state_->capacity();
    }
    error_info sqlite_storage::error_details() const noexcept {
        return state_->diagnostic;
    }
}
