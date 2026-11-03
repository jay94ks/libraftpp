#ifndef RAFTPP_DETAIL_CORE_STATE_HPP
#define RAFTPP_DETAIL_CORE_STATE_HPP

#include <raftpp/detail/progress.hpp>
#include <raftpp/detail/trail.hpp>
#include <raftpp/detail/log.hpp>
#include <raftpp/tracing.hpp>

namespace raftpp::detail {

    struct follower_state {
        unsigned randomized_election_timeout = 0;
        server_id leader_id = 0;
        resource_string leader_address;
        log_index match = 0;
    };

    struct vote {
        bool grant = false;
        uint16_t features = 0;
        uint16_t capacity = 0;
    };

    struct candidate_state {
        unsigned randomized_election_timeout = 0;
        resource_vector<vote> votes;
        bool disrupt_leader = false;
        bool in_pre_vote = false;
    };

    struct leader_state {
        resource_vector<detail::progress> progress;
        server_id promotee_id = 0;
        uint16_t round_number = 0;
        log_index round_index = 0;
        time round_start = 0;
        server_id transferee = 0;
        time transfer_start = 0;
        bool transferring = false;
    };

    // Owned values replace the C union and role-specific manual cleanup.
    // Consensus data shared by the public event API and callback node.
    struct core_state {
        core_state() = default;
        core_state(const core_state&) = delete;
        core_state& operator=(const core_state&) = delete;
        core_state(core_state&&) = delete;
        core_state& operator=(core_state&&) = delete;

        server_id id = 0;
        resource_string address;
        tracer tracing;
        term current_term = 0;
        server_id voted_for = 0;
        raftpp::configuration configuration;
        raftpp::configuration configuration_committed;
        log_index configuration_committed_index = 0;
        log_index configuration_uncommitted_index = 0;
        log_index configuration_last_snapshot_index = 0;
        detail::trail trail;
        detail::log cache;
        log_index commit_index = 0;
        log_index last_stored = 0;
        variant<follower_state, candidate_state, leader_state> state;
        unsigned election_timeout = 1000;
        unsigned heartbeat_timeout = 100;
        unsigned install_snapshot_timeout = 30000;
        time election_timer_start = 0;
        time now = 0;
        uint32_t random = 0;
        uint16_t capacity = 0;
        uint16_t capacity_threshold = 0;
        bool pre_vote = false;
        bool snapshot_installing = false;
        // Prepare both values before submitting snapshot I/O; completion can
        // install durable membership without another fallible allocation.
        optional<pair<raftpp::configuration, raftpp::configuration>> snapshot_configurations;
        log_index snapshot_configuration_index = 0;
        unsigned max_catch_up_rounds = 10;
        unsigned max_catch_up_round_duration = 5000;
        unsigned max_inflight_entries = 32;
        bool force_barrier = false; // Set by the compatibility driver if needed.
        bool started = false;
        bool stepping = false;
        core_update update;
    };

}

#endif // RAFTPP_DETAIL_CORE_STATE_HPP
