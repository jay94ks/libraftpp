#include <raftpp/asyncpp/asyncpp.hpp>
#include <stdexcept>

namespace raftpp {
    asyncpp_executor::asyncpp_executor(shared<asyncpp::dispatcher> dispatcher)
        : dispatcher_(std::move(dispatcher)) {
        if (!dispatcher_)
            throw std::invalid_argument("null asyncpp dispatcher");
    }
    error_code asyncpp_executor::post(function<void()> callback) noexcept {
        if (!callback)
            return errc::invalid;
        try {
            dispatcher_->push(
                [owner = dispatcher_, callback = std::move(callback)] { callback(); });
            return {};
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        } catch (...) {
            return errc::io_error;
        }
    }
}
