#ifndef RAFTPP_UV_STREAM_HPP
#define RAFTPP_UV_STREAM_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/config.hpp>
#include <raftpp/io.hpp>
#include <raftpp/stream.hpp>
#if RAFTPP_WITH_UV
struct uv_stream_s;
namespace raftpp {
    namespace detail {
        struct uv_stream_state;
    }
    using stream_deleter = function<void(borrowed<uv_stream_s>)>;
    // Owns an already initialized stream. The native handle stays at its original
    // address; accepted writes and asynchronous close retain the private owner.
    class RAFTPP_UV_API uv_stream : public byte_stream {
    public:
        uv_stream() noexcept = default;
        // Ownership transfers only after successful construction. Native data
        // is reserved while owned; the old value is restored for the deleter.
        uv_stream(uv_stream_s& stream, stream_deleter release);
        ~uv_stream() override;
        uv_stream(const uv_stream&) = delete;
        uv_stream& operator=(const uv_stream&) = delete;
        uv_stream(uv_stream&&) noexcept;
        uv_stream& operator=(uv_stream&&) noexcept;
        [[nodiscard]] explicit operator bool() const noexcept;
        [[nodiscard]] borrowed<uv_stream_s> native_handle() const noexcept;
        [[nodiscard]] error_code read_start(stream_read_callback callback,
                                            size_t buffer_size = 65536) noexcept override;
        [[nodiscard]] error_code read_stop() noexcept override;
        [[nodiscard]] error_code write(view<const shared_buffer> buffers,
                                       completion callback = {}) noexcept override;
        [[nodiscard]] error_code write(shared_buffer bytes, completion callback = {}) noexcept;
        [[nodiscard]] error_code close(close_callback callback = {}) noexcept override;
        [[nodiscard]] error_info error_details() const noexcept override;

    private:
        shared<detail::uv_stream_state> state_;
    };
}
#endif
#endif // RAFTPP_UV_STREAM_HPP
