// Contract cases from cowsql/raft test/unit; see LICENSE.md.
#include <raftpp/configuration.hpp>
#include <raftpp/detail/log.hpp>
#include <raftpp/random.hpp>

#include <algorithm>
#include <iostream>
#include <source_location>

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
    extern thread_local std::ptrdiff_t remaining;
}
namespace {
    void check(bool value, r::string_view message,
               r::source_location where = r::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    struct fault {
        explicit fault(std::ptrdiff_t position) {
            allocation_failure::after(position);
        }
        ~fault() {
            allocation_failure::after(-1);
        }
    };
    r::view<const r::byte> bytes(r::string_view value) {
        return {reinterpret_cast<const r::byte*>(value.data()), value.size()};
    }
    r::configuration members() {
        r::configuration value;
        for (unsigned id = 1; id <= 4; ++id)
            check(!value.add(id, r::string(192, static_cast<char>('a' + id)),
                             id % 2 ? r::role::voter : r::role::standby),
                  "long-address member");
        return value;
    }
    r::buffer encoded(const r::configuration& value) {
        r::buffer result;
        check(!value.encode(result), "encode baseline");
        return result;
    }
    bool same(r::view<const r::byte> left, r::view<const r::byte> right) {
        return std::ranges::equal(left, right);
    }
    void unchanged(const r::configuration& value, const r::buffer& expected) {
        check(same(encoded(value).bytes(), expected.bytes()),
              "configuration preserved byte for byte");
    }

    template <typename F> void sweep(r::string_view name, F operation) {
        unsigned failures = 0;
        bool succeeded = false;
        for (std::ptrdiff_t position = 0; position < 32; ++position) {
            if (operation(position)) {
                succeeded = true;
                break;
            }
            ++failures;
        }
        check(failures != 0 && succeeded, "sweep observes failure and first success");
        std::cout << name << ": " << failures << " failing allocations, first success\n";
    }

    void configurations() {
        const auto source = members();
        const auto input = encoded(source);
        sweep("configuration.add", [&](auto position) {
            auto value = source;
            r::error_code status;
            {
                fault fail{position};
                status =
                    value.add(5, r::string_view{source.get(1)->address}.substr(1), r::role::spare);
            }
            unchanged(source, input);
            if (status) {
                check(status == r::errc::out_of_memory, "add failure status");
                unchanged(value, input);
                check(
                    !value.add(5, r::string_view{source.get(1)->address}.substr(1), r::role::spare),
                    "add retry");
            }
            check(value.size() == 5 && value.get(5)->role == r::role::spare,
                  "add retains complete new member");
            return !status;
        });
        sweep("configuration.copy", [&](auto position) {
            r::optional<r::configuration> copy;
            bool failed = false;
            {
                fault fail{position};
                try {
                    copy.emplace(source);
                } catch (const r::bad_alloc&) {
                    failed = true;
                }
            }
            unchanged(source, input);
            if (failed)
                check(!copy, "failed copy leaves optional disengaged");
            else {
                check(copy.has_value(), "copy constructed");
                unchanged(*copy, input);
            }
            return !failed;
        });
        sweep("configuration.encode", [&](auto position) {
            auto output = r::buffer::copy(bytes("preserved"));
            r::error_code status;
            {
                fault fail{position};
                status = source.encode(output);
            }
            unchanged(source, input);
            if (status) {
                check(status == r::errc::out_of_memory && same(output.bytes(), bytes("preserved")),
                      "encode preserves output");
                check(!source.encode(output), "encode retry");
            }
            check(same(output.bytes(), input.bytes()), "encode produces complete bytes");
            return !status;
        });
        sweep("configuration.decode", [&](auto position) {
            r::configuration output;
            check(!output.add(99, "preserved", r::role::spare), "decode output baseline");
            const auto before = encoded(output);
            r::error_code status;
            {
                fault fail{position};
                status = output.decode(input.bytes());
            }
            unchanged(source, input);
            if (status) {
                check(status == r::errc::out_of_memory, "decode failure status");
                unchanged(output, before);
                check(!output.decode(input.bytes()), "decode retry");
            }
            unchanged(output, input);
            return !status;
        });
        // std::vector::erase does not allocate with this value type. Preserve
        // order and the C++ no-throw API rather than emulating C's realloc OOM.
        for (const auto id : {1U, 2U, 4U, 99U}) {
            auto value = source;
            r::error_code status;
            bool no_allocation;
            {
                fault fail{0};
                status = value.remove(id);
                no_allocation = allocation_failure::remaining == 0;
            }
            check(no_allocation &&
                      status == (id == 99 ? r::make_error_code(r::errc::bad_id) : r::error_code{}),
                  "remove allocates no storage");
            if (id == 99)
                unchanged(value, input);
            else {
                check(!value.get(id) && value.size() == 3, "remove deletes one member");
                r::server_id previous = 0;
                for (const auto& member : value.servers()) {
                    check(member.id > previous, "survivor order preserved");
                    previous = member.id;
                }
            }
        }
    }

    r::entry entry(r::term term, r::string_view value) {
        return {term, r::entry_type::command, r::shared_buffer::copy(bytes(value))};
    }
    void appends() {
        const auto first = entry(1, "first"), second = entry(2, "second");
        for (const auto prefix_size : {1U, 31U})
            sweep(prefix_size == 1 ? "log.append" : "log.growing_append", [&](auto position) {
                r::detail::log log;
                for (unsigned i = 0; i < prefix_size; ++i)
                    check(!log.append(first), "log prefix");
                r::error_code status;
                {
                    fault fail{position};
                    status = log.append(second);
                }
                for (unsigned i = 1; i <= prefix_size; ++i)
                    check(log.get(i)->term == 1 &&
                              same(log.get(i)->payload.bytes(), bytes("first")),
                          "prefix payload preserved");
                if (status) {
                    check(status == r::errc::out_of_memory && log.last_index() == prefix_size &&
                              !log.get(prefix_size + 1),
                          "failed append preserves suffix");
                    check(!log.append(second), "failed append is retryable without stale busy key");
                }
                check(log.last_index() == prefix_size + 1 &&
                          same(log.get(prefix_size + 1)->payload.bytes(), bytes("second")),
                      "appended entry intact");
                return !status;
            });
        const auto configuration = members();
        const auto payload = encoded(configuration);
        sweep("log.append_configuration", [&](auto position) {
            r::detail::log log;
            check(!log.append(first), "configuration log prefix");
            r::error_code status;
            {
                fault fail{position};
                status = log.append_configuration(2, configuration);
            }
            unchanged(configuration, payload);
            if (status) {
                check(status == r::errc::out_of_memory && log.last_index() == 1,
                      "failed configuration append preserves log");
                check(!log.append_configuration(2, configuration), "configuration append retry");
            }
            check(log.get(2)->type == r::entry_type::change &&
                      same(log.get(2)->payload.bytes(), payload.bytes()),
                  "configuration entry intact");
            return !status;
        });
    }

    template <typename Output> void acquisition(r::string_view name) {
        const auto first = entry(1, "first"), second = entry(2, "second"), old = entry(9, "old");
        sweep(name, [&](auto position) {
            r::detail::log log;
            check(!log.append(first) && !log.append(second), "acquire input");
            Output output;
            output.push_back(std::make_shared<const r::entry>(old));
            const r::weak<const r::entry> previous = output.front();
            r::error_code status;
            {
                fault fail{position};
                status = log.acquire(1, output);
            }
            if (status) {
                check(status == r::errc::out_of_memory && output.size() == 1 &&
                          !previous.expired() && same(output[0]->payload.bytes(), bytes("old")),
                      "failed acquisition preserves old owner and output");
                check(!log.acquire(1, output), "acquire retry");
            }
            check(output.size() == 2 && previous.expired() &&
                      same(output[0]->payload.bytes(), bytes("first")) &&
                      same(output[1]->payload.bytes(), bytes("second")),
                  "acquire atomically replaces owners");
            bool no_allocation;
            r::error_code truncated;
            {
                fault fail{0};
                truncated = log.truncate(1);
                no_allocation = allocation_failure::remaining == 0;
            }
            check(!truncated && no_allocation && log.empty() && output[1]->term == 2,
                  "truncate needs no allocation and retains acquired values");
            check(log.append(first) == r::errc::busy, "retained header keeps same term/index busy");
            output.clear();
            check(!log.append(first), "releasing acquisition permits reuse");
            return !status;
        });
    }
    void seeded_random() {
        r::uint32_t state = 42;
        r::array<r::uint32_t, 10> values{};
        for (r::size_t i = 0; i < values.size(); ++i) {
            values[i] = r::random_within_range(state, 1000, 2000);
            check(values[i] >= 1000 && values[i] <= 2000, "original inclusive random bounds");
            check(std::find(values.begin(), values.begin() + static_cast<r::ptrdiff_t>(i),
                            values[i]) == values.begin() + static_cast<r::ptrdiff_t>(i),
                  "original seed-42 sequence has no repeated value");
        }
        check(values.front() == 1650, "original seed-42 first value");
        constexpr r::array<unsigned, 20> expected{1571, 1410, 1735, 1743, 1995, 1353, 1589,
                                                  1478, 1753, 1367, 1112, 1216, 1727, 1057,
                                                  1061, 1669, 1773, 1425, 1864, 1035};
        state = 0;
        for (const auto value : expected)
            check(r::random_within_range(state, 1000, 2000) == value,
                  "original seed-0 golden sequence");
        std::cout << "random: original seed-42 first/unique sequence and 20 seed-0 golden values\n";
    }
}
int main() {
    try {
        configurations();
        appends();
        acquisition<r::vector<r::shared<const r::entry>>>("log.acquire");
        acquisition<r::resource_vector<r::shared<const r::entry>>>("log.acquire_resource");
        seeded_random();
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
