#include <raftpp/asio/asio.hpp>
#include <asio/steady_timer.hpp>

namespace raftpp::detail {
    struct asio_timer_state : enable_shared_from_this<asio_timer_state> {
        asio::steady_timer native;
        shared<completion> tick;
        close_callback on_close;
        error_info error;
        unsigned interval = 0;
        size_t active = 0;
        bool waiting = false, started = false, closing = false, closed = false;
        explicit asio_timer_state(asio::any_io_executor executor) : native(std::move(executor)) {}
        void finish() noexcept {
            if (!closing || closed || waiting || active)
                return;
            closed = true;
            auto callback = std::move(on_close);
            try {
                if (callback)
                    callback();
            } catch (...) {
            }
        }
        void close() noexcept {
            if (!closing) {
                closing = true;
                tick.reset();
                try {
                    (void)native.cancel();
                } catch (const system_error& failure) {
                    error =
                        make_error_info(errc::io_error, "asio.timer.cancel", {}, failure.code());
                } catch (...) {
                    error = make_error_info(errc::io_error, "asio.timer.cancel");
                }
            }
            finish();
        }
        void deliver(error_code code) noexcept {
            auto callback = tick;
            if (code && error.code != code)
                error = make_error_info(code, "asio.timer");
            try {
                if (callback)
                    (*callback)(code);
            } catch (...) {
                error = make_error_info(errc::io_error, "asio.timer.callback");
                close();
            }
            if (code)
                close();
        }
        error_code arm() noexcept {
            try {
                native.expires_after(chrono::milliseconds(interval));
                waiting = true;
                native.async_wait([self = shared_from_this()](std::error_code cause) {
                    self->waiting = false;
                    ++self->active;
                    if (!self->closing) {
                        if (cause) {
                            self->error =
                                make_error_info(errc::io_error, "asio.timer.wait", {}, cause);
                            self->deliver(errc::io_error);
                        } else {
                            self->deliver({});
                            if (!self->closing)
                                if (auto error = self->arm())
                                    self->deliver(error);
                        }
                    }
                    --self->active;
                    self->finish();
                });
                return {};
            } catch (const bad_alloc&) {
                waiting = false;
                return errc::out_of_memory;
            } catch (const system_error& failure) {
                waiting = false;
                error = make_error_info(errc::io_error, "asio.timer.arm", {}, failure.code());
                return errc::io_error;
            } catch (...) {
                waiting = false;
                return errc::io_error;
            }
        }
    };
}
namespace raftpp {
    asio_timer::asio_timer(asio::any_io_executor executor)
        : state_(std::make_shared<detail::asio_timer_state>(std::move(executor))) {}
    asio_timer::~asio_timer() {
        state_->close();
    }
    error_code asio_timer::start(unsigned interval, completion tick) noexcept {
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
        state->interval = interval;
        const auto error = state->arm();
        if (error) {
            state->tick.reset();
            return error;
        }
        state->started = true;
        return {};
    }
    void asio_timer::close(close_callback callback) noexcept {
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
    uint64_t asio_timer::time() const noexcept {
        return static_cast<uint64_t>(chrono::duration_cast<chrono::milliseconds>(
                                         chrono::steady_clock::now().time_since_epoch())
                                         .count());
    }
    error_info asio_timer::error_details() const noexcept {
        return state_->error;
    }
}
