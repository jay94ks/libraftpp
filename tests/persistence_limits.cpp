// Adversarial disk declarations compared with unchanged cowsql/raft readers;
// see LICENSE.md. Large declarations use small, real backing files.
#include <raftpp.hpp>
#include "byte.hpp"
#include "disk.hpp"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <source_location>
#include <unistd.h>

namespace r = raftpp;
namespace d = raftpp::detail;
namespace {
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string pattern = "/tmp/raftpp-disk-limits-XXXXXX";
            check(mkdtemp(pattern.data()) != nullptr, "temporary directory");
            path = pattern;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    void write(const r::fs::path& path, r::view<const r::byte> value) {
        std::ofstream output{path, std::ios::binary | std::ios::trunc};
        if (!value.empty())
            output.write(reinterpret_cast<const char*>(value.data()),
                         static_cast<std::streamsize>(value.size()));
        check(output.good(), "write boundary input");
    }
    template <typename T> void put(r::buffer& value, r::size_t offset, T number) {
        d::store_little_endian<T>(
            number, value.bytes().subspan(offset, sizeof(T)).template first<sizeof(T)>());
    }
    r::shared_buffer payload() {
        r::buffer bytes{8};
        std::ranges::fill(bytes.bytes(), r::byte{0x6a});
        return r::shared_buffer{std::move(bytes)};
    }
    r::buffer segment() {
        r::array<r::entry, 1> entries{r::entry{2, r::entry_type::command, payload()}};
        r::buffer record;
        check(!r::encode_record(entries, record), "encode boundary record");
        r::buffer result{8 + record.size()};
        put(result, 0, r::disk_format_version);
        std::ranges::copy(record.bytes(), result.bytes().begin() + 8);
        return result;
    }
    r::string closed_name(r::log_index first, r::log_index last) {
        const auto a = std::to_string(first), b = std::to_string(last);
        return r::string(16 - a.size(), '0') + a + "-" + r::string(16 - b.size(), '0') + b;
    }
    struct statistics {
        unsigned compared = 0, limited = 0;
    };

    void closed_case(r::log_index first, r::log_index last, const r::buffer& bytes,
                     r::error_code expected, statistics& counts) {
        directory dir;
        d::disk_storage disk{dir.path};
        check(!disk.open(), "open segment store");
        const auto name = closed_name(first, last);
        write(dir.path / name, bytes.bytes());
        d::segment_file file;
        check(d::parse_segment_filename(name, file), "parse closed range");
        const auto owner = payload();
        r::vector<r::entry> output{{99, r::entry_type::barrier, owner}};
        r::error_info info;
        const auto fds = descriptors();
        const auto status = disk.load_closed(file, output, &info);
        check(status == expected, "closed segment status");
        ++counts.compared;
        if (status) {
            check(output.size() == 1 && output[0].term == 99 &&
                      output[0].type == r::entry_type::barrier &&
                      output[0].payload.shares_storage_with(owner) && info.code == status,
                  "failed closed read preserves output and reports diagnostic");
        } else {
            check(output.size() == 1 && output[0].term == 2 &&
                      output[0].type == r::entry_type::command &&
                      std::ranges::equal(output[0].payload.bytes(), owner.bytes()),
                  "loaded closed entry");
        }
        check(descriptors() == fds && r::fs::file_size(dir.path / name) == bytes.size(),
              "readers preserve file size and descriptors");
        std::ifstream input{dir.path / name, std::ios::binary};
        r::buffer after{bytes.size()};
        input.read(reinterpret_cast<char*>(after.data()),
                   static_cast<std::streamsize>(after.size()));
        check(input.good() && std::ranges::equal(after.bytes(), bytes.bytes()),
              "readers preserve file bytes");
    }
    void segments(statistics& counts) {
        const auto bytes = segment();
        constexpr auto wide = r::uint64_t{1} << 32;
        constexpr r::uint64_t last = 9999999999999999ULL;
        closed_case(1, 1, bytes, {}, counts);
        closed_case(1, 2, bytes, r::errc::corrupt, counts);
        closed_case(1, wide, bytes, r::errc::corrupt, counts);
        // C truncates expected_n to unsigned, accepting one actual entry here.
        // C++ retains the full filename range and rejects the inconsistent file.
        closed_case(1, wide + 1, bytes, r::errc::corrupt, counts);
        closed_case(1, wide * 2 + 1, bytes, r::errc::corrupt, counts);
        closed_case(wide + 17, wide + 17, bytes, {}, counts);
        closed_case(last, last, bytes, {}, counts);
        closed_case(last - 2, last, bytes, r::errc::corrupt, counts);

        for (const auto count : {r::uint64_t{0}, r::uint64_t{262144}, r::uint64_t{262145}, wide,
                                 r::numeric_limits<r::uint64_t>::max()}) {
            auto changed = r::buffer::copy(bytes.bytes());
            put(changed, 16, count);
            const auto status = count == 262144 ? r::errc::io_error : r::errc::corrupt;
            closed_case(1, 1, changed, status, counts);
        }
        auto changed = r::buffer::copy(bytes.bytes());
        // One aligned payload length near UINT32_MAX, without a large payload.
        put(changed, 36, r::uint32_t{4294967288U});
        put(changed, 8, r::crc32(changed.bytes().subspan(16, 24)));
        closed_case(1, 1, changed, r::errc::io_error, counts);
        changed = r::buffer::copy(bytes.bytes());
        changed.bytes()[32] = r::byte{99};
        put(changed, 8, r::crc32(changed.bytes().subspan(16, 24)));
        closed_case(1, 1, changed, r::errc::malformed, counts);
    }

    r::snapshot snapshot(r::size_t address_size = 3) {
        r::snapshot result;
        result.term = 2;
        result.index = 10;
        result.configuration_index = 1;
        check(!result.configuration.add(1, r::string(address_size, 'a'), r::role::voter),
              "snapshot configuration");
        result.buffers.push_back(payload());
        return result;
    }
    void snapshot_case(const r::buffer& metadata, r::error_code expected, statistics& counts) {
        directory dir;
        d::disk_storage disk{dir.path};
        check(!disk.open(), "open snapshot store");
        constexpr auto name = "snapshot-2-10-1.meta";
        write(dir.path / name, metadata.bytes());
        const auto data = payload();
        write(dir.path / "snapshot-2-10-1", data.bytes());
        d::snapshot_file file;
        check(d::parse_snapshot_filename(name, true, file), "parse snapshot filename");
        auto output = snapshot();
        output.term = 77;
        output.index = 88;
        output.configuration_index = 99;
        const auto owner = output.buffers[0];
        const auto fds = descriptors();
        r::error_info info;
        const auto status = disk.load_snapshot(file, output, 8, &info);
        check(status == expected, "snapshot boundary status");
        ++counts.compared;
        if (!status)
            check(output.term == 2 && output.index == 10 && output.configuration_index == 1 &&
                      output.buffers.size() == 1 &&
                      std::ranges::equal(output.buffers[0].bytes(), data.bytes()),
                  "snapshot metadata and bytes have prescribed fixture values");
        if (status)
            check(info.code == status && output.term == 77 && output.index == 88 &&
                      output.configuration_index == 99 &&
                      output.configuration.servers()[0].address == "aaa" &&
                      output.buffers[0].shares_storage_with(owner),
                  "failed snapshot preserves all output");
        check(descriptors() == fds && r::fs::file_size(dir.path / name) == metadata.size(),
              "snapshot readers preserve descriptors and file size");
        std::ifstream input{dir.path / name, std::ios::binary};
        r::buffer after{metadata.size()};
        input.read(reinterpret_cast<char*>(after.data()),
                   static_cast<std::streamsize>(after.size()));
        check(input.good() && std::ranges::equal(after.bytes(), metadata.bytes()),
              "snapshot readers preserve metadata bytes");
    }
    void snapshots(statistics& counts) {
        r::buffer good;
        check(!r::encode_snapshot_metadata(snapshot(), good), "snapshot metadata setup");
        constexpr r::uint64_t maximum = 1024 * 1024;
        for (auto length :
             {r::uint64_t{0}, maximum + 1, r::uint64_t{4294967295U}, r::uint64_t{1} << 32,
              r::uint64_t{1} << 63, r::numeric_limits<r::uint64_t>::max(), maximum, maximum - 1,
              static_cast<r::uint64_t>(good.size() - 32)}) {
            auto header = r::buffer::copy(good.bytes().first(32));
            put(header, 24, length);
            snapshot_case(header,
                          length != 0 && length <= maximum ? r::errc::io_error : r::errc::corrupt,
                          counts);
        }
        auto changed = r::buffer::copy(good.bytes());
        put(changed, 0, r::uint64_t{99});
        put(changed, 24, r::numeric_limits<r::uint64_t>::max());
        snapshot_case(changed, r::errc::malformed, counts);
        changed = r::buffer::copy(good.bytes());
        changed.bytes()[8] ^= r::byte{1};
        snapshot_case(changed, r::errc::corrupt, counts);
        changed = r::buffer::copy(good.bytes());
        changed.bytes()[12] = r::byte{0xff};
        snapshot_case(changed, {}, counts); // Only the low 32 checksum bits count.
        changed = r::buffer::copy(good.bytes());
        changed.resize(good.size() + 16);
        std::ranges::fill(changed.bytes().subspan(good.size()), r::byte{0xff});
        snapshot_case(changed, {}, counts); // Bytes beyond declared metadata are ignored.
        auto boundary = snapshot(static_cast<r::size_t>(maximum) - 19);
        check(!r::encode_snapshot_metadata(boundary, changed) && changed.size() == 32 + maximum,
              "valid configuration exactly at one MiB");
        snapshot_case(changed, {}, counts);

        directory dir;
        unsigned data_opens = 0;
        d::disk_storage disk{dir.path, [&](auto operation, r::string_view name) {
                                 if (operation == d::file_operation::open &&
                                     name == "snapshot-2-10-1")
                                     ++data_opens;
                                 return r::error_code{};
                             }};
        check(!disk.open(), "open limited payload store");
        write(dir.path / "snapshot-2-10-1.meta", good.bytes());
        const auto owner = payload();
        write(dir.path / "snapshot-2-10-1", owner.bytes());
        d::snapshot_file file;
        check(d::parse_snapshot_filename("snapshot-2-10-1.meta", true, file), "limit filename");
        auto output = snapshot();
        output.buffers[0] = owner;
        output.index = 99;
        r::error_info info;
        const auto fds = descriptors();
        check(disk.load_snapshot(file, output, 7, &info) == r::errc::too_big &&
                  info.code == r::errc::too_big && data_opens == 0 && output.index == 99 &&
                  output.buffers[0].shares_storage_with(owner),
              "payload maximum checked before data open and preserves output");
        ++counts.limited;
        check(descriptors() == fds, "payload limits preserve descriptors");
        check(!disk.load_snapshot(file, output, 8, &info) && !info.code && data_opens == 1 &&
                  output.index == 10 &&
                  std::ranges::equal(output.buffers[0].bytes(), owner.bytes()),
              "exact payload maximum succeeds and clears diagnostic");
        ++counts.limited;
    }
}
void storage_limits_suite() {
    statistics counts;
    segments(counts);
    snapshots(counts);
    std::cout << "storage boundaries: " << counts.compared << " reader cases, " << counts.limited
              << " payload-limit cases\n";
}
