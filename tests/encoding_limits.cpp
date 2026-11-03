#include <raftpp/encoding.hpp>
#include "byte.hpp"
#include <raftpp/config.hpp>

#include <algorithm>
#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace {
    constexpr auto maximum = r::numeric_limits<r::size_t>::max();
    constexpr auto entry_maximum = r::numeric_limits<r::uint32_t>::max();
    constexpr r::encoding_limits unlimited{maximum, maximum, maximum};
    unsigned comparisons = 0;

    void check(bool value, r::string_view message,
               r::source_location where = r::source_location::current()) {
        if (!value)
            throw r::system_error(r::make_error_code(r::errc::invalid),
                                  r::string{where.file_name()} + ":" +
                                      std::to_string(where.line()) + ": " + r::string{message});
    }
    r::shared_buffer marker() {
        const r::array<r::byte, 4> data{r::byte{1}, r::byte{2}, r::byte{3}, r::byte{4}};
        return r::shared_buffer::copy(data);
    }
    void put(r::buffer& data, r::size_t offset, r::uint64_t value) {
        r::detail::store_little_endian<r::uint64_t>(
            value, r::view<r::byte, 8>{data.bytes().subspan(offset, 8)});
    }

    r::buffer append_header(r::view<const r::byte> batch) {
        r::buffer data{40 + batch.size()};
        put(data, 0, 7);
        put(data, 8, 2);
        put(data, 16, 5);
        put(data, 24, 2);
        std::ranges::copy(batch, data.bytes().begin() + 32);
        return data;
    }
    r::shared_buffer frame(r::view<const r::byte> header, r::uint8_t type = 1) {
        r::buffer data{16 + header.size()};
        data.bytes()[0] = static_cast<r::byte>(type);
        put(data, 8, header.size());
        std::ranges::copy(header, data.bytes().begin() + 16);
        return r::shared_buffer{std::move(data)};
    }

    void check_descriptors(r::view<const r::entry_header> entries, r::view<const r::byte> batch,
                           r::view<const r::byte> header, r::size_t total) {
        r::vector<r::entry_header> decoded;
        r::size_t consumed = 0;
        check(!r::decode_batch_header(batch, decoded, consumed, unlimited) &&
                  decoded.size() == entries.size() && consumed == batch.size(),
              "maximum-width header descriptors decode without payload allocation");
        for (r::size_t i = 0; i < entries.size(); ++i)
            check(decoded[i].term == entries[i].term && decoded[i].type == entries[i].type &&
                      decoded[i].size == entries[i].size,
                  "maximum-width header values");
        r::message_header message;
        check(!r::decode_message_header({1, 0, header.size()}, header, message, unlimited) &&
                  message.payload_size == total && message.entries.size() == entries.size(),
              "aggregate payload declaration is checked without allocating payload");
        ++comparisons;
    }

    unsigned descriptor_limits() {
        const r::vector<r::vector<r::size_t>> sizes{{0},
                                                    {entry_maximum},
                                                    {entry_maximum - 7},
                                                    {entry_maximum, 1},
                                                    {entry_maximum, entry_maximum},
                                                    {0, entry_maximum, 8},
                                                    {8, 0, 16},
                                                    {0, 0, 0}};
        unsigned cases = 0;
        for (const auto& lengths : sizes) {
            r::uint64_t wide_total = 0;
            for (auto length : lengths)
                wide_total += length;
            if (wide_total > maximum)
                continue;
            ++cases;
            r::vector<r::entry_header> entries;
            r::size_t total = 0;
            for (r::size_t i = 0; i < lengths.size(); ++i) {
                entries.push_back({r::numeric_limits<r::term>::max() - i,
                                   static_cast<r::entry_type>(i % 3 + 1), lengths[i]});
                total += lengths[i];
            }
            auto limits = unlimited;
            limits.max_header_size = 8 + 16 * lengths.size();
            limits.max_payload_size = total;
            limits.max_entries = lengths.size();
            r::buffer batch;
            check(!r::encode_batch_header(entries, batch, limits) &&
                      batch.size() == limits.max_header_size,
                  "exact batch header, aggregate payload and count limits are inclusive");
            r::vector<r::entry_header> decoded;
            r::size_t consumed = 99;
            check(!r::decode_batch_header(batch.bytes(), decoded, consumed, limits) &&
                      consumed == batch.size() && decoded.size() == entries.size(),
                  "exact batch limits decode without allocating payload");
            auto header = append_header(batch.bytes());
            r::message_header rpc;
            auto rpc_limits = limits;
            rpc_limits.max_header_size = header.size();
            check(
                !r::decode_message_header({1, 0, header.size()}, header.bytes(), rpc, rpc_limits) &&
                    rpc.payload_size == total && rpc.entries.size() == entries.size(),
                "exact full append header limit and aggregate length decode");
            check_descriptors(entries, batch.bytes(), header.bytes(), total);
            for (unsigned field = 0; field < 3; ++field) {
                if (field == 1 && total == 0)
                    continue;
                auto limited = limits;
                if (field == 0)
                    --limited.max_header_size;
                else if (field == 1)
                    --limited.max_payload_size;
                else
                    --limited.max_entries;
                r::buffer output{3};
                auto* address = output.data();
                check(r::encode_batch_header(entries, output, limited) == r::errc::too_big &&
                          output.data() == address && output.size() == 3,
                      "encoding one byte/entry beyond each limit preserves output");
                r::vector<r::entry_header> target{{99, r::entry_type::barrier, 8}};
                consumed = 77;
                check(r::decode_batch_header(batch.bytes(), target, consumed, limited) ==
                              r::errc::too_big &&
                          target.size() == 1 && target[0].term == 99 && consumed == 77,
                      "decoding one byte/entry beyond each limit preserves metadata and consumed");
                limited.max_header_size = header.size() - (field == 0 ? 1 : 0);
                rpc.payload_size = 123;
                check(r::decode_message_header({1, 0, header.size()}, header.bytes(), rpc,
                                               limited) == r::errc::too_big &&
                          rpc.payload_size == 123,
                      "staged append limit failure preserves an existing decoded header");
            }
        }
        if constexpr (maximum > entry_maximum) {
            const r::array<r::entry_header, 1> entries{
                {{1, r::entry_type::command, r::size_t{entry_maximum} + 1}}};
            r::buffer output{3};
            const auto* address = output.data();
            check(
                r::encode_batch_header(entries, output, unlimited) == r::errc::too_big &&
                    output.data() == address,
                "entry length above uint32_t cannot silently truncate even with unlimited configured limits");
        }
        return cases + (maximum > entry_maximum ? 1 : 0);
    }

    unsigned hostile_counts() {
        for (const auto count :
             {r::uint64_t{entry_maximum} + 1, r::numeric_limits<r::uint64_t>::max()}) {
            r::buffer batch{8};
            put(batch, 0, count);
            r::vector<r::entry_header> output{{99, r::entry_type::barrier, 8}};
            r::size_t consumed = 77;
            check(
                r::decode_batch_header(batch.bytes(), output, consumed, unlimited) ==
                        r::errc::too_big &&
                    output.size() == 1 && output.front().term == 99 && consumed == 77,
                "untrusted uint64_t count cannot truncate to unsigned or require enormous header allocation");
            auto header = append_header(batch.bytes());
            r::message_header decoded;
            decoded.payload_size = 99;
            check(r::decode_message_header({1, 0, header.size()}, header.bytes(), decoded,
                                           unlimited) == r::errc::too_big &&
                      decoded.payload_size == 99,
                  "same count is rejected by staged RPC header decoding");
        }
        return 2;
    }

    unsigned declared_lengths() {
        for (const auto length : {r::uint64_t{0}, r::numeric_limits<r::uint64_t>::max() / 2,
                                  r::numeric_limits<r::uint64_t>::max()}) {
            r::buffer bytes{16};
            bytes.bytes()[0] = r::byte{1};
            put(bytes, 8, length);
            r::message_preamble preamble{6, 9, 24};
            if (length == 0) {
                check(r::decode_preamble(bytes.bytes(), preamble, unlimited) ==
                              r::errc::malformed &&
                          preamble.type == 6 && preamble.header_size == 24,
                      "zero header length is malformed and preserves preamble");
            } else if (length > maximum) {
                check(r::decode_preamble(bytes.bytes(), preamble, unlimited) == r::errc::too_big &&
                          preamble.type == 6 && preamble.header_size == 24,
                      "nonrepresentable preamble length preserves output");
            } else {
                check(!r::decode_preamble(bytes.bytes(), preamble, unlimited) &&
                          preamble.header_size == length,
                      "preamble may describe a large header without allocating it");
                r::message output;
                output.server_id = 999;
                r::size_t consumed = 77;
                check(r::decode_message(r::shared_buffer::copy(bytes.bytes()), output, consumed,
                                        unlimited) == r::errc::malformed &&
                          output.server_id == 999 && consumed == 77,
                      "full-frame bounds subtract before adding an untrusted header length");
                preamble = {6, 9, 24};
                check(r::decode_preamble(bytes.bytes(), preamble) == r::errc::too_big &&
                          preamble.type == 6 && preamble.header_size == 24,
                      "default header cap rejects large declarations before allocation");
            }
        }
        r::install_snapshot snapshot;
        snapshot.term = 7;
        snapshot.last_index = 9;
        snapshot.last_term = 6;
        snapshot.configuration_index = 1;
        check(!snapshot.configuration.add(1, "one", r::role::voter),
              "configure snapshot length header");
        r::message input;
        input.payload = std::move(snapshot);
        r::encoded_message encoded;
        check(!r::encode_message(input, encoded), "encode snapshot header without payload");
        for (const auto length :
             {r::uint64_t{0}, r::uint64_t{1}, r::numeric_limits<r::uint64_t>::max()}) {
            auto header = r::buffer::copy(encoded.header.bytes().subspan(16));
            put(header, header.size() - 16, length);
            r::message_header decoded;
            if (length > maximum) {
                decoded.payload_size = 99;
                check(r::decode_message_header({5, 0, header.size()}, header.bytes(), decoded,
                                               unlimited) == r::errc::too_big &&
                          decoded.payload_size == 99,
                      "nonrepresentable snapshot length preserves staged header");
                continue;
            }
            check(!r::decode_message_header({5, 0, header.size()}, header.bytes(), decoded,
                                            unlimited) &&
                      decoded.payload_size == length,
                  "snapshot length is a checked size_t descriptor, not an allocation");

            r::message output;
            output.server_id = 999;
            r::size_t consumed = 77;
            const auto status =
                r::decode_message(frame(header.bytes(), 5), output, consumed, unlimited);
            if (length == 0)
                check(!status && consumed == 16 + header.size(),
                      "zero-byte snapshot completes at header boundary");
            else
                check(
                    status == r::errc::malformed && output.server_id == 999 && consumed == 77,
                    "untrusted snapshot payload length cannot wrap full-frame bounds or publish partial output");
            if (length > 8 * 1024 * 1024) {
                decoded.payload_size = 99;
                check(r::decode_message_header({5, 0, header.size()}, header.bytes(), decoded) ==
                              r::errc::too_big &&
                          decoded.payload_size == 99,
                      "default snapshot payload cap preserves prior parsed header");
            }
        }
        return 6;
    }

    unsigned staged_assembly() {
        r::message_header header;
        header.payload = r::append_entries{};
        header.entries = {{1, r::entry_type::command, 8},
                          {2, r::entry_type::barrier, 0},
                          {3, r::entry_type::change, 8}};
        header.payload_size = 16;
        r::buffer bytes{16};
        for (r::size_t i = 0; i < bytes.size(); ++i)
            bytes.bytes()[i] = static_cast<r::byte>(i);
        r::shared_buffer payload{std::move(bytes)};
        r::weak<const r::byte> observer = payload.storage();
        r::message result;
        check(!r::decode_message_payload(header, payload, result),
              "assemble a mixed zero/nonzero-entry batch");
        auto entries = std::get<r::append_entries>(result.payload).entries;
        check(entries.size() == 3 && entries[1]->payload.empty() &&
                  entries[0]->payload.size() == 8 && entries[2]->payload.bytes()[0] == r::byte{8} &&
                  entries[0]->payload.shares_storage_with(entries[2]->payload),
              "assembly offsets ignore zero lengths and retain a single shared batch");
        result = {};
        payload = {};
        header = {};
        check(!observer.expired() && entries[2]->payload.bytes()[7] == r::byte{15},
              "saved entries outlive payload, parsed header, and message");
        entries.clear();
        check(observer.expired(), "final entry release destroys the shared batch owner");

        const unsigned faults = maximum > entry_maximum ? 7 : 6;
        for (unsigned fault = 0; fault < faults; ++fault) {
            r::message_header invalid;
            invalid.payload = r::append_entries{};
            invalid.entries = {{1, r::entry_type::command, 4}};
            invalid.payload_size = 4;
            auto data = marker();
            if (fault == 0)
                invalid.payload_size = 3;
            else if (fault == 1)
                invalid.entries.front().size = 3;
            else if (fault == 2)
                invalid.entries.front().type = static_cast<r::entry_type>(0);
            else if (fault == 3)
                std::get<r::append_entries>(invalid.payload)
                    .entries.push_back(std::make_shared<const r::entry>());
            else if (fault == 4)
                invalid.payload = r::timeout_now{};
            else if (fault == 5) {
                invalid.payload = r::install_snapshot{};
                invalid.entries.clear();
                std::get<r::install_snapshot>(invalid.payload).needs_data = true;
            } else
                invalid.entries.front().size = r::size_t{entry_maximum} + 1;
            r::message output;
            output.server_id = 999;
            output.server_address = "unchanged";
            const auto status = r::decode_message_payload(invalid, std::move(data), output);
            check(status == (fault == 6 ? r::errc::too_big : r::errc::malformed) &&
                      output.server_id == 999 && output.server_address == "unchanged",
                  "inconsistent caller-built staged header cannot publish output");
        }
        return faults + 1;
    }
}

void encoding_limits_suite() {
    const auto count =
        descriptor_limits() + hostile_counts() + declared_lengths() + staged_assembly();
    std::cout << "encoding limits: " << count << " cases, " << comparisons
              << " descriptor checks\n";
}
