#ifndef RAFTPP_DETAIL_PROTOCOL_HPP
#define RAFTPP_DETAIL_PROTOCOL_HPP

#include <raftpp/message.hpp>

namespace raftpp::detail {

    using raftpp::entry_batch;
    using raftpp::request_vote;
    using raftpp::request_vote_result;
    using raftpp::append_entries;
    using raftpp::append_entries_result;
    using raftpp::install_snapshot;
    using raftpp::timeout_now;
    using raftpp::message_payload;
    using raftpp::message;

    using time = uint64_t;
    inline constexpr auto never = numeric_limits<time>::max();
    inline constexpr uint16_t feature_capacity = message_feature_capacity;

    inline constexpr unsigned update_current_term = 1U << 0;
    inline constexpr unsigned update_voted_for = 1U << 1;
    inline constexpr unsigned update_entries = 1U << 2;
    inline constexpr unsigned update_snapshot = 1U << 3;
    inline constexpr unsigned update_messages = 1U << 4;
    inline constexpr unsigned update_state = 1U << 5;
    inline constexpr unsigned update_commit_index = 1U << 6;
    inline constexpr unsigned update_timeout = 1U << 7;

    struct snapshot_metadata {
        log_index index = 0;
        raftpp::term term = 0;
        raftpp::configuration configuration;
        log_index configuration_index = 0;
    };

    struct core_update {
        unsigned flags = 0;
        resource_vector<message> messages;
        struct entries_update {
            log_index index = 0;
            entry_batch batch;
        } entries;
        struct snapshot_update {
            snapshot_metadata metadata;
            size_t offset = 0;
            shared_buffer chunk;
            bool last = true;
        } snapshot;
    };

    // Own addresses and RPC values; queued work does not borrow a configuration.
    [[nodiscard]] RAFTPP_API error_code enqueue_message(core_update& update, server_id id,
                                                        string_view address,
                                                        message_payload payload) noexcept;

}

#endif // RAFTPP_DETAIL_PROTOCOL_HPP
