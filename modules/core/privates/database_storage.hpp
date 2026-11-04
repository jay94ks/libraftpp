#ifndef RAFTPP_PRIVATE_DATABASE_STORAGE_HPP
#define RAFTPP_PRIVATE_DATABASE_STORAGE_HPP
#include <raftpp/storage.hpp>
#include <raftpp/encoding.hpp>
#include "byte.hpp"
#include <algorithm>

namespace raftpp::detail {
    struct database_change {
        string key;
        optional<string> value; // nullopt deletes the key in the same transaction.
    };
    class database_engine {
    public:
        virtual ~database_engine() = default;
        virtual error_code open(const fs::path& path, size_t max_value, size_t max_rows) = 0;
        virtual error_code get(string_view key, string& output) = 0;
        virtual error_code scan(vector<pair<string, string>>& output) = 0;
        virtual error_code write(const vector<database_change>& changes) = 0;
        virtual void close() noexcept = 0;
        virtual error_info error_details() const noexcept = 0;
    };
    inline view<const byte> database_bytes(string_view value) noexcept {
        return {reinterpret_cast<const byte*>(value.data()), value.size()};
    }
    inline void database_u64(string& output, uint64_t value) {
        auto pos = output.size();
        output.resize(pos + 8);
        store_little_endian<uint64_t>(
            value, view<byte, 8>{reinterpret_cast<byte*>(output.data() + pos), 8});
    }
    struct database_reader {
        view<const byte> bytes;
        view<const byte> take(size_t count) {
            if (count > bytes.size())
                throw system_error(make_error_code(errc::corrupt));
            auto result = bytes.first(count);
            bytes = bytes.subspan(count);
            return result;
        }
        uint64_t integer() {
            return load_little_endian<uint64_t>(take(8).first<8>());
        }
        void end() {
            if (!bytes.empty())
                throw system_error(make_error_code(errc::corrupt));
        }
    };
    inline string database_entry_key(log_index index) {
        string key(9, '\0');
        key[0] = 'e';
        for (size_t i = 0; i < 8; ++i)
            key[i + 1] = static_cast<char>(index >> (56 - 8 * i));
        return key;
    }
    inline string database_entry(const entry& value, storage_limits limits) {
        if (!value.term || (value.type != entry_type::command &&
                            value.type != entry_type::barrier && value.type != entry_type::change))
            throw system_error(make_error_code(errc::invalid));
        if (value.payload.size() > limits.max_entry_size)
            throw system_error(make_error_code(errc::too_big));
        string output;
        database_u64(output, value.term);
        database_u64(output, static_cast<uint64_t>(value.type));
        database_u64(output, value.payload.size());
        if (!value.payload.empty())
            output.append(reinterpret_cast<const char*>(value.payload.bytes().data()),
                          value.payload.size());
        return output;
    }
    inline entry database_decode_entry(string_view value, storage_limits limits) {
        database_reader reader{database_bytes(value)};
        entry output;
        output.term = reader.integer();
        const auto type = reader.integer();
        if (!output.term || (type != static_cast<uint64_t>(entry_type::command) &&
                             type != static_cast<uint64_t>(entry_type::barrier) &&
                             type != static_cast<uint64_t>(entry_type::change)))
            throw system_error(make_error_code(errc::corrupt));
        output.type = static_cast<entry_type>(type);
        auto size = reader.integer();
        if (size > limits.max_entry_size)
            throw system_error(make_error_code(errc::too_big));
        output.payload = shared_buffer::copy(reader.take(static_cast<size_t>(size)));
        reader.end();
        return output;
    }
    inline string database_snapshot(const snapshot& value, storage_limits limits) {
        if (!value.index || value.index == numeric_limits<log_index>::max() || !value.term ||
            !value.configuration_index || value.configuration_index > value.index ||
            !value.configuration.voter_count())
            throw system_error(make_error_code(errc::invalid));
        buffer configuration;
        if (auto error = value.configuration.encode(configuration))
            throw system_error(error);
        if (configuration.size() > 8 * 1024 * 1024)
            throw system_error(make_error_code(errc::too_big));
        size_t size = 0;
        for (const auto& bytes : value.buffers) {
            if (bytes.size() > limits.max_snapshot_size - size)
                throw system_error(make_error_code(errc::too_big));
            size += bytes.size();
        }
        string output;
        for (auto word : {value.index, value.term, value.configuration_index,
                          static_cast<uint64_t>(configuration.size()), static_cast<uint64_t>(size)})
            database_u64(output, word);
        output.append(reinterpret_cast<const char*>(configuration.data()), configuration.size());
        for (const auto& bytes : value.buffers)
            if (!bytes.empty())
                output.append(reinterpret_cast<const char*>(bytes.bytes().data()), bytes.size());
        return output;
    }
    inline snapshot database_decode_snapshot(string_view bytes, storage_limits limits) {
        database_reader reader{database_bytes(bytes)};
        snapshot output;
        output.index = reader.integer();
        output.term = reader.integer();
        output.configuration_index = reader.integer();
        auto configuration_size = reader.integer();
        auto size = reader.integer();
        if (!output.index || output.index == numeric_limits<log_index>::max() || !output.term ||
            !output.configuration_index || output.configuration_index > output.index)
            throw system_error(make_error_code(errc::corrupt));
        if (size > limits.max_snapshot_size || configuration_size > 8 * 1024 * 1024)
            throw system_error(make_error_code(errc::too_big));
        if (auto error =
                output.configuration.decode(reader.take(static_cast<size_t>(configuration_size))))
            throw system_error(error == errc::out_of_memory ? error
                                                            : make_error_code(errc::corrupt));
        if (!output.configuration.voter_count())
            throw system_error(make_error_code(errc::corrupt));
        output.buffers.push_back(shared_buffer::copy(reader.take(static_cast<size_t>(size))));
        reader.end();
        return output;
    }
    struct database_state : enable_shared_from_this<database_state> {
        owned<database_engine> engine;
        fs::path path;
        storage_limits limits;
        error_info diagnostic;
        close_callback observer;
        term current_term = 0;
        server_id vote = 0;
        log_index start = 1, next = 1, snapshot_index = 0;
        size_t active = 0;
        bool initialized = false, closing = false, closed = false;
        database_state(owned<database_engine> value, fs::path location, storage_limits bounds)
            : engine(std::move(value)), path(std::move(location)), limits(bounds) {
            const auto maximum = static_cast<size_t>(numeric_limits<int>::max()) / 2;
            if (path.empty() || path.native().find('\0') != fs::path::string_type::npos ||
                !limits.max_entries || limits.max_entries > maximum ||
                limits.max_entry_size > maximum || limits.max_snapshot_size > maximum)
                throw invalid_argument("invalid database path or storage limits");
        }
        ~database_state() {
            engine->close();
        }
        void finish() noexcept {
            if (!closing || closed || active)
                return;
            closed = true;
            engine->close();
            auto callback = std::move(observer);
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
        }
        struct activity {
            shared<database_state> owner;
            explicit activity(shared<database_state> state) : owner(std::move(state)) {
                ++owner->active;
            }
            ~activity() {
                --owner->active;
                owner->finish();
            }
        };
        void close(close_callback callback = {}) noexcept {
            if (closed) {
                try {
                    if (callback)
                        callback();
                } catch (...) {
                }
                return;
            }
            if (callback && !observer)
                observer = std::move(callback);
            closing = true;
            finish();
        }
        error_code ready() const noexcept {
            return closing       ? make_error_code(errc::shutdown)
                   : initialized ? error_code{}
                                 : make_error_code(errc::invalid);
        }
        template <typename F> error_code run(string_view operation, F&& function) noexcept {
            error_code result;
            bool native = false;
            try {
                result = function();
                native = true;
            } catch (const bad_alloc&) {
                result = errc::out_of_memory;
            } catch (const length_error&) {
                result = errc::too_big;
            } catch (const system_error& error) {
                result = error.code().category() == raft_category()
                             ? error.code()
                             : make_error_code(errc::io_error);
            } catch (...) {
                result = errc::io_error;
            }
            if (result) {
                auto source = native ? engine->error_details() : error_info{};
                diagnostic = source.code == result ? wrap_error_info(source, operation)
                                                   : make_error_info(result, operation);
            }
            return result;
        }
        string metadata(term value, server_id voted, log_index first) {
            string output("RAFTDB\1\0", 8);
            database_u64(output, value);
            database_u64(output, voted);
            database_u64(output, first);
            return output;
        }
        error_code read(io_state& output) {
            string encoded;
            if (auto error = engine->get("meta", encoded))
                return error;
            if (encoded.size() != 32 || encoded.compare(0, 8, "RAFTDB\1\0", 8) != 0)
                return errc::corrupt;
            database_reader reader{database_bytes(encoded).subspan(8)};
            io_state value;
            value.term = reader.integer();
            value.voted_for = reader.integer();
            value.start_index = reader.integer();
            if (!value.start_index || (!value.term && value.voted_for))
                return errc::corrupt;
            vector<pair<string, string>> rows;
            if (auto error = engine->scan(rows))
                return error;
            log_index expected = value.start_index;
            for (const auto& [key, bytes] : rows) {
                if (key == "meta")
                    continue;
                if (key == "snapshot") {
                    value.snapshot = database_decode_snapshot(bytes, limits);
                    if (value.snapshot->term > value.term)
                        return errc::corrupt;
                    continue;
                }
                if (key.size() != 9 || key != database_entry_key(expected) ||
                    expected == numeric_limits<log_index>::max())
                    return errc::corrupt;
                if (value.entries.size() >= limits.max_entries)
                    return errc::too_big;
                auto item = database_decode_entry(bytes, limits);
                if (item.term > value.term)
                    return errc::corrupt;
                value.entries.push_back(std::move(item));
                ++expected;
            }
            const auto floor = value.snapshot ? value.snapshot->index + 1 : 1;
            if (value.start_index > floor || expected < floor ||
                (value.entries.empty() && value.start_index != floor))
                return errc::corrupt;
            output = std::move(value);
            return {};
        }
        error_code init(server_id id, string_view address) {
            if (closing)
                return errc::shutdown;
            if (initialized)
                return errc::busy;
            if (!id)
                return errc::bad_id;
            if (address.empty())
                return errc::invalid;
            const auto bound = std::max(limits.max_snapshot_size + 8 * 1024 * 1024 + 40,
                                        limits.max_entry_size + 24);
            if (auto error = engine->open(path, bound, limits.max_entries + 2))
                return error;
            string bytes;
            auto error = engine->get("meta", bytes);
            if (error == errc::not_found) {
                vector<pair<string, string>> rows;
                if (auto scan = engine->scan(rows))
                    return scan;
                if (!rows.empty())
                    return errc::corrupt;
                if (auto write = engine->write({{"meta", metadata(0, 0, 1)}}))
                    return write;
            } else if (error)
                return error;
            io_state loaded;
            if (auto status = read(loaded))
                return status;
            current_term = loaded.term;
            vote = loaded.voted_for;
            start = loaded.start_index;
            next = start + loaded.entries.size();
            snapshot_index = loaded.snapshot ? loaded.snapshot->index : 0;
            initialized = true;
            return {};
        }
        error_code load(io_state& output) {
            if (auto error = ready())
                return error;
            return read(output);
        }
        error_code set_term(term value) {
            if (auto error = ready())
                return error;
            if (!value || value < current_term)
                return errc::invalid;
            if (auto error = engine->write({{"meta", metadata(value, 0, start)}}))
                return error;
            current_term = value;
            vote = 0;
            return {};
        }
        error_code set_vote(server_id value) {
            if (auto error = ready())
                return error;
            if (!current_term)
                return errc::invalid;
            if (auto error = engine->write({{"meta", metadata(current_term, value, start)}}))
                return error;
            vote = value;
            return {};
        }
        error_code append(log_index index, const shared<const entry_batch>& entries) {
            if (auto error = ready())
                return error;
            if (!entries || entries->empty() || index != next)
                return errc::invalid;
            if (entries->size() > limits.max_entries - static_cast<size_t>(next - start) ||
                entries->size() > numeric_limits<log_index>::max() - next)
                return errc::too_big;
            vector<database_change> changes;
            for (const auto& item : *entries) {
                if (!item || item->term > current_term)
                    return errc::invalid;
                changes.push_back({database_entry_key(index++), database_entry(*item, limits)});
            }
            if (auto error = engine->write(changes))
                return error;
            next = index;
            return {};
        }
        error_code configuration_entry(const configuration& value, bool recovering) {
            if (auto error = ready())
                return error;
            if (!value.voter_count())
                return errc::invalid;
            if (!recovering && current_term)
                return errc::cannot_bootstrap;
            io_state loaded;
            if (auto error = read(loaded))
                return error;
            auto entry_term = !loaded.entries.empty() ? loaded.entries.back().term
                              : loaded.snapshot       ? loaded.snapshot->term
                                                      : 0;
            if (recovering && !entry_term)
                return errc::invalid;
            if (next == numeric_limits<log_index>::max() || next - start >= limits.max_entries)
                return errc::too_big;
            buffer encoded;
            if (auto error = value.encode(encoded))
                return error;
            entry item{recovering ? entry_term : 1, entry_type::change,
                       shared_buffer{std::move(encoded)}};
            vector<database_change> changes{
                {database_entry_key(next), database_entry(item, limits)}};
            if (!recovering)
                changes.push_back({"meta", metadata(1, 0, start)});
            if (auto error = engine->write(changes))
                return error;
            if (!recovering) {
                current_term = 1;
                vote = 0;
            }
            ++next;
            return {};
        }
        error_code truncate(log_index index) {
            if (auto error = ready())
                return error;
            if (index < start || index > next || index <= snapshot_index)
                return errc::invalid;
            vector<database_change> changes;
            for (auto i = index; i < next; ++i)
                changes.push_back({database_entry_key(i), std::nullopt});
            if (auto error = engine->write(changes))
                return error;
            next = index;
            return {};
        }
        error_code snapshot_put(unsigned trailing, const shared<const snapshot>& value) {
            if (auto error = ready())
                return error;
            if (!value || value->index < snapshot_index || value->term > current_term)
                return errc::invalid;
            auto encoded = database_snapshot(*value, limits);
            bool matches = false;
            if (value->index == snapshot_index) {
                string bytes;
                if (auto error = engine->get("snapshot", bytes))
                    return error;
                if (database_decode_snapshot(bytes, limits).term != value->term)
                    return errc::invalid;
                matches = true;
            }
            if (value->index >= start && value->index < next) {
                string bytes;
                if (auto error = engine->get(database_entry_key(value->index), bytes))
                    return error;
                matches = database_decode_entry(bytes, limits).term == value->term;
            }
            auto first = value->index + 1;
            if (matches)
                first = std::max(start, first > trailing ? first - trailing : 1);
            vector<database_change> changes{{"snapshot", std::move(encoded)},
                                            {"meta", metadata(current_term, vote, first)}};
            for (auto i = start; i < next; ++i)
                if (!matches || i < first)
                    changes.push_back({database_entry_key(i), std::nullopt});
            if (auto error = engine->write(changes))
                return error;
            if (!matches)
                next = value->index + 1;
            start = first;
            snapshot_index = value->index;
            return {};
        }
        error_code snapshot_get(snapshot& output, bool& found) {
            if (auto error = ready())
                return error;
            string bytes;
            const auto error = engine->get("snapshot", bytes);
            if (error == errc::not_found) {
                found = false;
                return {};
            }
            if (error)
                return error;
            output = database_decode_snapshot(bytes, limits);
            found = true;
            return {};
        }
        uint16_t capacity() const noexcept {
            if (!initialized || closing)
                return 0;
            try {
                error_code error;
                auto space = fs::space(path, error);
                if (error)
                    return 0;
                return static_cast<uint16_t>(
                    std::min<decltype(space.available)>(space.available / 1024, 65535));
            } catch (...) {
                return 0;
            }
        }
    };
}
#endif
