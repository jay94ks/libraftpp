// C++ port of cowsql/raft src/recv.c, recv_request_vote.c and follower conversion
// in src/convert.c; see LICENSE.md.
#include "receive.hpp"
#include "election.hpp"
#include "tracing.hpp"

#include "assert.hpp"

namespace raftpp::detail {

    void convert_to_follower(core_state& core) noexcept {
        RAFTPP_ASSERT(!std::holds_alternative<follower_state>(core.state));
        // Destroy the old role's votes/progress through the standard variant.
        core.state.emplace<follower_state>();
        core.tracing.log(trace_level::debug, "convert to follower");
        core.update.flags |= update_state;
        election_reset_timer(core);
    }

    int check_matching_terms(const core_state& core, term term) noexcept {
        return term < core.current_term ? -1 : term > core.current_term ? 1 : 0;
    }

    void bump_current_term(core_state& core, term term) noexcept {
        RAFTPP_ASSERT(term > core.current_term);
        logf(core.tracing, trace_level::info, source_location::current(),
             "  bump term from %llu to %llu%s", static_cast<unsigned long long>(core.current_term),
             static_cast<unsigned long long>(term),
             std::holds_alternative<follower_state>(core.state) ? "" : ", step down");
        core.current_term = term;
        core.voted_for = 0;
        core.update.flags |= update_current_term | update_voted_for;
        if (!std::holds_alternative<follower_state>(core.state))
            convert_to_follower(core);
        std::get<follower_state>(core.state).match = 0;
    }

    int ensure_matching_terms(core_state& core, term term) noexcept {
        const auto match = check_matching_terms(core, term);
        if (match > 0)
            bump_current_term(core, term);
        return match;
    }

    error_code receive_vote_request(core_state& core, server_id id, string_view address,
                                    const request_vote& request) noexcept {
        RAFTPP_ASSERT(id != 0);
        request_vote_result result;
        result.pre_vote = request.pre_vote;
        const auto follower = std::get_if<follower_state>(&core.state);
        const bool has_leader = std::holds_alternative<leader_state>(core.state) ||
                                (follower != nullptr && follower->leader_id != 0);
        // Leader contact suppresses disruptive term changes as well as votes.
        if (!has_leader || request.disrupt_leader) {
            const auto match = request.pre_vote ? check_matching_terms(core, request.term)
                                                : ensure_matching_terms(core, request.term);
            if (!core.snapshot_installing && match >= 0) {
                result.vote_granted = election_vote(core, request);
            }
        }
        // A pre-vote response echoes the future term without persisting it.
        result.term = request.pre_vote ? request.term : core.current_term;
        result.features = feature_capacity;
        result.capacity = core.capacity;
        return enqueue_message(core.update, id, address, result);
    }

}
