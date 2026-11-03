// C++ port of cowsql/raft src/membership.c; see LICENSE.md.
#include "core_internal.hpp"

namespace raftpp::detail {

    error_code uncommitted_change(core_state& core, log_index index, const entry& entry) noexcept {
        configuration next;
        if (auto error = next.decode(entry.payload.bytes()))
            return error;
        if (auto leader = std::get_if<leader_state>(&core.state)) {
            // Public membership operations change one member at a time. Preserve
            // the reference's rule to rebuild only when the count changes.
            if (next.size() != core.configuration.size()) {
                if (auto error = rebuild_progress(core.configuration, next, core.trail.last_index(),
                                                  leader->progress))
                    return error;
            }
        }
        core.configuration = std::move(next);
        core.configuration_uncommitted_index = index;
        return {};
    }

    error_code rollback_configuration(core_state& core) noexcept {
        try {
            auto committed = core.configuration_committed;
            core.configuration = std::move(committed);
            core.configuration_uncommitted_index = 0;
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    void update_catch_up(core_state& core) noexcept {
        auto& leader = std::get<leader_state>(core.state);
        const auto i = core.configuration.index_of(leader.promotee_id);
        auto& progress = leader.progress[i];
        if (progress.match_index < leader.round_index)
            return;
        if (progress.match_index == core.trail.last_index() ||
            core.now - leader.round_start < core.election_timeout) {
            leader.round_number = 0;
            leader.round_index = 0;
            leader.round_start = 0;
            progress.catch_up_finish();
            leader.promotee_id = 0;
        } else {
            ++leader.round_number;
            leader.round_index = core.trail.last_index();
            leader.round_start = core.now;
        }
    }

    error_code transfer_start(core_state& core) noexcept {
        auto& leader = std::get<leader_state>(core.state);
        const auto server = core.configuration.get(leader.transferee);
        if (server == nullptr)
            return errc::bad_id;
        timeout_now request;
        request.term = core.current_term;
        request.last_log_index = core.trail.last_index();
        request.last_log_term = core.trail.last_term();
        if (auto error = enqueue_message(core.update, server->id, server->address, request))
            return error;
        leader.transferring = true;
        return {};
    }

    error_code apply_configuration(core_state& core, const configuration_event& event) noexcept {
        if (event.index == 0)
            return errc::invalid;
        if (core.configuration_uncommitted_index != event.index)
            return {};
        try {
            auto committed = event.configuration;
            core.configuration_uncommitted_index = 0;
            core.configuration_committed_index = event.index;
            core.configuration_committed = std::move(committed);
            if (std::holds_alternative<leader_state>(core.state)) {
                const auto self = core.configuration.get(core.id);
                if (self == nullptr || self->role != role::voter)
                    convert_to_follower(core);
            }
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

    error_code take_snapshot(core_state& core, const snapshot_event& event) noexcept {
        if (event.metadata.index == 0 || core.trail.term_of(event.metadata.index) == 0)
            return errc::invalid;
        try {
            if (event.metadata.configuration_index > core.configuration_committed_index) {
                auto committed = event.metadata.configuration;
                core.configuration_committed = std::move(committed);
            }
            core.configuration_last_snapshot_index = event.metadata.configuration_index;
            if (auto error = core.trail.snapshot(event.metadata.index, event.trailing))
                return error;
            return core.cache.snapshot(event.metadata.index, event.trailing);
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (const length_error&) {
            return errc::too_big;
        }
    }

}
