#ifndef RAFTPP_TRANSPORT_HPP
#define RAFTPP_TRANSPORT_HPP

#include <raftpp/io.hpp>

namespace raftpp {
    // Message transport contract. Calls/callbacks use the owning event loop.
    // An immediate send error never calls completion. Accepted sends complete
    // once, retain their message through completion, and drain before close.
    class RAFTPP_API transport {
    public:
        virtual ~transport() = default;
        virtual error_code init(server_id id, string_view address) = 0;
        virtual error_code start(function<void(message)> receive) = 0;
        virtual error_code send(shared<const message> value, completion callback) = 0;
        // Stop receiving and drain native operations, including after a failed
        // init/start. Release native handles before invoking the callback.
        virtual void close(close_callback callback) noexcept = 0;
        // Optional connection policy; default preserves custom transports.
        // Applies to future retry scheduling, without rearming pending timers.
        virtual void set_connect_retry_delay(unsigned) noexcept {}
        // Publish matching owning information before a failure return/completion.
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };
}
#endif // RAFTPP_TRANSPORT_HPP
