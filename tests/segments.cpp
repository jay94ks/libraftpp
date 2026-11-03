#include "disk.hpp"
#include "byte.hpp"
#include <raftpp/checksum.hpp>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <random>
#include <source_location>
#include <unistd.h>

namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
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
            r::string pattern = "/tmp/raftpp-segments-XXXXXX";
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
        check(output.good(), "write segment fixture");
    }
    r::buffer read(const r::fs::path& path) {
        r::buffer value{static_cast<r::size_t>(r::fs::file_size(path))};
        std::ifstream input{path, std::ios::binary};
        if (!value.empty())
            input.read(reinterpret_cast<char*>(value.data()),
                       static_cast<std::streamsize>(value.size()));
        check(input.good(), "read segment");
        return value;
    }
    r::string name(r::log_index first, r::log_index last) {
        auto a = std::to_string(first), b = std::to_string(last);
        return r::string(16 - a.size(), '0') + a + "-" + r::string(16 - b.size(), '0') + b;
    }
    r::vector<r::entry> entries(r::size_t count, r::term term = 2) {
        r::vector<r::entry> result;
        for (r::size_t i = 0; i < count; ++i) {
            r::buffer payload{8 * (1 + i % 4)};
            std::ranges::fill(payload.bytes(), static_cast<r::byte>(i + term));
            result.push_back({term, i % 2 == 0 ? r::entry_type::command : r::entry_type::barrier,
                              r::shared_buffer{std::move(payload)}});
        }
        return result;
    }
    r::buffer record(r::view<const r::entry> value) {
        r::buffer output;
        check(!r::encode_record(value, output), "encode segment record");
        return output;
    }
    r::buffer segment(r::view<const r::entry> value, r::size_t padding = 0) {
        auto encoded = record(value);
        r::buffer result{8 + encoded.size() + padding};
        d::store_little_endian<r::uint64_t>(1, result.bytes().first<8>());
        std::ranges::copy(encoded.bytes(), result.bytes().begin() + 8);
        return result;
    }
    void compare(r::view<const r::entry> actual, r::view<const r::entry> expected) {
        check(actual.size() == expected.size(), "entry count matches the prescribed input");
        for (r::size_t i = 0; i < actual.size(); ++i)
            check(actual[i].term == expected[i].term && actual[i].type == expected[i].type &&
                      std::ranges::equal(actual[i].payload.bytes(), expected[i].payload.bytes()),
                  "entry metadata and payload are preserved");
    }
    r::vector<r::string> files(const r::fs::path& path) {
        r::vector<r::string> result;
        for (const auto& file : r::fs::directory_iterator{path}) {
            auto filename = file.path().filename().native();
            if (filename.starts_with("corrupt-"))
                filename = filename.substr(filename.find('-', 8) + 1);
            result.push_back(std::move(filename));
        }
        std::ranges::sort(result);
        return result;
    }
    void compare_load(directory& original, bool auto_recovery, r::error_code expected = {}) {
        d::disk_storage storage{original.path};
        check(!storage.open(), "load store open");
        const auto metadata = storage.metadata();
        r::io_state actual;
        actual.start_index = 900;
        const auto status = storage.load(actual, auto_recovery);
        check(status == expected, "full-load status follows the corruption/recovery contract");
        if (status) {
            check(actual.start_index == 900 && actual.entries.empty() && !actual.snapshot,
                  "failed load preserves output");
            return;
        }
        check(actual.term == metadata.term && actual.voted_for == metadata.voted_for &&
                  actual.start_index != 0,
              "full load preserves durable metadata and valid starting index");
        // Recovery must settle to a stable directory and repeatable owning state.
        const auto names = files(original.path);
        r::map<r::string, r::buffer> preserved;
        for (const auto& file : r::fs::directory_iterator{original.path})
            preserved.emplace(file.path().filename().native(), read(file.path()));
        r::io_state repeated;
        check(!storage.load(repeated, auto_recovery), "recovered state can be loaded repeatedly");
        check(actual.term == repeated.term && actual.voted_for == repeated.voted_for &&
                  actual.start_index == repeated.start_index &&
                  actual.snapshot.has_value() == repeated.snapshot.has_value(),
              "repeated load preserves metadata and snapshot selection");
        compare(actual.entries, repeated.entries);
        if (actual.snapshot) {
            const auto& first = *actual.snapshot;
            const auto& second = *repeated.snapshot;
            check(first.index == second.index && first.term == second.term &&
                      first.configuration_index == second.configuration_index &&
                      first.configuration.size() == second.configuration.size() &&
                      first.buffers.size() == second.buffers.size(),
                  "repeated snapshot load preserves all metadata");
            for (r::size_t i = 0; i < first.configuration.size(); ++i) {
                const auto& a = first.configuration.servers()[i];
                const auto& b = second.configuration.servers()[i];
                check(a.id == b.id && a.role == b.role && a.address == b.address,
                      "snapshot member values and ordering are stable");
            }
            for (r::size_t i = 0; i < first.buffers.size(); ++i)
                check(std::ranges::equal(first.buffers[i].bytes(), second.buffers[i].bytes()),
                      "snapshot payload is stable across loads");
        }
        check(files(original.path) == names, "settled recovery does not rename files again");
        for (const auto& [name, contents] : preserved)
            check(std::ranges::equal(read(original.path / name).bytes(), contents.bytes()),
                  "repeat load preserves every published/quarantined file byte");
    }
    void segment_files() {
        d::segment_file parsed;
        check(d::parse_segment_filename("open-18446744073709551615", parsed) && parsed.is_open &&
                  parsed.counter == r::numeric_limits<r::uint64_t>::max(),
              "counter maximum");
        for (auto invalid : {"open--1", "open-+1", "open-18446744073709551616", "open-1-extra",
                             "1-2x", "../1-2", "10000000000000000-10000000000000000", "-"}) {
            parsed.filename = "keep";
            check(!d::parse_segment_filename(invalid, parsed) && parsed.filename == "keep",
                  "bounded segment name parser");
        }
        directory dir;
        d::disk_storage storage{dir.path};
        check(!storage.open(), "segment store");
        std::mt19937_64 random{77};
        r::log_index index = 1;
        for (unsigned step = 0; step < 80; ++step) {
            auto value = entries(1 + random() % 10, 1 + step / 5);
            check(!storage.write_segment(index, value), "write independent segment");
            d::segment_file file;
            check(d::parse_segment_filename(name(index, index + value.size() - 1), file),
                  "closed filename");
            r::vector<r::entry> loaded;
            check(!storage.load_closed(file, loaded), "load closed file");
            compare(loaded, value);
            if (loaded.size() > 1)
                check(loaded.front().payload.shares_storage_with(loaded.back().payload),
                      "entries share the loaded file allocation");
            check(storage.write_segment(index, value) == r::errc::io_error &&
                      !r::fs::exists(dir.path / ("tmp-" + file.filename)),
                  "collision preserves destination and cleans temporary");
            index += value.size();
        }
        compare_load(dir, false);
        check(storage.write_segment(9999999999999999ULL, entries(2)) == r::errc::too_big &&
                  storage.write_segment(r::numeric_limits<r::uint64_t>::max(), entries(2)) ==
                      r::errc::too_big,
              "closed index and arithmetic bounds");

        directory bootstrap_cpp;
        d::disk_storage boot{bootstrap_cpp.path};
        check(!boot.open(), "bootstrap store");
        r::configuration configuration;
        check(!configuration.add(1, "one", r::role::voter), "bootstrap voter");
        check(!boot.bootstrap(configuration) &&
                  boot.bootstrap(configuration) == r::errc::cannot_bootstrap,
              "bootstrap only initializes a new store");
        check(!boot.recover(configuration), "force membership recovery");
        r::io_state booted;
        check(!boot.load(booted) && booted.entries.size() == 2 &&
                  booted.entries[0].type == r::entry_type::change &&
                  booted.entries[1].type == r::entry_type::change,
              "bootstrap and recovery publish configuration entries");
        compare_load(bootstrap_cpp, true);

        directory trunc_cpp;
        d::disk_storage truncated{trunc_cpp.path};
        check(!truncated.open() && !truncated.write_segment(1, entries(6)) &&
                  !truncated.write_segment(7, entries(3)),
              "truncate input");
        check(!truncated.truncate(4), "truncate inside closed segment");
        d::segment_file prefix;
        check(d::parse_segment_filename(name(1, 3), prefix), "truncated segment filename");
        r::vector<r::entry> retained;
        check(!truncated.load_closed(prefix, retained), "load rewritten prefix");
        const auto submitted = entries(6);
        compare(retained, r::view<const r::entry>{submitted}.first(3));
        check(files(trunc_cpp.path) == r::vector<r::string>{name(1, 3)},
              "truncation removes the later segment");
        check(!truncated.truncate(1) && files(trunc_cpp.path).empty(), "truncate at segment start");
        check(truncated.truncate(0) == r::errc::invalid &&
                  truncated.truncate(9) == r::errc::not_found,
              "truncate bounds");
        check(!truncated.write_segment(1, entries(3)) && !truncated.write_segment(4, entries(3)) &&
                  !truncated.write_segment(7, entries(3)),
              "retention input");
        r::buffer zero{64};
        write(trunc_cpp.path / "open-2", zero.bytes());
        write(trunc_cpp.path / "open-10", zero.bytes());
        r::vector<d::segment_file> listed;
        check(!truncated.list_segments(listed) && listed[3].counter == 2 && listed[4].counter == 10,
              "numeric open-counter sorting");
        check(!truncated.prune_segments(9, 4) && !r::fs::exists(trunc_cpp.path / name(1, 3)) &&
                  r::fs::exists(trunc_cpp.path / name(4, 6)),
              "retain whole boundary segment");
        check(!truncated.prune_segments(9, 0) &&
                  files(trunc_cpp.path) == r::vector<r::string>{"open-10", "open-2"},
              "zero trailing removes closed files and retains open files");
    }
    void segment_recovery() {
        auto value = entries(2);
        // A valid batch followed by block padding, another batch, or a damaged tail.
        for (unsigned mode = 0; mode < 9; ++mode) {
            directory dir;
            auto data = segment(value, mode == 0 ? 64 : 0);
            if (mode == 1 || mode == 2 || mode == 3) {
                auto more = record(entries(3, 3));
                const auto before = data.size();
                data.resize(before + more.size());
                std::ranges::copy(more.bytes(),
                                  data.bytes().begin() + static_cast<r::ptrdiff_t>(before));
                if (mode == 2)
                    data.bytes()[before] ^= r::byte{1};
                if (mode == 3)
                    data.bytes()[data.size() - 1] ^= r::byte{1};
            } else if (mode == 4)
                data.resize(data.size() - 1);
            else if (mode == 5)
                data.resize(7);
            else if (mode == 6) {
                data = r::buffer{256};
            } else if (mode == 7)
                data.clear();
            else if (mode == 8) {
                data.bytes()[32] = r::byte{99}; // Unknown entry type with a valid header CRC.
                d::store_little_endian<r::uint32_t>(
                    r::crc32(data.bytes().subspan(16, 8 + 16 * value.size())),
                    data.bytes().subspan<8, 4>());
            }
            write(dir.path / "open-1", data.bytes());
            compare_load(dir, false,
                         mode == 4 || mode == 5 ? r::make_error_code(r::errc::io_error)
                         : mode == 8            ? r::make_error_code(r::errc::malformed)
                                                : r::error_code{});
            if (mode <= 3)
                compare_load(dir, false); // Closed repaired files must load again.
        }
        for (bool auto_recovery : {false, true}) {
            for (unsigned damaged = 0; damaged < 3; ++damaged) {
                directory dir;
                d::disk_storage storage{dir.path};
                check(!storage.open(), "quarantine store");
                check(!storage.write_segment(1, entries(2)) &&
                          !storage.write_segment(3, entries(2)),
                      "quarantine prefix");
                if (damaged == 0) {
                    r::buffer bad{8};
                    d::store_little_endian<r::uint64_t>(99, bad.bytes().first<8>());
                    write(dir.path / "open-4", bad.bytes());
                    write(dir.path / "open-5", segment(entries(2)).bytes());
                } else {
                    const auto filename = name(damaged == 1 ? 3 : 1, damaged == 1 ? 4 : 2);
                    auto bad = read(dir.path / filename);
                    bad.bytes()[0] = r::byte{99};
                    write(dir.path / filename, bad.bytes());
                    write(dir.path / "open-5", segment(entries(2)).bytes());
                }
                compare_load(dir, auto_recovery,
                             auto_recovery && damaged != 2 ? r::error_code{}
                                                           : r::make_error_code(r::errc::corrupt));
            }
        }
        directory multiple;
        d::disk_storage ordered{multiple.path};
        check(!ordered.open() && !ordered.write_segment(1, value), "multiple open files prefix");
        write(multiple.path / "open-10", segment(entries(2, 4), 64).bytes());
        write(multiple.path / "open-2", segment(entries(3, 3), 64).bytes());
        write(multiple.path / "open-3", r::buffer{128}.bytes());
        compare_load(multiple, false);
        compare_load(multiple, false);
        directory mismatch;
        write(mismatch.path / name(1, 3), segment(value).bytes());
        compare_load(mismatch, false, r::errc::corrupt);
        directory excessive;
        auto invalid_count = segment(value);
        d::store_little_endian<r::uint64_t>(262145, invalid_count.bytes().subspan<16, 8>());
        write(excessive.path / "open-1", invalid_count.bytes());
        compare_load(excessive,
                     false); // Invalid count is a corrupt open tail with no valid prefix.
        directory gap;
        d::disk_storage storage{gap.path};
        check(!storage.open() && !storage.write_segment(1, entries(2)) &&
                  !storage.write_segment(4, entries(2)),
              "gap fixture");
        compare_load(gap, true, r::errc::corrupt);
        directory malformed;
        write(malformed.path / "0000000000000002-0000000000000001", segment(value).bytes());
        d::disk_storage invalid{malformed.path};
        check(!invalid.open(), "invalid range open");
        r::io_state unchanged;
        unchanged.term = 99;
        check(invalid.load(unchanged) == r::errc::corrupt && unchanged.term == 99,
              "invalid range safely rejected");
        directory overlap;
        d::disk_storage overlapping{overlap.path};
        check(!overlapping.open() && !overlapping.write_segment(1, entries(3)) &&
                  !overlapping.write_segment(2, entries(3)),
              "overlap fixture");
        check(overlapping.load(unchanged) == r::errc::corrupt,
              "overlapping ranges do not violate sort ordering");
        directory duplicate;
        write(duplicate.path / "open-1", segment(value).bytes());
        write(duplicate.path / "open-01", segment(value).bytes());
        d::disk_storage ambiguous{duplicate.path};
        check(!ambiguous.open() && ambiguous.load(unchanged) == r::errc::corrupt,
              "duplicate counters safely rejected");
    }
    void snapshot(d::disk_storage& storage, r::log_index index, r::term term,
                  r::uint64_t timestamp) {
        r::snapshot value;
        value.term = term;
        value.index = index;
        value.configuration_index = 1;
        check(!value.configuration.add(1, "one", r::role::voter), "snapshot configuration");
        value.buffers = {entries(1)[0].payload};
        d::prepared_snapshot prepared;
        check(!storage.prepare_snapshot(value, timestamp, prepared) &&
                  !storage.commit_snapshot(prepared),
              "snapshot publication");
    }
    void segment_snapshot() {
        for (unsigned mode = 0; mode < 7; ++mode) {
            directory dir;
            d::disk_storage storage{dir.path};
            check(!storage.open() && !storage.set_term(4) && !storage.set_vote(2),
                  "snapshot store");
            snapshot(storage, 5, 2, 10);
            if (mode == 0) {
                snapshot(storage, 8, 3, 11);
                auto invalid = read(dir.path / "snapshot-3-8-11.meta");
                invalid.bytes()[8] ^= r::byte{1};
                write(dir.path / "snapshot-3-8-11.meta", invalid.bytes());
                check(!storage.write_segment(4, entries(4)), "snapshot fallback suffix");
            } else if (mode == 1) {
                check(!storage.write_segment(1, entries(2)) &&
                          !storage.write_segment(4, entries(3)),
                      "newest contiguous chunk");
            } else if (mode == 2)
                check(!storage.write_segment(1, entries(2)),
                      "closed files entirely behind snapshot");
            else if (mode == 3 || mode == 4) {
                check(!storage.write_segment(1, entries(2)), "closed before snapshot plus open");
                write(dir.path / "open-1", segment(entries(mode == 3 ? 4 : 1), 64).bytes());
            } else if (mode == 5)
                check(!storage.write_segment(7, entries(2)), "snapshot gap");
            // mode 6 is a snapshot-only restart.
            compare_load(dir, false,
                         mode == 4 || mode == 5 ? r::make_error_code(r::errc::corrupt)
                                                : r::error_code{});
        }
    }
    // Recovery contracts from cowsql/raft test/integration/test_uv_load.c;
    // see LICENSE.md. Keep independent copies for the actual C loader.
    void storage_load_contracts() {
        unsigned cases = 0;
        auto verify = [&](directory& dir, bool automatic, r::error_code expected,
                          r::string_view label) {
            try {
                compare_load(dir, automatic, expected);
            } catch (const std::exception& error) {
                throw std::runtime_error(r::string{label} + ", automatic=" +
                                         std::to_string(automatic) + ": " + error.what());
            }
            ++cases;
        };
        for (const auto length : {4U, 16U, 32U, 44U})
            for (const bool automatic : {false, true}) {
                directory dir;
                auto raw = segment(entries(1));
                raw.resize(length);
                write(dir.path / "open-1", raw.bytes());
                verify(dir, automatic, r::errc::io_error,
                       "incomplete first batch at byte " + std::to_string(length));
            }
        for (unsigned damage = 0; damage < 4; ++damage)
            for (const bool automatic : {false, true}) {
                directory dir;
                auto raw = segment(entries(1));
                if (damage < 2)
                    raw.bytes()[8 + damage * 4] ^= r::byte{1};
                else if (damage == 2)
                    raw.clear();
                else
                    d::store_little_endian<r::uint64_t>(2, raw.bytes().first<8>());
                write(dir.path / name(1, 1), raw.bytes());
                verify(dir, automatic,
                       automatic ? r::error_code{} : r::make_error_code(r::errc::corrupt),
                       "closed header/data CRC, empty, or bad format " + std::to_string(damage));
            }
        for (unsigned damage = 0; damage < 3; ++damage)
            for (const bool automatic : {false, true}) {
                directory dir;
                d::disk_storage storage{dir.path};
                check(!storage.open(), "snapshot suffix store");
                snapshot(storage, 4, 2, 10);
                check(!storage.write_segment(5, entries(1)) &&
                          !storage.write_segment(6, entries(2)),
                      "snapshot suffix prefix");
                if (damage == 2) {
                    check(!storage.write_segment(8, entries(1)),
                          "last closed segment before open suffix");
                    write(dir.path / "open-9", segment(entries(1), 128).bytes());
                } else
                    check(!storage.write_segment(8, entries(2)), "last closed segment");
                const auto filename = damage == 1 ? name(6, 7) : name(8, damage == 2 ? 8 : 9);
                auto raw = read(dir.path / filename);
                raw.bytes()[12] ^= r::byte{1};
                write(dir.path / filename, raw.bytes());
                verify(dir, automatic,
                       automatic && damage != 1 ? r::error_code{}
                                                : r::make_error_code(r::errc::corrupt),
                       "snapshot corrupt last, middle, or last-before-open " +
                           std::to_string(damage));
            }
        for (unsigned damaged = 0; damaged < 2; ++damaged)
            for (const bool automatic : {false, true}) {
                directory dir;
                d::disk_storage storage{dir.path};
                check(!storage.open(), "two-open snapshot store");
                snapshot(storage, 4, 2, 10);
                for (unsigned i = 0; i < 2; ++i) {
                    auto raw = segment(entries(2), 128);
                    if (i == damaged)
                        d::store_little_endian<r::uint64_t>(2, raw.bytes().first<8>());
                    write(dir.path / ("open-" + std::to_string(i + 1)), raw.bytes());
                }
                verify(dir, automatic,
                       automatic ? r::error_code{} : r::make_error_code(r::errc::corrupt),
                       "snapshot and damaged open suffix " + std::to_string(damaged));
            }
        for (const unsigned version : {0U, 2U})
            for (const bool automatic : {false, true}) {
                directory dir;
                auto raw = segment(entries(1));
                d::store_little_endian<r::uint64_t>(version, raw.bytes().first<8>());
                write(dir.path / "open-1", raw.bytes());
                verify(dir, automatic,
                       automatic ? r::error_code{} : r::make_error_code(r::errc::corrupt),
                       "nonempty open segment with format " + std::to_string(version));
            }
        std::cout << "storage load contracts: " << cases << " load cases\n";
    }

    void storage_recover_contracts() {
        for (unsigned mode = 0; mode < 9; ++mode)
            for (bool automatic : {false, true})
                for (unsigned members : {1U, 5U}) {
                    const auto context = "recover mode " + std::to_string(mode) + ", auto " +
                                         std::to_string(automatic) + ", members " +
                                         std::to_string(members);
                    directory original;
                    d::disk_storage storage{original.path};
                    check(!storage.open() && !storage.set_term(9) && !storage.set_vote(7),
                          context + ": metadata");
                    if (mode == 0) {
                        r::configuration initial;
                        check(!initial.add(1, "old", r::role::voter), "old membership");
                        r::buffer encoded;
                        check(!initial.encode(encoded), "encode old membership");
                        const r::array<r::entry, 1> first{r::entry{
                            1, r::entry_type::change, r::shared_buffer{std::move(encoded)}}};
                        check(!storage.write_segment(1, first) &&
                                  !storage.write_segment(2, entries(3, 7)),
                              "recover without snapshot");
                    } else if (mode <= 4) {
                        snapshot(storage, 5, 4, 10);
                        if (mode == 2 || mode == 4)
                            check(!storage.write_segment(1, entries(2, 4)), "retained old segment");
                        if (mode == 3) {
                            auto suffix = entries(4, 4);
                            suffix[2].term = 6;
                            suffix[3].term = 6;
                            check(!storage.write_segment(4, suffix), "snapshot overlaps suffix");
                        }
                        if (mode == 4)
                            write(original.path / "open-12", segment(entries(4, 4), 128).bytes());
                    } else {
                        check(!storage.write_segment(1, entries(mode == 7 ? 2 : 3, 4)),
                              "valid prefix before tail");
                        if (mode == 5) {
                            auto valid = segment(entries(2, 6));
                            auto bad = record(entries(2, 8));
                            bad.bytes().back() ^= r::byte{1};
                            const auto used = valid.size();
                            valid.resize(used + bad.size());
                            std::ranges::copy(bad.bytes(), valid.bytes().begin() +
                                                               static_cast<r::ptrdiff_t>(used));
                            write(original.path / "open-12", valid.bytes());
                        } else if (mode == 6) {
                            check(!storage.write_segment(4, entries(2, 6)), "last closed segment");
                            auto bad = read(original.path / name(4, 5));
                            bad.bytes()[0] = r::byte{99};
                            write(original.path / name(4, 5), bad.bytes());
                        } else if (mode == 7)
                            check(!storage.write_segment(4, entries(2, 6)), "missing segment gap");
                        else {
                            r::buffer bad{8};
                            d::store_little_endian<r::uint64_t>(99, bad.bytes().first<8>());
                            write(original.path / "open-12", bad.bytes());
                            write(original.path / "open-13", segment(entries(2, 6)).bytes());
                        }
                    }
                    r::configuration replacement;
                    const r::array roles{r::role::voter, r::role::voter, r::role::voter,
                                         r::role::standby, r::role::spare};
                    for (unsigned i = 0; i < members; ++i) {
                        const auto address = "replacement-" + std::to_string(i + 1);
                        check(!replacement.add(i + 1, address, roles[i]),
                              "replacement memberships");
                    }

                    const auto expected = mode == 7 || (!automatic && (mode == 6 || mode == 8))
                                              ? r::make_error_code(r::errc::corrupt)
                                              : r::error_code{};
                    for (unsigned pass = 0; pass < (expected ? 1U : 2U); ++pass) {
                        const auto before = storage.metadata();
                        const auto actual = storage.recover(replacement, 4096, automatic);
                        check(actual == expected, context + ": prescribed recovery status");
                        check(storage.metadata().version == before.version &&
                                  storage.metadata().term == 9 && storage.metadata().voted_for == 7,
                              context +
                                  ": forced membership recovery preserves term/vote metadata");
                        if (actual)
                            continue;
                        r::io_state loaded;
                        check(!storage.load(loaded, automatic), "load recovered C++ state");
                        check(loaded.term == 9 && loaded.voted_for == 7,
                              context + ": recovery preserves metadata");
                        check(!loaded.entries.empty() &&
                                  loaded.entries.back().type == r::entry_type::change,
                              "recovery appends configuration entry");
                        r::configuration decoded;
                        check(!decoded.decode(loaded.entries.back().payload.bytes()) &&
                                  decoded.size() == members,
                              "decode recovered membership");
                        for (unsigned i = 0; i < members; ++i) {
                            const auto member = decoded.get(i + 1);
                            check(member && member->role == roles[i] &&
                                      member->address == "replacement-" + std::to_string(i + 1),
                                  "recovered IDs/addresses/roles");
                        }
                        const auto last_term = mode == 0 ? 7U : mode == 3 || mode == 5 ? 6U : 4U;
                        check(
                            loaded.entries.back().term == last_term,
                            context +
                                ": appended entry uses last stored term rather than current metadata term");
                        constexpr r::array<r::log_index, 9> next_index{5, 6, 6, 8, 7, 6, 4, 0, 4};
                        check(
                            loaded.start_index + loaded.entries.size() - 1 ==
                                next_index[mode] + pass,
                            context +
                                ": appended configuration follows the recovered log/snapshot boundary");
                    }
                }
    }
    void segment_failures() {
        const auto value = entries(3);
        for (auto operation :
             {d::file_operation::open, d::file_operation::write, d::file_operation::sync_file,
              d::file_operation::close, d::file_operation::publish_segment,
              d::file_operation::sync_directory}) {
            directory dir;
            bool enabled = false;
            d::disk_storage storage{dir.path, [&](auto op, auto) {
                                        return enabled && op == operation
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open(), "file failure store");
            const auto count = descriptors();
            enabled = true;
            check(storage.write_segment(1, value) == r::errc::io_error, "segment write injection");
            enabled = false;
            check(files(dir.path).empty() && descriptors() == count,
                  "failed creation releases descriptor and temporary file");
            check(!storage.write_segment(1, value), "creation retry");
        }
        for (auto operation :
             {d::file_operation::scan, d::file_operation::read, d::file_operation::truncate,
              d::file_operation::sync_file, d::file_operation::close, d::file_operation::rename,
              d::file_operation::sync_directory}) {
            directory dir;
            write(dir.path / "open-1", segment(value, 64).bytes());
            bool enabled = false;
            d::disk_storage storage{dir.path, [&](auto op, auto) {
                                        return enabled && op == operation
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open(), "recovery failure store");
            const auto count = descriptors();
            enabled = true;
            r::io_state state;
            state.term = 99;
            check(storage.load(state) == r::errc::io_error && state.term == 99 &&
                      state.entries.empty(),
                  "recovery failure preserves output");
            enabled = false;
            check(descriptors() == count && !storage.load(state) &&
                      state.entries.size() == value.size(),
                  "recovery retry and descriptor cleanup");
        }
        {
            directory dir;
            bool enabled = false;
            r::vector<d::file_operation> operations;
            d::disk_storage storage{dir.path, [&](auto op, auto) {
                                        operations.push_back(op);
                                        return enabled && op == d::file_operation::remove
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open() && !storage.write_segment(1, value),
                  "truncate deletion failure input");
            operations.clear();
            enabled = true;
            check(storage.truncate(3) == r::errc::io_error, "truncate deletion failure reported");
            enabled = false;
            const auto published =
                std::ranges::find(operations, d::file_operation::publish_segment);
            const auto synchronized =
                std::ranges::find(operations, d::file_operation::sync_directory);
            const auto removed = std::ranges::find(operations, d::file_operation::remove);
            check(published < synchronized && synchronized < removed &&
                      r::fs::exists(dir.path / name(1, 2)) && r::fs::exists(dir.path / name(1, 3)),
                  "replacement persisted before deleting original; failed deletion retains both");
            r::io_state state;
            check(storage.load(state, false) == r::errc::corrupt,
                  "incomplete truncation is not silently treated as successful recovery");
        }
        {
            directory dir;
            write(dir.path / "open-1", r::buffer{64}.bytes());
            bool enabled = true;
            d::disk_storage storage{dir.path, [&](auto op, auto) {
                                        return enabled && op == d::file_operation::remove
                                                   ? r::make_error_code(r::errc::io_error)
                                                   : r::error_code{};
                                    }};
            check(!storage.open(), "empty-open removal failure store");
            r::io_state state;
            state.term = 99;
            check(storage.load(state) == r::errc::io_error && state.term == 99,
                  "empty-open removal failure preserves output");
            enabled = false;
            check(!storage.load(state) && state.entries.empty() && files(dir.path).empty(),
                  "empty-open removal retry");
        }
        unsigned failed = 0, succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 100; ++position) {
            directory dir;
            write(dir.path / "open-1", segment(value, 64).bytes());
            d::disk_storage storage{dir.path};
            check(!storage.open(), "allocation failure store");
            const auto count = descriptors();
            r::io_state result;
            result.term = 99;
            allocation_failure::after(position);
            const auto status = storage.load(result);
            allocation_failure::after(-1);
            if (status) {
                ++failed;
                check(status == r::errc::out_of_memory && result.term == 99 &&
                          result.entries.empty(),
                      "allocation failure preserves load output");
            } else {
                ++succeeded;
                check(result.entries.size() == 3, "allocation sweep load");
            }
            check(descriptors() == count && !storage.load(result) && result.entries.size() == 3,
                  "allocation recovery retry");
        }
        check(failed && succeeded, "load allocation sweep reaches success and failure");
        failed = 0;
        succeeded = 0;
        for (std::ptrdiff_t position = 0; position < 70; ++position) {
            directory dir;
            d::disk_storage storage{dir.path};
            check(!storage.open(), "creation allocation store");
            const auto count = descriptors();
            allocation_failure::after(position);
            const auto status = storage.write_segment(1, value);
            allocation_failure::after(-1);
            if (status) {
                ++failed;
                check(status == r::errc::out_of_memory && files(dir.path).empty(),
                      "creation allocation failure cleanup");
            } else
                ++succeeded;
            check(descriptors() == count, "creation allocation failure releases descriptors");
        }
        check(failed && succeeded, "creation allocation sweep");
    }
}
void segment_suite(r::string_view suite) {
    if (suite == "segment_files")
        segment_files();
    else if (suite == "segment_recovery")
        segment_recovery();
    else if (suite == "segment_snapshot")
        segment_snapshot();
    else if (suite == "storage_recover_contracts")
        storage_recover_contracts();
    else if (suite == "storage_load_contracts")
        storage_load_contracts();
    else if (suite == "segment_failures")
        segment_failures();
    else
        throw std::runtime_error("unknown segment suite");
}
