#include <raftpp.hpp>
#include "byte.hpp"
#if RAFTPP_WITH_UV
#include "disk.hpp"
#endif

#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <source_location>
#if RAFTPP_WITH_UV
#include <unistd.h>
extern "C" void raftpp_test_no_space(int);

#if RAFTPP_WITH_LZ4
#include <lz4frame.h>
#endif
#endif

namespace r = raftpp;
namespace d = raftpp::detail;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
#if RAFTPP_WITH_UV
void segment_suite(r::string_view);
void storage_limits_suite();
#endif
namespace {
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    r::view<const r::byte> bytes(r::string_view s) {
        return {reinterpret_cast<const r::byte*>(s.data()), s.size()};
    }
    r::shared_buffer data(r::string_view s) {
        return r::shared_buffer::copy(bytes(s));
    }
    bool equal(r::view<const r::byte> a, r::view<const r::byte> b) {
        return std::ranges::equal(a, b);
    }
    r::snapshot sample(r::uint64_t term = 2, r::uint64_t index = 10) {
        r::snapshot value;
        value.term = term;
        value.index = index;
        value.configuration_index = 1;
        check(!value.configuration.add(1, "node-1", r::role::voter) &&
                  !value.configuration.add(2, "node-2", r::role::standby),
              "snapshot configuration");
        value.buffers = {data("one-"), data("two")};
        return value;
    }
    void formats() {
        std::mt19937_64 random{21};
        for (unsigned i = 0; i < 500; ++i) {
            r::persistent_metadata input{1 + random() % 1000000, random(), random()}, output;
            r::buffer encoded;
            check(!r::encode_metadata(input, encoded) && encoded.size() == 32, "metadata encode");
            check(d::load_little_endian<r::uint64_t>(encoded.bytes().first<8>()) == 1 &&
                      d::load_little_endian<r::uint64_t>(encoded.bytes().subspan<8, 8>()) ==
                          input.version,
                  "metadata fixed layout");
            check(!r::decode_metadata(encoded.bytes(), output) && output.version == input.version &&
                      output.term == input.term && output.voted_for == input.voted_for,
                  "metadata roundtrip");
            auto value = sample();
            value.configuration_index = random();
            check(!r::encode_snapshot_metadata(value, encoded), "snapshot metadata encode");
            r::snapshot restored;
            restored.term = 999;
            restored.index = 888;
            restored.buffers = {data("keep")};
            check(!r::decode_snapshot_metadata(encoded.bytes(), restored) &&
                      restored.configuration_index == value.configuration_index &&
                      restored.configuration.size() == 2 && restored.term == 999 &&
                      restored.index == 888 && equal(restored.buffers[0].bytes(), bytes("keep")),
                  "snapshot decode updates only metadata fields");
        }
        auto value = sample();
        r::buffer good;
        check(!r::encode_snapshot_metadata(value, good), "corrupt setup");
        for (r::size_t cut = 0; cut < good.size(); ++cut) {
            r::snapshot output;
            output.configuration_index = 99;
            check(r::decode_snapshot_metadata(good.bytes().first(cut), output) != r::error_code{} &&
                      output.configuration_index == 99 && output.configuration.empty(),
                  "short snapshot metadata preserves output");
        }
        for (r::size_t position = 16; position < good.size(); ++position) {
            auto changed = r::buffer::copy(good.bytes());
            changed.bytes()[position] ^= r::byte{1};
            r::snapshot output;
            check(r::decode_snapshot_metadata(changed.bytes(), output) != r::error_code{},
                  "snapshot metadata checksum or length corruption");
        }
        r::persistent_metadata output{99, 99, 99};
        r::buffer short_file{31};
        check(r::decode_metadata(short_file.bytes(), output) == r::errc::io_error &&
                  output.version == 99,
              "short metadata");
        short_file.resize(33);
        check(r::decode_metadata(short_file.bytes(), output) == r::errc::corrupt, "long metadata");
        short_file.resize(32);
        check(r::decode_metadata(short_file.bytes(), output) == r::errc::malformed,
              "bad metadata format");
        d::store_little_endian<r::uint64_t>(1, short_file.bytes().first<8>());
        check(r::decode_metadata(short_file.bytes(), output) == r::errc::corrupt &&
                  output.version == 99,
              "zero metadata generation");
        for (unsigned operation = 0; operation < 3; ++operation) {
            unsigned failed = 0, succeeded = 0;
            for (std::ptrdiff_t limit = 0; limit < 20; ++limit) {
                auto target = r::buffer::copy(bytes("keep"));
                r::snapshot restored;
                restored.configuration_index = 99;
                allocation_failure::after(limit);
                const auto status = operation == 0 ? r::encode_metadata({1, 2, 3}, target)
                                    : operation == 1
                                        ? r::encode_snapshot_metadata(value, target)
                                        : r::decode_snapshot_metadata(good.bytes(), restored);
                allocation_failure::after(-1);
                if (status) {
                    ++failed;
                    check(status == r::errc::out_of_memory &&
                              equal(target.bytes(), bytes("keep")) &&
                              restored.configuration_index == 99 && restored.configuration.empty(),
                          "metadata allocation failure preserves output");
                } else
                    ++succeeded;
            }
            check(failed != 0 && succeeded != 0, "metadata allocation sweep");
        }
    }

#if RAFTPP_WITH_UV
    struct directory {
        r::fs::path path;
        directory() {
            r::string pattern = "/tmp/raftpp-storage-XXXXXX";
            auto result = mkdtemp(pattern.data());
            check(result != nullptr, "temporary directory");
            path = result;
        }
        ~directory() {
            std::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    void write(const r::fs::path& path, r::view<const r::byte> value) {
        std::ofstream out{path, std::ios::binary | std::ios::trunc};
        if (!value.empty())
            out.write(reinterpret_cast<const char*>(value.data()),
                      static_cast<std::streamsize>(value.size()));
        check(out.good(), "write fixture");
    }
    r::buffer read(const r::fs::path& path) {
        r::buffer value{static_cast<r::size_t>(r::fs::file_size(path))};
        std::ifstream input{path, std::ios::binary};
        if (!value.empty())
            input.read(reinterpret_cast<char*>(value.data()),
                       static_cast<std::streamsize>(value.size()));
        check(input.good(), "read fixture");
        return value;
    }
    r::size_t descriptors() {
        return static_cast<r::size_t>(
            std::distance(r::fs::directory_iterator{"/proc/self/fd"}, r::fs::directory_iterator{}));
    }
    void metadata_files() {
        directory dir;
        r::vector<d::file_operation> operations;
        d::disk_storage storage{dir.path, [&](auto op, r::string_view) {
                                    operations.push_back(op);
                                    return r::error_code{};
                                }};
        check(!storage.open() && storage.metadata().version == 0, "new store");
        operations.clear();
        check(!storage.set_term(2), "persist initial term");
        check(operations == r::vector<d::file_operation>{d::file_operation::open,
                                                         d::file_operation::write,
                                                         d::file_operation::sync_file,
                                                         d::file_operation::close,
                                                         d::file_operation::sync_directory},
              "new slot fsync before directory sync");
        check(!storage.set_vote(3), "persist vote");
        operations.clear();
        check(!storage.set_term(4) && storage.metadata().voted_for == 0, "new term clears vote");
        check(operations == r::vector<d::file_operation>{d::file_operation::open,
                                                         d::file_operation::write,
                                                         d::file_operation::sync_data,
                                                         d::file_operation::close},
              "existing slot uses fdatasync");
        check(storage.metadata().version == 3 && storage.metadata().term == 4 &&
                  storage.metadata().voted_for == 0,
              "stored metadata generation and reset vote");
        r::buffer fixture;
        check(!r::encode_metadata({4, 5, 2}, fixture), "newer slot fixture");
        write(dir.path / "metadata2", fixture.bytes());
        d::disk_storage reload{dir.path};
        check(!reload.open() && reload.metadata().term == 5 && reload.metadata().voted_for == 2,
              "loads fixed metadata fixture");
        write(dir.path / "metadata2", bytes("short"));
        d::disk_storage torn{dir.path};
        check(!torn.open() && torn.metadata().version == 3 &&
                  !r::fs::exists(dir.path / "metadata2"),
              "short newer slot discarded and previous slot recovered");
        r::buffer bad{33};
        write(dir.path / "metadata2", bad.bytes());
        d::disk_storage oversized{dir.path};
        check(oversized.open() == r::errc::corrupt,
              "oversized metadata is corruption rather than fallback");
        r::buffer encoded;
        check(!r::encode_metadata({3, 4, 0}, encoded), "equal slot setup");
        write(dir.path / "metadata2", encoded.bytes());
        d::disk_storage equal_versions{dir.path};
        check(equal_versions.open() == r::errc::corrupt, "equal generations are corrupt");
        d::disk_storage absent{dir.path / "absent"};
        check(absent.open() == r::errc::not_found, "absent directory");
    }
    void snapshot_files() {
        directory dir;
        d::disk_storage storage{dir.path};
        check(!storage.open(), "snapshot store open");
        auto value = sample();
        const auto fd_count = descriptors();
        {
            d::prepared_snapshot first;
            check(!storage.prepare_snapshot(value, 7, first) && !first.empty(),
                  "prepare anonymous files");
            r::vector<d::snapshot_file> files;
            check(!storage.list_snapshots(files) && files.empty(), "prepared files invisible");
            d::prepared_snapshot moved{std::move(first)};
            check(first.empty() && !moved.empty(), "prepared handles move");
        }
        check(descriptors() == fd_count, "abandoned snapshot closes both descriptors");
        d::prepared_snapshot lease;
        {
            d::disk_storage owner{dir.path};
            check(!owner.open() && !owner.prepare_snapshot(value, 90, lease),
                  "prepare with short-lived owner");
        }
        check(descriptors() == fd_count + 2 && !lease.empty(),
              "prepared snapshot retains resources after owner destruction");
        lease = {};
        check(descriptors() == fd_count, "last prepared owner releases file handles");
        d::prepared_snapshot prepared;
        check(!storage.prepare_snapshot(value, 7, prepared), "prepare snapshot");
        check(!storage.commit_snapshot(prepared) && prepared.empty(), "publish and sync snapshot");
        r::snapshot loaded;
        d::snapshot_file file;
        check(d::parse_snapshot_filename("snapshot-2-10-7.meta", true, file),
              "parse snapshot filename");
        check(!storage.load_snapshot(file, loaded) && loaded.term == 2 && loaded.index == 10 &&
                  loaded.configuration.size() == 2 &&
                  equal(loaded.buffers[0].bytes(), bytes("one-two")),
              "load snapshot pair");
        d::prepared_snapshot collision;
        auto replacement = sample();
        replacement.buffers = {data("replacement")};
        check(!storage.prepare_snapshot(replacement, 7, collision) &&
                  storage.commit_snapshot(collision) == r::errc::io_error && collision.empty() &&
                  !storage.load_snapshot(file, loaded) &&
                  equal(loaded.buffers[0].bytes(), bytes("one-two")),
              "filename collision never replaces an existing durable pair");
        auto fixture_snapshot = sample(3, 20);
        fixture_snapshot.configuration = {};
        check(!fixture_snapshot.configuration.add(1, "node-1", r::role::voter), "fixture voter");
        fixture_snapshot.configuration_index = 4;
        fixture_snapshot.buffers = {data("fixture snapshot")};
        d::prepared_snapshot fixture_files;
        check(!storage.prepare_snapshot(fixture_snapshot, 8, fixture_files) &&
                  !storage.commit_snapshot(fixture_files),
              "publish second snapshot fixture");
        r::optional<r::snapshot> latest;
        check(!storage.latest_snapshot(latest) && latest->term == 3 && latest->index == 20 &&
                  equal(latest->buffers[0].bytes(), bytes("fixture snapshot")),
              "reload snapshot with independent metadata and payload");
        auto newer = sample(3, 21);
        d::prepared_snapshot next;
        check(!storage.prepare_snapshot(newer, 9, next) && !storage.commit_snapshot(next) &&
                  !storage.prune_snapshots(),
              "publish and retain last two");
        r::vector<d::snapshot_file> files;
        check(!storage.list_snapshots(files) && files.size() == 2 &&
                  !r::fs::exists(dir.path / "snapshot-2-10-7.meta"),
              "prune old pair");
        // Sibling-less files and tmp-* files are discarded on startup.
        write(dir.path / "snapshot-4-25-10.meta", bytes("orphan"));
        write(dir.path / "snapshot-4-26-10", bytes("orphan"));
        write(dir.path / "tmp-leftover", bytes("temporary"));
        d::disk_storage cleaned{dir.path};
        check(!cleaned.open() && !r::fs::exists(dir.path / "snapshot-4-25-10.meta") &&
                  !r::fs::exists(dir.path / "snapshot-4-26-10") &&
                  !r::fs::exists(dir.path / "tmp-leftover"),
              "startup orphan cleanup");
        auto most_recent = sample(11, 30);
        check(!storage.prepare_snapshot(most_recent, 12, next) && !storage.commit_snapshot(next) &&
                  !storage.latest_snapshot(latest) && latest->term == 11,
              "snapshot ordering uses numeric term rather than filename text");
        check(d::parse_snapshot_filename("snapshot-11-30-12.meta", true, file),
              "new snapshot filename");
        auto corrupted = read(dir.path / file.filename);
        corrupted.bytes().back() ^= r::byte{1};
        write(dir.path / file.filename, corrupted.bytes());
        loaded.index = 777;
        check(storage.load_snapshot(file, loaded) == r::errc::corrupt && loaded.index == 777,
              "corrupt snapshot metadata is rejected without changing output");
    }
    void disk_failures() {
        for (auto operation : {d::file_operation::open, d::file_operation::write,
                               d::file_operation::sync_file, d::file_operation::sync_data,
                               d::file_operation::close, d::file_operation::sync_directory}) {
            directory dir;
            bool armed = false;
            d::disk_storage storage{dir.path, [&](auto op, r::string_view) {
                                        return armed && op == operation
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open(), "failure store open");
            if (operation == d::file_operation::sync_data)
                check(!storage.set_term(1) && !storage.set_vote(1),
                      "initialize both slots before fdatasync failure");
            const auto before = storage.metadata();
            const auto fds = descriptors();
            armed = true;
            check(storage.set_term(2) == r::errc::io_error &&
                      storage.metadata().version == before.version,
                  "failed metadata never acknowledged in cache");
            armed = false;
            check(descriptors() == fds, "metadata error closes descriptor");
            if (operation != d::file_operation::open)
                check(storage.set_vote(1) == r::errc::io_error,
                      "uncertain slot update stops further writes");
        }
        for (auto operation :
             {d::file_operation::open, d::file_operation::allocate, d::file_operation::write,
              d::file_operation::sync_file, d::file_operation::publish_metadata,
              d::file_operation::publish_snapshot, d::file_operation::close,
              d::file_operation::sync_directory}) {
            directory dir;
            bool armed = false;
            d::disk_storage storage{dir.path, [&](auto op, r::string_view) {
                                        return armed && op == operation
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open(), "snapshot failure store");
            auto value = sample();
            const auto fds = descriptors();
            d::prepared_snapshot prepared;
            armed = true;
            auto status = storage.prepare_snapshot(value, 1, prepared);
            if (!status)
                status = storage.commit_snapshot(prepared);
            armed = false;
            prepared = {};
            check(status == r::errc::io_error && descriptors() == fds,
                  "failed snapshot closes all handles");
            d::disk_storage reopen{dir.path};
            check(!reopen.open(), "reopen partial snapshot publication");
            r::optional<r::snapshot> visible;
            check(!reopen.latest_snapshot(visible), "ignore incomplete snapshot pairs");
            if (visible)
                check(equal(visible->buffers[0].bytes(), bytes("one-two")),
                      "published snapshot is complete even on late sync failure");
        }
        for (std::ptrdiff_t fail = 0; fail < 80; ++fail) {
            directory dir;
            d::disk_storage storage{dir.path};
            check(!storage.open(), "allocation failure store");
            d::prepared_snapshot prepared;
            auto value = sample();
            const auto fds = descriptors();
            allocation_failure::after(fail);
            const auto status = storage.prepare_snapshot(value, 5, prepared);
            allocation_failure::after(-1);
            if (status)
                check(status == r::errc::out_of_memory && prepared.empty(),
                      "prepare allocation failure output");
            prepared = {};
            check(descriptors() == fds, "prepare allocation sweep releases descriptors");
        }
    }
    void compressed_snapshot() {
        directory dir;
        d::disk_storage storage{dir.path};
        check(!storage.open(), "compressed store");
        auto value = sample();
#if RAFTPP_WITH_LZ4
        r::buffer frame{LZ4F_compressFrameBound(7, nullptr)};
        const auto size = LZ4F_compressFrame(frame.data(), frame.size(), "one-two", 7, nullptr);
        check(!LZ4F_isError(size), "make LZ4 frame");
        frame.resize(size);
        value.buffers = {r::shared_buffer{std::move(frame)}};
#else
        r::buffer frame{4};
        frame.bytes()[0] = r::byte{4};
        frame.bytes()[1] = r::byte{0x22};
        frame.bytes()[2] = r::byte{0x4d};
        frame.bytes()[3] = r::byte{0x18};
        value.buffers = {r::shared_buffer{std::move(frame)}};
#endif
        d::prepared_snapshot prepared;
        check(!storage.prepare_snapshot(value, 1, prepared) && !storage.commit_snapshot(prepared),
              "write compressed snapshot bytes");
        r::optional<r::snapshot> output;
#if RAFTPP_WITH_LZ4
        check(!storage.latest_snapshot(output) &&
                  equal(output->buffers[0].bytes(), bytes("one-two")),
              "LZ4 snapshot load");
#else
        check(storage.latest_snapshot(output) == r::errc::invalid && !output,
              "LZ4 disabled load error preserves output");
#endif
    }
    void no_space() {
        directory dir;
        d::disk_storage storage{dir.path};
        check(!storage.open(), "no-space store");
        auto value = sample();
        d::prepared_snapshot prepared;
        const auto fds = descriptors();
        raftpp_test_no_space(1);
        const auto actual = storage.prepare_snapshot(value, 1, prepared);
        raftpp_test_no_space(0);
        check(actual == r::errc::no_space && prepared.empty(),
              "native ENOSPC maps to the same no_space status as C");
        check(descriptors() == fds, "ENOSPC releases both implementations' temporary handles");
    }
#endif
}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected persistence suite");
        const r::string_view suite{argv[1]};
        if (suite == "metadata_formats")
            formats();
#if RAFTPP_WITH_UV
        else if (suite == "metadata_files")
            metadata_files();
        else if (suite == "snapshot_files")
            snapshot_files();
        else if (suite == "disk_failures")
            disk_failures();
        else if (suite == "compressed_snapshot")
            compressed_snapshot();
        else if (suite == "disk_no_space")
            no_space();
        else if (suite == "storage_limits")
            storage_limits_suite();
        else if (suite.starts_with("segment_") || suite == "storage_recover_contracts" ||
                 suite == "storage_load_contracts")
            segment_suite(suite);
#endif
        else
            throw std::runtime_error("unknown persistence suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
