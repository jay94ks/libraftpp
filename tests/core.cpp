#include <raftpp/detail/core.hpp>
#include <raftpp/core.hpp>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <random>
#include <source_location>

namespace r = raftpp;
namespace d = raftpp::detail;

namespace {

    void check(bool condition, r::string_view message,
               std::source_location location = std::source_location::current()) {
        if (!condition)
            throw std::runtime_error(r::string{location.file_name()} + ":" +
                                     std::to_string(location.line()) + ": " + r::string{message});
    }

    r::view<const r::byte> bytes(r::string_view value) {
        return {reinterpret_cast<const r::byte*>(value.data()), value.size()};
    }

    r::entry command(r::term term, r::string_view value) {
        return {term, r::entry_type::command, r::shared_buffer::copy(bytes(value))};
    }

    r::entry change(r::term term, const r::configuration& configuration) {
        r::buffer encoded;
        check(!configuration.encode(encoded), "encode configuration entry");
        return {term, r::entry_type::change, r::shared_buffer{std::move(encoded)}};
    }

    r::configuration members(unsigned voters = 3, bool extras = false) {
        r::configuration result;
        for (unsigned i = 1; i <= voters; ++i) {
            check(!result.add(i, "node-" + std::to_string(i), r::role::voter), "add voter");
        }
        if (extras) {
            check(!result.add(4, "standby", r::role::standby), "add standby");
            check(!result.add(5, "spare", r::role::spare), "add spare");
        }
        return result;
    }

    struct node {
        d::core_state state;
        explicit node(r::server_id id) {
            state.id = id;
            state.address = "node-" + std::to_string(id);
            state.random = 1729;
        }
        r::error_code run(d::event_payload payload, d::time time = 0, r::uint16_t capacity = 8192) {
            d::core_event event{time, capacity, std::move(payload)};
            const auto previous_term = state.current_term;
            const auto previous_commit = state.commit_index;
            const auto status = d::step(state, event);
            check(state.current_term >= previous_term && state.commit_index >= previous_commit,
                  "term and committed prefix never decrease");
            for (r::log_index i = 1; i <= state.trail.last_index() + 1; ++i)
                check(state.cache.term_of(i) == state.trail.term_of(i),
                      "cache and compressed trail retain identical terms");
            if (const auto leader = std::get_if<d::leader_state>(&state.state))
                check(leader->progress.size() == state.configuration.size(),
                      "leader owns one progress record per member");
            if (const auto candidate = std::get_if<d::candidate_state>(&state.state))
                check(candidate->votes.size() == state.configuration.voter_count(),
                      "candidate owns one vote per voter");
            return status;
        }
        void start(const r::configuration& configuration, bool pending = false) {
            d::start_event start;
            start.term = 1;
            start.entries.push_back(change(1, configuration));
            if (pending)
                start.entries.push_back(command(1, "previous-term"));
            check(!run(std::move(start)), "start node");
        }
        void elect(bool pre_vote = false, bool pending = false) {
            state.pre_vote = pre_vote;
            start(members(), pending);
            const auto time = d::next_timeout(state);
            check(!run(d::timeout_event{}, time), "candidate election timeout");
            d::request_vote_result vote;
            vote.term = state.current_term + (pre_vote ? 1 : 0);
            vote.pre_vote = pre_vote;
            vote.vote_granted = true;
            vote.features = d::feature_capacity;
            vote.capacity = 8192;
            check(!run(d::receive_event{{2, "node-2", vote}}, time), "receive election vote");
            if (pre_vote) {
                vote.pre_vote = false;
                vote.term = state.current_term;
                check(!run(d::receive_event{{2, "node-2", vote}}, time),
                      "receive actual election vote");
            }
            check(std::holds_alternative<d::leader_state>(state.state), "node became leader");
        }
    };

    void test_core_term_contracts() {
        constexpr auto maximum = r::numeric_limits<r::term>::max();
        unsigned events = 0;
        for (const auto loaded_term : {maximum - 2, maximum - 1}) {
            for (bool pre_vote : {false, true}) {
                node n{1};
                n.state.pre_vote = pre_vote;
                auto run = [&](auto value, d::time time = 0) {
                    ++events;
                    return n.run(std::move(value), time);
                };
                d::start_event start;
                start.term = loaded_term;
                start.entries.push_back(change(1, members()));
                check(!run(std::move(start)), "start near the term limit");
                check(!run(d::timeout_event{}, d::next_timeout(n.state)),
                      "start representable election");
                if (pre_vote) {
                    check(!run(d::timeout_event{}, d::next_timeout(n.state)),
                          "repeat representable pre-vote");
                    d::request_vote_result vote;
                    vote.term = n.state.current_term + 1;
                    vote.pre_vote = true;
                    vote.vote_granted = true;
                    check(!run(d::receive_event{{2, "node-2", vote}}, n.state.now),
                          "C pre-vote quorum near term limit");
                }
                if (n.state.current_term < maximum)
                    check(!run(d::timeout_event{}, d::next_timeout(n.state)),
                          "C actual election reaches maximum term");
                d::request_vote_result vote;
                vote.term = maximum;
                vote.vote_granted = true;
                check(!run(d::receive_event{{2, "node-2", vote}}, n.state.now) &&
                          r::state(n.state) == r::node_state::leader &&
                          n.state.current_term == maximum,
                      "largest valid term elects the expected leader");
                check(!run(d::submit_event{{command(maximum, "last-term")}}, n.state.now) &&
                          !run(d::persisted_entries_event{2}, n.state.now),
                      "persist maximum-term command");
                d::append_entries_result ack;
                ack.term = maximum;
                ack.last_log_index = 2;
                check(!run(d::receive_event{{2, "node-2", ack}}, n.state.now) &&
                          n.state.commit_index == 2,
                      "maximum-term command commits");
            }
        }
        for (bool pre_vote : {false, true}) {
            node n{1};
            n.state.pre_vote = pre_vote;
            d::start_event start;
            start.term = maximum;
            start.entries.push_back(change(1, members(1)));
            ++events;
            check(!n.run(std::move(start)) && r::state(n.state) == r::node_state::leader &&
                      n.state.current_term == maximum && n.state.voted_for == 0,
                  "C single-voter fast path accepts the maximum term without increment");
        }
        {
            node n{1};
            d::start_event start;
            start.term = maximum - 1;
            start.entries.push_back(change(1, members()));
            ++events;
            check(!n.run(std::move(start)), "start maximum received-term case");
            d::request_vote request;
            request.term = maximum;
            request.candidate_id = 2;
            request.last_log_index = 1;
            request.last_log_term = 1;
            ++events;
            check(!n.run(d::receive_event{{2, "node-2", request}}) &&
                      n.state.current_term == maximum && n.state.voted_for == 2 &&
                      std::get<d::request_vote_result>(n.state.update.messages.front().payload)
                          .vote_granted,
                  "grants and records a valid maximum-term request");
        }
        std::cout << "core term contracts: 7 cases, " << events << " checked events\n";
    }

    void test_core_tracing() {
        node n{1};
        r::vector<r::string> cpp;
        unsigned diagnostics = 0;
        n.state.tracing = r::tracer{[&](const r::trace_event& event) {
            check(event.type == r::trace_diagnostic && event.info, "core emits typed diagnostics");
            const auto& value = *static_cast<const r::diagnostic_trace*>(event.info);
            check(value.location.line() && !r::string_view{value.location.file_name()}.empty(),
                  "core trace carries source location");
            ++diagnostics;
            if (value.message.starts_with("> "))
                cpp.emplace_back(value.message);
        }};
        n.elect(true, true);
        check(!n.run(d::persisted_entries_event{3}, n.state.now), "persist barrier with tracing");
        check(!n.run(d::submit_event{{command(n.state.current_term, "traced")}}, n.state.now),
              "submit traced command");
        check(!n.run(d::persisted_entries_event{4}, n.state.now), "persist traced command");
        check(!n.run(d::timeout_event{}, n.state.now + 1001), "trace quorum loss");
        check(cpp.size() >= 8 && diagnostics > cpp.size(),
              "exercised event summaries have the expected shape; detailed traces also emitted");
        n.state.tracing = r::tracer{[](const auto&) { throw std::runtime_error("observer"); }};
        check(!n.run(d::timeout_event{}, d::next_timeout(n.state)),
              "throwing observer preserves state invariants");
    }

    void test_core_election() {
        for (bool pre_vote : {false, true}) {
            for (bool pending : {false, true}) {
                node n{1};
                n.elect(pre_vote, pending);
                if (pending) {
                    check(n.state.update.entries.batch.size() == 1 &&
                              n.state.update.entries.batch[0]->type == r::entry_type::barrier,
                          "pending previous term causes barrier");
                    check(!n.run(d::persisted_entries_event{3}, n.state.now),
                          "persist initial barrier");
                }
                check(!n.run(d::timeout_event{}, n.state.now + 1001), "lost quorum steps down");
                check(std::holds_alternative<d::follower_state>(n.state.state),
                      "isolated leader becomes follower");
            }
        }
        node single{1};
        single.start(members(1));
        check(single.state.current_term == 1 && single.state.commit_index == 1 &&
                  std::holds_alternative<d::leader_state>(single.state.state),
              "single voter self-elects without term increment");
        node learner{9};
        learner.start(members());
        check(!learner.run(d::timeout_event{}, d::next_timeout(learner.state)),
              "nonmember timeout");
        check(std::holds_alternative<d::follower_state>(learner.state.state),
              "nonmember stays follower");
    }

    void test_core_replication() {
        node n{1};
        n.elect();
        const auto now = n.state.now;
        check(!n.run(d::submit_event{{command(2, "first"), command(2, "second")}}, now),
              "submit commands");
        auto held = n.state.update.entries.batch;
        check(held.size() == 2 && n.state.commit_index == 1,
              "submission does not commit before quorum");
        check(!n.run(d::persisted_entries_event{3}, now), "persist commands locally");
        d::append_entries_result ack;
        ack.term = 2;
        ack.last_log_index = 3;
        ack.features = d::feature_capacity;
        ack.capacity = 8192;
        check(!n.run(d::receive_event{{2, "node-2", ack}}, now + 1), "follower acknowledges");
        check(n.state.commit_index == 3, "majority commits current-term entries");
        check(!n.run(d::receive_event{{2, "node-2", ack}}, now + 2), "duplicate acknowledgment");
        n.state.capacity_threshold = 4096;
        ack.capacity = 0;
        check(!n.run(d::receive_event{{2, "node-2", ack}}, now + 3, 0), "low peer capacity");
        check(n.run(d::submit_event{{command(2, "no-space")}}, now + 4, 0) == r::errc::no_space,
              "capacity quorum refuses writes");
        n.state.capacity_threshold = 0;
        d::append_entries request;
        request.term = 3;
        request.prev_log_index = 3;
        request.prev_log_term = 2;
        request.leader_commit = 3;
        request.entries.push_back(std::make_shared<const r::entry>(command(3, "next-leader")));
        check(!n.run(d::receive_event{{2, "node-2", request}}, now + 5),
              "higher leader appends after stepdown");
        check(!n.run(d::persisted_entries_event{4}, now + 6),
              "follower sends success only after persistence");
        check(
            std::get<d::append_entries_result>(n.state.update.messages[0].payload).last_log_index ==
                4,
            "persist reply advertises stored prefix");
        check(held[0]->payload.bytes()[0] == r::byte{'f'},
              "retained old persistence batch remains valid");
    }

    void test_core_conflict() {
        node n{1};
        n.start(members());
        d::append_entries request;
        request.term = 2;
        request.prev_log_index = 1;
        request.prev_log_term = 1;
        request.leader_commit = 1;
        request.entries.push_back(std::make_shared<const r::entry>(command(2, "uncommitted")));
        check(!n.run(d::receive_event{{2, "node-2", request}}, 1), "receive new entry");
        auto retained = n.state.update.entries.batch;
        check(n.state.update.messages.empty(), "new append waits for disk");
        check(!n.run(d::receive_event{{2, "node-2", request}}, 2), "duplicate pending append");
        check(
            std::get<d::append_entries_result>(n.state.update.messages[0].payload).last_log_index ==
                1,
            "duplicate pending append does not acknowledge volatile entry");
        d::append_entries replacement;
        replacement.term = 3;
        replacement.prev_log_index = 1;
        replacement.prev_log_term = 1;
        replacement.leader_commit = 1;
        replacement.entries.push_back(std::make_shared<const r::entry>(command(3, "replacement")));
        check(!n.run(d::receive_event{{3, "node-3", replacement}}, 3),
              "replace conflicting uncommitted entry");
        check(retained[0]->term == 2 && n.state.cache.get(2)->term == 3,
              "retired and replacement batches are independent");
        check(!n.run(d::persisted_entries_event{2}, 4), "persist replacement");
        replacement.entries.clear();
        replacement.prev_log_index = 2;
        replacement.prev_log_term = 2;
        check(!n.run(d::receive_event{{3, "node-3", replacement}}, 5),
              "reject previous-term mismatch");
        const auto result = std::get<d::append_entries_result>(n.state.update.messages[0].payload);
        check(result.rejected == 2 && result.last_log_index == 1, "rejection gives bounded hint");
        replacement.prev_log_index = 1;
        replacement.prev_log_term = 99;
        check(n.run(d::receive_event{{3, "node-3", replacement}}, 6) == r::errc::shutdown,
              "committed conflict is fatal");
    }

    void test_core_membership() {
        node n{1};
        n.elect();
        const auto now = n.state.now;
        auto next = members();
        check(!next.add(4, "node-4", r::role::spare), "add spare member");
        check(!n.run(d::submit_event{{change(2, next)}}, now), "submit membership change");
        check(n.state.configuration_uncommitted_index == 2,
              "membership is immediately uncommitted");
        check(n.run(d::submit_event{{change(2, next)}}, now) == r::errc::cannot_change,
              "reject concurrent membership change");
        check(!n.run(d::persisted_entries_event{2}, now), "persist configuration");
        d::append_entries_result ack;
        ack.term = 2;
        ack.last_log_index = 2;
        check(!n.run(d::receive_event{{2, "node-2", ack}}, now), "commit configuration entry");
        check(!n.run(d::configuration_event{2, next}, now), "apply committed configuration");
        check(!n.run(d::catch_up_event{4}, now), "catch up spare");
        check(!n.run(d::receive_event{{4, "node-4", ack}}, now + 1), "spare catches up");
        check(std::get<d::leader_state>(n.state.state).progress[3].catch_up ==
                  d::catch_up_status::finished,
              "catch-up completion recorded");
        check(!n.run(d::transfer_event{2}, now + 2), "transfer to caught-up voter");
        check(std::get<d::leader_state>(n.state.state).transferring,
              "TimeoutNow starts leadership transfer");
        check(n.run(d::submit_event{{command(2, "blocked")}}, now + 3) == r::errc::not_leader,
              "transfer suspends submissions");
        // A newer leader evicts an uncommitted configuration, restoring the
        // committed snapshot of membership rather than the discarded one.
        node follower{1};
        follower.start(members());
        d::append_entries request;
        request.term = 2;
        request.prev_log_index = 1;
        request.prev_log_term = 1;
        request.leader_commit = 1;
        request.entries.push_back(std::make_shared<const r::entry>(change(2, next)));
        check(!follower.run(d::receive_event{{2, "node-2", request}}, 1),
              "receive uncommitted membership");
        request.term = 3;
        request.entries[0] = std::make_shared<const r::entry>(command(3, "rollback"));
        check(!follower.run(d::receive_event{{3, "node-3", request}}, 2),
              "rollback conflicting membership");
        check(follower.state.configuration.size() == 3 &&
                  follower.state.configuration_uncommitted_index == 0,
              "committed membership restored");
    }

    void test_core_snapshot() {
        node n{1};
        n.start(members());
        d::install_snapshot request;
        request.term = 3;
        request.last_index = 20;
        request.last_term = 2;
        request.configuration = members();
        request.configuration_index = 10;
        request.data = r::shared_buffer::copy(bytes("snapshot-state"));
        check(!n.run(d::receive_event{{2, "node-2", request}}, 1), "start installing snapshot");
        auto pending = std::move(n.state.update);
        check(n.state.snapshot_installing && pending.snapshot.metadata.index == 20,
              "snapshot persistence requested");
        check(!n.run(d::receive_event{{2, "node-2", request}}, 2),
              "ignore concurrent snapshot install");
        check(!n.run(d::persisted_snapshot_event{pending.snapshot.metadata, 0, true}, 3),
              "complete snapshot persistence");
        check(n.state.commit_index == 20 && n.state.last_stored == 20 &&
                  !n.state.snapshot_installing,
              "snapshot updates durable state after completion");
        check(!n.run(d::receive_event{{2, "node-2", request}}, 4),
              "existing snapshot acknowledged without another write");
        d::start_event start;
        start.term = 3;
        start.metadata = pending.snapshot.metadata;
        start.start_index = 21;
        node restored{1};
        check(!restored.run(start), "restore snapshot metadata at startup");
        check(restored.state.commit_index == 20, "startup snapshot is committed");
        node leader{1};
        leader.elect();
        const auto now = leader.state.now;
        check(!leader.run(d::submit_event{{command(2, "second")}}, now),
              "append before local snapshot");
        check(!leader.run(d::persisted_entries_event{2}, now), "persist before local snapshot");
        d::append_entries_result ack;
        ack.term = 2;
        ack.last_log_index = 2;
        check(!leader.run(d::receive_event{{2, "node-2", ack}}, now),
              "commit before local snapshot");
        d::snapshot_metadata metadata{2, 2, members(), 1};
        check(!leader.run(d::snapshot_event{metadata, 0}, now), "compact local snapshot");
        ack.rejected = 1;
        ack.last_log_index = 0;
        check(!leader.run(d::receive_event{{3, "node-3", ack}}, now + 1),
              "snapshot needed by lagging follower");
        check(std::holds_alternative<d::install_snapshot>(leader.state.update.messages[0].payload),
              "stable snapshot load directive emitted");
    }

    // Contracts from cowsql/raft test/integration/test_start.c, test_tick.c
    // and test_election.c; see LICENSE.md.
    void test_core_restore_contracts() {
        unsigned cases = 0, events = 0;
        auto step = [&](node& n, d::event_payload payload, d::time time = 0) {
            ++events;
            return n.run(std::move(payload), time);
        };
        for (unsigned kind = 0; kind < 8; ++kind) {
            node n{kind == 6 ? 2U : 1U};
            d::start_event start;
            if (kind != 0)
                start.term = kind == 2 || kind == 4 || kind == 7 ? 3 : 1;
            if (kind == 2 || kind == 4 || kind == 7) {
                start.metadata = d::snapshot_metadata{6, 2, members(2), 1};
                start.start_index = kind == 7 ? 4 : 7;
                start.voted_for = 2;
                if (kind == 4 || kind == 7) {
                    for (r::log_index i = start.start_index; i <= 8; ++i)
                        start.entries.push_back(command(2, "restored-suffix"));
                }
            } else if (kind >= 3) {
                auto configuration = members(kind == 5 || kind == 6 ? 1 : 2);
                if (kind == 6)
                    check(!configuration.add(2, "node-2", r::role::spare), "restored non-voter");
                start.entries.push_back(change(1, configuration));
            }
            check(!step(n, std::move(start)), "restore persisted state");
            if (kind == 0 || kind == 1) {
                check(n.state.last_stored == 0 && n.state.commit_index == 0 &&
                          n.state.configuration.empty(),
                      "empty disk or persisted term has no fabricated log");
            } else if (kind == 2 || kind == 4 || kind == 7) {
                check(n.state.commit_index == 6 && n.state.last_stored == (kind == 2 ? 6U : 8U) &&
                          n.state.voted_for == 2 && n.state.configuration_committed_index == 1,
                      "snapshot commits its boundary and preserves durable suffix and vote");
                d::append_entries heartbeat;
                heartbeat.term = 3;
                heartbeat.prev_log_index = n.state.last_stored;
                heartbeat.prev_log_term = 2;
                heartbeat.leader_commit = n.state.last_stored;
                check(!step(n, d::receive_event{{2, "node-2", heartbeat}}, 1) &&
                          n.state.commit_index == n.state.last_stored,
                      "restored prefix accepts matching leader");
            } else if (kind == 5) {
                check(r::state(n.state) == r::node_state::leader && n.state.current_term == 1,
                      "single voter self-elects without incrementing persisted term");
                check(!step(n, d::submit_event{{command(1, "alone")}}, 1) &&
                          !step(n, d::persisted_entries_event{2}, 2) && n.state.commit_index == 2,
                      "single voter makes progress after restart");
            } else {
                check(r::state(n.state) == r::node_state::follower && n.state.commit_index == 1,
                      "bootstrap voter quorum and non-voter wait as followers");
            }
            ++cases;
        }
        for (unsigned kind = 0; kind < 3; ++kind) {
            node n{3};
            auto configuration = members(2);
            if (kind != 2)
                check(
                    !configuration.add(3, "node-3", kind == 0 ? r::role::standby : r::role::spare),
                    "non-voting timeout configuration");
            d::start_event start;
            start.term = 1;
            start.entries.push_back(change(1, configuration));
            check(!step(n, std::move(start)), "start non-voting follower");
            for (unsigned round = 0; round < 2; ++round)
                check(!step(n, d::timeout_event{}, d::next_timeout(n.state)) &&
                          r::state(n.state) == r::node_state::follower && n.state.current_term == 1,
                      "standby/spare/absent member cannot campaign");
            ++cases;
        }
        node pending{1};
        d::start_event start;
        start.term = 1;
        start.entries.push_back(change(1, members()));
        check(!step(pending, std::move(start)), "start pending-write election follower");
        d::append_entries append;
        append.term = 2;
        append.prev_log_index = 1;
        append.prev_log_term = 1;
        append.entries.push_back(std::make_shared<const r::entry>(command(2, "not-durable")));
        check(!step(pending, d::receive_event{{2, "node-2", append}}, 1), "receive slow write");
        auto now = d::next_timeout(pending.state);
        check(!step(pending, d::timeout_event{}, now) &&
                  r::state(pending.state) == r::node_state::follower,
              "unpersisted suffix blocks candidacy");
        check(!step(pending, d::persisted_entries_event{2}, now + 1),
              "complete write before election");
        check(!step(pending, d::timeout_event{}, d::next_timeout(pending.state)) &&
                  r::state(pending.state) == r::node_state::candidate &&
                  pending.state.current_term == 3,
              "durable follower can start next election");
        check(!step(pending, d::timeout_event{}, d::next_timeout(pending.state)) &&
                  pending.state.current_term == 4,
              "candidate retries election when quorum does not respond");
        ++cases;
        std::cout << "restore contracts: " << cases << " cases, " << events << " checked events\n";
    }

    // Contracts from cowsql/raft test/integration/test_replication.c; see LICENSE.md.
    void test_core_replication_contracts() {
        unsigned cases = 0, events = 0;
        auto step = [&](node& n, d::event_payload payload, d::time time = 0) {
            ++events;
            return n.run(std::move(payload), time);
        };
        for (unsigned kind = 0; kind < 3; ++kind) {
            node n{1};
            auto initial = members();
            auto previous = initial;
            if (kind != 0)
                check(!previous.add(4, "node-4", r::role::standby), "previous configuration");
            auto next = previous;
            check(!next.add(5, "node-5", r::role::spare), "uncommitted configuration");
            d::start_event start;
            start.term = 2;
            if (kind == 2) {
                start.metadata = d::snapshot_metadata{2, 1, previous, 1};
                start.start_index = 3;
            } else {
                start.entries.push_back(change(1, initial));
                if (kind == 1)
                    start.entries.push_back(change(1, previous));
            }
            start.entries.push_back(change(2, next));
            check(!step(n, std::move(start)), "restore uncommitted membership");
            const auto replaced = n.state.trail.last_index();
            check(n.state.configuration_uncommitted_index == replaced &&
                      n.state.configuration.get(5),
                  "latest configuration is effective before commitment");
            d::append_entries request;
            request.term = 3;
            request.prev_log_index = replaced - 1;
            request.prev_log_term = 1;
            request.leader_commit = n.state.commit_index;
            request.entries.push_back(
                std::make_shared<const r::entry>(command(3, "replace-change")));
            check(!step(n, d::receive_event{{2, "node-2", request}}, 1),
                  "replace conflicting membership entry");
            check(std::ranges::equal(n.state.configuration.servers(), previous.servers(),
                                     [](const auto& a, const auto& b) {
                                         return a.id == b.id && a.address == b.address &&
                                                a.role == b.role;
                                     }) &&
                      n.state.configuration_uncommitted_index == 0 &&
                      n.state.configuration_committed_index == (kind == 1 ? 2U : 1U),
                  "rollback uses initial, previous log, or snapshot configuration");
            check(!step(n, d::persisted_entries_event{replaced}, 2),
                  "persist replacement after rollback");
            ++cases;
        }
        for (const bool lose_leader : {false, true}) {
            node n{1};
            d::start_event start;
            start.term = 1;
            start.entries.push_back(change(1, members()));
            check(!step(n, std::move(start)), "start delayed-ACK follower");
            d::append_entries request;
            request.term = 2;
            request.prev_log_index = 1;
            request.prev_log_term = 1;
            request.entries.push_back(
                std::make_shared<const r::entry>(command(2, "old-leader-entry")));
            check(!step(n, d::receive_event{{2, "node-2", request}}, 1),
                  "receive old leader's unpersisted entry");
            if (lose_leader) {
                d::request_vote vote;
                vote.term = 3;
                vote.candidate_id = 3;
                vote.last_log_index = 1;
                vote.last_log_term = 1;
                vote.disrupt_leader = true;
                check(!step(n, d::receive_event{{3, "node-3", vote}}, 2),
                      "higher term clears known leader");
            } else {
                request.entries.clear();
                request.term = 3;
                check(
                    !step(n, d::receive_event{{3, "node-3", request}}, 2) &&
                        std::get<d::append_entries_result>(n.state.update.messages.front().payload)
                                .last_log_index == 1,
                    "new leader heartbeat acknowledges only its matched prefix");
            }
            check(!step(n, d::persisted_entries_event{2}, 3), "old write completes under new term");
            check(n.state.last_stored == 2 &&
                      (lose_leader ? n.state.update.messages.empty()
                                   : n.state.update.messages.size() == 1 &&
                                         std::get<d::append_entries_result>(
                                             n.state.update.messages.front().payload)
                                                 .last_log_index == 1),
                  "completion cannot acknowledge unchecked old-leader suffix");
            ++cases;
        }
        node slow{1};
        slow.elect();
        events += 3;
        auto now = slow.state.now;
        check(!step(slow, d::submit_event{{command(2, "slow-local-disk")}}, ++now),
              "submit on slow leader");
        d::append_entries_result ack;
        ack.term = 2;
        ack.last_log_index = 2;
        check(!step(slow, d::receive_event{{2, "node-2", ack}}, ++now) &&
                  !step(slow, d::receive_event{{3, "node-3", ack}}, ++now) &&
                  slow.state.commit_index == 2 && slow.state.last_stored == 1,
              "remote quorum can commit before leader's local write");
        d::append_entries replacement;
        replacement.term = 3;
        replacement.prev_log_index = 2;
        replacement.prev_log_term = 2;
        replacement.leader_commit = 3;
        replacement.entries.push_back(
            std::make_shared<const r::entry>(command(3, "new-leader-entry")));
        check(!step(slow, d::receive_event{{2, "node-2", replacement}}, ++now) &&
                  slow.state.commit_index == 3 && slow.state.last_stored == 1,
              "higher leader extends committed but locally pending prefix");
        check(!step(slow, d::persisted_entries_event{3}, ++now) && slow.state.last_stored == 3,
              "local persistence catches up without reducing commit");
        ++cases;
        std::cout << "replication contracts: " << cases << " cases, " << events
                  << " checked events\n";
    }

    // Contracts from cowsql/raft test/integration/test_snapshot.c; see LICENSE.md.
    void test_core_snapshot_interleaving() {
        unsigned cases = 0, events = 0;
        auto same = [](auto left, auto right) { return std::ranges::equal(left, right); };
        auto step = [&](node& target, d::event_payload payload, d::time time) {
            ++events;
            return target.run(std::move(payload), time);
        };
        auto image = [&] {
            d::install_snapshot request;
            request.term = 3;
            request.last_index = 20;
            request.last_term = 2;
            request.configuration_index = 1;
            request.configuration = members();
            request.data = r::shared_buffer::copy(bytes("install-state"));
            return request;
        };
        auto snapshots = [](const auto& update) {
            return std::ranges::count_if(update.messages, [](const auto& message) {
                return std::holds_alternative<d::install_snapshot>(message.payload);
            });
        };
        for (unsigned kind = 0; kind < 6; ++kind) {
            node follower{1};
            ++events;
            follower.start(members());
            d::core_update outstanding;
            r::weak<const r::entry> old_header;
            if (kind == 5) {
                d::append_entries request;
                request.term = 2;
                request.prev_log_index = 1;
                request.prev_log_term = 1;
                request.entries.push_back(
                    std::make_shared<const r::entry>(command(2, "pending-disk")));
                check(!step(follower, d::receive_event{{2, "node-2", std::move(request)}}, 1),
                      "begin old entries write");
                outstanding = std::move(follower.state.update);
                old_header = outstanding.entries.batch.front();
                check(follower.state.last_stored == 1 && follower.state.trail.last_index() == 2,
                      "old write remains pending");
            }
            check(!step(follower, d::receive_event{{2, "node-2", image()}}, 2),
                  "begin snapshot installation");
            auto installation = std::move(follower.state.update);
            check(follower.state.snapshot_installing && follower.state.last_stored == 0 &&
                      installation.snapshot.metadata.index == 20,
                  "snapshot waits for persistence");
            switch (kind) {
            case 0: {
                auto other = image();
                other.last_index = 30;
                check(!step(follower, d::receive_event{{2, "node-2", std::move(other)}}, 3),
                      "ignore overlapping snapshot");
                check(!(follower.state.update.flags & d::update_snapshot) &&
                          follower.state.trail.snapshot_index() == 20,
                      "first snapshot remains selected");
                break;
            }
            case 1: {
                d::append_entries heartbeat;
                heartbeat.term = 3;
                heartbeat.prev_log_index = 20;
                heartbeat.prev_log_term = 2;
                heartbeat.leader_commit = 20;
                check(!step(follower, d::receive_event{{2, "node-2", heartbeat}}, 3),
                      "heartbeat during install");
                check(follower.state.update.messages.size() == 1 &&
                          std::get<d::append_entries_result>(
                              follower.state.update.messages.front().payload)
                                  .last_log_index == 0,
                      "heartbeat never acknowledges an unpersisted snapshot");
                break;
            }
            case 2: {
                d::append_entries request;
                request.term = 3;
                request.prev_log_index = 20;
                request.prev_log_term = 2;
                request.entries.push_back(std::make_shared<const r::entry>(command(3, "deferred")));
                check(!step(follower, d::receive_event{{2, "node-2", std::move(request)}}, 3),
                      "ignore entries during install");
                check(!(follower.state.update.flags & d::update_entries) &&
                          follower.state.update.messages.empty() &&
                          follower.state.trail.last_index() == 20,
                      "no concurrent entries write or premature reply");
                break;
            }
            case 3:
                check(!step(follower, d::timeout_event{}, 3000) &&
                          std::holds_alternative<d::follower_state>(follower.state.state) &&
                          follower.state.current_term == 3,
                      "installing voter does not start an election");
                break;
            case 4: {
                d::request_vote vote;
                vote.term = 4;
                vote.candidate_id = 3;
                vote.last_log_index = 20;
                vote.last_log_term = 2;
                vote.disrupt_leader = true;
                check(!step(follower, d::receive_event{{3, "node-3", vote}}, 3),
                      "new term while installing");
                check(follower.state.current_term == 4 &&
                          !std::get<d::request_vote_result>(
                               follower.state.update.messages.front().payload)
                               .vote_granted,
                      "installing node rejects disruptive vote after term update");
                d::append_entries heartbeat;
                heartbeat.term = 5;
                heartbeat.prev_log_index = 20;
                heartbeat.prev_log_term = 2;
                check(!step(follower, d::receive_event{{3, "node-3", heartbeat}}, 4),
                      "observe replacement leader");
                break;
            }
            case 5:
                check(!old_header.expired() &&
                          same(outstanding.entries.batch.front()->payload.bytes(),
                               bytes("pending-disk")),
                      "accepted old entries retain payload after snapshot replaces cache");
                break;
            }
            check(same(installation.snapshot.chunk.bytes(), bytes("install-state")),
                  "pending snapshot owns its bytes through later events");
            auto now = follower.state.now + 1;
            check(!step(follower,
                        d::persisted_snapshot_event{installation.snapshot.metadata, 0, true}, now),
                  "complete selected snapshot");
            check(!follower.state.snapshot_installing && follower.state.last_stored == 20 &&
                      follower.state.commit_index == 20,
                  "snapshot becomes durable at completion");
            check(follower.state.update.messages.size() == 1 &&
                      follower.state.update.messages.front().server_id == (kind == 4 ? 3U : 2U) &&
                      std::get<d::append_entries_result>(
                          follower.state.update.messages.front().payload)
                              .term == follower.state.current_term,
                  "completion acknowledges current leader and term");
            d::append_entries suffix;
            suffix.term = follower.state.current_term;
            suffix.prev_log_index = 20;
            suffix.prev_log_term = 2;
            suffix.leader_commit = 21;
            suffix.entries.push_back(
                std::make_shared<const r::entry>(command(suffix.term, "suffix")));
            check(!step(follower,
                        d::receive_event{{kind == 4 ? 3U : 2U, kind == 4 ? "node-3" : "node-2",
                                          std::move(suffix)}},
                        ++now) &&
                      !step(follower, d::persisted_entries_event{21}, ++now),
                  "persist suffix after installation");
            check(follower.state.last_stored == 21 && follower.state.commit_index == 21,
                  "suffix catches up after snapshot");
            if (kind == 5) {
                outstanding.entries.batch.clear();
                check(old_header.expired(), "old completion state releases the retired header");
            }
            ++cases;
        }
        for (unsigned kind = 0; kind < 3; ++kind) {
            node leader{1};
            r::configuration configuration;
            check(!configuration.add(1, "node-1", r::role::voter) &&
                      !configuration.add(2, "node-2", r::role::standby),
                  "snapshot sender configuration");
            leader.state.install_snapshot_timeout = 100;
            leader.state.heartbeat_timeout = 50;
            ++events;
            leader.start(configuration, true);
            check(!step(leader, d::snapshot_event{{2, 1, configuration, 1}, 0}, 1),
                  "compact sender prefix");
            d::append_entries_result rejection;
            rejection.term = 1;
            rejection.rejected = 2;
            rejection.last_log_index = 1;
            check(!step(leader, d::receive_event{{2, "node-2", rejection}}, 2) &&
                      snapshots(leader.state.update) == 1,
                  "online mismatch starts snapshot transfer");
            if (kind == 0) {
                check(!step(leader, d::timeout_event{}, 52) && snapshots(leader.state.update) == 0,
                      "do not retry snapshot before deadline");
                d::append_entries_result heartbeat;
                heartbeat.term = 1;
                heartbeat.last_log_index = 0;
                check(!step(leader, d::receive_event{{2, "node-2", heartbeat}}, 82),
                      "peer stays responsive while installation is pending");
                check(!step(leader, d::timeout_event{}, 102) && snapshots(leader.state.update) == 1,
                      "retry snapshot at install deadline");
            } else if (kind == 1) {
                check(!step(leader, d::timeout_event{}, 1003) &&
                          snapshots(leader.state.update) == 0,
                      "offline peer receives no new snapshot");
                check(std::get<d::leader_state>(leader.state.state).progress[1].mode ==
                          d::replication_mode::probe,
                      "offline snapshot transfer returns to probe");
            } else {
                rejection.rejected = 1;
                rejection.last_log_index = 0;
                check(!step(leader, d::receive_event{{2, "node-2", rejection}}, 3) &&
                          snapshots(leader.state.update) == 0,
                      "stale rejection does not abort active snapshot");
                rejection.rejected = 2;
                check(!step(leader, d::receive_event{{2, "node-2", rejection}}, 4) &&
                          snapshots(leader.state.update) == 1,
                      "matching rejection immediately retries snapshot");
            }
            ++cases;
        }
        std::cout << "snapshot interleaving: " << cases << " cases, " << events
                  << " checked events\n";
    }

    // Contracts from cowsql/raft test/integration/test_catch_up.c; see LICENSE.md.
    void test_core_catch_up_timeout() {
        unsigned cases = 0, events = 0;
        for (const unsigned limit : {0U, 90U, 5000U}) {
            node leader{1};
            r::configuration configuration;
            check(!configuration.add(1, "node-1", r::role::voter) &&
                      !configuration.add(2, "node-2", r::role::spare),
                  "unresponsive spare");
            ++events;
            leader.start(configuration, true);
            leader.state.max_catch_up_round_duration = limit;
            auto step = [&](d::event_payload payload, d::time time) {
                ++events;
                return leader.run(std::move(payload), time);
            };
            auto status = [&](r::catch_up_status expected) {
                auto actual = r::catch_up_status::none;
                check(!r::catch_up(leader.state, 2, actual) && actual == expected,
                      "catch-up query follows expected lifecycle");
            };
            check(!step(d::catch_up_event{2}, 1), "start unresponsive catch-up");
            status(r::catch_up_status::running);
            check(!step(d::timeout_event{}, 1 + limit), "exact duration boundary");
            status(r::catch_up_status::running);
            check(!step(d::timeout_event{}, 2 + limit), "expired duration boundary");
            status(r::catch_up_status::aborted);
            check(!step(d::catch_up_event{2}, 3 + limit), "retry expired catch-up");
            status(r::catch_up_status::running);
            d::append_entries_result ack;
            ack.term = 1;
            ack.last_log_index = 2;
            check(!step(d::receive_event{{2, "node-2", ack}}, 4 + limit),
                  "late peer catches up retry");
            status(r::catch_up_status::finished);
            ++cases;
        }
        std::cout << "catch-up timeout: " << cases << " cases, " << events << " checked events\n";
    }

    void test_core_transfer() {
        unsigned cases = 0, events = 0;
        auto step = [&](node& n, d::event_payload payload, d::time time) {
            ++events;
            return n.run(std::move(payload), time);
        };
        auto elect = [&](node& n, bool pre_vote, bool extras = false) {
            n.state.pre_vote = pre_vote;
            d::start_event start;
            start.term = 1;
            start.entries.push_back(change(1, members(3, extras)));
            check(!step(n, std::move(start), 0), "start transfer leader");
            const auto time = d::next_timeout(n.state);
            check(!step(n, d::timeout_event{}, time), "start transfer election");
            d::request_vote_result vote;
            vote.term = n.state.current_term + (pre_vote ? 1 : 0);
            vote.pre_vote = pre_vote;
            vote.vote_granted = true;
            check(!step(n, d::receive_event{{2, "node-2", vote}}, time), "elect transfer leader");
            if (pre_vote) {
                vote.pre_vote = false;
                vote.term = n.state.current_term;
                check(!step(n, d::receive_event{{2, "node-2", vote}}, time),
                      "complete transfer election");
            }
            check(r::state(n.state) == r::node_state::leader, "transfer leader elected");
            return time;
        };
        auto hints = [](const node& n) {
            return std::ranges::count_if(n.state.update.messages, [](const auto& message) {
                return std::holds_alternative<d::timeout_now>(message.payload);
            });
        };
        for (const unsigned caught_up : {0, 2, 3}) {
            node n{1};
            const auto now = elect(n, false, true);
            for (const r::server_id id : {1, 4, 5, 99}) {
                check(
                    step(n, d::transfer_event{id}, now) == r::errc::bad_id &&
                        r::transferee(n.state) == 0,
                    "self/non-voter/unknown transfer target is rejected without starting transfer");
            }
            if (caught_up) {
                d::append_entries_result ack;
                ack.term = n.state.current_term;
                ack.last_log_index = 1;
                check(!step(n,
                            d::receive_event{{caught_up, "node-" + std::to_string(caught_up), ack}},
                            now),
                      "selected voter catches up");
            }
            check(!step(n, d::transfer_event{0}, now),
                  "event-core zero ID automatically selects a voter");
            check(r::transferee(n.state) == (caught_up ? caught_up : 3) && !r::leader(n.state) &&
                      hints(n) == (caught_up ? 1 : 0),
                  "selection prefers first caught-up voter, otherwise last eligible voter");
            for (const r::server_id id : {0, 2, 99})
                check(step(n, d::transfer_event{id}, now) == r::errc::not_leader,
                      "pending transfer rejects another request before validating its target");
            ++cases;
        }
        for (const bool pre_vote : {false, true})
            for (const bool delayed : {false, true}) {
                node leader{1};
                const auto now = elect(leader, pre_vote);
                leader.state.max_inflight_entries = 1;
                d::submit_event batch;
                for (unsigned i = 0; i < 5; ++i)
                    batch.entries.push_back(command(2, "entry-" + std::to_string(i)));
                check(!step(leader, std::move(batch), now) &&
                          !step(leader, d::persisted_entries_event{6}, now),
                      "persist backlog before transfer");
                d::append_entries_result ack;
                ack.term = 2;
                ack.last_log_index = 6;
                check(!step(leader, d::receive_event{{3, "node-3", ack}}, now),
                      "other voter commits backlog");
                ack.last_log_index = 1;
                check(!step(leader, d::receive_event{{2, "node-2", ack}}, now),
                      "transfer target starts behind");
                check(!step(leader, d::transfer_event{2}, now + 1) && hints(leader) == 0,
                      "lagging transfer initially waits without a TimeoutNow");
                check(step(leader, d::submit_event{{command(2, "blocked")}}, now + 2) ==
                          r::errc::not_leader,
                      "pending transfer rejects new submissions");
                ack.last_log_index = 2;
                check(!step(leader, d::receive_event{{2, "node-2", ack}}, now + 3) &&
                          hints(leader) == 1,
                      "reference behavior sends one TimeoutNow on the first partial progress ACK");
                const auto hint =
                    std::ranges::find_if(leader.state.update.messages, [](const auto& message) {
                        return std::holds_alternative<d::timeout_now>(message.payload);
                    });
                const auto request = std::get<d::timeout_now>(hint->payload);
                check(request.last_log_index == 6 && request.last_log_term == 2,
                      "transfer hint advertises the leader's complete log boundary");

                node follower{2};
                follower.state.pre_vote = pre_vote;
                d::start_event start;
                start.term = 1;
                start.entries.push_back(change(1, members()));
                check(!step(follower, std::move(start), 0), "start transfer recipient");
                d::append_entries append;
                append.term = 2;
                append.prev_log_index = 1;
                append.prev_log_term = 1;
                append.leader_commit = 1;
                append.entries.push_back(
                    std::make_shared<const r::entry>(*leader.state.cache.get(2)));
                check(!step(follower, d::receive_event{{1, "node-1", append}}, now + 3) &&
                          !step(follower, d::persisted_entries_event{2}, now + 3),
                      "persist recipient's partial prefix");
                if (!delayed) {
                    check(!step(follower, d::receive_event{{1, "node-1", request}}, now + 4) &&
                              r::state(follower.state) == r::node_state::follower &&
                              follower.state.update.messages.empty(),
                          "recipient ignores a hint whose advertised log is not yet durable");
                }
                for (unsigned index = 3; index <= 6; ++index) {
                    ack.last_log_index = index;
                    check(!step(leader, d::receive_event{{2, "node-2", ack}}, now + 4 + index) &&
                              hints(leader) == 0,
                          "later ACKs do not resend a transfer already marked as transferring");
                }
                append.prev_log_index = 2;
                append.prev_log_term = 2;
                append.leader_commit = 6;
                append.entries.clear();
                for (unsigned index = 3; index <= 6; ++index)
                    append.entries.push_back(
                        std::make_shared<const r::entry>(*leader.state.cache.get(index)));
                check(!step(follower, d::receive_event{{1, "node-1", append}}, now + 11) &&
                          !step(follower, d::persisted_entries_event{6}, now + 11),
                      "recipient finishes persisting backlog");
                if (delayed) {
                    check(!step(follower, d::receive_event{{1, "node-1", request}}, now + 12),
                          "deliver delayed hint after catch-up");
                    const auto candidate = std::get_if<d::candidate_state>(&follower.state.state);
                    check(
                        candidate && candidate->disrupt_leader && !candidate->in_pre_vote &&
                            follower.state.current_term == 3,
                        "matching durable boundary starts disruptive election even with pre-vote enabled");
                    const auto vote = std::ranges::find_if(
                        follower.state.update.messages, [](const auto& message) {
                            return message.server_id == 1 &&
                                   std::holds_alternative<d::request_vote>(message.payload);
                        });
                    check(vote != follower.state.update.messages.end(),
                          "recipient requests old leader's vote");
                    check(
                        !step(leader,
                              d::receive_event{
                                  {2, "node-2", std::get<d::request_vote>(vote->payload)}},
                              now + 13) &&
                            r::state(leader.state) == r::node_state::follower &&
                            r::transferee(leader.state) == 0,
                        "higher-term disruptive request steps down old leader and clears transfer");
                    check(leader.state.update.messages.size() == 1,
                          "old leader replies to disruptive vote");
                    const auto result = std::get<d::request_vote_result>(
                        leader.state.update.messages.front().payload);
                    check(!step(follower, d::receive_event{{1, "node-1", result}}, now + 14) &&
                              r::state(follower.state) == r::node_state::leader,
                          "recipient wins the transfer election");
                } else {
                    const auto deadline = now + 1 + leader.state.election_timeout;
                    ack.last_log_index = 6;
                    check(!step(leader, d::receive_event{{3, "node-3", ack}}, deadline) &&
                              !step(leader, d::timeout_event{}, deadline),
                          "maintain quorum while transfer expires");
                    check(r::state(leader.state) == r::node_state::leader &&
                              r::transferee(leader.state) == 0 &&
                              !std::get<d::leader_state>(leader.state.state).transferring &&
                              r::leader(leader.state),
                          "expiry clears the transfer without claiming another leader was elected");
                    check(
                        !step(leader, d::receive_event{{2, "node-2", ack}}, deadline + 1) &&
                            hints(leader) == 0 &&
                            !step(leader, d::transfer_event{2}, deadline + 2) && hints(leader) == 1,
                        "caller may explicitly retry an expired transfer to the now-caught-up voter");
                }
                ++cases;
            }
        node single{1};
        d::start_event start;
        start.term = 1;
        start.entries.push_back(change(1, members(1)));
        check(!step(single, std::move(start), 0) &&
                  step(single, d::transfer_event{0}, 0) == r::errc::not_found,
              "single voter has no automatic transfer target");
        ++cases;
        node follower{1};
        d::start_event initial;
        initial.term = 1;
        initial.entries.push_back(change(1, members()));
        check(!step(follower, std::move(initial), 0) &&
                  step(follower, d::transfer_event{0}, 0) == r::errc::not_leader,
              "follower transfer is rejected");
        const auto time = d::next_timeout(follower.state);
        check(!step(follower, d::timeout_event{}, time) &&
                  step(follower, d::transfer_event{2}, time) == r::errc::not_leader,
              "candidate transfer is rejected");
        ++cases;
        std::cout << "core transfer: " << cases << " cases, " << events << " checked events\n";
    }

    void test_core_cluster() {
        for (bool pre_vote : {false, true}) {
            r::array<r::owned<node>, 3> nodes;
            r::array<r::log_index, 3> applied{1, 1, 1};
            for (unsigned i = 0; i < nodes.size(); ++i) {
                nodes[i] = std::make_unique<node>(i + 1);
                auto& n = *nodes[i];
                n.state.pre_vote = pre_vote;
                n.state.max_inflight_entries = 2;
                n.start(members());
            }
            struct packet {
                r::server_id target;
                d::message rpc;
            };
            r::deque<packet> queue;
            d::time now = 0;
            unsigned events = 0;
            r::server_id isolated = 0;
            std::mt19937 random{85213};
            auto collect = [&](auto&& self, node& n) -> void {
                auto update = std::move(n.state.update);
                for (auto& message : update.messages) {
                    if (auto request = std::get_if<d::append_entries>(&message.payload)) {
                        // Simulate transport decoding into independent headers.
                        for (auto& entry : request->entries)
                            entry = std::make_shared<const r::entry>(*entry);
                    }
                    const auto target = message.server_id;
                    message.server_id = n.state.id;
                    message.server_address = n.state.address;
                    queue.push_back({target, std::move(message)});
                }
                if (update.flags & d::update_entries) {
                    const auto last = update.entries.index + update.entries.batch.size() - 1;
                    check(!n.run(d::persisted_entries_event{last}, ++now),
                          "cluster persistence completion");
                    ++events;
                    self(self, n);
                }
                auto& index = applied[n.state.id - 1];
                while (index < n.state.commit_index) {
                    ++index;
                    const auto entry = n.state.cache.get(index);
                    check(entry != nullptr, "committed entry available for application");
                    if (entry->type == r::entry_type::change) {
                        r::configuration configuration;
                        check(!configuration.decode(entry->payload.bytes()),
                              "decode committed configuration");
                        check(
                            !n.run(d::configuration_event{index, std::move(configuration)}, ++now),
                            "cluster applies configuration");
                        ++events;
                        self(self, n);
                    }
                }
            };
            auto send = [&](node& n, auto&& event) {
                check(!n.run(std::forward<decltype(event)>(event), ++now), "cluster event");
                ++events;
                collect(collect, n);
            };
            auto drain = [&](bool unreliable = false) {
                unsigned deliveries = 0;
                while (!queue.empty()) {
                    check(++deliveries < 20000, "cluster message queue converges");
                    const auto i = unreliable ? random() % queue.size() : 0;
                    auto packet = std::move(queue[i]);
                    queue.erase(queue.begin() + static_cast<r::ptrdiff_t>(i));
                    if (packet.target == isolated || packet.rpc.server_id == isolated)
                        continue;
                    if (unreliable && random() % 7 == 0)
                        continue;
                    if (unreliable && random() % 19 == 0)
                        queue.push_back(packet);
                    auto& n = *nodes[packet.target - 1];
                    send(n, d::receive_event{std::move(packet.rpc)});
                }
            };
            auto leader = [&](r::server_id excluded = 0) -> node* {
                node* result = nullptr;
                for (auto& n : nodes) {
                    if (n->state.id != excluded &&
                        std::holds_alternative<d::leader_state>(n->state.state) &&
                        (result == nullptr || n->state.current_term > result->state.current_term))
                        result = n.get();
                }
                return result;
            };
            now = d::next_timeout(nodes[0]->state);
            send(*nodes[0], d::timeout_event{});
            drain();
            check(leader() != nullptr && leader()->state.id == 1, "cluster elects first leader");
            for (unsigned round = 0; round < 80; ++round) {
                auto n = leader();
                check(n != nullptr, "cluster retains a leader");
                d::submit_event batch;
                for (unsigned i = 0; i < 5; ++i)
                    batch.entries.push_back(
                        command(n->state.current_term,
                                "command-" + std::to_string(round) + "-" + std::to_string(i)));
                send(*n, std::move(batch));
                drain(true);
                now += 100;
                send(*n, d::timeout_event{});
                drain();
            }
            auto old = leader();
            check(old != nullptr, "leader available before transfer");
            const auto target = old->state.id == 2 ? 3 : 2;
            send(*old, d::transfer_event{static_cast<r::server_id>(target)});
            drain();
            check(leader() != nullptr && leader()->state.id == static_cast<r::server_id>(target),
                  "TimeoutNow transfers cluster leadership");
            old = leader();
            const auto committed = old->state.commit_index;
            r::vector<r::entry> prefix;
            for (r::log_index i = 1; i <= committed; ++i)
                prefix.push_back(*old->state.cache.get(i));
            isolated = old->state.id;
            send(*old, d::submit_event{{command(old->state.current_term, "isolated-write")}});
            drain();
            check(old->state.commit_index == committed, "isolated leader cannot commit alone");
            for (unsigned round = 0; round < 20 && leader(isolated) == nullptr; ++round) {
                for (auto& n : nodes) {
                    if (n->state.id == isolated)
                        continue;
                    now = std::max(now + 1, d::next_timeout(n->state));
                    send(*n, d::timeout_event{});
                    drain();
                    if (leader(isolated) != nullptr)
                        break;
                }
            }
            auto elected = leader(isolated);
            check(elected != nullptr && elected->state.current_term > old->state.current_term,
                  "majority partition elects newer leader");
            isolated = 0;
            now += 100;
            send(*elected, d::timeout_event{});
            drain();
            send(*elected, d::submit_event{{command(elected->state.current_term, "after-heal")}});
            drain();
            for (unsigned i = 0; i < 5; ++i) {
                now += 100;
                send(*elected, d::timeout_event{});
                drain();
            }
            const auto final_commit = elected->state.commit_index;
            for (auto& n : nodes) {
                check(n->state.commit_index == final_commit,
                      "healed cluster agrees on commit index");
                for (r::log_index i = 1; i <= final_commit; ++i) {
                    const auto value = n->state.cache.get(i);
                    const auto expected = elected->state.cache.get(i);
                    check(value != nullptr && expected != nullptr &&
                              value->term == expected->term &&
                              std::ranges::equal(value->payload.bytes(), expected->payload.bytes()),
                          "healed logs agree on committed bytes");
                    check(!std::ranges::equal(value->payload.bytes(), bytes("isolated-write")),
                          "isolated uncommitted write overwritten");
                    if (i <= prefix.size())
                        check(std::ranges::equal(value->payload.bytes(),
                                                 prefix[i - 1].payload.bytes()),
                              "previously committed prefix survives partition");
                }
            }
            check(events > 500, "cluster exercised substantial event sequences");
        }
    }

}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected a core test suite");
        const r::string_view suite{argv[1]};
        if (suite == "core_election")
            test_core_election();
        else if (suite == "core_term_contracts")
            test_core_term_contracts();
        else if (suite == "core_tracing")
            test_core_tracing();
        else if (suite == "core_replication")
            test_core_replication();
        else if (suite == "core_conflict")
            test_core_conflict();
        else if (suite == "core_membership")
            test_core_membership();
        else if (suite == "core_snapshot")
            test_core_snapshot();
        else if (suite == "core_snapshot_interleaving")
            test_core_snapshot_interleaving();
        else if (suite == "core_catch_up_timeout")
            test_core_catch_up_timeout();
        else if (suite == "core_restore_contracts")
            test_core_restore_contracts();
        else if (suite == "core_replication_contracts")
            test_core_replication_contracts();
        else if (suite == "core_transfer")
            test_core_transfer();
        else if (suite == "core_cluster")
            test_core_cluster();
        else
            throw std::runtime_error("unknown core test suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
