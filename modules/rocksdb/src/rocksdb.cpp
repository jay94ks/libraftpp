#include <raftpp/rocksdb/rocksdb.hpp>
#include "database_storage.hpp"
#include <rocksdb/db.h>
#include <rocksdb/write_batch.h>

namespace raftpp::detail {
    class rocksdb_engine final : public database_engine {
        owned<rocksdb::DB> database;
        error_info diagnostic;
        size_t max_value = 0, max_rows = 0;
        error_code status(const rocksdb::Status& value) {
            if (value.ok())
                return {};
            const auto error = value.IsNotFound()                     ? errc::not_found
                               : value.IsCorruption()                 ? errc::corrupt
                               : value.IsInvalidArgument()            ? errc::invalid
                               : value.IsNoSpace()                    ? errc::no_space
                               : value.IsMemoryLimit()                ? errc::out_of_memory
                               : value.IsBusy() || value.IsTimedOut() ? errc::busy
                                                                      : errc::io_error;
            diagnostic = make_error_info(error, "rocksdb", value.ToString());
            return error;
        }

    public:
        error_code open(const fs::path& path, size_t value, size_t rows) override {
            if (database)
                return errc::busy;
            max_value = value;
            max_rows = rows;
            rocksdb::Options options;
            options.create_if_missing = true;
            options.paranoid_checks = true;
            options.compression = rocksdb::kNoCompression;
            options.bottommost_compression = rocksdb::kNoCompression;
            const auto result = rocksdb::DB::Open(options, path.string(), &database);
            return status(result);
        }
        error_code get(string_view key, string& output) override {
            rocksdb::PinnableSlice bytes;
            if (auto error =
                    status(database->Get(rocksdb::ReadOptions{}, database->DefaultColumnFamily(),
                                         rocksdb::Slice(key.data(), key.size()), &bytes)))
                return error;
            if (bytes.size() > max_value)
                return errc::too_big;
            output.assign(bytes.data(), bytes.size());
            return {};
        }
        error_code scan(vector<pair<string, string>>& output) override {
            rocksdb::ReadOptions options;
            options.verify_checksums = true;
            owned<rocksdb::Iterator> iterator{database->NewIterator(options)};
            for (iterator->SeekToFirst(); iterator->Valid(); iterator->Next()) {
                if (output.size() >= max_rows || iterator->key().size() > 16 ||
                    iterator->value().size() > max_value)
                    return errc::too_big;
                output.emplace_back(iterator->key().ToString(), iterator->value().ToString());
            }
            return status(iterator->status());
        }
        error_code write(const vector<database_change>& changes) override {
            rocksdb::WriteBatch batch;
            for (const auto& change : changes) {
                if (auto error = status(change.value ? batch.Put(change.key, *change.value)
                                                     : batch.Delete(change.key)))
                    return error;
            }
            rocksdb::WriteOptions options;
            options.sync = true;
            options.disableWAL = false;
            return status(database->Write(options, &batch));
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
    rocksdb_storage::rocksdb_storage(fs::path path, storage_limits limits)
        : state_(std::make_shared<detail::database_state>(
              std::make_unique<detail::rocksdb_engine>(), std::move(path), limits)) {}
    rocksdb_storage::~rocksdb_storage() {
        auto state = state_;
        state->close();
    }
    error_code rocksdb_storage::init(server_id id, string_view address) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.init", [&] { return state->init(id, address); });
    }
    error_code rocksdb_storage::bootstrap(const configuration& value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.bootstrap",
                          [&] { return state->configuration_entry(value, false); });
    }
    error_code rocksdb_storage::recover(const configuration& value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.recover",
                          [&] { return state->configuration_entry(value, true); });
    }
    error_code rocksdb_storage::load(io_state& output) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.load", [&] { return state->load(output); });
    }
    error_code rocksdb_storage::set_term(term value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.set_term", [&] { return state->set_term(value); });
    }
    error_code rocksdb_storage::set_vote(server_id value) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.set_vote", [&] { return state->set_vote(value); });
    }
    error_code rocksdb_storage::truncate(log_index index) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        return state->run("rocksdb.truncate", [&] { return state->truncate(index); });
    }
    error_code rocksdb_storage::append(log_index index, shared<const entry_batch> value,
                                       completion callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        auto error = state->run("rocksdb.append", [&] { return state->append(index, value); });
        if (!error) {
            try {
                if (callback)
                    callback({});
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "rocksdb.callback");
                state->close();
            }
        }
        callback = {};
        value.reset();
        return error;
    }
    error_code rocksdb_storage::snapshot_put(unsigned trailing, shared<const snapshot> value,
                                             completion callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        auto error = state->run("rocksdb.snapshot_put",
                                [&] { return state->snapshot_put(trailing, value); });
        if (!error) {
            try {
                if (callback)
                    callback({});
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "rocksdb.callback");
                state->close();
            }
        }
        callback = {};
        value.reset();
        return error;
    }

    error_code rocksdb_storage::snapshot_get(snapshot_callback callback) noexcept {
        auto state = state_;
        detail::database_state::activity guard{state};
        snapshot value;
        bool found = false;
        auto error =
            state->run("rocksdb.snapshot_get", [&] { return state->snapshot_get(value, found); });
        if (!error) {
            try {
                if (callback) {
                    if (found)
                        callback({}, optional<snapshot>{std::move(value)});
                    else
                        callback({}, std::nullopt);
                }
            } catch (...) {
                state->diagnostic = make_error_info(errc::io_error, "rocksdb.callback");
                state->close();
            }
        }
        callback = {};
        value = snapshot{};
        return error;
    }
    void rocksdb_storage::close(close_callback callback) noexcept {
        auto state = state_;
        state->close(std::move(callback));
    }
    uint16_t rocksdb_storage::capacity() const noexcept {
        return state_->capacity();
    }
    error_info rocksdb_storage::error_details() const noexcept {
        return state_->diagnostic;
    }
}
