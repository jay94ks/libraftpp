#include "../composition_support.hpp"
#include <raftpp/config.hpp>
#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>
#include <asio/io_context.hpp>
#include <thread>
#if RAFTPP_WITH_UV
#include <raftpp/uv/uv_storage.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <uv.h>
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

namespace {
    using namespace composition_testing;
    template <typename Step, typename Predicate> void until(Step&& step, Predicate&& predicate) {
        const auto deadline = chrono::steady_clock::now() + chrono::seconds(15);
        while (!predicate()) {
            check(chrono::steady_clock::now() < deadline, "event-loop timeout");
            step();
            std::this_thread::sleep_for(chrono::milliseconds(1));
        }
    }
    void timer_contracts() {
        asio::io_context context;
        auto timer = std::make_shared<asio_timer>(context.get_executor());
        check(timer->start(0, [](error_code) {}) == errc::invalid, "invalid interval");
        check(timer->start(1, {}) == errc::invalid, "empty tick");
        bool inside = false, closed = false;
        unsigned calls = 0;
        weak<asio_timer> owner = timer;
        check(!timer->start(1,
                            [&](error_code error) {
                                check(!error, "tick");
                                inside = true;
                                ++calls;
                                timer->close([&] {
                                    check(!inside, "timer close outside tick");
                                    closed = true;
                                });
                                timer.reset();
                                inside = false;
                            }),
              "timer start");
        check(timer->start(1, [](error_code) {}) == errc::busy, "repeat start");
        context.run();
        check(calls == 1 && closed && owner.expired(), "timer lifetime/drain");
        context.restart();
        timer = std::make_shared<asio_timer>(context.get_executor());
        check(!timer->start(1000, [&](error_code) { ++calls; }), "start pending timer");
        timer->close([&] { closed = true; });
        closed = false;
        timer.reset();
        context.run();
        check(closed && calls == 1, "cancel pending wait");
    }
    void cluster() {
        asio::io_context context;
        array<shared<framed_transport>, 3> networks;
        array<shared<memory_storage>, 3> disks;
        array<shared<machine>, 3> machines;
        array<owned<node>, 3> nodes;
        for (auto& network : networks)
            network = std::make_shared<framed_transport>();
        for (size_t a = 0; a < 3; ++a)
            for (size_t b = a + 1; b < 3; ++b) {
                asio::ip::tcp::acceptor listener{context, {asio::ip::address_v4::loopback(), 0}};
                asio::ip::tcp::socket client{context};
                client.connect(listener.local_endpoint());
                auto accepted = listener.accept();
                listener.close();
                check(!networks[a]->attach(b + 1, "node-" + std::to_string(b + 1),
                                           std::make_shared<asio_stream>(std::move(client))),
                      "attach client");
                check(!networks[b]->attach(a + 1, "node-" + std::to_string(a + 1),
                                           std::make_shared<asio_stream>(std::move(accepted))),
                      "attach server");
            }
        configuration members;
        for (server_id id = 1; id <= 3; ++id)
            check(!members.add(id, "node-" + std::to_string(id), role::voter), "cluster members");
        for (size_t i = 0; i < 3; ++i) {
            disks[i] = std::make_shared<memory_storage>();
            machines[i] = std::make_shared<machine>();
            auto backend = make_io({.storage = disks[i],
                                    .timer = std::make_shared<asio_timer>(context.get_executor()),
                                    .network = networks[i],
                                    .random_seed = static_cast<uint32_t>(i + 9)});
            node_options options;
            options.election_timeout = 100;
            options.heartbeat_timeout = 10;
            options.tracing = tracer::noop();
            nodes[i] = std::make_unique<node>(i + 1, "node-" + std::to_string(i + 1), backend,
                                              machines[i], options);
            check(!nodes[i]->bootstrap(members) && !nodes[i]->start(), "start cluster");
        }
        auto poll = [&] {
            if (context.stopped())
                context.restart();
            context.poll();
        };
        auto leader = [&] {
            for (size_t i = 0; i < 3; ++i)
                if (nodes[i]->state() == node_state::leader)
                    return i;
            return size_t{3};
        };
        until(poll, [&] { return leader() != 3; });
        const auto id = leader();
        unsigned completed = 0;
        check(!nodes[id]->apply(bytes("replicated"),
                                [&](error_code error, shared<void>) {
                                    check(!error, "cluster apply");
                                    ++completed;
                                }),
              "submit cluster command");
        until(poll, [&] {
            return completed == 1 && std::ranges::all_of(machines, [](const auto& machine) {
                       return machine->value == "replicated";
                   });
        });
        unsigned closed = 0;
        for (auto& node : nodes)
            check(!node->close([&] { ++closed; }), "close cluster");
        nodes = {};
        until(poll, [&] { return closed == 3; });
        check(std::ranges::all_of(disks, [](const auto& disk) { return disk->closed; }),
              "storage closed");
    }
#if RAFTPP_WITH_UV
    struct directory {
        fs::path path;
        directory() {
            const auto stamp = chrono::steady_clock::now().time_since_epoch().count();
            path = fs::temp_directory_path() / ("raftpp-composition-" + std::to_string(stamp));
            check(fs::create_directory(path), "temporary directory");
        }
        ~directory() {
            std::error_code ignored;
            fs::remove_all(path, ignored);
        }
    };
    struct environment {
        uv_loop_t loop{};
        asio::io_context context;
        environment() {
            check(uv_loop_init(&loop) == 0, "UV loop");
        }
        void poll() {
            uv_run(&loop, UV_RUN_NOWAIT);
            if (context.stopped())
                context.restart();
            context.poll();
        }
        ~environment() {
            // Component facades have already left scope on both success/failure.
            // Drain native work before the loop storage or directory disappears.
            while (uv_loop_alive(&loop)) {
                poll();
                std::this_thread::sleep_for(chrono::milliseconds(1));
            }
            if (context.stopped())
                context.restart();
            context.run();
            if (uv_loop_close(&loop) != 0)
                std::abort();
        }
    };
    void persistence(shared<compression_codec> codec, bool native_timer) {
        directory root;
        environment execution;
        auto& loop = execution.loop;
        auto& context = execution.context;
        auto step = [&] { execution.poll(); };
        struct progress {
            bool completed = false, published = false, reading = false, closed = false;
            error_code error;
            string restored;
        };
        for (unsigned restart = 0; restart < 2; ++restart) {
            auto state = std::make_shared<progress>();
            auto disk = std::make_shared<uv_storage>(loop, root.path);
            shared<timer> timer;
            if (native_timer)
                timer = std::make_shared<uv_timer>(loop);
            else
                timer = std::make_shared<asio_timer>(context.get_executor());
            auto backend = make_io({.storage = disk,
                                    .timer = std::move(timer),
                                    .network = std::make_shared<framed_transport>(),
                                    .snapshot_codec = codec});
            auto fsm = std::make_shared<machine>();
            node_options options;
            options.tracing = tracer::noop();
            options.snapshot_threshold = 2;
            options.snapshot_trailing = 0;
            auto member = std::make_unique<node>(1, "local", backend, fsm, options);
            if (!restart)
                check(!member->bootstrap(members()), "durable bootstrap");
            check(!member->start(), "durable node start");
            if (restart)
                check(fsm->value == "persisted-state!", "snapshot restore");
            else {
                // Preparing the durable write pool is asynchronous. Preserve the
                // node's normal capacity admission policy instead of disabling it.
                until(step, [&] {
                    return member->state() == node_state::leader &&
                           backend->capacity() >= options.capacity_threshold;
                });
                if (auto error = member->apply(bytes("persisted-state!"),
                                               [state](error_code error, shared<void>) {
                                                   state->error = error;
                                                   state->completed = true;
                                               }))
                    throw system_error(error, "durable submission");
                until(step, [&] { return state->completed; });
                check(!state->error, "durable apply");
                until(
                    [&] {
                        step();
                        if (!state->published && !state->reading) {
                            state->reading = true;
                            if (auto error = backend->snapshot_get(
                                    [state](error_code error, optional<snapshot> value) {
                                        state->reading = false;
                                        state->error = error;
                                        if (value) {
                                            state->restored = text(*value);
                                            state->published = true;
                                        }
                                    }))
                                throw system_error(error, "snapshot get submit");
                        }
                        check(!state->error, "snapshot get");
                    },
                    [&] { return state->published; });
                check(state->restored == "persisted-state!", "decoded durable snapshot");
            }
            check(!member->close([state] { state->closed = true; }), "durable close");
            member.reset();
            backend.reset();
            disk.reset();
            until(step, [&] { return state->closed; });
        }
    }

#endif
}
int main(int argc, char** argv) {
    try {
        check(argc == 2, "suite");
        string_view suite = argv[1];
        if (suite == "timer")
            timer_contracts();
        else if (suite == "cluster")
            cluster();
#if RAFTPP_WITH_UV
        else if (suite == "uv")
            persistence({}, true);
#if RAFTPP_MODULE_LZ4
        else if (suite == "lz4")
            persistence(std::make_shared<lz4_codec>(), false);
#endif
#if RAFTPP_MODULE_ZSTD
        else if (suite == "zstd")
            persistence(std::make_shared<zstd_codec>(), false);
#endif
#if RAFTPP_MODULE_ZLIB
        else if (suite == "zlib")
            persistence(std::make_shared<zlib_codec>(), false);
#endif
#endif
        else
            check(false, "unknown suite");
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
