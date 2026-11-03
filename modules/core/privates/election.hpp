#ifndef RAFTPP_DETAIL_ELECTION_HPP
#define RAFTPP_DETAIL_ELECTION_HPP

#include <raftpp/detail/core_state.hpp>
#include <raftpp/detail/random.hpp>

namespace raftpp::detail {

    RAFTPP_API void election_update_randomized_timeout(core_state& core) noexcept;
    RAFTPP_API void election_reset_timer(core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API time election_timer_expiration(const core_state& core) noexcept;
    // Candidate votes must have one element per voter. Per-peer enqueue failures
    // are nonfatal, as in the C algorithm; term/vote/timer changes still apply.
    // An exhausted term returns too_big before starting or resetting the round.
    [[nodiscard]] RAFTPP_API error_code election_start(core_state& core) noexcept;
    // The receive handler checks terms and leader contact before calling this.
    [[nodiscard]] RAFTPP_API bool election_vote(core_state& core,
                                                const request_vote& request) noexcept;
    [[nodiscard]] RAFTPP_API bool election_tally(core_state& core, size_t voter_index,
                                                 size_t& votes, size_t& voters) noexcept;

}

#endif // RAFTPP_DETAIL_ELECTION_HPP
