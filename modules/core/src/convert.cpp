// C++ port of cowsql/raft src/convert.c; see LICENSE.md.
#include "core_internal.hpp"

#include <cassert>

namespace raftpp::detail {

    error_code convert_to_candidate(core_state& core, bool disrupt_leader) noexcept {
        const auto self = core.configuration.get(core.id);
        if (!std::holds_alternative<follower_state>(core.state) || self == nullptr ||
            self->role != role::voter)
            return errc::invalid;
        // Keep the single-voter fast path: it never increments the term.
        if (core.configuration.voter_count() > 1 &&
            core.current_term == numeric_limits<term>::max())
            return errc::too_big;
        try {
            candidate_state candidate;
            candidate.votes.resize(core.configuration.voter_count());
            candidate.disrupt_leader = disrupt_leader;
            candidate.in_pre_vote = disrupt_leader ? false : core.pre_vote;
            core.state = std::move(candidate);
            core.update.flags |= update_state;
            core.tracing.log(trace_level::debug, "convert to candidate");
            if (core.configuration.voter_count() == 1)
                return convert_to_leader(core);
            return election_start(core);
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code convert_to_leader(core_state& core) noexcept {
        auto candidate = std::get_if<candidate_state>(&core.state);
        if (candidate == nullptr)
            return errc::invalid;
        leader_state leader;
        if (auto error = build_progress(core.configuration, core.id, core.trail.last_index(),
                                        core.last_stored, leader.progress))
            return error;
        const auto voters = core.configuration.voter_count();
        for (size_t i = 0; i < voters; ++i) {
            const auto j = core.configuration.actual_index_of_voter(i);
            leader.progress[j].features = candidate->votes[i].features;
            leader.progress[j].capacity = candidate->votes[i].capacity;
        }
        core.state = std::move(leader);
        core.update.flags |= update_state | update_timeout;
        core.tracing.log(trace_level::debug, "convert to leader");
        core.election_timer_start = core.now;
        if (voters == 1) {
            if (core.last_stored > core.commit_index) {
                core.commit_index = core.last_stored;
                core.update.flags |= update_commit_index;
            }
        } else if (core.trail.last_index() > core.commit_index || core.force_barrier) {
            try {
                const entry barrier{core.current_term, entry_type::barrier,
                                    shared_buffer{buffer{8}}};
                return submit_entries(core, {&barrier, 1});
            } catch (const bad_alloc&) {
                return errc::out_of_memory;
            } catch (const length_error&) {
                return errc::too_big;
            }
        }
        return {};
    }

}
