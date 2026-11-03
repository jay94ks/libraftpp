#ifndef RAFTPP_UV_STREAM_TRANSPORT_HPP
#define RAFTPP_UV_STREAM_TRANSPORT_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/uv/uv_stream.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#if RAFTPP_WITH_UV
namespace raftpp {
    using stream_accept_callback = function<void(server_id, string, uv_stream)>;
    using stream_connect_callback = function<void(error_code, uv_stream)>;
    // Counterpart of C raft_uv_transport: established streams are handed to the
    // caller and outlive this connector. Close drains only its pending work.
    class RAFTPP_UV_API uv_stream_transport {
    public:
        virtual ~uv_stream_transport() = default;
        virtual error_code init(server_id id, string_view address) = 0;
        virtual error_code listen(stream_accept_callback callback) = 0;
        virtual error_code connect(server_id id, string_view address,
                                   stream_connect_callback callback) = 0;
        virtual void close(close_callback callback = {}) noexcept = 0;
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };
    class RAFTPP_UV_API tcp_stream_transport final : public uv_stream_transport {
    public:
        explicit tcp_stream_transport(uv_loop_s& loop, tcp_transport_options options = {});
        ~tcp_stream_transport() override;
        tcp_stream_transport(const tcp_stream_transport&) = delete;
        tcp_stream_transport& operator=(const tcp_stream_transport&) = delete;
        error_code init(server_id id, string_view address) noexcept override;
        error_code listen(stream_accept_callback callback) noexcept override;
        error_code connect(server_id id, string_view address,
                           stream_connect_callback callback) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        [[nodiscard]] error_info error_details() const noexcept override;
        [[nodiscard]] error_code set_bind_address(string_view address) noexcept;

    private:
        shared<detail::tcp_transport_state> state_;
    };
}
#endif
#endif // RAFTPP_UV_STREAM_TRANSPORT_HPP
