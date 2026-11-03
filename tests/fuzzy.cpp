// Scenario contracts ported from cowsql/raft test/fuzzy; see LICENSE.md.
#include <raftpp/fixture.hpp>
#include "byte.hpp"

#include <algorithm>
#include <iostream>
#include <random>
#include <source_location>

namespace r = raftpp;
namespace d = raftpp::detail;

namespace {
    r::string context;
    unsigned scenarios = 0;
    r::uint64_t events = 0;
    constexpr r::array<unsigned, 3> seeds{85213, 70011, 0x3914306f};

    void check(bool condition, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error(context + " " + where.file_name() + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }

    struct counter final : r::state_machine {
        r::uint64_t count = 0;
        r::error_code apply(r::view<const r::byte> input, r::shared<void>&) override {
            if (input.size() != 8)
                return r::errc::malformed;
            count += d::load_little_endian<r::uint64_t>(input.first<8>());
            return {};
        }
        r::error_code restore(r::shared_buffer) override {
            return r::errc::io_error;
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return r::errc::io_error;
        }
    };
    struct result {
        unsigned calls = 0;
        r::error_code status;
    };

    struct cluster {
        std::mt19937 random;
        r::vector<r::shared<counter>> machines;
        r::vector<r::shared<result>> requests;
        r::node_options options;
        r::function<void()> invariant;
        r::array<unsigned, 8> timeouts{}, latencies{};
        r::fixture fixture;

        cluster(unsigned size, bool pre_vote, unsigned seed) : random{seed} {
            options.pre_vote = pre_vote;
            options.capacity_threshold = 0;
            options.snapshot_threshold = r::numeric_limits<unsigned>::max();
            options.tracing = r::tracer::noop();
            for (unsigned i = 0; i < size; ++i)
                grow();
            r::configuration members;
            check(!fixture.configuration(size, members) && !fixture.bootstrap(members),
                  "bootstrap");
            // Preserve append completion order: disk latency stays fixed.
            // Original randomize() also leaves disk randomization disabled.
            fixture.hook([this](auto& value, const auto& event) {
                ++events;
                if (invariant)
                    invariant();
                // Exercise bounded timing changes without a second implementation.
                if (event.type == r::fixture_event_type::tick) {
                    const auto timeout = draw(1000, 2000);
                    check(!value.set_randomized_election_timeout(event.server_index, timeout),
                          "vary reset election timeout");
                }
                if (event.type == r::fixture_event_type::network) {
                    const auto latency = draw(25, 50);
                    check(!value.set_network_latency(event.server_index, latency),
                          "vary network latency");
                }
                check(!value.error(), "fixture safety invariants");
            });
            check(!fixture.start(), "start");
        }
        ~cluster() {
            fixture.hook({});
            fixture.close();
        }
        unsigned draw(unsigned first, unsigned last) {
            return std::uniform_int_distribution<unsigned>{first, last}(random);
        }
        void grow() {
            const auto index = fixture.size();
            auto machine = std::make_shared<counter>();
            check(!fixture.grow(machine, options), "grow");
            machines.push_back(std::move(machine));
            timeouts[index] = draw(1000, 2000);
            latencies[index] = draw(25, 50);
            check(!fixture.set_randomized_election_timeout(index, timeouts[index]) &&
                      !fixture.set_network_latency(index, latencies[index]) &&
                      !fixture.set_disk_latency(index, 10),
                  "initial randomized timing");
        }

        r::error_code kill(unsigned index) {
            return fixture.kill(index);
        }
        r::error_code revive(unsigned index) {
            return fixture.revive(index);
        }
        r::error_code saturate(unsigned from, unsigned to) {
            return fixture.saturate(from, to);
        }
        r::error_code desaturate(unsigned from, unsigned to) {
            return fixture.desaturate(from, to);
        }
        void healthy() {
            check(!fixture.error(), "fixture safety");
            for (unsigned i = 0; i < fixture.size(); ++i)
                if (fixture.alive(i))
                    check(!fixture.get(i).error(), "live node healthy");
            for (const auto& request : requests)
                check(request->calls <= 1, "request completes at most once");
        }
        void dump() {
            std::cerr << context << " time=" << fixture.time()
                      << " stable_leader=" << fixture.leader_index() << '\n';
            for (unsigned i = 0; i < fixture.size(); ++i) {
                const auto& node = fixture.get(i);
                std::cerr << " node=" << i << " alive=" << fixture.alive(i)
                          << " state=" << static_cast<unsigned>(node.state())
                          << " term=" << node.current_term() << " last=" << node.last_index()
                          << " commit=" << node.commit_index() << " applied=" << node.last_applied()
                          << " count=" << machines[i]->count << '\n';
            }
        }
        template <typename F>
        void until(F condition, unsigned limit = 20000,
                   std::source_location where = std::source_location::current()) {
            const bool completed = fixture.step_until(
                [&](const auto&) {
                    healthy();
                    return condition();
                },
                limit);
            if (!completed)
                dump();
            check(completed, "condition before virtual deadline", where);
        }
        unsigned leader(unsigned limit = 20000,
                        std::source_location where = std::source_location::current()) {
            until([&] { return fixture.leader_index() < fixture.size(); }, limit, where);
            return static_cast<unsigned>(fixture.leader_index());
        }
        r::shared<result> apply(unsigned index) {
            auto request = std::make_shared<result>();
            r::buffer input{8};
            d::store_little_endian<r::uint64_t>(1, input.bytes().first<8>());
            check(!fixture.get(index).apply(std::move(input),
                                            [request](auto status, auto) {
                                                request->status = status;
                                                ++request->calls;
                                            }),
                  "accept command");
            requests.push_back(request);
            return request;
        }
        void commit(std::source_location where = std::source_location::current()) {
            auto request = apply(leader(20000, where));
            until([&] { return request->calls == 1; }, 10000, where);
            if (request->status) {
                dump();
                std::cerr << " request=" << request->status << '\n';
            }
            check(!request->status, "successful command", where);
        }
        void all_applied(r::uint64_t expected,
                         std::source_location where = std::source_location::current()) {
            until(
                [&] {
                    for (unsigned i = 0; i < machines.size(); ++i)
                        if (fixture.alive(i) && machines[i]->count != expected)
                            return false;
                    return true;
                },
                10000, where);
        }
        void kill_majority(unsigned except) {
            unsigned killed = 0;
            for (unsigned i = 0; i < fixture.size() && killed < fixture.size() / 2 + 1; ++i)
                if (i != except) {
                    check(!kill(i), "kill majority");
                    ++killed;
                }
            check(killed == fixture.size() / 2 + 1, "actual majority killed");
        }
        void heal() {
            for (unsigned i = 0; i < fixture.size(); ++i)
                for (unsigned j = 0; j < fixture.size(); ++j)
                    if (i != j)
                        check(!desaturate(i, j), "heal directed link");
            // Re-evaluate the cached stable leader after changing connectivity.
            check(fixture.step().has_value(), "observe healed links");
            until([&] {
                const auto primary = fixture.leader_index();
                if (primary == fixture.size())
                    return false;
                const auto term = fixture.get(primary).current_term();
                for (unsigned i = 0; i < fixture.size(); ++i)
                    if (fixture.alive(i)) {
                        const auto& node = fixture.get(i);
                        if (node.current_term() != term ||
                            (i != primary && node.state() != r::node_state::follower))
                            return false;
                    }
                return true;
            });
        }
    };

    void scenario(r::string_view name, unsigned size, bool pre_vote, unsigned seed) {
        context = r::string{name} + " n=" + std::to_string(size) +
                  " pre_vote=" + std::to_string(pre_vote) + " seed=" + std::to_string(seed);
        ++scenarios;
    }

    void election() {
        constexpr r::array<r::string_view, 4> names{"win", "change", "change_revive_outdated",
                                                    "no_quorum"};
        for (const auto seed : seeds)
            for (const auto size : {3U, 4U, 5U, 7U})
                for (const bool pre_vote : {false, true})
                    for (unsigned kind = 0; kind < 4; ++kind) {
                        scenario("election/" + r::string{names[kind]}, size, pre_vote, seed);
                        cluster value{size, pre_vote, seed};
                        if (kind == 0) {
                            (void)value.leader(10000);
                            continue;
                        }
                        if (kind == 1) {
                            const auto old = value.leader(10000);
                            check(!value.kill(old), "kill leader");
                            check(value.fixture.step_until_has_no_leader(10000),
                                  "old stable leader disappears");
                            check(value.leader() != old, "replacement leader");
                        } else if (kind == 2) {
                            const auto outdated = value.draw(0, size - 1);
                            check(!value.kill(outdated), "kill before election");
                            value.commit();
                            value.commit();
                            value.all_applied(2);
                            check(value.machines[outdated]->count == 0,
                                  "outdated FSM remains behind");
                            const auto old = value.leader();
                            check(!value.kill(old), "kill current leader");
                            check(value.fixture.step_until_has_no_leader(10000),
                                  "leader disappears before revive");
                            check(!value.revive(outdated), "revive outdated node");
                            (void)value.leader();
                            value.all_applied(2);
                        } else {
                            value.kill_majority(size);
                            value.fixture.step_until_elapsed(30000);
                            value.healthy();
                            check(value.fixture.leader_index() == size,
                                  "minority does not elect leader");
                        }
                    }
    }

    void replication() {
        constexpr r::array<r::string_view, 4> names{"append_entries", "availability", "no_quorum",
                                                    "partitioned"};
        for (const auto seed : seeds)
            for (const auto size : {3U, 5U, 7U})
                for (unsigned kind = 0; kind < 4; ++kind) {
                    scenario("replication/" + r::string{names[kind]}, size, false, seed);
                    cluster value{size, false, seed};
                    const auto old = value.leader(10000);
                    if (kind == 0) {
                        value.commit();
                        value.all_applied(1);
                    } else if (kind == 1) {
                        value.commit();
                        value.all_applied(1);
                        check(!value.kill(old), "kill after first command");
                        check(value.fixture.step_until_has_no_leader(10000),
                              "old leader disappears");
                        value.commit();
                        value.all_applied(2);
                        check(!value.revive(old), "revive old leader");
                        value.all_applied(2);
                    } else if (kind == 2) {
                        auto request = value.apply(old);
                        value.kill_majority(old);
                        value.fixture.step_until_elapsed(10000);
                        value.healthy();
                        for (const auto& machine : value.machines)
                            check(machine->count == 0, "no quorum means no applied command");
                        check(request->calls == 1 && request->status == r::errc::leadership_lost,
                              "pending command completes on quorum loss");
                    } else {
                        unsigned isolated = 0;
                        for (unsigned i = 0; i < size && isolated < size / 2 + 1; ++i)
                            if (i != old) {
                                check(!value.saturate(old, i) && !value.saturate(i, old),
                                      "partition old leader");
                                ++isolated;
                            }
                        const auto term = value.fixture.get(old).current_term();
                        value.invariant = [&] {
                            if (value.fixture.get(old).state() == r::node_state::leader &&
                                value.fixture.get(old).current_term() == term)
                                check(value.machines[old]->count == 0,
                                      "minority leader cannot commit in its term");
                        };
                        (void)value.apply(old);
                        check(value.fixture.step_until_has_no_leader(10000),
                              "partitioned leader deposed");
                        value.fixture.step_until_elapsed(5000);
                        value.healthy();
                        // Peers outside the leader's cut still communicate. A new
                        // leader may legitimately commit the accepted command.
                        for (const auto& machine : value.machines)
                            check(machine->count <= 1, "command applies at most once");
                        value.invariant = {};
                        value.heal();
                        value.commit();
                        const auto count = value.machines[value.leader()]->count;
                        check(count >= 1, "healed majority commits new work");
                        value.all_applied(count);
                    }
                }
    }

    void membership() {
        constexpr r::array<r::string_view, 3> names{"add_non_voting", "add_voting",
                                                    "remove_voting"};
        for (const auto seed : seeds)
            for (const auto size : {3U, 4U, 5U})
                for (unsigned kind = 0; kind < 3; ++kind) {
                    scenario("membership/" + r::string{names[kind]}, size, false, seed);
                    cluster value{size, false, seed};
                    const auto leader = value.leader(10000);
                    auto request = std::make_shared<result>();
                    auto done = [request](auto status) {
                        request->status = status;
                        ++request->calls;
                    };
                    r::server_id id = 0;
                    if (kind < 2) {
                        value.grow();
                        const auto index = value.fixture.size() - 1;
                        id = index + 1;
                        check(!value.fixture.get(index).start(), "start pristine added node");
                        check(!value.fixture.get(leader).add(id, std::to_string(id), done),
                              "submit add");
                        value.until([&] { return request->calls == 1; }, 2000);
                        check(!request->status, "add committed");
                        request->calls = 0;
                        const auto role = kind == 0 ? r::role::standby : r::role::voter;
                        const auto primary = value.leader();
                        check(!value.fixture.get(primary).assign(id, role, done),
                              "submit promotion");
                        value.until([&] { return request->calls == 1; }, 10000);
                        check(!request->status, "promotion committed");
                        const auto index_to_apply =
                            value.fixture.get(value.leader()).last_applied();
                        check(value.fixture.step_until_applied(value.fixture.size(), index_to_apply,
                                                               10000),
                              "added node receives committed configuration");
                        const auto members = value.fixture.get(value.leader()).configuration();
                        const auto server = members.get(id);
                        check(server && server->role == role, "new member has requested role");
                    } else {
                        // The original removeVoting removes the leader itself.
                        id = leader + 1;
                        check(!value.fixture.get(leader).remove(id, done), "submit remove");
                        value.until([&] { return request->calls == 1; }, 2000);
                        check(!request->status, "remove committed");
                        const auto members = value.fixture.get(leader).configuration();
                        check(members.size() == size - 1 && !members.get(id),
                              "removed voter absent");
                    }
                    value.commit();
                    value.until([&] {
                        const auto& members = value.fixture.get(value.leader()).configuration();
                        for (unsigned i = 0; i < value.fixture.size(); ++i)
                            if (members.get(i + 1) && value.machines[i]->count != 1)
                                return false;
                        return true;
                    });
                }
    }

    void liveness() {
        struct link {
            unsigned from, to, remaining = 0;
        };
        for (const auto seed : seeds)
            for (const auto size : {3U, 4U})
                for (const bool pre_vote : {false, true}) {
                    scenario("liveness", size, pre_vote, seed);
                    cluster value{size, pre_vote, seed};
                    r::vector<link> links;
                    for (unsigned i = 0; i < size; ++i)
                        for (unsigned j = i + 1; j < size; ++j)
                            links.push_back({i, j});
                    bool progressed = false;
                    for (unsigned iteration = 0; iteration < 25000; ++iteration) {
                        for (auto& link : links) {
                            if (link.remaining) {
                                if (--link.remaining == 0)
                                    check(!value.desaturate(link.from, link.to) &&
                                              !value.desaturate(link.to, link.from),
                                          "reconnect pair");
                            } else if (value.draw(1, 10) == 1) {
                                link.remaining = value.draw(50, 150);
                                check(!value.saturate(link.from, link.to) &&
                                          !value.saturate(link.to, link.from),
                                      "disconnect pair");
                            }
                        }
                        check(value.fixture.step().has_value(), "scheduled event");
                        value.healthy();
                        if (value.fixture.leader_index() < size) {
                            const auto leader = static_cast<unsigned>(value.fixture.leader_index());
                            (void)value.apply(leader);
                            if (value.machines[leader]->count != 0) {
                                progressed = true;
                                break;
                            }
                        }
                    }
                    // Finite partitions can deny quorum for the entire random phase.
                    // Unlike the disabled original final assertion, always require
                    // actual quorum progress after healing. This does not impose an
                    // all-followers catch-up deadline absent from the original test.
                    value.heal();
                    value.commit();
                    const auto primary = value.leader();
                    const auto count = value.machines[primary]->count;
                    const auto applied = value.fixture.get(primary).last_applied();
                    check(count >= 1, "progress after bounded disruptions");
                    value.until(
                        [&] {
                            unsigned reached = 0;
                            for (unsigned i = 0; i < size; ++i)
                                if (value.fixture.get(i).last_applied() >= applied &&
                                    value.machines[i]->count == count)
                                    ++reached;
                            return reached >= size / 2 + 1;
                        },
                        10000);
                    std::cout << "liveness seed=" << seed << " n=" << size
                              << " pre_vote=" << pre_vote
                              << " progress_during_disruption=" << progressed << '\n';
                }
    }
}

int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite argument");
        const r::string_view suite = argv[1];
        if (suite == "election")
            election();
        else if (suite == "replication")
            replication();
        else if (suite == "membership")
            membership();
        else if (suite == "liveness")
            liveness();
        else
            check(false, "unknown suite");
        std::cout << suite << ": " << scenarios << " scenarios, " << events
                  << " checked fixture events\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
