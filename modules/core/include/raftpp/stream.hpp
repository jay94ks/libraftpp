#ifndef RAFTPP_STREAM_HPP
#define RAFTPP_STREAM_HPP

#include <raftpp/io.hpp>

namespace raftpp {
    using stream_read_callback = function<void(error_code, shared_buffer)>;

    // An ordered, reliable, connected byte stream. Operations and callbacks are
    // serialized on its owning event loop. EOF is errc::no_connection. A read
    // callback may receive any nonempty fragment, independently of write sizes.
    class RAFTPP_API byte_stream {
    public:
        virtual ~byte_stream() = default;
        virtual error_code read_start(stream_read_callback callback,
                                      size_t buffer_size = 65536) noexcept = 0;
        virtual error_code read_stop() noexcept = 0;
        // An accepted write retains every buffer and completes once. Immediate
        // submission failure does not invoke callback. Success means local SDK
        // acceptance/completion, not peer application acknowledgement.
        virtual error_code write(view<const shared_buffer> buffers,
                                 completion callback = {}) noexcept = 0;
        error_code write(shared_buffer bytes, completion callback = {}) noexcept {
            return write(view<const shared_buffer>{&bytes, 1}, std::move(callback));
        }
        // Accepted operations drain/cancel before close completion. The event
        // loop must continue running until completion; destruction initiates it.
        virtual error_code close(close_callback callback = {}) noexcept = 0;
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };
}
#endif
