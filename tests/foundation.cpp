#include <raftpp.hpp>
#include <raftpp/detail/trail.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <random>
#include <source_location>
#include <stdexcept>

#if RAFTPP_WITH_LZ4
#include <lz4frame.h>
#endif

namespace r = raftpp;

namespace {

    void check(bool condition, r::string_view message,
               std::source_location location = std::source_location::current()) {
        if (!condition) {
            throw std::runtime_error(r::string{location.file_name()} + ":" +
                                     std::to_string(location.line()) + ": " + r::string{message});
        }
    }

    r::view<const r::byte> bytes(r::string_view text) {
        return {reinterpret_cast<const r::byte*>(text.data()), text.size()};
    }

    void test_buffer() {
        r::string source{"a\0b", 3};
        auto original = r::buffer::copy(bytes(source));
        source[0] = 'x';
        check(original.size() == 3 && original.bytes()[0] == r::byte{'a'}, "copy owns its data");
        auto moved = std::move(original);
        check(moved.bytes()[1] == r::byte{0}, "move preserves binary data");
        moved.resize(10);
        check(moved.bytes()[9] == r::byte{0}, "new storage is zero initialized");
        auto replacement = r::buffer::copy(bytes("replacement"));
        moved.swap(replacement);
        check(moved.size() == 11 && replacement.size() == 10, "swap transfers ownership");
        auto empty = r::buffer::copy({});
        check(empty.empty(), "empty null view can be copied");
    }

    void test_configuration() {
        r::configuration actual;
        r::vector<r::server> expected;
        check(actual.add(0, "zero", r::role::voter) == r::errc::bad_id, "reject zero ID");
        check(actual.add(1, {"a\0b", 3}, r::role::voter) == r::errc::invalid,
              "reject unrepresentable embedded NUL address");

        std::mt19937 random{7201};
        for (unsigned step = 0; step < 600; ++step) {
            const auto id = 1 + random() % 40;
            auto found = std::ranges::find(expected, id, &r::server::id);
            if (random() % 3 == 0) {
                const auto status = actual.remove(id);
                check(status == (found == expected.end() ? r::make_error_code(r::errc::bad_id)
                                                         : r::error_code{}),
                      "remove validates membership");
                if (found != expected.end())
                    expected.erase(found);
            } else {
                const auto address = "node-" + std::to_string(random() % 40);
                const auto role = random() % 4;
                r::error_code wanted;
                if (role > 2)
                    wanted = r::errc::bad_role;
                else {
                    for (const auto& member : expected) {
                        if (member.id == id) {
                            wanted = r::errc::duplicate_id;
                            break;
                        }
                        if (member.address == address) {
                            wanted = r::errc::duplicate_address;
                            break;
                        }
                    }
                }
                check(actual.add(id, address, static_cast<r::role>(role)) == wanted,
                      "add validates role and unique members");
                if (!wanted)
                    expected.push_back({id, address, static_cast<r::role>(role)});
            }
            check(actual.size() == expected.size(), "member count matches independent sequence");
            unsigned voters = 0;
            for (r::size_t i = 0; i < expected.size(); ++i) {
                const auto& member = expected[i];
                const auto& got = actual.servers()[i];
                check(got.id == member.id && got.address == member.address &&
                          got.role == member.role && actual.index_of(member.id) == i &&
                          actual.get(member.id) == &got,
                      "member values and lookup preserve order");
                if (member.role == r::role::voter) {
                    check(actual.index_of_voter(member.id) == voters &&
                              actual.actual_index_of_voter(voters) == i,
                          "voter index maps to member");
                    ++voters;
                }
            }
            check(actual.voter_count() == voters && actual.get(99999) == nullptr &&
                      actual.index_of(99999) == actual.size(),
                  "counts and missing-member sentinel");
            if (!actual.empty()) {
                r::buffer encoded, roundtrip;
                r::configuration decoded;
                check(!actual.encode(encoded) && !decoded.decode(encoded.bytes()) &&
                          !decoded.encode(roundtrip),
                      "configuration value roundtrip");
                check(std::ranges::equal(encoded.bytes(), roundtrip.bytes()),
                      "roundtrip preserves every encoded byte");
            }
        }
        // Format 1, one voter (ID 1), address "a", with zero-filled padding.
        r::configuration wire;
        check(!wire.add(1, "a", r::role::voter), "wire fixture");
        const r::array<unsigned char, 24> expected_wire{1, 1, 0, 0, 0, 0,   0, 0, 0, 1, 0, 0,
                                                        0, 0, 0, 0, 0, 'a', 0, 1, 0, 0, 0, 0};
        r::buffer encoded_wire;
        check(!wire.encode(encoded_wire) && encoded_wire.size() == expected_wire.size(),
              "fixed wire size");
        for (r::size_t i = 0; i < expected_wire.size(); ++i)
            check(encoded_wire.bytes()[i] == r::byte{expected_wire[i]},
                  "fixed wire bytes and padding");

        r::configuration source;
        check(!source.add(1, "server-one", r::role::voter), "create malformed-input baseline");
        auto copy = source;
        check(!copy.remove(1) && source.size() == 1, "configuration copies are independent");
        r::buffer encoded;
        check(!source.encode(encoded), "encode malformed-input baseline");
        const auto logical_size = 9 + 10 + r::string_view{"server-one"}.size();
        for (r::size_t length = 0; length < logical_size; ++length) {
            check(source.decode(encoded.bytes().first(length)) == r::errc::malformed,
                  "every truncated header/address/role is rejected");
            check(source.size() == 1 && source.get(1)->address == "server-one",
                  "failed decode preserves the old configuration");
        }
        auto invalid = r::buffer::copy(encoded.bytes());
        invalid.bytes()[0] = r::byte{2};
        check(source.decode(invalid.bytes()) == r::errc::malformed, "reject unknown format");
        std::fill(invalid.bytes().begin() + 1, invalid.bytes().begin() + 9, r::byte{0xff});
        invalid.bytes()[0] = r::byte{1};
        check(source.decode(invalid.bytes()) == r::errc::malformed,
              "reject oversized count before attempting allocation");
        r::buffer short_output{1};
        short_output.bytes()[0] = r::byte{0xab};
        check(source.encode(short_output.bytes()) == r::errc::too_big &&
                  short_output.bytes()[0] == r::byte{0xab},
              "short output remains untouched");
    }

    void test_trail() {
        r::detail::trail actual;
        r::vector<r::term> expected;
        r::log_index offset = 1, snapshot_index = 3;
        r::term snapshot_term = 4;
        check(!actual.start(snapshot_index, snapshot_term, offset + 1), "start snapshot replay");
        auto compare = [&] {
            const auto last = expected.empty() ? snapshot_index : offset + expected.size();
            check(actual.size() == expected.size() && actual.last_index() == last &&
                      actual.snapshot_index() == snapshot_index &&
                      actual.snapshot_term() == snapshot_term,
                  "dense model preserves trail bounds");
            for (r::log_index i = 0; i <= last + 1; ++i) {
                const bool exists = i > offset && i - offset <= expected.size();
                const auto term = exists                ? expected[i - offset - 1]
                                  : i == snapshot_index ? snapshot_term
                                                        : 0;
                check(actual.has_entry(i) == exists && actual.term_of(i) == term,
                      "dense model preserves every term and entry presence");
            }
        };
        for (auto term : {3, 4, 4}) {
            check(!actual.append(term), "append replay");
            expected.push_back(term);
            compare();
        }
        std::mt19937 random{12199};
        for (unsigned step = 0; step < 10000; ++step) {
            const auto action = random() % 10;
            if (action < 6 || expected.empty()) {
                const auto term = std::max<r::term>(1, actual.last_term()) + random() % 2;
                check(!actual.append(term), "append term");
                expected.push_back(term);
            } else if (action < 8) {
                const auto index = offset + 1 + random() % expected.size();
                check(!actual.truncate(index), "truncate suffix");
                expected.resize(index - offset - 1);
            } else if (action == 8) {
                const auto index = offset + 1 + random() % expected.size();
                if (index != snapshot_index) {
                    const auto trailing = random() % 8;
                    const auto term = expected[index - offset - 1];
                    check(!actual.snapshot(index, trailing), "snapshot prefix");
                    snapshot_index = index;
                    snapshot_term = term;
                    if (index > trailing && index - trailing > offset) {
                        const auto cutoff = index - trailing;
                        expected.erase(expected.begin(), expected.begin() + (cutoff - offset));
                        offset = cutoff;
                    }
                }
            } else {
                const auto index = actual.last_index() + 1 + random() % 5;
                const auto term = actual.last_term() + 1;
                check(!actual.restore(index, term), "restore snapshot");
                expected.clear();
                offset = snapshot_index = index;
                snapshot_term = term;
            }
            compare();
        }
        check(actual.append(0) == r::errc::invalid, "reject zero term");
        check(actual.truncate(0) == r::errc::invalid, "reject invalid truncate");
        check(!actual.restore(std::numeric_limits<r::log_index>::max(), 1),
              "restore maximum index");
        check(actual.append(1) == r::errc::too_big, "index overflow leaves state intact");
    }

#if RAFTPP_WITH_LZ4
    r::buffer compress(r::view<const r::byte> input, bool known_size) {
        LZ4F_preferences_t preferences{};
        preferences.frameInfo.contentSize = known_size ? input.size() : 0;
        preferences.frameInfo.contentChecksumFlag = LZ4F_contentChecksumEnabled;
        r::buffer encoded{LZ4F_compressFrameBound(input.size(), &preferences)};
        const auto size = LZ4F_compressFrame(encoded.data(), encoded.size(), input.data(),
                                             input.size(), &preferences);
        check(!LZ4F_isError(size), "create test LZ4 frame");
        encoded.resize(size);
        return encoded;
    }
#endif

#if RAFTPP_WITH_UV
    void test_compression() {
        r::array<r::byte, 4> magic{r::byte{0x04}, r::byte{0x22}, r::byte{0x4d}, r::byte{0x18}};
        check(r::is_compressed(magic), "frame magic is recognized in every build");
        check(!r::is_compressed(r::view<const r::byte>{magic}.first(3)), "short magic is rejected");
        check(!r::is_compressed({}), "empty input is not a frame");
        auto output = r::buffer::copy(bytes("unchanged"));
#if RAFTPP_WITH_LZ4
        r::buffer payload{256 * 1024 + 7};
        for (r::size_t i = 0; i < payload.size(); ++i) {
            payload.bytes()[i] = static_cast<r::byte>(i % 251);
        }
        for (const bool known_size : {false, true}) {
            auto encoded = compress(payload.bytes(), known_size);
            check(r::is_compressed(encoded.bytes()), "LZ4 output has recognized magic");
            check(!r::decompress(encoded.bytes(), output, payload.size()), "decode bounded frame");
            check(std::ranges::equal(output.bytes(), payload.bytes()), "all decoded bytes match");
            output = r::buffer::copy(bytes("unchanged"));
            check(r::decompress(encoded.bytes(), output, 10) == r::errc::too_big,
                  "decoded size limit is enforced");
            check(std::ranges::equal(output.bytes(), bytes("unchanged")), "limit preserves output");
            check(bool(r::decompress(encoded.bytes().first(encoded.size() - 1), output)),
                  "truncated frame fails instead of spinning");
            encoded.bytes().back() ^= r::byte{0xff};
            check(bool(r::decompress(encoded.bytes(), output)), "bad checksum is rejected");
            check(std::ranges::equal(output.bytes(), bytes("unchanged")),
                  "corruption preserves output");
        }
        auto encoded_empty = compress({}, false);
        check(!r::decompress(encoded_empty.bytes(), output, 0) && output.empty(),
              "decode empty frame");
#else
        check(r::decompress(magic, output) == r::errc::invalid,
              "disabled LZ4 returns original status");
        check(std::ranges::equal(output.bytes(), bytes("unchanged")),
              "disabled feature preserves output");
#endif
    }

#endif

}

int main(int argc, char** argv) {
    try {
        if (argc != 2) {
            throw std::runtime_error("expected one test suite name");
        }
        const r::string_view suite{argv[1]};
        if (suite == "buffer")
            test_buffer();
        else if (suite == "configuration")
            test_configuration();
        else if (suite == "trail")
            test_trail();
#if RAFTPP_WITH_UV
        else if (suite == "compression")
            test_compression();
#endif
        else
            throw std::runtime_error("unknown test suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
