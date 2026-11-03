#ifndef RAFTPP_DETAIL_FILE_BACKEND_HPP
#define RAFTPP_DETAIL_FILE_BACKEND_HPP

#include <raftpp/uv/export.hpp>
#include "log_writer.hpp"

#if RAFTPP_WITH_UV
namespace raftpp::detail {
    enum class file_backend_operation { queue_prepare, queue_publish, queue_get };
    struct file_backend_options {
        log_writer_options log;
        size_t max_snapshot_size = numeric_limits<size_t>::max();
        function<error_code(file_backend_operation)> hook;
    };
    struct file_backend_state;
    // All calls and completions use one loop thread. Native workers retain
    // requests, immutable snapshots, and storage until they finish.
    class RAFTPP_UV_API file_backend {
    public:
        file_backend(uv_loop_s& loop, shared<disk_storage> disk, file_backend_options options = {});
        ~file_backend();
        file_backend(const file_backend&) = delete;
        file_backend& operator=(const file_backend&) = delete;
        [[nodiscard]] error_code open(log_index next_index) noexcept;
        [[nodiscard]] error_code append(log_index index, shared<const entry_batch> entries,
                                        completion callback) noexcept;
        [[nodiscard]] error_code truncate(log_index index) noexcept;
        [[nodiscard]] error_code snapshot_put(unsigned trailing, shared<const snapshot> value,
                                              completion callback) noexcept;
        [[nodiscard]] error_code snapshot_get(snapshot_callback callback) noexcept;
        [[nodiscard]] error_code close(close_callback callback = {}) noexcept;
        [[nodiscard]] size_t capacity() const noexcept;
        [[nodiscard]] error_code last_error() const noexcept;
        [[nodiscard]] error_info error_details() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        void set_tracer(tracer value) noexcept;
        [[nodiscard]] error_code set_segment_size(size_t size) noexcept;
        void set_disk_retry(unsigned msecs) noexcept;

    private:
        shared<file_backend_state> state_;
    };
}
#endif
#endif // RAFTPP_DETAIL_FILE_BACKEND_HPP
