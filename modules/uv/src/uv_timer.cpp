#include <raftpp/uv/uv_timer.hpp>
#include <uv.h>

namespace raftpp::detail {
    struct uv_timer_state : enable_shared_from_this<uv_timer_state> {
        uv_timer_t native{};
        shared<uv_timer_state> native_owner;
        shared<completion> tick;
        close_callback on_close;
        error_info error;
        size_t active = 0;
        bool started = false, closing = false, closed = false, native_closed = false;
        void finish() noexcept {
            if (!native_closed || active || closed)
                return;
            closed = true;
            auto keep = std::move(native_owner);
            auto callback = std::move(on_close);
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
        }
        void close() noexcept {
            if (closing)
                return;
            closing = true;
            tick.reset();
            (void)uv_timer_stop(&native);
            uv_close(reinterpret_cast<uv_handle_t*>(&native), [](uv_handle_t* handle) noexcept {
                auto state = static_cast<uv_timer_state*>(handle->data)->native_owner;
                state->native_closed = true;
                state->finish();
            });
        }
    };
}
namespace raftpp {
    uv_timer::uv_timer(uv_loop_s& loop) : state_(std::make_shared<detail::uv_timer_state>()) {
        const auto status = uv_timer_init(&loop, &state_->native);
        if (status < 0)
            throw system_error(make_error_code(errc::io_error), uv_strerror(status));
        state_->native.data = state_.get();
        state_->native_owner = state_;
    }
    uv_timer::~uv_timer() {
        state_->close();
    }
    error_code uv_timer::start(unsigned interval, completion tick) noexcept {
        auto state = state_;
        if (state->closing)
            return errc::shutdown;
        if (!interval || !tick)
            return errc::invalid;
        if (state->started)
            return errc::busy;
        try {
            state->tick = std::make_shared<completion>(std::move(tick));
        } catch (const bad_alloc&) {
            return errc::out_of_memory;
        }
        const auto status = uv_timer_start(
            &state->native,
            [](uv_timer_t* native) noexcept {
                auto state = static_cast<detail::uv_timer_state*>(native->data)->native_owner;
                if (state->closing)
                    return;
                ++state->active;
                auto callback = state->tick;
                try {
                    if (callback)
                        (*callback)({});
                } catch (...) {
                    state->error = make_error_info(errc::io_error, "uv.timer.callback");
                    state->close();
                }
                callback.reset();
                --state->active;
                state->finish();
            },
            interval, interval);
        if (status < 0) {
            state->tick.reset();
            state->error = make_error_info(errc::io_error, "uv.timer.start");
            return errc::io_error;
        }
        state->started = true;
        return {};
    }
    void uv_timer::close(close_callback callback) noexcept {
        auto state = state_;
        if (state->closed) {
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
            return;
        }
        if (callback && !state->on_close)
            state->on_close = std::move(callback);
        state->close();
    }
    uint64_t uv_timer::time() const noexcept {
        return uv_now(state_->native.loop);
    }
    error_info uv_timer::error_details() const noexcept {
        return state_->error;
    }
}
