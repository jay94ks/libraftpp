#ifndef RAFTPP_CORE_HPP
#define RAFTPP_CORE_HPP

#include <raftpp/detail/core.hpp>

namespace raftpp {
    // The public event API and callback node share one C++ consensus engine.
    using core_state = detail::core_state;
    using core_event = detail::core_event;
    using core_update = detail::core_update;
    using snapshot_metadata = detail::snapshot_metadata;
    using follower_state = detail::follower_state;
    using candidate_state = detail::candidate_state;
    using leader_state = detail::leader_state;
    using start_event = detail::start_event;
    using receive_event = detail::receive_event;
    using persisted_entries_event = detail::persisted_entries_event;
    using persisted_snapshot_event = detail::persisted_snapshot_event;
    using configuration_event = detail::configuration_event;
    using snapshot_event = detail::snapshot_event;
    using timeout_event = detail::timeout_event;
    using submit_event = detail::submit_event;
    using catch_up_event = detail::catch_up_event;
    using transfer_event = detail::transfer_event;
    using event_payload = detail::event_payload;
    using detail::update_current_term;
    using detail::update_voted_for;
    using detail::update_entries;
    using detail::update_snapshot;
    using detail::update_messages;
    using detail::update_state;
    using detail::update_commit_index;
    using detail::update_timeout;
    using detail::step;
    using detail::next_timeout;

    // Read-only queries share their semantics with the callback node API.
    [[nodiscard]] RAFTPP_API node_state state(const core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API optional<role> local_role(const core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API optional<leader_info> leader(const core_state& core);
    [[nodiscard]] RAFTPP_API error_code match_index(const core_state& core, server_id id,
                                                    log_index& index) noexcept;
    [[nodiscard]] RAFTPP_API error_code catch_up(const core_state& core, server_id id,
                                                 catch_up_status& status) noexcept;
    [[nodiscard]] RAFTPP_API server_id transferee(const core_state& core) noexcept;

    // Low-level event API setters have no legacy callback-driver workarounds.
    [[nodiscard]] RAFTPP_API error_code set_election_timeout(core_state& core,
                                                             unsigned msecs) noexcept;
    [[nodiscard]] RAFTPP_API error_code set_heartbeat_timeout(core_state& core,
                                                              unsigned msecs) noexcept;
    [[nodiscard]] RAFTPP_API error_code set_install_snapshot_timeout(core_state& core,
                                                                     unsigned msecs) noexcept;
}
#endif // RAFTPP_CORE_HPP
