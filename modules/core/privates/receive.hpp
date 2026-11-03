#ifndef RAFTPP_DETAIL_RECEIVE_HPP
#define RAFTPP_DETAIL_RECEIVE_HPP

#include <raftpp/detail/core_state.hpp>

namespace raftpp::detail {

    RAFTPP_API void convert_to_follower(core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API int check_matching_terms(const core_state& core, term term) noexcept;
    RAFTPP_API void bump_current_term(core_state& core, term term) noexcept;
    [[nodiscard]] RAFTPP_API int ensure_matching_terms(core_state& core, term term) noexcept;
    [[nodiscard]] RAFTPP_API error_code receive_vote_request(core_state& core, server_id id,
                                                             string_view address,
                                                             const request_vote& request) noexcept;

}

#endif // RAFTPP_DETAIL_RECEIVE_HPP
