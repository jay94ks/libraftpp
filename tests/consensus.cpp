#include "election.hpp"
#include "receive.hpp"

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
        if (!condition) {
            throw std::runtime_error(r::string{location.file_name()} + ":" +
                                     std::to_string(location.line()) + ": " + r::string{message});
        }
    }

    void configure(d::core_state& state) {
        state.id = 1;
        for (const auto& [id, role] :
             r::array{r::pair{2, r::role::standby}, r::pair{1, r::role::voter},
                      r::pair{3, r::role::spare}, r::pair{4, r::role::voter},
                      r::pair{5, r::role::voter}})
            check(!state.configuration.add(id, "node-" + std::to_string(id), role),
                  "configure election members");
    }
    void candidate(d::core_state& state, bool pre_vote, bool disrupt) {
        auto& value = state.state.emplace<d::candidate_state>();
        value.in_pre_vote = pre_vote;
        value.disrupt_leader = disrupt;
        value.votes.resize(state.configuration.voter_count());
    }

    void test_random() {
        std::mt19937 random{81413};
        for (r::uint32_t seed : {0U, 1U, 1729U, 4294967295U}) {
            auto first = seed, second = seed;
            for (unsigned i = 0; i < 20000; ++i) {
                const auto low = static_cast<r::uint32_t>(random());
                const auto room = r::numeric_limits<r::uint32_t>::max() - low;
                if (!room)
                    continue;
                const auto high = static_cast<r::uint32_t>(low + 1 + random() % room);
                const auto value = d::random_within_range(first, low, high);
                check(value >= low && value <= high &&
                          value == d::random_within_range(second, low, high) && first == second,
                      "seeded range and reproducibility");
            }
            for (unsigned i = 0; i < 100; ++i)
                check(d::random_within_range(first, 0, r::numeric_limits<r::uint32_t>::max()) ==
                          d::random_within_range(second, 0, r::numeric_limits<r::uint32_t>::max()),
                      "full range remains reproducible");
        }
        // Fixed outputs additionally prevent a different PRNG from passing replay checks.
        r::uint32_t seed = 0;
        constexpr r::array<r::uint32_t, 4> expected{1571, 1410, 1735, 1743};
        for (const auto value : expected)
            check(d::random_within_range(seed, 1000, 2000) == value, "fixed PRNG vectors");
        r::uint32_t state = 7;
        check(d::random_within_range(state, 9, 9) == 9 && state == 7,
              "singleton range preserves state");
    }

    void test_progress() {
        d::core_state state;
        configure(state);
        auto& progress = state.state.emplace<d::leader_state>().progress;
        check(!d::build_progress(state.configuration, 1, 1000, 995, progress),
              "build member progress");
        check(progress.size() == 5 && progress[1].match_index == 995,
              "local progress starts at durable prefix");
        auto& peer = progress[0];
        check(peer.next_index == 1001 && peer.match_index == 0 && peer.state_name() == "probe",
              "remote peer initially probes");
        for (r::log_index index = 1; index <= 1000; ++index) {
            const auto before = peer.match_index;
            check(peer.maybe_update(index) && peer.match_index == index &&
                      peer.match_index >= before,
                  "ACK advances matching prefix monotonically");
            check(!peer.maybe_update(index) && !peer.maybe_update(index - 1),
                  "duplicate and stale ACKs preserve prefix");
        }
        check(peer.up_to_date(1000), "full prefix is current");
        peer = d::progress{1000};
        peer.sent(1999900, state.update);
        peer.to_snapshot(200);
        peer.snapshot.last_send = 2000000;
        for (auto elapsed : {29999U, 30000U, 30001U}) {
            auto value = peer;
            const auto replicate = value.should_replicate(1000, 2000000 + elapsed, 100, 30000);
            check((value.mode != d::replication_mode::snapshot) == (elapsed >= 30000),
                  "snapshot deadline before/at/after boundary");
            (void)replicate;
        }
        peer.abort_snapshot();
        peer.to_pipeline();
        peer.match_index = 10;
        peer.next_index = 20;
        check(!peer.maybe_decrement(10, 9) && peer.match_index == 10,
              "stale rejection preserves acknowledged data");
        check(peer.maybe_decrement(19, 12) && peer.mode == d::replication_mode::probe &&
                  peer.next_index == 11,
              "pipeline rejection returns to probing durable prefix");
        check(peer.last_message_sent() == 2000000,
              "aborted snapshot retains last transmission time");
        peer.sent(2000500, state.update);
        peer.last_recv = 2000600;
        check(peer.last_message_sent() == 2000500 && peer.online(2000601, 1000) &&
                  !peer.online(2001601, 1000),
              "send/receive timestamps and online expiry");
        check(peer.contacted_recently(2000600) && !peer.contacted_recently(2000601),
              "recent contact boundary");
        peer.catch_up_start();
        check(peer.catch_up == r::catch_up_status::running, "start catch-up");
        peer.catch_up_abort();
        check(peer.catch_up == r::catch_up_status::aborted, "abort catch-up");
        peer.catch_up_start();
        peer.catch_up_finish();
        check(peer.catch_up == r::catch_up_status::finished, "retry and finish catch-up");
        r::configuration next;
        for (auto id : {5, 7, 1, 2})
            check(!next.add(id, "node-" + std::to_string(id), r::role::voter), "next membership");
        const auto retained = peer;
        check(!d::rebuild_progress(state.configuration, next, 1000, progress),
              "rebuild reordered membership");
        check(progress[3].match_index == retained.match_index &&
                  progress[3].last_recv == retained.last_recv && progress[1].match_index == 0 &&
                  progress[1].next_index == 1001,
              "existing ID retains state and new ID starts probing");
        const auto saved = progress;
        check(d::rebuild_progress(state.configuration, next, 1000, progress) == r::errc::invalid &&
                  progress.size() == saved.size() && progress[0].next_index == saved[0].next_index,
              "mismatched rebuild preserves output");
        check(d::build_progress(next, 1, r::numeric_limits<r::log_index>::max(), 0, progress) ==
                      r::errc::too_big &&
                  progress.size() == saved.size(),
              "unrepresentable next index preserves output");
    }

    void test_election() {
        for (const bool pre_vote : {false, true}) {
            d::core_state state;
            configure(state);
            check(!state.trail.restore(8, 3) && !state.trail.append(4),
                  "prepare pending election suffix");
            state.last_stored = 8;
            state.current_term = 5;
            state.voted_for = 99;
            state.random = 1729;
            state.capacity = 4096;
            candidate(state, pre_vote, !pre_vote);
            for (unsigned round = 0; round < 200; ++round) {
                state.now = round * 10000;
                state.update = {};
                const auto before = state.current_term;
                check(!d::election_start(state), "start election round");
                check(state.current_term == before + !pre_vote &&
                          state.voted_for == (pre_vote ? 99U : 1U),
                      "pre-vote leaves persistent vote and term unchanged");
                check(d::election_timer_expiration(state) >= state.now + 1000 &&
                          d::election_timer_expiration(state) <= state.now + 2000,
                      "randomized election deadline");
                check(state.update.messages.size() == 2, "request only remote voters");
                for (r::size_t i = 0; i < 2; ++i) {
                    const auto& message = state.update.messages[i];
                    const auto& request = std::get<d::request_vote>(message.payload);
                    check(message.server_id == i + 4 &&
                              message.server_address == "node-" + std::to_string(i + 4),
                          "remote voter destination");
                    check(request.candidate_id == 1 &&
                              request.term == state.current_term + pre_vote &&
                              request.pre_vote == pre_vote && request.disrupt_leader == !pre_vote &&
                              request.last_log_index == 8 && request.last_log_term == 3,
                          "request advertises durable prefix and election phase");
                }
                r::size_t votes = 0, voters = 0;
                check(!d::election_tally(state, 0, votes, voters) && votes == 1 && voters == 3,
                      "self vote alone is minority");
                check(d::election_tally(state, 1, votes, voters) && votes == 2,
                      "second distinct voter reaches quorum");
                check(d::election_tally(state, 1, votes, voters) && votes == 2,
                      "duplicate vote counts once");
                check(d::election_tally(state, 2, votes, voters) && votes == 3,
                      "third voter counts");
            }
            auto messages = std::move(state.update.messages);
            state.configuration = {};
            check(messages[0].server_address == "node-4", "queued address outlives configuration");
        }
        d::core_state state;
        for (unsigned i = 0; i < 1000; ++i) {
            state.now = i * 500;
            state.election_timeout = 1 + i;
            d::election_reset_timer(state);
            check(state.election_timer_start == state.now &&
                      d::election_timer_expiration(state) >= state.now + state.election_timeout &&
                      d::election_timer_expiration(state) <= state.now + 2 * state.election_timeout,
                  "follower reset captures clock and bounded random deadline");
        }
    }

    void test_vote() {
        std::mt19937 random{82713};
        for (const auto role : {r::role::voter, r::role::standby, r::role::spare}) {
            d::core_state state;
            state.id = 1;
            check(!state.configuration.add(1, "one", role), "local membership");
            for (unsigned i = 0; i < 5000; ++i) {
                const auto index = 10 + random() % 30, term = 1 + random() % 6;
                check(!state.trail.restore(index, term), "local log snapshot");
                const auto previous_vote = state.voted_for = random() % 5;
                state.now = i;
                state.update = {};
                d::request_vote request;
                request.candidate_id = 1 + random() % 4;
                request.last_log_index = random() % 50;
                request.last_log_term = random() % 8;
                request.pre_vote = random() % 2;
                const bool eligible =
                    role == r::role::voter &&
                    (request.pre_vote || !previous_vote || previous_vote == request.candidate_id);
                const bool fresh =
                    request.last_log_term > term ||
                    (request.last_log_term == term && request.last_log_index >= index);
                const bool granted = d::election_vote(state, request);
                check(granted == (eligible && fresh),
                      "voting role, one vote per term, and log freshness");
                check(state.voted_for ==
                          (granted && !request.pre_vote ? request.candidate_id : previous_vote),
                      "only granted real vote changes persisted selection");
                if (request.pre_vote)
                    check(state.update.flags == 0, "pre-vote has no persistence effects");
            }
        }
        d::core_state state;
        state.id = 1;
        d::request_vote request;
        request.candidate_id = 2;
        check(!d::election_vote(state, request), "absent local server cannot vote");
        check(!state.configuration.add(1, "one", r::role::voter) &&
                  d::election_vote(state, request),
              "empty voter log grants eligible vote");
    }

    void test_messages() {
        d::core_update update;
        r::weak<const r::byte> lifetime;
        {
            auto storage = r::buffer{16};
            storage.bytes()[5] = r::byte{42};
            r::shared_buffer batch{std::move(storage)};
            lifetime = batch.storage();
            d::append_entries request;
            request.entries.push_back(std::make_shared<const r::entry>(
                r::entry{1, r::entry_type::command, batch.slice(4, 4)}));
            r::string address = "original-node";
            check(!d::enqueue_message(update, 9, address, std::move(request)),
                  "enqueue owned append request");
            address = "changed-node";
        }
        check(!lifetime.expired() && update.messages[0].server_address == "original-node",
              "queue owns address and shared entry storage");
        auto copied = update.messages;
        update.messages.clear();
        check(!lifetime.expired() &&
                  std::get<d::append_entries>(copied[0].payload).entries[0]->payload.bytes()[1] ==
                      r::byte{42},
              "copied message retains complete batch");
        copied.clear();
        check(lifetime.expired(), "message destruction releases final batch reference");
        d::install_snapshot snapshot;
        check(!snapshot.configuration.add(1, "snapshot-node", r::role::voter),
              "snapshot message configuration");
        check(!d::enqueue_message(update, 10, "receiver", std::move(snapshot)),
              "enqueue snapshot message");
        auto saved = update.messages;
        update.messages.clear();
        check(std::get<d::install_snapshot>(saved[0].payload).configuration.get(1)->address ==
                  "snapshot-node",
              "snapshot message owns its configuration");
    }

    void test_vote_request() {
        std::mt19937 random{71691};
        for (unsigned i = 0; i < 3000; ++i) {
            d::core_state state;
            configure(state);
            check(!state.trail.restore(10, 3), "prepare vote log");
            state.last_stored = 10;
            state.current_term = 5;
            state.voted_for = random() % 3;
            state.random = random();
            state.now = i * 1000;
            state.capacity = 4096;
            state.election_timer_start = i;
            state.snapshot_installing = random() % 3 == 0;
            const auto kind = random() % 3;
            if (kind == 0) {
                if (random() % 2)
                    std::get<d::follower_state>(state.state).leader_id = 4;
                std::get<d::follower_state>(state.state).match = 9;
            } else if (kind == 1)
                candidate(state, random() % 2, false);
            else {
                auto& peers = state.state.emplace<d::leader_state>().progress;
                check(!d::build_progress(state.configuration, 1, 10, 10, peers), "leader progress");
            }
            d::request_vote request;
            request.term = 4 + random() % 3;
            request.candidate_id = 4;
            request.last_log_index = 9 + random() % 3;
            request.last_log_term = 2 + random() % 3;
            request.pre_vote = random() % 2;
            request.disrupt_leader = random() % 2;
            const bool has_leader =
                kind == 2 || (kind == 0 && std::get<d::follower_state>(state.state).leader_id != 0);
            const auto previous_vote = state.voted_for;
            const bool process = !has_leader || request.disrupt_leader;
            const bool bump = process && !request.pre_vote && request.term > 5;
            const bool can_vote =
                request.pre_vote || bump || !previous_vote || previous_vote == request.candidate_id;
            const bool fresh = request.last_log_term > 3 ||
                               (request.last_log_term == 3 && request.last_log_index >= 10);
            const bool granted =
                process && !state.snapshot_installing && request.term >= 5 && can_vote && fresh;
            check(!d::receive_vote_request(state, 4, "node-4", request), "receive vote request");
            check(state.current_term == (bump ? request.term : 5) &&
                      state.update.messages.size() == 1,
                  "real higher term persists before exactly one reply");
            const auto& message = state.update.messages.front();
            const auto& reply = std::get<d::request_vote_result>(message.payload);
            check(message.server_id == 4 && message.server_address == "node-4" &&
                      reply.term == (request.pre_vote ? request.term : state.current_term) &&
                      reply.pre_vote == request.pre_vote && reply.vote_granted == granted &&
                      reply.capacity == 4096,
                  "reply destination, phase, term, vote and capacity");
            check(state.voted_for == (granted && !request.pre_vote ? 4
                                      : bump                       ? 0
                                                                   : previous_vote),
                  "vote persistence follows accepted real election");
            if (bump)
                check(std::holds_alternative<d::follower_state>(state.state) &&
                          (state.update.flags & d::update_current_term),
                      "higher real term demotes and requests persistence");
        }
    }

}

int main(int argc, char** argv) {
    try {
        if (argc != 2)
            throw std::runtime_error("expected a consensus test suite");
        const r::string_view suite{argv[1]};
        if (suite == "random")
            test_random();
        else if (suite == "progress")
            test_progress();
        else if (suite == "election")
            test_election();
        else if (suite == "vote")
            test_vote();
        else if (suite == "vote_request")
            test_vote_request();
        else if (suite == "messages")
            test_messages();
        else
            throw std::runtime_error("unknown consensus test suite");
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
