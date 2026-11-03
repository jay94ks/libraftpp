#ifndef RAFTPP_DETAIL_CORE_HPP
#define RAFTPP_DETAIL_CORE_HPP

#include <raftpp/detail/core_state.hpp>

namespace raftpp::detail {

    struct start_event {
        raftpp::term term = 0;
        server_id voted_for = 0;
        optional<snapshot_metadata> metadata;
        log_index start_index = 1;
        vector<entry> entries;
    };
    struct receive_event {
        message rpc;
    };
    struct persisted_entries_event {
        log_index index = 0;
    };
    struct persisted_snapshot_event {
        snapshot_metadata metadata;
        size_t offset = 0;
        bool last = true;
    };
    struct configuration_event {
        log_index index = 0;
        raftpp::configuration configuration;
    };
    struct snapshot_event {
        snapshot_metadata metadata;
        unsigned trailing = 0;
    };
    struct timeout_event {};
    struct submit_event {
        vector<entry> entries;
    };
    struct catch_up_event {
        server_id id = 0;
    };
    struct transfer_event {
        server_id id = 0;
    };
    using event_payload = variant<start_event, receive_event, persisted_entries_event,
                                  persisted_snapshot_event, configuration_event, snapshot_event,
                                  timeout_event, submit_event, catch_up_event, transfer_event>;
    struct core_event {
        detail::time time = 0;
        uint16_t capacity = 0;
        event_payload payload = timeout_event{};
    };

    // Owns its update; a driver may move core.update out to keep all asynchronous
    // work alive. Only one step runs at a time. No I/O is run here. Synchronous
    // tracing callbacks must not mutate the core or invoke its driver.
    [[nodiscard]] RAFTPP_API error_code step(core_state& core, const core_event& event) noexcept;
    [[nodiscard]] RAFTPP_API time next_timeout(const core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API error_code convert_to_candidate(core_state& core,
                                                             bool disrupt_leader) noexcept;
    [[nodiscard]] RAFTPP_API error_code convert_to_leader(core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API error_code submit_entries(core_state& core,
                                                       view<const entry> entries) noexcept;
    [[nodiscard]] RAFTPP_API error_code replication_progress(core_state& core,
                                                             size_t peer) noexcept;
    RAFTPP_API void replication_heartbeat(core_state& core) noexcept;
    [[nodiscard]] RAFTPP_API error_code receive_message(core_state& core,
                                                        const message& message) noexcept;

}

#endif // RAFTPP_DETAIL_CORE_HPP
