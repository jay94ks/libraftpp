#ifndef RAFTPP_DETAIL_PROGRESS_HPP
#define RAFTPP_DETAIL_PROGRESS_HPP

#include <raftpp/detail/protocol.hpp>

namespace raftpp::detail {

    enum class replication_mode : uint16_t { probe, pipeline, snapshot };
    using raftpp::catch_up_status;

    // Per-peer leader state. The protocol keeps all indexes and timeouts in the
    // same units and preserves the original never-sent/never-received sentinel.
    class RAFTPP_API progress {
    public:
        explicit progress(log_index last_index = 0) noexcept : next_index(last_index + 1) {}

        [[nodiscard]] bool up_to_date(log_index last_index) const noexcept;
        [[nodiscard]] bool online(time now, unsigned election_timeout) const noexcept;
        [[nodiscard]] bool contacted_recently(time election_timer_start) const noexcept;
        [[nodiscard]] bool should_replicate(log_index last_index, time now,
                                            unsigned heartbeat_timeout,
                                            unsigned install_snapshot_timeout) noexcept;
        void sent(time now, core_update& update) noexcept;
        [[nodiscard]] time last_message_sent() const noexcept;
        void to_snapshot(log_index index) noexcept;
        void abort_snapshot() noexcept;
        void to_probe() noexcept;
        void to_pipeline() noexcept {
            mode = replication_mode::pipeline;
        }
        [[nodiscard]] string_view state_name() const noexcept;
        [[nodiscard]] bool maybe_decrement(log_index rejected, log_index last_index) noexcept;
        [[nodiscard]] bool maybe_update(log_index last_index) noexcept;
        [[nodiscard]] bool snapshot_done() const noexcept;
        void catch_up_start() noexcept;
        void catch_up_abort() noexcept;
        void catch_up_finish() noexcept;

        replication_mode mode = replication_mode::probe;
        catch_up_status catch_up = catch_up_status::none;
        uint16_t features = 0;
        uint16_t capacity = 0;
        log_index next_index;
        log_index match_index = 0;
        time last_send = never;
        time last_recv = never;
        struct snapshot_state {
            log_index index = 0;
            time last_send = never;
        } snapshot;
    };

    [[nodiscard]] RAFTPP_API error_code build_progress(const configuration& configuration,
                                                       server_id self, log_index last_index,
                                                       log_index last_stored,
                                                       vector<progress>& output) noexcept;
    [[nodiscard]] RAFTPP_API error_code build_progress(const configuration& configuration,
                                                       server_id self, log_index last_index,
                                                       log_index last_stored,
                                                       resource_vector<progress>& output) noexcept;
    // Retain state by server ID when reordering, adding, or removing servers.
    // On error output is unchanged. current and next may alias each other.
    [[nodiscard]] RAFTPP_API error_code rebuild_progress(const configuration& current,
                                                         const configuration& next,
                                                         log_index last_index,
                                                         vector<progress>& output) noexcept;
    [[nodiscard]] RAFTPP_API error_code
    rebuild_progress(const configuration& current, const configuration& next, log_index last_index,
                     resource_vector<progress>& output) noexcept;

}

#endif // RAFTPP_DETAIL_PROGRESS_HPP
