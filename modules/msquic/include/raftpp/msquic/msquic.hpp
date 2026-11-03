#ifndef RAFTPP_MSQUIC_HPP
#define RAFTPP_MSQUIC_HPP

#include <raftpp/stream.hpp>
#include <raftpp/msquic/export.hpp>
#include <msquic.h>

namespace raftpp {
    // Shared API ownership; the table is released with MsQuicClose.
    RAFTPP_MSQUIC_API shared<const QUIC_API_TABLE> open_msquic_api();
    namespace detail {
        struct msquic_stream_state;
    }
    // Takes exclusive ownership of an established native stream handle and its
    // callback. connection_owner must retain the connection/registration. Call
    // methods on the connection's serialized MsQuic callback execution domain.
    // Native connection/TLS/listener configuration remains with the connector.
    class RAFTPP_MSQUIC_API msquic_stream final : public byte_stream {
    public:
        msquic_stream(shared<const QUIC_API_TABLE> api, HQUIC stream,
                      shared<void> connection_owner);
        ~msquic_stream() override;
        msquic_stream(const msquic_stream&) = delete;
        msquic_stream& operator=(const msquic_stream&) = delete;
        using byte_stream::write;
        error_code read_start(stream_read_callback callback,
                              size_t buffer_size = 65536) noexcept override;
        error_code read_stop() noexcept override;
        error_code write(view<const shared_buffer> buffers,
                         completion callback = {}) noexcept override;
        error_code close(close_callback callback = {}) noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::msquic_stream_state> state_;
    };
}
#endif
