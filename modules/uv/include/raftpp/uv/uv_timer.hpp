#ifndef RAFTPP_UV_TIMER_HPP
#define RAFTPP_UV_TIMER_HPP

#include <raftpp/timer.hpp>
#include <raftpp/uv/export.hpp>

struct uv_loop_s;
namespace raftpp {
    namespace detail {
        struct uv_timer_state;
    }
    class RAFTPP_UV_API uv_timer final : public timer {
    public:
        explicit uv_timer(uv_loop_s& loop);
        ~uv_timer() override;
        uv_timer(const uv_timer&) = delete;
        uv_timer& operator=(const uv_timer&) = delete;
        error_code start(unsigned interval, completion tick) noexcept override;
        void close(close_callback callback = {}) noexcept override;
        uint64_t time() const noexcept override;
        error_info error_details() const noexcept override;

    private:
        shared<detail::uv_timer_state> state_;
    };
}
#endif
