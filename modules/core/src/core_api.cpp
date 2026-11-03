// C++ event-API settings from cowsql/raft src/raft.c; see LICENSE.md.
#include <raftpp/core.hpp>
#include "election.hpp"

namespace raftpp {
    namespace {
        error_code find_progress(const core_state& core, server_id id,
                                 borrowed<const detail::progress>& output) noexcept {
            const auto value = std::get_if<leader_state>(&core.state);
            if (!value)
                return errc::not_leader;
            const auto index = core.configuration.index_of(id);
            if (index == core.configuration.size())
                return errc::bad_id;
            if (index >= value->progress.size())
                return errc::invalid;
            output = &value->progress[index];
            return {};
        }
    }
    node_state state(const core_state& core) noexcept {
        return core.state.valueless_by_exception()
                   ? node_state::unavailable
                   : static_cast<node_state>(core.state.index() + 1);
    }
    optional<role> local_role(const core_state& core) noexcept {
        const auto self = core.configuration.get(core.id);
        return self ? optional<role>{self->role} : optional<role>{};
    }
    optional<leader_info> leader(const core_state& core) {
        if (const auto follower = std::get_if<follower_state>(&core.state)) {
            if (follower->leader_id)
                return leader_info{follower->leader_id, string{follower->leader_address}};
        } else if (const auto value = std::get_if<leader_state>(&core.state);
                   value && !value->transferee) {
            return leader_info{core.id, string{core.address}};
        }
        return {};
    }
    error_code match_index(const core_state& core, server_id id, log_index& index) noexcept {
        borrowed<const detail::progress> value = nullptr;
        if (auto error = find_progress(core, id, value))
            return error;
        index = value->match_index;
        return {};
    }
    error_code catch_up(const core_state& core, server_id id, catch_up_status& status) noexcept {
        borrowed<const detail::progress> value = nullptr;
        if (auto error = find_progress(core, id, value))
            return error;
        status = value->catch_up;
        return {};
    }
    server_id transferee(const core_state& core) noexcept {
        const auto value = std::get_if<leader_state>(&core.state);
        return value ? value->transferee : 0;
    }
    error_code set_election_timeout(core_state& core, unsigned value) noexcept {
        if (value <= core.heartbeat_timeout || value > numeric_limits<uint32_t>::max() / 2 ||
            core.state.valueless_by_exception())
            return errc::invalid;
        if (core.now > numeric_limits<detail::time>::max() - 2 * static_cast<detail::time>(value))
            return errc::too_big;
        core.election_timeout = value;
        if (!std::holds_alternative<leader_state>(core.state))
            detail::election_update_randomized_timeout(core);
        return {};
    }
    error_code set_heartbeat_timeout(core_state& core, unsigned value) noexcept {
        if (value == 0 || value >= core.election_timeout)
            return errc::invalid;
        core.heartbeat_timeout = value;
        return {};
    }
    error_code set_install_snapshot_timeout(core_state& core, unsigned value) noexcept {
        if (value == 0)
            return errc::invalid;
        core.install_snapshot_timeout = value;
        return {};
    }
}
