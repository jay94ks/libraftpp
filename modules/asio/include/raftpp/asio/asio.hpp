#ifndef RAFTPP_ASIO_HPP
#define RAFTPP_ASIO_HPP

#include <raftpp/executor.hpp>
#include <raftpp/stream.hpp>
#include <raftpp/timer.hpp>
#include <raftpp/asio/export.hpp>
#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>

namespace raftpp {
    class RAFTPP_ASIO_API asio_executor final : public executor {
    public:
        explicit asio_executor(asio::any_io_executor executor);
        error_code post(function<void()> callback) noexcept override;

    private:
        asio::any_io_executor executor_;
    };
    namespace detail {
        struct asio_stream_state;
        struct asio_timer_state;
    }
    class RAFTPP_ASIO_API asio_timer final : public timer {
    public:
        explicit asio_timer(asio::any_io_executor executor);
        ~asio_timer() override;
        asio_timer(const asio_timer&) = delete;
        asio_timer& operator=(const asio_timer&) = delete;
        error_code start(unsigned interval, completion tick) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        uint64_t time() const noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::asio_timer_state> state_;
    };
    // Takes ownership of a connected TCP socket. Call methods and drive the
    // socket's executor on one thread (or one externally serialized strand).
    class RAFTPP_ASIO_API asio_stream final : public byte_stream {
    public:
        explicit asio_stream(asio::ip::tcp::socket socket);
        ~asio_stream() override;
        asio_stream(const asio_stream&) = delete;
        asio_stream& operator=(const asio_stream&) = delete;
        using byte_stream::write;
        error_code read_start(stream_read_callback callback,
                              size_t buffer_size = 65536) noexcept override;
        error_code read_stop() noexcept override;
        error_code write(view<const shared_buffer> buffers,
                         completion callback = {}) noexcept override;
        error_code close(close_callback callback = {}) noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::asio_stream_state> state_;
    };
}
#endif
