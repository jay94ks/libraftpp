#ifndef RAFTPP_MVFST_HPP
#define RAFTPP_MVFST_HPP

#include <raftpp/stream.hpp>
#include <raftpp/mvfst/export.hpp>
#include <quic/api/QuicSocketLite.h>

namespace raftpp {
    namespace detail {
        struct mvfst_stream_state;
    }
    // Exclusively owns one established bidirectional stream's callbacks. The
    // shared connection and its event base remain alive until stream close.
    // Call every method on that connection's event-base thread.
    class RAFTPP_MVFST_API mvfst_stream final : public byte_stream {
    public:
        mvfst_stream(shared<quic::QuicSocketLite> connection, quic::StreamId stream);
        ~mvfst_stream() override;
        mvfst_stream(const mvfst_stream&) = delete;
        mvfst_stream& operator=(const mvfst_stream&) = delete;
        using byte_stream::write;
        error_code read_start(stream_read_callback callback,
                              size_t buffer_size = 65536) noexcept override;
        error_code read_stop() noexcept override;
        error_code write(view<const shared_buffer> buffers,
                         completion callback = {}) noexcept override;
        error_code close(close_callback callback = {}) noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::mvfst_stream_state> state_;
    };
}
#endif
