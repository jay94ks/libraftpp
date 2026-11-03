#ifndef RAFTPP_MESSAGE_HPP
#define RAFTPP_MESSAGE_HPP

#include <raftpp/configuration.hpp>
#include <raftpp/entry.hpp>

namespace raftpp {

    using entry_batch = vector<shared<const entry>>;
    inline constexpr uint16_t message_feature_capacity = 1U << 0;

    struct request_vote {
        uint8_t version = 2;
        raftpp::term term = 0;
        server_id candidate_id = 0;
        log_index last_log_index = 0;
        raftpp::term last_log_term = 0;
        bool disrupt_leader = false;
        bool pre_vote = false;
    };

    struct request_vote_result {
        uint8_t version = 2;
        raftpp::term term = 0;
        bool vote_granted = false;
        bool pre_vote = false;
        uint16_t features = 0;
        uint16_t capacity = 0;
    };

    struct append_entries {
        uint8_t version = 0;
        raftpp::term term = 0;
        log_index prev_log_index = 0;
        raftpp::term prev_log_term = 0;
        log_index leader_commit = 0;
        entry_batch entries;
    };

    struct append_entries_result {
        uint8_t version = 2;
        raftpp::term term = 0;
        log_index rejected = 0;
        log_index last_log_index = 0;
        uint16_t features = 0;
        uint16_t capacity = 0;
    };

    struct install_snapshot {
        uint8_t version = 0;
        raftpp::term term = 0;
        log_index last_index = 0;
        raftpp::term last_term = 0;
        raftpp::configuration configuration;
        log_index configuration_index = 0;
        shared_buffer data;
        // Outgoing core directive: the I/O driver must load the stable snapshot
        // before encoding/sending. Received RPCs always have this set to false.
        bool needs_data = false;
    };

    struct timeout_now {
        uint8_t version = 0;
        raftpp::term term = 0;
        log_index last_log_index = 0;
        raftpp::term last_log_term = 0;
    };

    // Variant order preserves the original RPC codes (index + 1).
    using message_payload = variant<append_entries, append_entries_result, request_vote,
                                    request_vote_result, install_snapshot, timeout_now>;

    struct message {
        raftpp::server_id server_id = 0;
        string server_address;
        message_payload payload;
    };

}

#endif // RAFTPP_MESSAGE_HPP
