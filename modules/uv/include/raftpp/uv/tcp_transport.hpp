#ifndef RAFTPP_TCP_TRANSPORT_HPP
#define RAFTPP_TCP_TRANSPORT_HPP

#include <raftpp/uv/export.hpp>
#include <raftpp/config.hpp>
#include <raftpp/transport.hpp>
#include <raftpp/encoding.hpp>
#include <raftpp/tracing.hpp>

#if RAFTPP_WITH_UV
struct uv_loop_s;
namespace raftpp {
    namespace detail {
        struct tcp_transport_state;
    }
    struct tcp_transport_options {
        string bind_address; // Empty uses the advertised address.
        unsigned connect_retry = 1000;
        size_t max_handshake_address = 2048; // Includes NUL and 8-byte padding.
        encoding_limits limits;
        tracer tracing{};
    };

    // IPv4 TCP, using the cowsql/raft handshake and RPC wire formats.
    // Calls/callbacks are serialized on loop, which must outlive close completion.
    class RAFTPP_UV_API tcp_transport final : public transport {
    public:
        explicit tcp_transport(uv_loop_s& loop, tcp_transport_options options = {});
        ~tcp_transport() override;
        tcp_transport(const tcp_transport&) = delete;
        tcp_transport& operator=(const tcp_transport&) = delete;
        error_code init(server_id id, string_view address) noexcept override;
        error_code start(function<void(message)> receive) noexcept override;
        error_code send(shared<const message> value, completion callback) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        [[nodiscard]] error_code last_error() const noexcept;
        [[nodiscard]] error_info error_details() const noexcept override;
        void set_tracer(tracer value) noexcept;
        [[nodiscard]] error_code set_bind_address(string_view address) noexcept;
        void set_connect_retry_delay(unsigned msecs) noexcept override;

    private:
        shared<detail::tcp_transport_state> state_;
    };
}
#endif
#endif // RAFTPP_TCP_TRANSPORT_HPP
