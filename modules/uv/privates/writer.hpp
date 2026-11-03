#ifndef RAFTPP_DETAIL_WRITER_HPP
#define RAFTPP_DETAIL_WRITER_HPP

#include <raftpp/uv/export.hpp>
#include "file.hpp"
#include <raftpp/buffer.hpp>
#include <raftpp/io.hpp>
#include <raftpp/tracing.hpp>

#if RAFTPP_WITH_UV
struct uv_loop_s;
namespace raftpp::detail {

    struct writer_options {
        bool direct = false;
        bool async = false;
        unsigned max_concurrent_writes = 1; // 1..65536; submissions beyond this return busy.
        tracer tracing{};
    };
    enum class writer_operation {
        setup,
        direct,
        eventfd,
        poll_init,
        poll_start,
        submit_nowait,
        submit_worker,
        queue_work,
        queue_shutdown,
        read_event,
        poll_result,
        worker_result,
        destroy_result
    };
    // Internal fault/diagnostic hook. For syscall points, zero continues and a
    // negative errno injects failure before submission. Result points receive
    // actual completed byte counts and may replace them. Can run on workers.
    // The probe-only destroy_result receives the completed context teardown
    // status (zero or negative errno), after real cleanup has been attempted.
    using writer_hook = function<int64_t(writer_operation, int64_t)>;
    struct writer_state;

    // Calls and callbacks use one loop thread. The supplied loop must outlive
    // close completion. Native requests, iovecs, and payloads retain ownership.
    class RAFTPP_UV_API file_writer {
    public:
        file_writer() noexcept = default;
        ~file_writer();
        file_writer(file_writer&&) noexcept;
        file_writer& operator=(file_writer&&) noexcept;
        file_writer(const file_writer&) = delete;
        file_writer& operator=(const file_writer&) = delete;

        // Consumes file even on error. Partial initialization cleans up on the
        // loop; drain the loop before destroying it. Output is preserved on error.
        // On failure, failed_init_cleanup runs after native cleanup (possibly
        // inline when no native work remains), allowing owners to await it.
        [[nodiscard]] static error_code open(uv_loop_s& loop, file_handle file,
                                             writer_options options, file_writer& output,
                                             writer_hook hook = {},
                                             close_callback failed_init_cleanup = {},
                                             error_info* diagnostic = nullptr) noexcept;
        [[nodiscard]] error_code submit(view<const shared_buffer> buffers, uint64_t offset,
                                        completion callback) noexcept;
        // No allocation or synchronous wait. Worker completions drain normally;
        // polled requests complete canceled after the kernel releases buffers.
        [[nodiscard]] error_code close(close_callback callback = {}) noexcept;
        [[nodiscard]] error_code last_error() const noexcept;
        [[nodiscard]] error_info error_details() const noexcept;
        [[nodiscard]] bool closed() const noexcept;
        void set_tracer(tracer value) noexcept;

    private:
        shared<writer_state> state_;
    };

}
#endif
#endif // RAFTPP_DETAIL_WRITER_HPP
