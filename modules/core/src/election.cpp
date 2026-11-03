// C++ port of cowsql/raft src/election.c; see LICENSE.md.
#include "election.hpp"
#include "tracing.hpp"

#include "assert.hpp"

namespace raftpp::detail {

    namespace {

        unsigned& randomized_timeout(core_state& core) noexcept {
            if (auto state = std::get_if<follower_state>(&core.state)) {
                return state->randomized_election_timeout;
            }
            RAFTPP_ASSERT(std::holds_alternative<candidate_state>(core.state));
            return std::get<candidate_state>(core.state).randomized_election_timeout;
        }

    }

    void election_update_randomized_timeout(core_state& core) noexcept {
        RAFTPP_ASSERT(core.election_timeout > 0 &&
                      core.election_timeout <= numeric_limits<uint32_t>::max() / 2);
        randomized_timeout(core) =
            random_within_range(core.random, core.election_timeout, 2 * core.election_timeout);
    }

    void election_reset_timer(core_state& core) noexcept {
        election_update_randomized_timeout(core);
        core.election_timer_start = core.now;
        core.update.flags |= update_timeout;
    }

    time election_timer_expiration(const core_state& core) noexcept {
        if (auto state = std::get_if<follower_state>(&core.state)) {
            return core.election_timer_start + state->randomized_election_timeout;
        }
        RAFTPP_ASSERT(std::holds_alternative<candidate_state>(core.state));
        return core.election_timer_start +
               std::get<candidate_state>(core.state).randomized_election_timeout;
    }

    error_code election_start(core_state& core) noexcept {
        // Both real elections and pre-votes must advertise a future term.
        if (core.current_term == numeric_limits<term>::max())
            return errc::too_big;
        auto& state = std::get<candidate_state>(core.state);
        const auto voter_index = core.configuration.index_of_voter(core.id);
        logf(core.tracing, trace_level::debug, source_location::current(),
             "start %selection for term %llu", state.in_pre_vote ? "pre-" : "",
             static_cast<unsigned long long>(core.current_term + 1));
        RAFTPP_ASSERT(voter_index < core.configuration.voter_count());
        RAFTPP_ASSERT(state.votes.size() == core.configuration.voter_count());
        if (!state.in_pre_vote) {
            ++core.current_term;
            core.voted_for = core.id;
            core.update.flags |= update_current_term | update_voted_for;
        }
        election_reset_timer(core);
        for (size_t i = 0; i < state.votes.size(); ++i) {
            state.votes[i] =
                i == voter_index ? vote{true, feature_capacity, core.capacity} : vote{};
        }
        for (const auto& server : core.configuration.servers()) {
            if (server.id == core.id || server.role != role::voter)
                continue;
            request_vote request;
            request.term = core.current_term + (state.in_pre_vote ? 1 : 0);
            request.candidate_id = core.id;
            // Advertise persisted log data, never pending writes.
            request.last_log_index = core.last_stored;
            request.last_log_term = core.trail.term_of(core.last_stored);
            request.disrupt_leader = state.disrupt_leader;
            request.pre_vote = state.in_pre_vote;
            // Individual send failures do not stop a round, as in the reference.
            (void)enqueue_message(core.update, server.id, server.address, request);
        }
        return {};
    }

    bool election_vote(core_state& core, const request_vote& request) noexcept {
        const auto local = core.configuration.get(core.id);
        if (local == nullptr || local->role != role::voter) {
            core.tracing.log(trace_level::info, "  local server is not voting -> deny vote");
            return false;
        }
        if (!request.pre_vote && core.voted_for != 0 && core.voted_for != request.candidate_id) {
            logf(core.tracing, trace_level::info, source_location::current(),
                 "  already voted for server %llu -> deny vote",
                 static_cast<unsigned long long>(core.voted_for));
            return false;
        }
        const auto last_index = core.trail.last_index();
        if (last_index != 0) {
            const auto last_term = core.trail.last_term();
            if (request.last_log_term < last_term ||
                (request.last_log_term == last_term && request.last_log_index < last_index)) {
                core.tracing.log(trace_level::info,
                                 "  remote log is older or shorter -> deny vote");
                return false;
            }
        }
        if (!request.pre_vote) {
            core.update.flags |= update_current_term | update_voted_for | update_timeout;
            core.voted_for = request.candidate_id;
            core.election_timer_start = core.now;
        }
        core.tracing.log(trace_level::info, "  grant vote");
        return true;
    }

    bool election_tally(core_state& core, size_t voter_index, size_t& votes,
                        size_t& voters) noexcept {
        auto& state = std::get<candidate_state>(core.state);
        voters = core.configuration.voter_count();
        RAFTPP_ASSERT(state.votes.size() == voters && voter_index < voters);
        state.votes[voter_index].grant = true;
        votes = 0;
        for (const auto& vote : state.votes)
            if (vote.grant)
                ++votes;
        return votes > voters / 2;
    }

}
