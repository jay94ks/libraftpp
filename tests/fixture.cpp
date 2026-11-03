#include <raftpp.hpp>
#include <algorithm>
#include <cstring>
#include <iostream>
#include <source_location>
#include "memory_resource.hpp"

namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace r = raftpp;
namespace {
    void check(bool condition, r::string_view text,
               std::source_location where = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{text});
    }
    struct machine final : r::state_machine {
        r::uint64_t count = 0;
        unsigned restores = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>& result) override {
            ++count;
            result.reset();
            return {};
        }
        r::error_code restore(r::shared_buffer data) override {
            if (data.size() != sizeof(count))
                return r::errc::malformed;
            std::memcpy(&count, data.bytes().data(), sizeof(count));
            ++restores;
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>& values) override {
            r::buffer bytes{sizeof(count)};
            std::memcpy(bytes.data(), &count, sizeof(count));
            values.emplace_back(std::move(bytes));
            return {};
        }
    };
    r::vector<r::shared<machine>> initialize(r::fixture& fixture, unsigned count = 3,
                                             bool snapshots = false) {
        r::vector<r::shared<machine>> machines;
        for (unsigned i = 0; i < count; ++i) {
            auto value = std::make_shared<machine>();
            r::node_options options;
            if (snapshots) {
                options.snapshot_threshold = 4;
                options.snapshot_trailing = 1;
            }
            check(!fixture.grow(value, options), "grow fixture");
            machines.push_back(value);
        }
        r::configuration configuration;
        check(!fixture.configuration(count, configuration) && !fixture.bootstrap(configuration) &&
                  !fixture.start(),
              "start fixture");
        return machines;
    }
    void validate(const r::fixture& fixture) {
        check(!fixture.error(), "fixture safety invariants");
        for (r::size_t i = 0; i < fixture.size(); ++i) {
            const auto& node = fixture.get(i);
            check(node.last_applied() <= node.commit_index() &&
                      node.commit_index() <= node.last_index(),
                  "applied/commit/log ordering");
            const auto members = node.configuration();
            for (r::size_t j = 0; j < members.size(); ++j)
                check(members.servers()[j].id && members.index_of(members.servers()[j].id) == j,
                      "unique membership and stable index lookup");
        }
    }
    void contracts() {
        r::fixture fixture;
        auto machines = initialize(fixture);
        auto step = [&] {
            const auto before = fixture.time();
            const auto event = fixture.step();
            check(event && event->server_index < fixture.size() && event->time >= before,
                  "event identity and nondecreasing virtual time");
            validate(fixture);
        };
        for (unsigned i = 0; i < 300; ++i)
            step();
        check(fixture.leader_index() == 0 && fixture.time() >= 1000, "default first election");
        check(!fixture.get(0).apply(r::buffer{8}), "submit command");
        for (unsigned i = 0; i < 80; ++i)
            step();
        for (unsigned i = 0; i < 3; ++i)
            check(machines[i]->count == 1, "each FSM applies exactly one command");
    }
    void membership_contracts() {
        for (const unsigned initial : {1U, 3U}) {
            r::fixture fixture;
            auto machines = initialize(fixture, initial);
            unsigned steps = 0, completions = 0;
            r::error_code completion_status;
            r::string phase = "initial cluster";
            auto compare = [&](unsigned count) {
                for (unsigned k = 0; k < count; ++k) {
                    const auto before = fixture.time();
                    const auto event = fixture.step();
                    ++steps;
                    check(event && event->server_index < fixture.size() && event->time >= before,
                          phase + ": valid ordered event");
                    validate(fixture);
                }
            };
            auto change = [&](unsigned operation, unsigned id, r::role role = r::role::spare) {
                const auto leader = fixture.leader_index();
                check(leader < fixture.size(), "leader before membership request");
                auto callback = [&](auto status) {
                    ++completions;
                    completion_status = status;
                };
                r::error_code actual;
                if (operation == 0)
                    actual = fixture.get(leader).add(id, std::to_string(id), callback);
                else if (operation == 1)
                    actual = fixture.get(leader).assign(id, role, callback);
                else
                    actual = fixture.get(leader).remove(id, callback);
                return actual;
            };
            auto grow = [&] {
                auto machine = std::make_shared<struct machine>();
                check(!fixture.grow(machine), "C++ grow during running cluster");
                machines.push_back(machine);
                check(!fixture.get(fixture.size() - 1).start(), "start only the newly grown node");
            };
            auto apply = [&] {
                const auto leader = fixture.leader_index();
                check(leader < fixture.size(), "leader before command");
                check(!fixture.get(leader).apply(r::buffer{8}),
                      "identical command after membership change");
            };
            compare(300);
            phase = "grow and add first spare";
            grow();
            check(!change(0, initial + 1), "add first spare");
            compare(160);
            phase = "promote uncaught spare";
            check(!change(1, initial + 1, r::role::voter), "promote spare");
            compare(320);
            apply();
            compare(100);
            phase = "grow and add second spare";
            grow();
            check(!change(0, initial + 2), "add second spare");
            compare(160);
            phase = "assign standby";
            check(!change(1, initial + 2, r::role::standby), "standby assignment");
            compare(160);
            apply();
            compare(100);
            phase = "promote standby";
            check(!change(1, initial + 2, r::role::voter), "standby promotion");
            compare(160);
            phase = "demote voter";
            check(!change(1, initial + 1, r::role::standby), "voter demotion");
            compare(160);
            phase = "remove standby";
            check(!change(2, initial + 1), "standby removal");
            compare(160);
            phase = "reject duplicate and absent members";
            check(change(0, initial + 2) == r::errc::duplicate_id, "duplicate addition status");
            check(change(2, 99) == r::errc::bad_id, "missing removal status");
            compare(20);
            apply();
            compare(160);
            check(completions == 7 && !completion_status && !fixture.error(),
                  "seven membership completions and healthy fixture");
            std::cout << "initial=" << initial << " members=" << fixture.size()
                      << " events=" << steps << " membership_completions=" << completions << '\n';
        }
    }
    void network_contracts() {
        for (const unsigned count : {3U, 5U}) {
            r::fixture fixture;
            auto machines = initialize(fixture, count);
            unsigned step = 0;
            r::string phase = "initial election";
            auto compare = [&](unsigned count) {
                for (unsigned k = 0; k < count; ++k) {
                    const auto before = fixture.time();
                    const auto event = fixture.step();
                    ++step;
                    check(event && event->server_index < fixture.size() && event->time >= before,
                          phase + ": valid ordered event");
                    validate(fixture);
                }
            };
            auto control = [&](unsigned operation, unsigned index, unsigned peer = 0,
                               unsigned value = 0) {
                r::error_code status;
                switch (operation) {
                case 0:
                    status = fixture.disconnect(index, peer);
                    break;
                case 1:
                    status = fixture.reconnect(index, peer);
                    break;
                case 2:
                    status = fixture.saturate(index, peer);
                    break;
                case 3:
                    status = fixture.desaturate(index, peer);
                    break;
                case 4:
                    status = fixture.kill(index);
                    break;
                case 5:
                    status = fixture.revive(index);
                    break;
                case 6:
                    status = fixture.set_network_latency(index, value);
                    break;
                case 7:
                    status = fixture.set_disk_latency(index, value);
                    break;
                case 8:
                    status =
                        fixture.io_fault(index, static_cast<int>(peer), static_cast<int>(value));
                    break;
                default:
                    check(false, "unknown fixture control");
                }
                check(!status, "apply identical fixture control");
            };
            auto apply = [&] {
                const auto leader = fixture.leader_index();
                check(leader < count, "stable leader before controlled apply");
                check(!fixture.get(leader).apply(r::buffer{8}),
                      "submit identical controlled command");
            };
            compare(400);
            apply();
            compare(100);
            phase = "changed disk/network latencies";
            for (unsigned i = 0; i < count; ++i) {
                control(6, i, 0, 5 + i * 7);
                control(7, i, 0, 3 + i * 11);
            }
            apply();
            compare(200);
            phase = "directional disconnect";
            control(0, 0, 1);
            compare(100);
            phase = "directional reconnect";
            control(1, 0, 1);
            compare(100);
            phase = "directional saturation";
            control(2, 0, 1);
            compare(100);
            phase = "directional desaturation";
            control(3, 0, 1);
            compare(100);
            phase = "dead follower";
            control(4, count - 1);
            apply();
            compare(200);
            phase = "revived follower";
            control(5, count - 1);
            compare(200);
            phase = "dead leader";
            const auto leader = static_cast<unsigned>(fixture.leader_index());
            control(4, leader);
            compare(500);
            phase = "revived old leader";
            control(5, leader);
            compare(300);
            check(!fixture.error(), "controlled fixture safety checks remain valid");
        }
    }
    void disk_contracts() {
        r::fixture fixture;
        check(!fixture.grow(std::make_shared<machine>()), "raw memory-backend server");
        auto backend = fixture.backend(0);
        check(!backend->start(100, [] {}, [](auto) {}), "raw memory-backend timer");
        unsigned completed = 0;
        r::error_code completion;
        auto compare_disk = [&] {
            r::io_state loaded;
            check(!backend->load(loaded), "load memory backend");
            check(!loaded.snapshot && loaded.start_index == 1,
                  "raw log starts at one without a snapshot");
            for (const auto& entry : loaded.entries)
                check(entry.type == r::entry_type::command && entry.payload.size() == 8 &&
                          std::ranges::all_of(
                              entry.payload.bytes(),
                              [&](auto byte) { return byte == static_cast<r::byte>(entry.term); }),
                      "persisted payload matches submitted term fixture");
        };
        auto fault = [&](int delay, int repeat) {
            check(!fixture.io_fault(0, delay, repeat), "inject identical I/O fault counters");
        };
        auto synchronous = [&](unsigned operation, uint64_t value, r::error_code expected = {}) {
            const auto actual = operation == 0   ? backend->set_term(value)
                                : operation == 1 ? backend->set_vote(value)
                                                 : backend->truncate(value);
            check(actual == expected,
                  "synchronous memory I/O/fault result matches expected values");
        };
        auto append = [&](r::term term, r::log_index index, r::error_code submission = {}) {
            const auto count = completed;
            r::buffer bytes{8};
            std::ranges::fill(bytes.bytes(), static_cast<r::byte>(term));
            auto entries = std::make_shared<r::entry_batch>();
            entries->push_back(std::make_shared<r::entry>(
                r::entry{term, r::entry_type::command, r::shared_buffer{std::move(bytes)}}));
            const auto actual = backend->append(index, entries, [&](auto status) {
                ++completed;
                completion = status;
            });
            check(actual == submission, "append submission matches expected values");
            check(completed == count, "append submission never completes inline");
        };
        auto complete = [&](r::error_code expected = {}) {
            const auto previous = completed;
            for (unsigned i = 0; completed == previous; ++i)
                check(i < 20 && fixture.step().has_value(),
                      "raw append completes within finite events");
            check(completed == previous + 1 && completion == expected,
                  "append completes once with expected status");
            compare_disk();
        };
        compare_disk();
        synchronous(0, 4);
        synchronous(1, 2);
        compare_disk();
        fault(0, 2);
        synchronous(1, 3, r::errc::io_error);
        synchronous(1, 3, r::errc::io_error);
        synchronous(1, 3);
        compare_disk();
        fault(0, 1);
        append(4, 1, r::errc::io_error);
        compare_disk();
        fault(1, 1);
        append(4, 1);
        complete(r::errc::io_error);
        append(4, 1);
        complete();
        append(5, 2);
        complete();
        fault(0, 1);
        synchronous(2, 10);
        synchronous(1, 4, r::errc::io_error);
        compare_disk();
        fault(0, 1);
        synchronous(2, 2, r::errc::io_error);
        compare_disk();
        synchronous(2, 2);
        compare_disk();
        append(6, 2);
        complete();
        synchronous(0, 9);
        compare_disk();
        synchronous(2, 1);
        compare_disk();
        append(9, 1);
        complete();
        check(!fixture.error(), "direct I/O comparison preserves fixture safety");
    }
    void snapshot_load_contracts() {
        unsigned cases = 0, comparisons = 0;
        auto payload = [](r::uint64_t value) {
            r::buffer bytes{sizeof value};
            std::memcpy(bytes.data(), &value, sizeof value);
            return r::shared_buffer{std::move(bytes)};
        };
        auto compare = [&](r::fixture& fixture, unsigned count, r::log_index index,
                           bool queued = false) {
            r::io_state loaded;
            check(!fixture.backend(0)->load(loaded), "load snapshot and suffix");
            check(loaded.term == (queued ? 0U : 7U) && loaded.voted_for == 0 &&
                      loaded.start_index == index + 1 && loaded.entries.size() == count,
                  "snapshot load retains all injected suffix entries");
            check(loaded.snapshot && loaded.snapshot->index == index &&
                      loaded.snapshot->term == (queued ? 4U : 2U) &&
                      loaded.snapshot->configuration_index == 1 &&
                      loaded.snapshot->configuration.size() == 1 &&
                      loaded.snapshot->buffers.size() == 1,
                  "snapshot metadata has prescribed values");
            const auto member = loaded.snapshot->configuration.get(1);
            check(member && member->role == r::role::voter && member->address == "1",
                  "snapshot owns its voter configuration");
            const auto bytes = loaded.snapshot->buffers[0].bytes();
            check(bytes.size() == 8, "snapshot buffer length");
            if (queued)
                check(std::ranges::all_of(bytes, [](auto value) { return value == r::byte{42}; }),
                      "queued snapshot contents");
            else {
                r::uint64_t state = 0;
                std::memcpy(&state, bytes.data(), 8);
                check(state == 17, "injected snapshot contents");
            }
            for (r::size_t i = 0; i < loaded.entries.size(); ++i) {
                const auto& entry = loaded.entries[i];
                r::uint64_t value = 0;
                check(entry.term == 3 + i && entry.type == r::entry_type::command &&
                          entry.payload.size() == 8,
                      "injected suffix term/type/length");
                std::memcpy(&value, entry.payload.bytes().data(), 8);
                check(value == 61 + i, "injected suffix contents");
            }
            ++comparisons;
        };
        for (const r::log_index index : {2, 5, 10, 64}) {
            for (const unsigned count : {0, 1, 3})
                for (unsigned order = 0; order < 3; ++order) {
                    r::fixture fixture;
                    auto fsm = std::make_shared<machine>();
                    check(!fixture.grow(fsm), "grow injected fixture");
                    r::configuration configuration;
                    check(!fixture.configuration(1, configuration), "snapshot configuration");
                    const auto before = order == 0 ? count : order == 1 ? 0U : count / 2;
                    auto add = [&](unsigned i) {
                        check(
                            !fixture.add_entry(0, {3 + i, r::entry_type::command, payload(61 + i)}),
                            "inject suffix entry");
                    };
                    for (unsigned i = 0; i < before; ++i)
                        add(i);
                    r::snapshot snapshot;
                    snapshot.index = index;
                    snapshot.term = 2;
                    snapshot.configuration = configuration;
                    snapshot.configuration_index = 1;
                    snapshot.buffers.push_back(payload(17));
                    check(!fixture.set_snapshot(0, snapshot) && !fixture.set_term(0, 7),
                          "inject snapshot and term");
                    for (unsigned i = before; i < count; ++i)
                        add(i);
                    compare(fixture, count, index);
                    compare(fixture, count, index);
                    if (index == 10 && count == 3 && order == 0) {
                        unsigned failures = 0, successes = 0;
                        for (std::ptrdiff_t limit = 0; limit < 24; ++limit) {
                            r::io_state output;
                            output.term = 999;
                            output.voted_for = 99;
                            output.start_index = 321;
                            allocation_failure::after(limit);
                            const auto status = fixture.backend(0)->load(output);
                            allocation_failure::after(-1);
                            if (status) {
                                ++failures;
                                check(
                                    status == r::errc::out_of_memory && output.term == 999 &&
                                        output.voted_for == 99 && output.start_index == 321 &&
                                        !output.snapshot && output.entries.empty(),
                                    "snapshot-load allocation failure preserves the caller output");
                            } else {
                                ++successes;
                                check(
                                    output.entries.size() == count &&
                                        output.start_index == index + 1 && output.snapshot,
                                    "snapshot-load allocation sweep reaches a complete suffix copy");
                            }
                        }
                        check(failures >= 3 && successes != 0,
                              "snapshot load covers failing and successful deep copies");
                    }
                    check(!fixture.start() && fsm->restores == 1 && fsm->count == 17 + count,
                          "startup restores snapshot and applies each suffix command");
                    check(fixture.get(0).last_index() == index + count &&
                              fixture.get(0).commit_index() == index + count &&
                              fixture.get(0).last_applied() == index + count &&
                              fixture.get(0).current_term() == 7 &&
                              fixture.get(0).state() == r::node_state::leader,
                          "single-voter startup restores indexes and commits suffix");
                    compare(fixture, count, index);
                    ++cases;
                }
        }
        for (const unsigned trailing : {0, 1, 4})
            for (const unsigned count : {0, 1, 3}) {
                r::fixture fixture;
                check(!fixture.grow(std::make_shared<machine>()), "grow queued snapshot fixture");
                auto backend = fixture.backend(0);
                check(!backend->start(100, [] {}, [](auto) {}), "start raw fixture I/O");
                for (unsigned i = 0; i < count; ++i) {
                    check(!fixture.add_entry(0, {3 + i, r::entry_type::command, payload(61 + i)}),
                          "inject retained entries");
                }
                auto snapshot = std::make_shared<r::snapshot>();
                snapshot->index = 5;
                snapshot->term = 4;
                snapshot->configuration_index = 1;
                check(!fixture.configuration(1, snapshot->configuration),
                      "queued snapshot configuration");
                r::buffer bytes{8};
                std::ranges::fill(bytes.bytes(), r::byte{42});
                snapshot->buffers.emplace_back(std::move(bytes));
                unsigned completed = 0;
                check(!backend->snapshot_put(trailing, snapshot,
                                             [&](auto status) {
                                                 check(!status, "queued snapshot succeeds");
                                                 ++completed;
                                             }),
                      "submit identical queued snapshots");
                for (unsigned steps = 0; completed == 0; ++steps) {
                    check(steps < 10, "queued snapshot terminates");
                    check(fixture.step().has_value(),
                          "queued snapshot has a disk completion event");
                }
                check(completed == 1, "queued snapshot completes exactly once");
                compare(fixture, trailing ? count : 0, 5, true);
                compare(fixture, trailing ? count : 0, 5, true);
                ++cases;
            }
        std::cout << "fixture snapshot load: " << cases << " cases, " << comparisons
                  << " load checks\n";
    }
    void snapshot_fault_contracts() {
        for (const unsigned trailing : {0U, 1U}) {
            r::fixture fixture;
            check(!fixture.grow(std::make_shared<machine>()), "snapshot fault fixture");
            auto backend = fixture.backend(0);
            check(!backend->start(100, [] {}, [](auto) {}), "snapshot raw I/O timer");
            r::buffer prefix{8};
            std::ranges::fill(prefix.bytes(), r::byte{4});
            auto entries = std::make_shared<r::entry_batch>();
            entries->push_back(std::make_shared<r::entry>(
                r::entry{4, r::entry_type::command, r::shared_buffer{std::move(prefix)}}));
            check(!backend->append(1, entries,
                                   [](auto status) { check(!status, "snapshot prefix append"); }),
                  "persist identical prefix");
            auto step = [&] { check(fixture.step().has_value(), "snapshot completion event"); };
            step();
            r::snapshot value;
            value.index = 5;
            value.term = 4;
            value.configuration_index = 1;
            check(!value.configuration.add(1, "1", r::role::voter), "snapshot voters");
            r::buffer bytes{8};
            std::ranges::fill(bytes.bytes(), r::byte{42});
            value.buffers.emplace_back(std::move(bytes));
            // The successful internal truncate consumes the one eligible call;
            // the following vote then fails. No C legacy error callback is used.
            check(!fixture.io_fault(0, 1, 1), "snapshot truncate fault countdown");
            unsigned done = 0;
            check(!backend->snapshot_put(trailing, std::make_shared<r::snapshot>(value),
                                         [&](auto status) {
                                             check(!status, "snapshot persists before fault");
                                             ++done;
                                         }),
                  "submit identical snapshot");
            step();
            check(done == 1, "snapshot callback matches expected values");
            const auto actual = backend->set_vote(2);
            check(actual ==
                      (trailing == 0 ? r::make_error_code(r::errc::io_error) : r::error_code{}),
                  "snapshot truncate consumes its fault countdown");
            r::io_state disk;
            check(!fixture.persisted(0, disk) && disk.snapshot && disk.snapshot->index == 5 &&
                      disk.start_index == (trailing == 0 ? 6U : 1U) &&
                      disk.entries.size() == (trailing == 0 ? 0U : 1U),
                  "snapshot trailing state remains consistent");
            // The C fixture asserts on a failed internal truncate. Check the
            // C++ error/ownership contract independently rather than disabling it.
            value.index = 7;
            value.term = 8;
            value.buffers.clear();
            r::buffer replacement{8};
            std::ranges::fill(replacement.bytes(), r::byte{99});
            value.buffers.emplace_back(std::move(replacement));
            auto pending = std::make_shared<r::snapshot>(value);
            check(!fixture.io_fault(0, 0, 1), "fail internal snapshot truncation");
            r::error_code completed;
            check(!backend->snapshot_put(0, pending,
                                         [&](auto status) {
                                             completed = status;
                                             ++done;
                                         }),
                  "accept failing snapshot truncation");
            check(fixture.step().has_value() && done == 2 && completed == r::errc::io_error,
                  "failed truncate completes once with I/O error");
            r::io_state preserved;
            check(!fixture.persisted(0, preserved), "query state after snapshot truncate failure");
            check(preserved.snapshot && preserved.snapshot->index == disk.snapshot->index &&
                      preserved.snapshot->term == disk.snapshot->term &&
                      preserved.start_index == disk.start_index &&
                      preserved.entries.size() == disk.entries.size() &&
                      std::ranges::equal(preserved.snapshot->buffers[0].bytes(),
                                         disk.snapshot->buffers[0].bytes()),
                  "failed snapshot preserves old snapshot and log");
            check(!backend->snapshot_put(0, pending,
                                         [&](auto status) {
                                             completed = status;
                                             ++done;
                                         }),
                  "retry snapshot after finite failure");
            check(fixture.step().has_value() && done == 3 && !completed,
                  "snapshot retry succeeds exactly once");
            check(!fixture.persisted(0, preserved) && preserved.snapshot &&
                      preserved.snapshot->index == 7 && preserved.start_index == 8 &&
                      preserved.entries.empty() && !fixture.error(),
                  "successful retry commits replacement and removes prefix");
        }
    }
    void ordering() {
        r::fixture fixture;
        check(!fixture.grow(std::make_shared<machine>()) &&
                  !fixture.grow(std::make_shared<machine>()),
              "ordering servers");
        auto first = fixture.backend(0), second = fixture.backend(1);
        unsigned ticks = 0, received = 0, sent = 0, appended = 0;
        r::optional<r::message> retained;
        check(!first->start(
                  100, [&] { ++ticks; }, [](auto) {}) &&
                  !second->start(
                      100, [&] { ++ticks; },
                      [&](auto message) {
                          check(message.server_id == 1 && message.server_address == "1",
                                "transmit uses sender identity");
                          ++received;
                          retained = std::move(message);
                      }),
              "direct fixture subscriptions");
        check(!fixture.set_randomized_election_timeout(0, 1500) &&
                  !fixture.set_disk_latency(0, 100) && !fixture.set_network_latency(0, 0),
              "configured latencies and election draws");
        auto batch = std::make_shared<r::entry_batch>();
        batch->push_back(std::make_shared<r::entry>(r::entry{1, r::entry_type::command, {}}));
        check(!first->append(1, batch,
                             [&](auto status) {
                                 check(!status, "equal-time append");
                                 ++appended;
                             }),
              "equal-time append submission");
        check(!fixture.set_disk_latency(0, 1),
              "changed latency does not reschedule queued disk work");
        auto message = std::make_shared<r::message>();
        message->server_id = 2;
        message->server_address = "2";
        message->payload = r::request_vote{};
        r::weak<const r::message> weak = message;
        check(!first->send(message,
                           [&](auto status) {
                               check(!status && !weak.expired(),
                                     "accepted send retains input through completion");
                               ++sent;
                           }),
              "zero-latency send");
        message.reset();
        auto event = fixture.step();
        check(event && event->type == r::fixture_event_type::network && event->time == 0 &&
                  sent == 1 && received == 0,
              "send completion precedes delivery");
        event = fixture.step();
        check(event && event->time == 0 && received == 1 && weak.expired(),
              "separate zero-latency delivery owns copy");
        event = fixture.step();
        check(event && event->server_index == 0 && event->type == r::fixture_event_type::tick &&
                  event->time == 100,
              "tick precedes same-server equal-time disk event");
        event = fixture.step();
        check(event && event->server_index == 0 && event->type == r::fixture_event_type::disk &&
                  appended == 1,
              "lower-index request precedes other-server equal-time tick");
        event = fixture.step();
        check(event && event->server_index == 1 && event->type == r::fixture_event_type::tick &&
                  ticks == 2,
              "second server tick follows first server work");
        check(!fixture.disconnect(0, 1), "disconnect send");
        auto packet = std::make_shared<r::message>();
        packet->server_id = 2;
        packet->payload = r::request_vote{};
        check(!first->send(packet,
                           [&](auto status) {
                               check(status == r::errc::no_connection,
                                     "disconnected send fails asynchronously");
                               ++sent;
                           }),
              "disconnected send accepted");
        check(fixture.step().has_value() && sent == 2 &&
                  fixture.n_send(0, r::fixture_message_type::request_vote) == 1,
              "failed sends do not increment success counter");
        check(!fixture.reconnect(0, 1) && !fixture.saturate(0, 1), "saturate restored link");
        check(!first->send(packet,
                           [&](auto status) {
                               check(!status, "saturated send succeeds");
                               ++sent;
                           }),
              "saturated send");
        check(fixture.step_n(2).has_value() && sent == 3 && received == 1,
              "saturation drops delivery only");
        check(!fixture.desaturate(0, 1) && !fixture.set_network_latency(0, 25), "restore delivery");
        auto payload = r::shared_buffer::copy(r::array{r::byte{42}});
        r::weak<const r::byte> payload_owner = payload.storage();
        r::append_entries append;
        append.entries.push_back(
            std::make_shared<r::entry>(r::entry{1, r::entry_type::command, payload}));
        append.entries.push_back(
            std::make_shared<r::entry>(r::entry{1, r::entry_type::command, payload}));
        payload = {};
        packet->payload = std::move(append);
        check(!first->send(packet,
                           [&](auto status) {
                               check(!status, "batched send");
                               ++sent;
                           }),
              "batch transfer");
        packet.reset();
        check(!fixture.set_network_latency(0, 50),
              "transmit latency is captured at send completion");
        check(fixture.step().has_value() && payload_owner.expired(),
              "transmit owns an independent batch after send completion");
        event = fixture.step();
        check(event && event->time == 150 && received == 2,
              "delivery uses completion-time network latency");
        const auto& delivered = std::get<r::append_entries>(retained->payload).entries;
        check(delivered.size() == 2 &&
                  delivered[0]->payload.shares_storage_with(delivered[1]->payload) &&
                  delivered[1]->payload.bytes()[0] == r::byte{42},
              "copied entries preserve shared batch ownership");
    }
    void elections() {
        for (unsigned target = 0; target < 3; ++target) {
            r::fixture fixture;
            auto machines = initialize(fixture);
            check(fixture.elect(target), "choose any voter for election");
            check(fixture.step_until_term_is(target, 2, 1) &&
                      fixture.step_until_voted_for(target, target, 1),
                  "term/vote waits");
            check(fixture.depose() && fixture.step_until_has_no_leader(1), "depose stable leader");
            check(fixture.elect((target + 1) % 3), "elect after deposition");
            const auto leader = fixture.leader_index();
            bool applied = false;
            check(!fixture.get(leader).apply(r::buffer{8},
                                             [&](auto status, auto) {
                                                 check(!status, "commit command");
                                                 applied = true;
                                             }),
                  "apply fixture command");
            const auto index = fixture.get(leader).last_index();
            check(fixture.step_until_applied(fixture.size(), index, 2000) && applied,
                  "all servers apply");
            for (const auto& value : machines)
                check(value->count == 1, "replicated FSM count");
            check(fixture.step_until_delivered(leader, (leader + 1) % 3, 1000),
                  "wait for network drain");
            check(!fixture.error() && fixture.commit_index() >= index,
                  "fixture safety observations");
        }
    }
    void network() {
        r::fixture fixture;
        auto machines = initialize(fixture, 3, true);
        check(fixture.elect(0), "network leader");
        check(!fixture.disconnect(0, 1) && !fixture.reconnect(0, 1),
              "directional disconnect/reconnect");
        check(!fixture.saturate(0, 1) && fixture.saturated(0, 1) && !fixture.saturated(1, 0),
              "directional saturation");
        check(!fixture.desaturate(0, 1) && !fixture.kill(2) && !fixture.alive(2), "kill follower");
        for (unsigned i = 0; i < 16; ++i) {
            bool applied = false;
            check(!fixture.get(0).apply(r::buffer{8},
                                        [&](auto status, auto) {
                                            check(!status, "majority commit");
                                            applied = true;
                                        }),
                  "majority apply");
            check(fixture.step_until([&](const auto&) { return applied; }, 2000),
                  "majority completion");
        }
        fixture.step_until_elapsed(1000);
        check(machines[0]->count == 16 && machines[1]->count == 16 && machines[2]->count == 0,
              "dead server skips ticks/delivery");
        check(!fixture.revive(2), "revive follower");
        check(fixture.step_until([&](const auto&) { return machines[2]->count == 16; }, 5000),
              "snapshot catch-up after revival");
        check(machines[2]->restores > 0, "actual fixture snapshot installation");
        check(fixture.n_send(0, r::fixture_message_type::install_snapshot) > 0 &&
                  fixture.n_recv(2, r::fixture_message_type::install_snapshot) > 0,
              "message counters");
        check(!fixture.kill(0), "kill leader");
        check(fixture.step_until(
                  [](const auto& value) {
                      return value.leader_index() < value.size() && value.leader_index() != 0;
                  },
                  5000),
              "remaining majority elects leader");
        check(!fixture.revive(0) && fixture.step_until_state_is(0, r::node_state::follower, 5000),
              "old leader steps down");
    }
    void storage() {
        r::fixture fixture;
        auto machine = std::make_shared<struct machine>();
        check(!fixture.grow(machine), "storage server");
        r::configuration conf;
        check(!fixture.configuration(1, conf), "storage configuration");
        check(!fixture.io_fault(0, 0, 2), "fault countdown");
        check(fixture.bootstrap(conf) == r::errc::io_error &&
                  fixture.bootstrap(conf) == r::errc::io_error && !fixture.bootstrap(conf),
              "finite repeated faults");
        auto backend = fixture.backend(0);
        check(backend->recover(conf) == r::errc::io_error,
              "memory fixture preserves unsupported recovery contract");
        auto entries = std::make_shared<r::entry_batch>();
        entries->push_back(std::make_shared<r::entry>(
            r::entry{2, r::entry_type::command, r::shared_buffer::copy(r::array{r::byte{7}})}));
        check(!fixture.io_fault(0, 1, 1), "fail append completion");
        unsigned done = 0;
        check(!backend->append(2, entries,
                               [&](auto status) {
                                   check(status == r::errc::io_error, "completion failure");
                                   ++done;
                               }),
              "accepted failing append");
        check(fixture.step().has_value() && done == 1, "append failure at disk latency");
        r::io_state disk;
        check(!fixture.persisted(0, disk) && disk.entries.size() == 1,
              "failed append preserves disk");
        check(!backend->append(2, entries,
                               [&](auto status) {
                                   check(!status, "append retry");
                                   ++done;
                               }),
              "retry append");
        check(fixture.step().has_value() && done == 2, "append retry completion");
        r::snapshot snapshot;
        snapshot.index = 10;
        snapshot.term = 2;
        snapshot.configuration = conf;
        snapshot.configuration_index = 1;
        r::buffer bytes{sizeof(r::uint64_t)};
        r::uint64_t count = 5;
        std::memcpy(bytes.data(), &count, sizeof(count));
        snapshot.buffers.emplace_back(std::move(bytes));
        check(!fixture.set_snapshot(0, snapshot) && !fixture.set_term(0, 2),
              "set persisted snapshot/term");
        // Install semantics discard the old prefix and start the suffix at index 11.
        check(!backend->snapshot_put(0, std::make_shared<r::snapshot>(snapshot),
                                     [&](auto status) {
                                         check(!status, "snapshot put");
                                         ++done;
                                     }),
              "snapshot put submit");
        check(fixture.step().has_value() && done == 3, "snapshot write completion");
        check(!fixture.add_entry(0, *entries->front()), "persist suffix after snapshot");
        check(!fixture.start() && machine->restores == 1 && machine->count == 6 &&
                  fixture.get(0).last_applied() == 11,
              "load snapshot and suffix into public node");
        check(!backend->load(disk) && disk.start_index == 11 && disk.entries.size() == 1,
              "load returns installed snapshot suffix");
        check(!fixture.io_fault(0, 0, -1) && backend->set_vote(1) == r::errc::io_error &&
                  backend->set_vote(1) == r::errc::io_error,
              "permanent fault");
        check(!fixture.io_fault(0, -1, -1), "disable faults");
    }
    void lifetime() {
        auto previous = r::get_memory_resource();
        struct selection {
            r::shared<r::pmr::memory_resource> previous;
            ~selection() {
                (void)r::set_memory_resource(std::move(previous));
            }
        } restore{previous};
        auto stats = std::make_shared<memory_testing::statistics>();
        auto resource = std::make_shared<memory_testing::resource>(stats);
        r::weak<r::pmr::memory_resource> observer = resource;
        (void)r::set_memory_resource(resource);
        resource.reset();
        r::shared<r::io> backend;
        unsigned canceled = 0;
        {
            r::fixture first;
            auto machine = std::make_shared<struct machine>();
            check(!first.grow(machine), "lifetime grow");
            backend = first.backend(0);
            r::fixture second{std::move(first)};
            check(first.size() == 0 && second.size() == 1,
                  "fixture move retains stable node/backend addresses");
            r::reset_memory_resource();
            stats->fail_after = 0;
            check(backend->snapshot_get([&](auto, auto) { ++canceled; }) ==
                          r::errc::out_of_memory &&
                      canceled == 0,
                  "new request fails cleanly when its old-resource queue cannot grow");
            stats->fail_after = -1;
            check(!backend->snapshot_get([&](auto status, auto) {
                check(status == r::errc::canceled, "get canceled on close");
                ++canceled;
            }),
                  "pending get");
            auto entries = std::make_shared<r::entry_batch>();
            entries->push_back(std::make_shared<r::entry>(r::entry{1, r::entry_type::command, {}}));
            check(!backend->append(1, entries,
                                   [&](auto status) {
                                       check(status == r::errc::canceled,
                                             "append canceled on close");
                                       ++canceled;
                                   }),
                  "pending append");
            check(!second.get(0).close(), "explicit node close cancels accepted backend work");
            second.close();
            second.close();
            check(canceled == 2 && !second.step(), "close drains exactly once");
        }
        check(!observer.expired(), "external closed backend retains its container resource");
        check(backend->set_vote(1) == r::errc::shutdown,
              "retained backend rejects work after fixture destruction");
        r::configuration configuration;
        check(backend->recover(configuration) == r::errc::shutdown,
              "closed backend recovery also reports shutdown");
        backend.reset();
        check(observer.expired() && stats->destroyed && stats->live == 0 &&
                  stats->allocated == stats->freed && !stats->mismatch,
              "all fixture allocations drain through original resource");
    }
    void failures() {
        unsigned failures = 0, successes = 0;
        for (std::ptrdiff_t position = 0; position < 35; ++position) {
            r::fixture fixture;
            auto machine = std::make_shared<struct machine>();
            allocation_failure::after(position);
            auto error = fixture.grow(machine);
            allocation_failure::after(-1);
            if (error) {
                ++failures;
                check(error == r::errc::out_of_memory && fixture.size() == 0,
                      "failed grow preserves cluster");
            } else {
                ++successes;
                check(fixture.size() == 1, "successful grow");
            }
        }
        check(failures && successes, "grow allocation failures and successes covered");
        r::fixture fixture;
        check(!fixture.step() && fixture.start() == r::errc::invalid, "empty fixture");
        for (unsigned i = 0; i < r::fixture::max_servers; ++i)
            check(!fixture.grow(std::make_shared<machine>()), "grow to limit");
        check(fixture.grow(std::make_shared<machine>()) == r::errc::too_many && fixture.size() == 8,
              "server limit");
        check(fixture.disconnect(8, 0) == r::errc::invalid &&
                  fixture.set_randomized_election_timeout(0, 999) == r::errc::invalid,
              "invalid controls");
        auto machines = std::make_shared<machine>();
        r::fixture events;
        check(!events.grow(machines), "event server");
        r::configuration conf;
        check(!events.configuration(1, conf) && !events.bootstrap(conf), "event bootstrap");
        auto entries = std::make_shared<r::entry_batch>();
        entries->push_back(std::make_shared<r::entry>(r::entry{1, r::entry_type::command, {}}));
        auto backend = events.backend(0);
        unsigned done = 0;
        auto invalid = std::make_shared<r::message>();
        invalid->server_id = 1;
        r::install_snapshot value;
        value.configuration = conf;
        bool failed = false;
        allocation_failure::after(0);
        try {
            invalid->payload.emplace<r::install_snapshot>(value);
        } catch (const r::bad_alloc&) {
            failed = true;
        }
        allocation_failure::after(-1);
        check(failed, "message variant construction failure");
        if (invalid->payload.valueless_by_exception())
            check(backend->send(invalid, [&](auto) { ++done; }) == r::errc::invalid && done == 0,
                  "valueless messages are rejected before queueing");
        allocation_failure::after(0);
        const auto submitted = backend->append(2, entries, [&](auto) { ++done; });
        allocation_failure::after(-1);
        check(submitted == r::errc::out_of_memory && done == 0,
              "direct backend submission allocation failure does not invoke completion");
        check(!backend->append(2, entries,
                               [&](auto status) {
                                   check(status == r::errc::out_of_memory,
                                         "disk copy allocation failure");
                                   ++done;
                               }),
              "submit allocation check");
        allocation_failure::after(0);
        (void)events.step();
        allocation_failure::after(-1);
        r::io_state persisted;
        check(done == 1 && !events.persisted(0, persisted) && persisted.entries.size() == 1,
              "completion OOM preserves disk");
        check(!events.start(), "start hook fixture");
        check(!events.step_until([](const auto& value) { return value.time() >= 110; }, 100),
              "stop condition exactly at deadline is false like C");
        unsigned hooked = 0;
        events.hook([&](auto& value, const auto&) {
            ++hooked;
            value.close();
        });
        check(events.step().has_value() && hooked == 1 && events.size() == 0,
              "close from event hook");
        r::fixture flushing;
        check(!flushing.grow(std::make_shared<machine>()), "flush-close server");
        auto retained_backend = flushing.backend(0);
        unsigned flushed = 0;
        check(!retained_backend->append(
                  1, entries,
                  [&](auto status) {
                      check(!status, "fixture close flushes disk requests before closing nodes");
                      r::io_state disk;
                      check(!flushing.persisted(0, disk) && disk.entries.size() == 1,
                            "close completion observes durable data");
                      ++flushed;
                      flushing.close();
                  }),
              "append before fixture close");
        flushing.close();
        check(flushed == 1 && flushing.time() == 0,
              "close flushes without advancing virtual time and tolerates reentrancy");
        r::fixture throwing;
        check(!throwing.grow(std::make_shared<machine>()), "throwing callback server");
        check(!throwing.backend(0)->snapshot_get(
                  [](auto, auto) { throw r::invalid_argument("completion"); }),
              "throwing get submission");
        check(throwing.step().has_value() && throwing.error() == r::errc::io_error,
              "snapshot completion exceptions are recorded without escaping");
    }
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "fixture_contracts")
            contracts();
        else if (suite == "fixture_network_contracts")
            network_contracts();
        else if (suite == "fixture_membership_contracts")
            membership_contracts();
        else if (suite == "fixture_disk_contracts")
            disk_contracts();
        else if (suite == "fixture_snapshot_fault_contracts")
            snapshot_fault_contracts();
        else if (suite == "fixture_snapshot_load_contracts")
            snapshot_load_contracts();
        else if (suite == "fixture_ordering")
            ordering();
        else if (suite == "fixture_election")
            elections();
        else if (suite == "fixture_network")
            network();
        else if (suite == "fixture_storage")
            storage();
        else if (suite == "fixture_lifetime")
            lifetime();
        else if (suite == "fixture_failure")
            failures();
        else
            check(false, "unknown suite");
        return 0;
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
