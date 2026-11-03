#ifndef RAFTPP_DETAIL_LOG_WRITER_HPP
#define RAFTPP_DETAIL_LOG_WRITER_HPP

#include <raftpp/uv/export.hpp>
#include "disk.hpp"

#if RAFTPP_WITH_UV
namespace raftpp::detail {
    struct log_writer_options {
        size_t segment_size = 8 * 1024 * 1024;
        unsigned disk_retry = 5000;
        optional<disk_capabilities> capabilities; // Empty selects an initialization probe.
        writer_hook writer;
        tracer tracing{};
    };
    struct log_writer_state;
    // Internal append/barrier coordinator. Call load() before opening it.
    // All methods/callbacks use one loop thread; disk workers retain ownership.
    class RAFTPP_UV_API log_writer {
    public:
        log_writer() noexcept = default;
        ~log_writer();
        log_writer(log_writer&&) noexcept;
        log_writer& operator=(log_writer&&) noexcept;
        log_writer(const log_writer&) = delete;
        log_writer& operator=(const log_writer&) = delete;
        [[nodiscard]] static error_code open(uv_loop_s& loop, shared<disk_storage> storage,
                                             log_index next_index, log_writer_options options,
                                             log_writer& output,
                                             close_callback failed_init_cleanup = {},
                                             error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code append(log_index index, shared<const entry_batch> entries,
                                        completion callback) noexcept;
        [[nodiscard]] error_code truncate(log_index index, completion callback = {}) noexcept;
        // Ready callbacks must call unblock() after their work. Close cancels
        // callbacks that have not started and waits for already-started work.
        [[nodiscard]] error_code barrier(log_index next_index, bool blocking,
                                         completion ready) noexcept;
        void unblock() noexcept;
        [[nodiscard]] error_code close(close_callback callback = {}) noexcept;
        [[nodiscard]] size_t capacity() const noexcept;
        [[nodiscard]] log_index next_index() const noexcept;
        [[nodiscard]] error_code last_error() const noexcept;
        [[nodiscard]] error_info error_details() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        void set_tracer(tracer value) noexcept;
        [[nodiscard]] error_code set_segment_size(size_t size) noexcept;
        void set_disk_retry(unsigned msecs) noexcept;

    private:
        shared<log_writer_state> state_;
    };
}
#endif
#endif // RAFTPP_DETAIL_LOG_WRITER_HPP
