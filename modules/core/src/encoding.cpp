// C++ port of cowsql/raft src/uv_encoding.c and segment batch encoding;
// see LICENSE.md. No libuv types or C reference functions are used here.
#include <raftpp/encoding.hpp>
#include <raftpp/checksum.hpp>
#include "byte.hpp"

namespace raftpp {

    namespace {
        using detail::byte_reader;
        using detail::byte_writer;
        bool valid_type(entry_type type) noexcept {
            return type == entry_type::command || type == entry_type::barrier ||
                   type == entry_type::change;
        }
        bool add_size(size_t& value, size_t added) noexcept {
            if (added > numeric_limits<size_t>::max() - value)
                return false;
            value += added;
            return true;
        }
        error_code header_size(size_t count, size_t& size, encoding_limits limits) noexcept {
            if (count > limits.max_entries || count > numeric_limits<unsigned>::max() ||
                count > (numeric_limits<size_t>::max() - 8) / 16)
                return errc::too_big;
            size = 8 + 16 * count;
            return size > limits.max_header_size ? make_error_code(errc::too_big) : error_code{};
        }
        error_code describe(view<const entry_header> entries, size_t& total,
                            encoding_limits limits) noexcept {
            total = 0;
            for (const auto& entry : entries) {
                if (!valid_type(entry.type))
                    return errc::malformed;
                if (entry.size > numeric_limits<uint32_t>::max() || !add_size(total, entry.size) ||
                    total > limits.max_payload_size)
                    return errc::too_big;
            }
            return {};
        }
        void write_headers(byte_writer& writer, view<const entry_header> entries) noexcept {
            writer.write<uint64_t>(entries.size());
            for (const auto& entry : entries) {
                writer.write<uint64_t>(entry.term);
                writer.write<uint8_t>(static_cast<uint8_t>(entry.type));
                writer.write<uint8_t>(0);
                writer.write<uint16_t>(0);
                writer.write<uint32_t>(static_cast<uint32_t>(entry.size));
            }
        }
        error_code read_headers(view<const byte> input, vector<entry_header>& output,
                                size_t& consumed, encoding_limits limits) {
            byte_reader reader{input};
            uint64_t count = 0;
            if (!reader.read(count))
                return errc::malformed;
            if (count > numeric_limits<size_t>::max())
                return errc::too_big;
            size_t size = 0;
            if (auto error = header_size(static_cast<size_t>(count), size, limits))
                return error;
            if (size > input.size())
                return errc::malformed;
            vector<entry_header> entries;
            entries.reserve(static_cast<size_t>(count));
            size_t total = 0;
            for (uint64_t i = 0; i < count; ++i) {
                entry_header entry;
                uint8_t type = 0;
                uint32_t length = 0;
                (void)reader.read(entry.term);
                (void)reader.read(type);
                (void)reader.skip(3);
                (void)reader.read(length);
                entry.type = static_cast<entry_type>(type);
                entry.size = length;
                if (!valid_type(entry.type))
                    return errc::malformed;
                if (!add_size(total, length) || total > limits.max_payload_size)
                    return errc::too_big;
                entries.push_back(entry);
            }
            output = std::move(entries);
            consumed = size;
            return {};
        }
        template <typename F> error_code allocation_guard(F&& action) noexcept {
            try {
                return action();
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            }
        }
    }

    error_code encode_batch_header(view<const entry_header> entries, buffer& output,
                                   encoding_limits limits) noexcept {
        return allocation_guard([&]() -> error_code {
            size_t size = 0, total = 0;
            if (auto error = header_size(entries.size(), size, limits))
                return error;
            if (auto error = describe(entries, total, limits))
                return error;
            buffer result{size};
            byte_writer writer{result.bytes()};
            write_headers(writer, entries);
            output = std::move(result);
            return {};
        });
    }
    error_code decode_batch_header(view<const byte> input, vector<entry_header>& output,
                                   size_t& consumed, encoding_limits limits) noexcept {
        return allocation_guard([&] { return read_headers(input, output, consumed, limits); });
    }

    error_code encode_message(const message& input, encoded_message& output,
                              encoding_limits limits) noexcept {
        return allocation_guard([&]() -> error_code {
            if (input.payload.valueless_by_exception())
                return errc::malformed;
            vector<entry_header> entries;
            buffer configuration;
            size_t size = 0, total = 0;
            const auto* append = std::get_if<append_entries>(&input.payload);
            const auto* snapshot = std::get_if<install_snapshot>(&input.payload);
            if (append) {
                size_t batch_size = 0;
                if (auto error = header_size(append->entries.size(), batch_size, limits))
                    return error;
                entries.reserve(append->entries.size());
                for (const auto& entry : append->entries) {
                    if (!entry)
                        return errc::malformed;
                    entries.push_back({entry->term, entry->type, entry->payload.size()});
                }
                if (auto error = describe(entries, total, limits))
                    return error;
                size = batch_size;
                if (!add_size(size, 40))
                    return errc::too_big;
            } else if (snapshot) {
                if (snapshot->needs_data)
                    return errc::invalid;
                const auto conf_size = snapshot->configuration.encoded_size();
                if (conf_size == 0 || conf_size > limits.max_header_size ||
                    snapshot->data.size() > limits.max_payload_size)
                    return errc::too_big;
                if (auto error = snapshot->configuration.encode(configuration))
                    return error;
                size = conf_size;
                if (!add_size(size, 56))
                    return errc::too_big;
            } else if (std::holds_alternative<request_vote>(input.payload))
                size = 40;
            else if (std::holds_alternative<request_vote_result>(input.payload))
                size = 24;
            else if (std::holds_alternative<append_entries_result>(input.payload))
                size = 32;
            else
                size = 24;
            if (size > limits.max_header_size || size > numeric_limits<size_t>::max() - 16)
                return errc::too_big;
            buffer header{16 + size};
            byte_writer writer{header.bytes()};
            writer.write<uint8_t>(static_cast<uint8_t>(input.payload.index() + 1));
            writer.write<uint8_t>(0);
            writer.write<uint8_t>(
                std::visit([](const auto& payload) { return payload.version; }, input.payload));
            writer.write<uint8_t>(0);
            writer.write<uint32_t>(0);
            writer.write<uint64_t>(size);
            std::visit(
                [&](const auto& value) {
                    using T = remove_cvref_t<decltype(value)>;
                    writer.write<uint64_t>(value.term);
                    if constexpr (std::is_same_v<T, request_vote>) {
                        writer.write<uint64_t>(value.candidate_id);
                        writer.write<uint64_t>(value.last_log_index);
                        writer.write<uint64_t>(value.last_log_term);
                        writer.write<uint64_t>((value.disrupt_leader ? 1U : 0U) |
                                               (value.pre_vote ? 2U : 0U));
                    } else if constexpr (std::is_same_v<T, request_vote_result>) {
                        writer.write<uint64_t>(value.vote_granted);
                        writer.write<uint8_t>(value.pre_vote ? 1 : 0);
                        writer.write<uint8_t>(0);
                        writer.write<uint16_t>(value.features);
                        writer.write<uint16_t>(value.capacity);
                        writer.write<uint16_t>(0);
                    } else if constexpr (std::is_same_v<T, append_entries>) {
                        writer.write<uint64_t>(value.prev_log_index);
                        writer.write<uint64_t>(value.prev_log_term);
                        writer.write<uint64_t>(value.leader_commit);
                        write_headers(writer, entries);
                        writer.write<uint64_t>(0);
                    } else if constexpr (std::is_same_v<T, append_entries_result>) {
                        writer.write<uint64_t>(value.rejected);
                        writer.write<uint64_t>(value.last_log_index);
                        writer.write<uint16_t>(value.features);
                        writer.write<uint16_t>(value.capacity);
                        writer.write<uint32_t>(0);
                    } else if constexpr (std::is_same_v<T, install_snapshot>) {
                        writer.write<uint64_t>(value.last_index);
                        writer.write<uint64_t>(value.last_term);
                        writer.write<uint64_t>(value.configuration_index);
                        writer.write<uint64_t>(configuration.size());
                        writer.write(configuration.bytes());
                        writer.write<uint64_t>(value.data.size());
                        writer.write<uint64_t>(0);
                    } else {
                        writer.write<uint64_t>(value.last_log_index);
                        writer.write<uint64_t>(value.last_log_term);
                    }
                },
                input.payload);
            encoded_message result;
            result.header = shared_buffer{std::move(header)};
            if (append) {
                result.payload.reserve(append->entries.size());
                for (const auto& entry : append->entries)
                    result.payload.push_back(entry->payload);
            } else if (snapshot)
                result.payload.push_back(snapshot->data);
            output = std::move(result);
            return {};
        });
    }

    error_code decode_preamble(view<const byte> input, message_preamble& output,
                               encoding_limits limits) noexcept {
        if (input.size() < 16)
            return errc::malformed;
        message_preamble result;
        result.type = std::to_integer<uint8_t>(input[0]);
        result.version = std::to_integer<uint8_t>(input[2]);
        if (result.type < 1 || result.type > 6)
            return errc::io_error;
        const auto size = detail::load_little_endian<uint64_t>(input.subspan<8, 8>());
        if (size == 0)
            return errc::malformed;
        if (size > limits.max_header_size || size > numeric_limits<size_t>::max())
            return errc::too_big;
        result.header_size = static_cast<size_t>(size);
        output = result;
        return {};
    }

    error_code decode_message_header(const message_preamble& preamble, view<const byte> input,
                                     message_header& output, encoding_limits limits) noexcept {
        return allocation_guard([&]() -> error_code {
            if (input.size() != preamble.header_size || input.empty())
                return errc::malformed;
            if (input.size() > limits.max_header_size)
                return errc::too_big;
            byte_reader reader{input};
            message_header result;
            const auto version = preamble.version;
            switch (preamble.type) {
            case 1: {
                append_entries value;
                value.version = version;
                if (!reader.read(value.term) || !reader.read(value.prev_log_index) ||
                    !reader.read(value.prev_log_term) || !reader.read(value.leader_commit))
                    return errc::malformed;
                size_t used;
                if (auto error = read_headers(input.subspan(32), result.entries, used, limits))
                    return error;
                if (auto error = describe(result.entries, result.payload_size, limits))
                    return error;
                result.payload = std::move(value);
                break;
            }
            case 2: {
                append_entries_result value;
                value.version = version == 0 && input.size() > 24 ? 2 : version;
                if (!reader.read(value.term) || !reader.read(value.rejected) ||
                    !reader.read(value.last_log_index))
                    return errc::malformed;
                if (value.version >= 1 && !reader.read(value.features))
                    return errc::malformed;
                if (value.version >= 2 && !reader.read(value.capacity))
                    return errc::malformed;
                result.payload = value;
                break;
            }
            case 3: {
                request_vote value;
                value.version = version == 0 ? (input.size() == 32 ? 1 : 2) : version;
                if (!reader.read(value.term) || !reader.read(value.candidate_id) ||
                    !reader.read(value.last_log_index) || !reader.read(value.last_log_term))
                    return errc::malformed;
                // Legacy 32-byte V1 requests have no flags. The C decoder reads past their end.
                if (reader.remaining() != 0 || value.version >= 2) {
                    uint64_t flags;
                    if (!reader.read(flags))
                        return errc::malformed;
                    value.disrupt_leader = (flags & 1) != 0;
                    value.pre_vote = (flags & 2) != 0;
                }
                result.payload = value;
                break;
            }
            case 4: {
                request_vote_result value;
                value.version = version == 0 ? (input.size() == 16 ? 1 : 2) : version;
                uint64_t granted;
                if (!reader.read(value.term) || !reader.read(granted))
                    return errc::malformed;
                value.vote_granted = granted != 0;
                if (value.version >= 2) {
                    uint8_t flags;
                    if (!reader.read(flags) || !reader.skip(1) || !reader.read(value.features) ||
                        !reader.read(value.capacity))
                        return errc::malformed;
                    value.pre_vote = (flags & 1) != 0;
                    value.version = 2;
                }
                result.payload = value;
                break;
            }
            case 5: {
                install_snapshot value;
                value.version = version;
                uint64_t size;
                view<const byte> configuration;
                if (!reader.read(value.term) || !reader.read(value.last_index) ||
                    !reader.read(value.last_term) || !reader.read(value.configuration_index) ||
                    !reader.read(size))
                    return errc::malformed;
                if (size > reader.remaining() ||
                    !reader.take(static_cast<size_t>(size), configuration))
                    return errc::malformed;
                if (auto error = value.configuration.decode(configuration))
                    return error;
                if (!reader.read(size))
                    return errc::malformed;
                if (size > limits.max_payload_size || size > numeric_limits<size_t>::max())
                    return errc::too_big;
                result.payload_size = static_cast<size_t>(size);
                result.payload = std::move(value);
                break;
            }
            case 6: {
                timeout_now value;
                if (!reader.read(value.term) || !reader.read(value.last_log_index) ||
                    !reader.read(value.last_log_term))
                    return errc::malformed;
                result.payload = value;
                break;
            }
            default:
                return errc::io_error;
            }
            output = std::move(result);
            return {};
        });
    }

    error_code decode_message_payload(const message_header& header, shared_buffer payload,
                                      message& output) noexcept {
        return allocation_guard([&]() -> error_code {
            if (header.payload.valueless_by_exception() || payload.size() != header.payload_size)
                return errc::malformed;
            const bool append = std::holds_alternative<append_entries>(header.payload);
            const bool snapshot = std::holds_alternative<install_snapshot>(header.payload);
            if ((!append && !header.entries.empty()) || (!append && !snapshot && !payload.empty()))
                return errc::malformed;
            if (append) {
                if (!std::get<append_entries>(header.payload).entries.empty())
                    return errc::malformed;
                size_t total = 0;
                encoding_limits unlimited{numeric_limits<size_t>::max(),
                                          numeric_limits<size_t>::max(),
                                          numeric_limits<size_t>::max()};
                if (auto error = describe(header.entries, total, unlimited))
                    return error;
                if (total != payload.size())
                    return errc::malformed;
            }
            message result;
            result.payload = header.payload;
            if (append) {
                auto& value = std::get<append_entries>(result.payload);
                value.entries.reserve(header.entries.size());
                size_t offset = 0;
                for (const auto& entry : header.entries) {
                    value.entries.push_back(std::allocate_shared<const raftpp::entry>(
                        resource_allocator<raftpp::entry>{},
                        raftpp::entry{entry.term, entry.type, payload.slice(offset, entry.size)}));
                    offset += entry.size;
                }
            } else if (snapshot) {
                auto& value = std::get<install_snapshot>(result.payload);
                if (value.needs_data)
                    return errc::malformed;
                value.data = std::move(payload);
            }
            output = std::move(result);
            return {};
        });
    }

    error_code decode_message(shared_buffer input, message& output, size_t& consumed,
                              encoding_limits limits) noexcept {
        message_preamble preamble;
        if (auto error = decode_preamble(input.bytes(), preamble, limits))
            return error;
        if (preamble.header_size > input.size() - 16)
            return errc::malformed;
        message_header header;
        if (auto error = decode_message_header(
                preamble, input.bytes().subspan(16, preamble.header_size), header, limits))
            return error;
        const auto offset = 16 + preamble.header_size;
        if (header.payload_size > input.size() - offset)
            return errc::malformed;
        if (auto error =
                decode_message_payload(header, input.slice(offset, header.payload_size), output))
            return error;
        consumed = offset + header.payload_size;
        return {};
    }

    error_code encode_record(view<const entry> entries, buffer& output,
                             encoding_limits limits) noexcept {
        return allocation_guard([&]() -> error_code {
            if (entries.empty())
                return errc::invalid;
            if (entries.size() > 262144)
                return errc::too_big;
            size_t size = 0;
            if (auto error = header_size(entries.size(), size, limits))
                return error;
            vector<entry_header> headers;
            headers.reserve(entries.size());
            for (const auto& entry : entries) {
                if (entry.payload.size() % 8 != 0)
                    return errc::invalid;
                headers.push_back({entry.term, entry.type, entry.payload.size()});
            }
            size_t data_size;
            if (auto error = describe(headers, data_size, limits))
                return error;
            auto total = size;
            if (!add_size(total, 8) || !add_size(total, data_size))
                return errc::too_big;
            buffer result{total};
            byte_writer writer{result.bytes().subspan(8)};
            write_headers(writer, headers);
            for (const auto& entry : entries)
                writer.write(entry.payload.bytes());
            byte_writer checksums{result.bytes().first(8)};
            checksums.write<uint32_t>(crc32(result.bytes().subspan(8, size)));
            checksums.write<uint32_t>(crc32(result.bytes().subspan(8 + size)));
            output = std::move(result);
            return {};
        });
    }

    error_code decode_record(shared_buffer input, vector<entry>& output, size_t& consumed,
                             encoding_limits limits) noexcept {
        return allocation_guard([&]() -> error_code {
            if (input.size() < 16)
                return errc::io_error;
            byte_reader reader{input.bytes()};
            uint32_t header_crc = 0, data_crc = 0;
            uint64_t count = 0;
            (void)reader.read(header_crc);
            (void)reader.read(data_crc);
            (void)reader.read(count);
            if (count == 0 || count > 262144)
                return errc::corrupt;
            size_t size = 0;
            if (auto error = header_size(static_cast<size_t>(count), size, limits))
                return error;
            if (size > input.size() - 8)
                return errc::io_error;
            const auto bytes = input.bytes().subspan(8, size);
            if (crc32(bytes) != header_crc)
                return errc::corrupt;
            vector<entry_header> headers;
            size_t used;
            if (auto error = read_headers(bytes, headers, used, limits))
                return error;
            size_t total = 0;
            for (const auto& entry : headers) {
                if (entry.size % 8 != 0)
                    return errc::corrupt;
                if (!add_size(total, entry.size))
                    return errc::too_big;
            }
            const auto offset = 8 + size;
            if (total > input.size() - offset)
                return errc::io_error;
            if (crc32(input.bytes().subspan(offset, total)) != data_crc)
                return errc::corrupt;
            vector<entry> entries;
            entries.reserve(headers.size());
            size_t position = offset;
            for (const auto& entry : headers) {
                entries.push_back({entry.term, entry.type, input.slice(position, entry.size)});
                position += entry.size;
            }
            output = std::move(entries);
            consumed = position;
            return {};
        });
    }

}
