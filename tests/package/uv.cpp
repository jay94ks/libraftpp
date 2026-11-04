#include <raftpp/composed_io.hpp>
#include <raftpp/uv/uv.hpp>
#include <uv.h>

int main() {
    uv_loop_t loop;
    if (uv_loop_init(&loop))
        return 1;
    bool closed = false;
    {
        auto backend = raftpp::make_io({.storage = std::make_shared<raftpp::uv_storage>(loop, "."),
                                        .timer = std::make_shared<raftpp::uv_timer>(loop),
                                        .network = std::make_shared<raftpp::tcp_transport>(loop)});
        backend->close([&] { closed = true; });
    }
    uv_run(&loop, UV_RUN_DEFAULT);
    return uv_loop_close(&loop) || !closed;
}
