#include <raftpp.hpp>
#include "byte.hpp"

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <source_location>

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
void encoding_limits_suite();
namespace {
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    r::view<const r::byte> bytes(r::string_view value) {
        return {reinterpret_cast<const r::byte*>(value.data()), value.size()};
    }
    bool equal(r::view<const r::byte> a, r::view<const r::byte> b) {
        return std::ranges::equal(a, b);
    }
    r::shared_buffer payload(r::string_view value) {
        return r::shared_buffer::copy(bytes(value));
    }
    r::shared_buffer flatten(const r::encoded_message& message) {
        r::size_t size = message.header.size();
        for (const auto& data : message.payload)
            size += data.size();
        r::buffer result{size};
        r::detail::byte_writer writer{result.bytes()};
        writer.write(message.header.bytes());
        for (const auto& data : message.payload)
            writer.write(data.bytes());
        return r::shared_buffer{std::move(result)};
    }
    r::configuration configuration() {
        r::configuration result;
        check(!result.add(1, "node-1", r::role::voter) &&
                  !result.add(2, "node-2", r::role::standby),
              "configuration");
        return result;
    }
    r::message sample(unsigned type, r::uint64_t seed, bool aligned = true) {
        r::message result;
        switch (type) {
        case 1: {
            r::append_entries value;
            value.term = seed;
            value.prev_log_index = seed >> 1;
            value.prev_log_term = seed >> 2;
            value.leader_commit = seed >> 3;
            for (unsigned i = 0; i < seed % 7; ++i) {
                r::buffer data{aligned ? i * 8 : i * 3};
                for (r::size_t j = 0; j < data.size(); ++j)
                    data.bytes()[j] = static_cast<r::byte>(seed + j + i);
                value.entries.push_back(std::make_shared<const r::entry>(
                    r::entry{seed + i, static_cast<r::entry_type>(i % 3 + 1),
                             r::shared_buffer{std::move(data)}}));
            }
            result.payload = std::move(value);
            break;
        }
        case 2: {
            r::append_entries_result value;
            value.term = seed;
            value.rejected = seed >> 1;
            value.last_log_index = seed >> 2;
            value.features = static_cast<r::uint16_t>(seed);
            value.capacity = static_cast<r::uint16_t>(seed >> 4);
            result.payload = value;
            break;
        }
        case 3: {
            r::request_vote value;
            value.term = seed;
            value.candidate_id = seed >> 1;
            value.last_log_index = seed >> 2;
            value.last_log_term = seed >> 3;
            value.disrupt_leader = seed & 1;
            value.pre_vote = seed & 2;
            result.payload = value;
            break;
        }
        case 4: {
            r::request_vote_result value;
            value.term = seed;
            value.vote_granted = seed & 1;
            value.pre_vote = seed & 2;
            value.features = static_cast<r::uint16_t>(seed);
            value.capacity = static_cast<r::uint16_t>(seed >> 4);
            result.payload = value;
            break;
        }
        case 5: {
            r::install_snapshot value;
            value.term = seed;
            value.last_index = seed >> 1;
            value.last_term = seed >> 2;
            value.configuration = configuration();
            value.configuration_index = seed >> 3;
            value.data = payload("snapshot");
            result.payload = std::move(value);
            break;
        }
        default: {
            r::timeout_now value;
            value.term = seed;
            value.last_log_index = seed >> 1;
            value.last_log_term = seed >> 2;
            result.payload = value;
            break;
        }
        }
        return result;
    }

    void checksums() {
        std::mt19937_64 random{9};
        for (unsigned trial = 0; trial < 600; ++trial) {
            r::buffer data{static_cast<r::size_t>(random() % 4096)};
            for (auto& value : data.bytes())
                value = static_cast<r::byte>(random());
            const auto initial = static_cast<r::uint32_t>(random());
            const auto split = data.size() / 2;
            check(r::crc32(data.bytes().subspan(split),
                           r::crc32(data.bytes().first(split), initial)) ==
                      r::crc32(data.bytes(), initial),
                  "CRC chunking preserves initial state");
            r::sha1 actual;
            for (r::size_t offset = 0; offset < data.size();) {
                const auto count = std::min<r::size_t>(data.size() - offset, 1 + random() % 79);
                actual.update(data.bytes().subspan(offset, count));
                offset += count;
            }
            r::sha1 whole;
            whole.update(data.bytes());
            check(actual.digest() == whole.digest() && actual.digest() == actual.digest(),
                  "chunked and repeated digest preserve SHA-1 state");
            actual.update(bytes("suffix"));
            r::sha1 continued;
            continued.update(data.bytes());
            continued.update(bytes("suffix"));
            check(actual.digest() == continued.digest(), "digest preserves update state");
            actual.reset();
            check(actual.digest() == r::sha1{}.digest(), "SHA1 reset");
            const auto text = "node-" + std::to_string(trial);
            const auto n = random();
            r::sha1 server;
            server.update(bytes(text));
            r::array<r::byte, 8> number{};
            r::detail::store_little_endian<r::uint64_t>(n, number);
            server.update(number);
            const auto digest = server.digest();
            check(r::digest(text, n) == r::detail::load_little_endian<r::uint64_t>(
                                            r::view<const r::byte>{digest}.last<8>()),
                  "server digest uses SHA-1 suffix and little-endian number");
        }
        r::sha1 known;
        known.update(bytes("abc"));
        const r::array<r::uint8_t, 20> expected{0xa9, 0x99, 0x3e, 0x36, 0x47, 0x06, 0x81,
                                                0x6a, 0xba, 0x3e, 0x25, 0x71, 0x78, 0x50,
                                                0xc2, 0x6c, 0x9c, 0xd0, 0xd8, 0x9d};
        check(equal(known.digest(),
                    {reinterpret_cast<const r::byte*>(expected.data()), expected.size()}),
              "SHA1 known vector");
        check(r::crc32({}, 123) == 123, "empty CRC32");
        check(r::crc32(bytes("123456789"), 0) == 0x89a1897f &&
                  r::crc32(bytes("123456789"), 0xffffffff) == 0x0376e6e7,
              "fixed non-reflected CRC32 vectors");
        r::buffer million{1000000};
        std::ranges::fill(million.bytes(), r::byte{'a'});
        r::sha1 large;
        large.update(million.bytes());
        const r::array<r::uint8_t, 20> million_expected{0x34, 0xaa, 0x97, 0x3c, 0xd4, 0xc4, 0xda,
                                                        0xa4, 0xf6, 0x1e, 0xeb, 0x2b, 0xdb, 0xad,
                                                        0x27, 0x31, 0x65, 0x34, 0x01, 0x6f};
        check(
            equal(large.digest(), {reinterpret_cast<const r::byte*>(million_expected.data()), 20}),
            "million-byte SHA1 known vector");
    }

    void messages() {
        std::mt19937_64 random{18};
        for (unsigned trial = 0; trial < 600; ++trial) {
            const auto type = trial % 6 + 1;
            const auto seed = random();
            auto value = sample(type, seed, trial % 2 == 0);
            r::encoded_message encoded;
            check(!r::encode_message(value, encoded), "encode RPC");
            auto frame = flatten(encoded);
            r::message decoded;
            r::size_t consumed = 0;
            check(!r::decode_message(frame, decoded, consumed) && consumed == frame.size(),
                  "decode full RPC");
            r::encoded_message again;
            check(!r::encode_message(decoded, again), "reencode RPC");
            check(equal(flatten(again).bytes(), frame.bytes()), "RPC byte roundtrip");
            if (auto append = std::get_if<r::append_entries>(&decoded.payload)) {
                const auto& input = std::get<r::append_entries>(value.payload);
                check(append->entries.size() == input.entries.size(), "entry count");
                for (r::size_t i = 0; i < append->entries.size(); ++i)
                    check(equal(append->entries[i]->payload.bytes(),
                                input.entries[i]->payload.bytes()) &&
                              append->entries[i]->payload.shares_storage_with(frame),
                          "packed payload and shared frame ownership");
            }
        }
        r::timeout_now known;
        known.term = 7;
        known.last_log_index = 3;
        known.last_log_term = 2;
        r::encoded_message packet;
        check(!r::encode_message(r::message{0, "", known}, packet), "encode fixed TimeoutNow");
        const r::array<unsigned char, 40> expected{6, 0, 0, 0, 0, 0, 0, 0, 24, 0, 0, 0, 0, 0,
                                                   0, 0, 7, 0, 0, 0, 0, 0, 0,  0, 3, 0, 0, 0,
                                                   0, 0, 0, 0, 2, 0, 0, 0, 0,  0, 0, 0};
        const auto actual = flatten(packet);
        check(actual.size() == expected.size(), "fixed frame size");
        for (r::size_t i = 0; i < expected.size(); ++i)
            check(actual.bytes()[i] == r::byte{expected[i]}, "fixed wire bytes");
        r::entry_batch retained;
        r::weak<const r::byte> lifetime;
        {
            r::encoded_message encoded;
            check(!r::encode_message(sample(1, 6), encoded), "lifetime encode");
            auto frame = flatten(encoded);
            lifetime = frame.storage();
            r::message decoded;
            r::size_t consumed = 0;
            check(!r::decode_message(frame, decoded, consumed), "lifetime decode");
            retained = std::get<r::append_entries>(decoded.payload).entries;
        }
        check(!lifetime.expired() && retained.size() == 6,
              "decoded entries own packet after decoder destruction");
        retained.clear();
        check(lifetime.expired(), "packet released after last entry");
    }

    void records() {
        std::mt19937_64 random{71};
        for (unsigned trial = 0; trial < 400; ++trial) {
            auto value = sample(1, 1 + random() % 6);
            r::vector<r::entry> entries;
            for (const auto& entry : std::get<r::append_entries>(value.payload).entries)
                entries.push_back(*entry);
            r::buffer encoded;
            check(!r::encode_record(entries, encoded), "encode record");

            auto stored = r::shared_buffer{std::move(encoded)};
            r::vector<r::entry> decoded;
            r::size_t consumed = 0;
            check(!r::decode_record(stored, decoded, consumed) && consumed == stored.size() &&
                      decoded.size() == entries.size(),
                  "decode record");
            for (r::size_t i = 0; i < entries.size(); ++i)
                check(entries[i].term == decoded[i].term && entries[i].type == decoded[i].type &&
                          equal(entries[i].payload.bytes(), decoded[i].payload.bytes()) &&
                          decoded[i].payload.shares_storage_with(stored),
                      "record metadata, bytes, and ownership");
        }
    }

    void malformed() {
        for (unsigned type = 1; type <= 6; ++type) {
            r::encoded_message encoded;
            check(!r::encode_message(sample(type, 6), encoded), "malformed setup");
            auto frame = flatten(encoded);
            for (r::size_t cut = 0; cut < frame.size(); ++cut) {
                r::message output;
                output.server_id = 999;
                r::size_t consumed = 77;
                check(r::decode_message(frame.slice(0, cut), output, consumed) != r::error_code{} &&
                          output.server_id == 999 && consumed == 77,
                      "truncated frame preserves outputs");
            }
            r::encoding_limits limit;
            limit.max_header_size = 1;
            r::encoded_message sentinel;
            sentinel.header = payload("keep");
            check(r::encode_message(sample(type, 6), sentinel, limit) == r::errc::too_big &&
                      equal(sentinel.header.bytes(), bytes("keep")),
                  "encode header limit preserves output");
        }
        r::message_preamble preamble{3, 0, 32};
        r::buffer legacy{32};
        r::message_header header;
        check(!r::decode_message_header(preamble, legacy.bytes(), header) &&
                  std::get<r::request_vote>(header.payload).version == 1 &&
                  !std::get<r::request_vote>(header.payload).pre_vote,
              "safe legacy 32-byte vote request");
        preamble = {4, 0, 16};
        legacy.resize(16);
        check(!r::decode_message_header(preamble, legacy.bytes(), header) &&
                  std::get<r::request_vote_result>(header.payload).version == 1,
              "legacy vote result");
        preamble = {2, 1, 26};
        legacy.resize(26);
        check(!r::decode_message_header(preamble, legacy.bytes(), header) &&
                  std::get<r::append_entries_result>(header.payload).version == 1,
              "legacy append result");
        r::array<r::entry_header, 1> one{{{1, r::entry_type::command, 0}}};
        r::buffer batch;
        check(!r::encode_batch_header(one, batch), "batch header setup");
        auto broken = r::buffer::copy(batch.bytes());
        broken.bytes()[16] = r::byte{255};
        r::vector<r::entry_header> metadata{{9, r::entry_type::barrier, 8}};
        r::size_t consumed = 123;
        check(r::decode_batch_header(broken.bytes(), metadata, consumed) == r::errc::malformed &&
                  metadata[0].term == 9 && consumed == 123,
              "invalid entry type preserves outputs");
        r::detail::store_little_endian<r::uint64_t>(r::numeric_limits<r::uint64_t>::max(),
                                                    broken.bytes().first<8>());
        check(r::decode_batch_header(broken.bytes(), metadata, consumed) == r::errc::too_big,
              "oversized count rejected before allocation");
        r::vector<r::entry> entries{{1, r::entry_type::command, payload("12345678")}};
        r::buffer record;
        check(!r::encode_record(entries, record), "malformed record setup");
        r::encoding_limits no_entries;
        no_entries.max_entries = 0;
        r::vector<r::entry> limited;
        consumed = 88;
        check(r::decode_record(r::shared_buffer::copy(record.bytes()), limited, consumed,
                               no_entries) == r::errc::too_big &&
                  limited.empty() && consumed == 88,
              "configured entry-count limit differs from corrupt format count");
        for (r::size_t i = 0; i < record.size(); ++i) {
            auto corrupt = r::buffer::copy(record.bytes());
            corrupt.bytes()[i] ^= r::byte{1};
            r::vector<r::entry> output{{99, r::entry_type::barrier, {}}};
            consumed = 88;
            check(r::decode_record(r::shared_buffer{std::move(corrupt)}, output, consumed) !=
                          r::error_code{} &&
                      output[0].term == 99 && consumed == 88,
                  "record corruption preserves outputs");
        }
        for (r::size_t cut = 0; cut < record.size(); ++cut) {
            r::vector<r::entry> output;
            consumed = 88;
            check(r::decode_record(r::shared_buffer::copy(record.bytes().first(cut)), output,
                                   consumed) == r::errc::io_error &&
                      consumed == 88,
                  "short record is I/O error");
        }
        entries[0].payload = payload("unaligned");
        check(r::encode_record(entries, record) == r::errc::invalid,
              "disk payload alignment required");
        std::mt19937_64 random{99};
        for (unsigned i = 0; i < 5000; ++i) {
            r::buffer data{static_cast<r::size_t>(random() % 256)};
            for (auto& byte : data.bytes())
                byte = static_cast<r::byte>(random());
            r::message result;
            result.server_id = 87;
            consumed = 99;
            const auto status =
                r::decode_message(r::shared_buffer{std::move(data)}, result, consumed);
            check(status ? (result.server_id == 87 && consumed == 99) : consumed <= 256,
                  "random malformed input preserves state without out-of-bounds reads");
        }
    }

    void failures() {
        auto append = sample(1, 6), snapshot = sample(5, 9);
        r::encoded_message encoded, encoded_snapshot;
        check(!r::encode_message(append, encoded) && !r::encode_message(snapshot, encoded_snapshot),
              "allocation setup messages");
        auto frame = flatten(encoded), snapshot_frame = flatten(encoded_snapshot),
             marker = payload("keep");
        r::vector<r::entry> entries;
        for (const auto& e : std::get<r::append_entries>(append.payload).entries)
            entries.push_back(*e);
        r::buffer disk;
        check(!r::encode_record(entries, disk), "allocation setup record");
        r::shared_buffer record{std::move(disk)};
        r::vector<r::entry_header> metadata{{1, r::entry_type::command, 8}};
        r::buffer batch;
        check(!r::encode_batch_header(metadata, batch), "allocation setup batch");
        for (unsigned operation = 0; operation < 8; ++operation) {
            unsigned failed = 0, succeeded = 0;
            for (std::ptrdiff_t limit = 0; limit < 80; ++limit) {
                r::encoded_message packet;
                packet.header = marker;
                packet.payload = {marker};
                r::message message;
                message.server_id = 999;
                r::vector<r::entry> values{{99, r::entry_type::barrier, marker}};
                r::vector<r::entry_header> headers{{99, r::entry_type::barrier, 8}};
                auto buffer = r::buffer::copy(marker.bytes());
                r::size_t consumed = 999;
                r::error_code status;
                allocation_failure::after(limit);
                switch (operation) {
                case 0:
                    status = r::encode_message(append, packet);
                    break;
                case 1:
                    status = r::decode_message(frame, message, consumed);
                    break;
                case 2:
                    status = r::encode_record(entries, buffer);
                    break;
                case 3:
                    status = r::decode_record(record, values, consumed);
                    break;
                case 4:
                    status = r::encode_batch_header(metadata, buffer);
                    break;
                case 5:
                    status = r::decode_batch_header(batch.bytes(), headers, consumed);
                    break;
                case 6:
                    status = r::encode_message(snapshot, packet);
                    break;
                default:
                    status = r::decode_message(snapshot_frame, message, consumed);
                    break;
                }
                allocation_failure::after(-1);
                if (!status) {
                    ++succeeded;
                    continue;
                }
                ++failed;
                check(status == r::errc::out_of_memory, "allocation failures mapped to error_code");
                check(consumed == 999 && message.server_id == 999 &&
                          packet.header.shares_storage_with(marker) && packet.payload.size() == 1 &&
                          packet.payload[0].shares_storage_with(marker) && values[0].term == 99 &&
                          headers[0].term == 99 && equal(buffer.bytes(), marker.bytes()),
                      "allocation failure preserves every output");
            }
            check(failed != 0 && succeeded != 0, "allocation sweep covers failure and success");
        }
    }
}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected encoding suite");
        const r::string_view suite{argv[1]};
        if (suite == "checksums")
            checksums();
        else if (suite == "wire")
            messages();
        else if (suite == "records")
            records();
        else if (suite == "encoding_malformed")
            malformed();
        else if (suite == "encoding_limits")
            encoding_limits_suite();
        else if (suite == "encoding_failure")
            failures();
        else
            throw std::runtime_error("unknown encoding suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
