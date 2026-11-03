// C++ port of cowsql/raft src/recv_request_vote_result.c, recv_timeout_now.c
// and RPC dispatch in src/recv.c; see LICENSE.md.
#include "core_internal.hpp"

namespace raftpp::detail {

    namespace {

        error_code receive(core_state& core, server_id id, string_view address,
                           const request_vote& request) {
            return receive_vote_request(core, id, address, request);
        }

        error_code receive(core_state& core, server_id id, string_view,
                           const request_vote_result& result) {
            const auto candidate = std::get_if<candidate_state>(&core.state);
            // Pre-vote replies echo a proposed future term. Once this node is
            // no longer a candidate, a delayed reply cannot advance its term.
            if (candidate == nullptr && result.version > 1 && result.pre_vote)
                return {};
            const bool pre_vote = candidate != nullptr && candidate->in_pre_vote;
            const auto match = pre_vote ? check_matching_terms(core, result.term)
                                        : ensure_matching_terms(core, result.term);
            const auto state = std::get_if<candidate_state>(&core.state);
            if (state == nullptr)
                return {};
            const auto i = core.configuration.index_of_voter(id);
            if (i == core.configuration.size() || match < 0)
                return {};
            if (result.version > 1 && result.pre_vote != state->in_pre_vote)
                return {};
            if (state->in_pre_vote && match > 0 && result.term - core.current_term > 1) {
                bump_current_term(core, result.term);
                return {};
            }
            state->votes[i].features = result.features;
            state->votes[i].capacity = result.capacity;
            if (result.vote_granted) {
                size_t votes, voters;
                if (election_tally(core, i, votes, voters)) {
                    if (state->in_pre_vote) {
                        if (core.current_term == numeric_limits<term>::max())
                            return errc::too_big;
                        state->in_pre_vote = false;
                        return election_start(core);
                    } else {
                        if (auto error = convert_to_leader(core))
                            return error;
                        replication_heartbeat(core);
                    }
                }
            }
            return {};
        }

        error_code receive(core_state& core, server_id id, string_view address,
                           const append_entries& request) {
            return receive_append(core, id, address, request);
        }

        error_code receive(core_state& core, server_id id, string_view,
                           const append_entries_result& result) {
            return receive_append_result(core, id, result);
        }

        error_code receive(core_state& core, server_id id, string_view address,
                           const install_snapshot& request) {
            return receive_snapshot(core, id, address, request);
        }

        error_code receive(core_state& core, server_id id, string_view,
                           const timeout_now& request) {
            const auto self = core.configuration.get(core.id);
            const auto follower = std::get_if<follower_state>(&core.state);
            if (self == nullptr || self->role != role::voter || follower == nullptr ||
                follower->leader_id != id)
                return {};
            if (ensure_matching_terms(core, request.term) < 0)
                return {};
            if (core.trail.last_index() != request.last_log_index ||
                core.trail.last_term() != request.last_log_term ||
                core.last_stored < core.trail.last_index() || core.snapshot_installing)
                return {};
            return convert_to_candidate(core, true);
        }

    }

    error_code receive_message(core_state& core, const message& message) noexcept {
        if (message.server_id == 0 || message.payload.valueless_by_exception())
            return errc::malformed;
        return std::visit(
            [&](const auto& payload) {
                return receive(core, message.server_id, message.server_address, payload);
            },
            message.payload);
    }

}
