#ifndef RAFTPP_TIMER_HPP
#define RAFTPP_TIMER_HPP

#include <raftpp/io.hpp>

namespace raftpp {
    // A periodic subscription and monotonic millisecond clock in one serialized
    // domain. A failed tick ends the subscription. close drains active callbacks.
    class RAFTPP_API timer {
    public:
        virtual ~timer() = default;
        virtual error_code start(unsigned interval, completion tick) = 0;
        virtual void close(close_callback callback) noexcept = 0;
        [[nodiscard]] virtual uint64_t time() const noexcept = 0;
        [[nodiscard]] virtual error_info error_details() const noexcept {
            return {};
        }
    };
}
#endif
