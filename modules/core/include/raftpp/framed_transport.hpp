#ifndef RAFTPP_FRAMED_TRANSPORT_HPP
#define RAFTPP_FRAMED_TRANSPORT_HPP

#include <raftpp/encoding.hpp>
#include <raftpp/stream.hpp>
#include <raftpp/transport.hpp>

namespace raftpp {
    namespace detail {
        struct framed_transport_state;
    }
    // Raft wire protocol over established streams, independent of networking
    // SDK, discovery, TLS or reconnect policy. Each peer stream is exclusive to
    // this transport; all streams must share one serialized execution domain.
    class RAFTPP_API framed_transport final : public transport {
    public:
        explicit framed_transport(encoding_limits limits = {});
        ~framed_transport() override;
        framed_transport(const framed_transport&) = delete;
        framed_transport& operator=(const framed_transport&) = delete;
        // Register connected peers before start. Connection authentication and
        // binding peer identity to its address are the connector's responsibility.
        error_code attach(server_id peer, string_view address, shared<byte_stream> stream) noexcept;
        error_code init(server_id id, string_view address) noexcept override;
        error_code start(function<void(message)> receive) noexcept override;
        error_code send(shared<const message> value, completion callback) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::framed_transport_state> state_;
    };
}
#endif
