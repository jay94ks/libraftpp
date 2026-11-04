#include "counter.hpp"
#include <raftpp/composed_io.hpp>
#include <raftpp/config.hpp>
#include <raftpp/node.hpp>
#include <raftpp/uv/uv_storage.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/framed_transport.hpp>
#include <uv.h>
#include <iostream>
#include <thread>
#if RAFTPP_MODULE_ASIO
#include <raftpp/asio/asio.hpp>
#include <asio/io_context.hpp>
#endif
#if RAFTPP_MODULE_LZ4
#include <raftpp/lz4/lz4.hpp>
#endif
#if RAFTPP_MODULE_ZSTD
#include <raftpp/zstd/zstd.hpp>
#endif
#if RAFTPP_MODULE_ZLIB
#include <raftpp/zlib/zlib.hpp>
#endif

int main(int argc, char** argv) {
    using namespace raftpp;
    if (argc < 2 || argc > 4) {
        std::cerr << "usage: raftpp-example-composed DIRECTORY [none|lz4|zstd|zlib] [uv|asio]\n";
        return 1;
    }
    const string_view encoding = argc > 2 ? argv[2] : "none";
    const string_view timer_kind = argc > 3 ? argv[3] : "uv";
    uv_loop_t loop;
    if (uv_loop_init(&loop) != 0)
        return 1;
#if RAFTPP_MODULE_ASIO
    asio::io_context context;
#endif
    auto poll = [&] {
        uv_run(&loop, UV_RUN_NOWAIT);
#if RAFTPP_MODULE_ASIO
        if (context.stopped())
            context.restart();
        context.poll();
#endif
    };
    int result = 0;
    try {
        fs::create_directories(argv[1]);
        shared<compression_codec> codec;
        if (encoding == "none") {
        }
#if RAFTPP_MODULE_LZ4
        else if (encoding == "lz4")
            codec = std::make_shared<lz4_codec>();
#endif
#if RAFTPP_MODULE_ZSTD
        else if (encoding == "zstd")
            codec = std::make_shared<zstd_codec>();
#endif
#if RAFTPP_MODULE_ZLIB
        else if (encoding == "zlib")
            codec = std::make_shared<zlib_codec>();
#endif
        else
            throw invalid_argument("codec is unknown or its module is disabled");
        shared<timer> timer;
        if (timer_kind == "uv")
            timer = std::make_shared<uv_timer>(loop);
#if RAFTPP_MODULE_ASIO
        else if (timer_kind == "asio")
            timer = std::make_shared<asio_timer>(context.get_executor());
#endif
        else
            throw invalid_argument("timer is unknown or its module is disabled");
        auto backend = make_io({.storage = std::make_shared<uv_storage>(loop, argv[1]),
                                .timer = std::move(timer),
                                .network = std::make_shared<framed_transport>(),
                                .snapshot_codec = std::move(codec)});
        auto fsm = std::make_shared<example::counter>();
        node_options options;
        options.snapshot_threshold = 1;
        options.snapshot_trailing = 0;
        node member{1, "local", backend, fsm, options};
        configuration initial;
        if (auto error = initial.add(1, "local", role::voter))
            throw system_error(error);
        const auto bootstrap = member.bootstrap(initial);
        if (bootstrap && bootstrap != errc::cannot_bootstrap)
            throw system_error(bootstrap);
        if (auto error = member.start())
            throw system_error(error);
        buffer command{sizeof(uint64_t)};
        const uint64_t increment = 1;
        std::memcpy(command.data(), &increment, sizeof(increment));
        struct progress {
            bool applied = false, published = false, reading = false, closed = false;
            log_index target = 0;
            error_code failure;
        };
        auto state = std::make_shared<progress>();
        auto wait = [&](auto&& ready) {
            const auto deadline = chrono::steady_clock::now() + chrono::seconds(30);
            while (!ready()) {
                if (chrono::steady_clock::now() >= deadline)
                    throw std::runtime_error("operation timed out");
                poll();
                std::this_thread::sleep_for(chrono::milliseconds(1));
            }
        };
        wait([&] {
            return member.state() == node_state::leader &&
                   backend->capacity() >= options.capacity_threshold;
        });
        if (auto error = member.apply(std::move(command), [state](error_code error, shared<void>) {
                state->failure = error;
                state->applied = true;
            }))
            throw system_error(error);
        wait([&] { return state->applied; });
        if (state->failure)
            throw system_error(state->failure);
        state->target = member.last_applied();
        // Wait for the node's asynchronous snapshot publication before exit.
        wait([&] {
            if (!state->reading && !state->published) {
                state->reading = true;
                if (auto error =
                        backend->snapshot_get([state](error_code error, optional<snapshot> value) {
                            state->reading = false;
                            state->failure = error;
                            state->published = value && value->index >= state->target;
                        }))
                    throw system_error(error);
            }
            if (state->failure)
                throw system_error(state->failure);
            return state->published;
        });
        std::cout << "value=" << fsm->value() << '\n';
        if (auto error = member.close([state] { state->closed = true; }))
            throw system_error(error);
        wait([&] { return state->closed; });
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        result = 1;
    }
    // Facades have left scope; drain their native cancellation/close callbacks.
    do {
        poll();
    } while (uv_loop_alive(&loop));
#if RAFTPP_MODULE_ASIO
    if (context.stopped())
        context.restart();
    context.run();
#endif
    if (uv_loop_close(&loop) != 0)
        result = 1;
    return result;
}
