#ifndef RAFTPP_STREAM_TRANSPORT_HPP
#define RAFTPP_STREAM_TRANSPORT_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/uv/uv_stream_transport.hpp>
#if RAFTPP_WITH_UV
namespace raftpp {
    namespace detail {
        struct stream_transport_state;
    }
    struct stream_transport_options {
        unsigned connect_retry = 1000;
        size_t read_buffer_size = 65536;
        encoding_limits limits;
        tracer tracing{};
    };
    // Raft framing, queues and retries over a caller-selected raw connector.
    // Owns that connector's init/listen/connect/close lifecycle exclusively.
    class RAFTPP_UV_API stream_transport final : public transport {
    public:
        stream_transport(uv_loop_s& loop, shared<uv_stream_transport> connector,
                         stream_transport_options options = {});
        ~stream_transport() override;
        stream_transport(const stream_transport&) = delete;
        stream_transport& operator=(const stream_transport&) = delete;
        error_code init(server_id id, string_view address) noexcept override;
        error_code start(function<void(message)> receive) noexcept override;
        error_code send(shared<const message> value, completion callback) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        void set_connect_retry_delay(unsigned msecs) noexcept override;
        [[nodiscard]] error_info error_details() const noexcept override;

    private:
        shared<detail::stream_transport_state> state_;
    };
}
#endif
#endif // RAFTPP_STREAM_TRANSPORT_HPP
