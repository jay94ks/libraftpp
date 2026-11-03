#include <raftpp.hpp>
#include "byte.hpp"
#include "memory_resource.hpp"
#include <algorithm>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <exception>
#include <iostream>
#include <thread>
#include <termios.h>
#include <uv.h>
#include <sys/socket.h>
#include <unistd.h>
namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    std::exception_ptr failure;
    void check(bool value, r::string_view text) {
        if (value)
            return;
        auto error = std::make_exception_ptr(std::runtime_error(r::string{text}));
        if (!failure)
            failure = error;
        std::rethrow_exception(error);
    }
    struct loop {
        uv_loop_t value{};
        loop() {
            check(!uv_loop_init(&value), "initialize adapter loop");
        }
        ~loop() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (uv_loop_close(&value))
                std::abort();
        }
        template <typename F> void until(F done) {
            auto end = uv_hrtime() + 12000000000ULL;
            while (!done() && uv_hrtime() < end) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                if (failure)
                    std::rethrow_exception(failure);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(done(), "adapter operation completed before deadline");
        }
    };
    struct statistics {
        unsigned releases = 0, accepts = 0, connects = 0, ttys = 0;
        r::borrowed<uv_stream_s> last_tty = nullptr;
    };
    r::uv_stream pipe(uv_loop_t& loop, int fd, r::shared<statistics> counts) {
        auto native = std::make_unique<uv_pipe_t>();
        if (uv_pipe_init(&loop, native.get(), 0)) {
            ::close(fd);
            check(false, "pipe init");
        }
        if (uv_pipe_open(native.get(), fd)) {
            ::close(fd);
            uv_close(reinterpret_cast<uv_handle_t*>(native.release()),
                     [](auto* value) { delete reinterpret_cast<uv_pipe_t*>(value); });
            check(false, "pipe open");
        }
        try {
            auto pointer = reinterpret_cast<uv_stream_t*>(native.get());
            r::uv_stream stream{*pointer, [counts = std::move(counts)](auto* handle) {
                                    ++counts->releases;
                                    delete reinterpret_cast<uv_pipe_t*>(handle);
                                }};
            native.release();
            return stream;
        } catch (...) {
            uv_close(reinterpret_cast<uv_handle_t*>(native.release()),
                     [](auto* value) { delete reinterpret_cast<uv_pipe_t*>(value); });
            throw;
        }
    }
    r::pair<r::uv_stream, r::uv_stream> pipes(uv_loop_t& loop, r::shared<statistics> counts) {
        int fds[2];
        check(!::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, fds), "local pipe pair");
        r::uv_stream first;
        try {
            first = pipe(loop, fds[0], counts);
        } catch (...) {
            ::close(fds[1]);
            throw;
        }
        return {std::move(first), pipe(loop, fds[1], counts)};
    }
    r::pair<r::uv_stream, r::uv_stream> terminals(uv_loop_t& loop, r::shared<statistics> counts) {
        struct descriptor {
            int value = -1;
            ~descriptor() {
                if (value >= 0)
                    (void)::close(value);
            }
            int release() noexcept {
                return std::exchange(value, -1);
            }
        } master, slave;
        master.value = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
        check(master.value >= 0 && !::grantpt(master.value) && !::unlockpt(master.value),
              "create private adapter PTY");
        r::array<char, 128> name{};
        check(!::ptsname_r(master.value, name.data(), name.size()), "resolve private adapter PTY");
        slave.value = ::open(name.data(), O_RDWR | O_NOCTTY | O_CLOEXEC);
        check(slave.value >= 0, "open private adapter slave");
        termios mode{};
        check(!::tcgetattr(slave.value, &mode), "read adapter PTY mode");
        ::cfmakeraw(&mode);
        check(!::tcsetattr(slave.value, TCSANOW, &mode),
              "use binary mode only on newly created PTY");
        auto first = pipe(loop, master.release(), counts);
        auto native = std::make_unique<uv_tty_t>();
        check(!uv_tty_init(&loop, native.get(), slave.value, 1), "initialize adapter TTY");
        try {
            uv_os_fd_t fd;
            check(!uv_fileno(reinterpret_cast<uv_handle_t*>(native.get()), &fd),
                  "query adapter TTY fd");
            if (fd == slave.value)
                (void)slave.release();
            check(!uv_stream_set_blocking(reinterpret_cast<uv_stream_t*>(native.get()), 0),
                  "use nonblocking adapter TTY writes");
            r::uv_stream second{*reinterpret_cast<uv_stream_s*>(native.get()),
                                [counts](uv_stream_s* value) {
                                    if (counts->last_tty == value)
                                        counts->last_tty = nullptr;
                                    ++counts->releases;
                                    delete reinterpret_cast<uv_tty_t*>(value);
                                }};
            counts->last_tty = reinterpret_cast<uv_stream_t*>(native.get());
            ++counts->ttys;
            native.release();
            return {std::move(first), std::move(second)};
        } catch (...) {
            uv_close(reinterpret_cast<uv_handle_t*>(native.release()),
                     [](uv_handle_t* value) { delete reinterpret_cast<uv_tty_t*>(value); });
            throw;
        }
    }
    struct connector;
    struct bus {
        r::map<r::string, connector*> addresses;
        r::shared<statistics> counts = std::make_shared<statistics>();
        bool tty = false;
    };
    struct connector final : r::uv_stream_transport {
        uv_loop_t& loop;
        r::shared<bus> network;
        r::stream_accept_callback accept;
        r::vector<r::stream_connect_callback> pending;
        r::server_id id = 0;
        r::string address;
        r::error_info diagnostic;
        unsigned attempts = 0, immediate_failures = 0, async_failures = 0;
        bool stall = false, closing = false;
        connector(uv_loop_t& loop, r::shared<bus> network)
            : loop(loop), network(std::move(network)) {}
        ~connector() {
            close();
        }
        r::error_code init(r::server_id value, r::string_view name) override {
            id = value;
            address = name;
            network->addresses[address] = this;
            return {};
        }
        r::error_code listen(r::stream_accept_callback callback) override {
            accept = std::move(callback);
            return {};
        }
        r::error_code connect(r::server_id, r::string_view name,
                              r::stream_connect_callback callback) override {
            ++attempts;
            if (immediate_failures) {
                --immediate_failures;
                return r::errc::io_error;
            }
            if (stall) {
                pending.push_back(std::move(callback));
                return {};
            }
            const auto target = network->addresses.find(r::string{name});
            if (async_failures || target == network->addresses.end() || !target->second->accept) {
                if (async_failures)
                    --async_failures;
                diagnostic =
                    r::make_error_info(r::errc::no_connection, "pipe.connect", name,
                                       std::make_error_code(std::errc::connection_refused));
                callback(r::errc::no_connection, {});
                return {};
            }
            auto streams =
                network->tty ? terminals(loop, network->counts) : pipes(loop, network->counts);
            if (network->tty && id % 2 == 0)
                std::swap(streams.first, streams.second);
            ++network->counts->connects;
            ++network->counts->accepts;
            auto accepted = target->second->accept;
            accepted(id, address, std::move(streams.second));
            callback({}, std::move(streams.first));
            return {};
        }
        void close(r::close_callback callback = {}) noexcept override {
            if (!closing) {
                closing = true;
                accept = {};
                network->addresses.erase(address);
                auto callbacks = std::move(pending);
                for (auto& done : callbacks)
                    done(r::errc::canceled, {});
            }
            if (callback)
                callback();
        }
        r::error_info error_details() const noexcept override {
            return diagnostic;
        }
    };
    struct endpoint {
        r::shared<connector> raw;
        r::owned<r::stream_transport> messages;
        endpoint(loop& events, r::shared<bus> network, r::server_id id, r::string name,
                 size_t chunk = 65536) {
            raw = std::make_shared<connector>(events.value, std::move(network));
            r::stream_transport_options options;
            options.connect_retry = 1;
            options.read_buffer_size = chunk;
            messages = std::make_unique<r::stream_transport>(events.value, raw, options);
            check(!messages->init(id, name), "initialize non-TCP raw plugin adapter");
        }
        void close(loop& events) {
            if (!messages)
                return;
            bool done = false;
            messages->close([&] { done = true; });
            events.until([&] { return done; });
            messages.reset();
        }
    };
    r::shared<r::message> message(unsigned kind, r::server_id id, r::string name) {
        auto result = std::make_shared<r::message>();
        result->server_id = id;
        result->server_address = std::move(name);
        switch (kind % 6) {
        case 0: {
            r::append_entries value;
            value.term = 7;
            value.prev_log_index = 3;
            value.prev_log_term = 6;
            value.leader_commit = 3;
            for (unsigned i = 0; i < 3; ++i) {
                r::buffer bytes{8192 + i};
                std::ranges::fill(bytes.bytes(), static_cast<r::byte>(i + 1));
                value.entries.push_back(std::make_shared<r::entry>(
                    r::entry{7, r::entry_type::command, r::shared_buffer{std::move(bytes)}}));
            }
            result->payload = std::move(value);
            break;
        }
        case 1:
            result->payload = r::append_entries_result{2, 7, 0, 4, 1, 65535};
            break;
        case 2:
            result->payload = r::request_vote{2, 7, 99, 4, 6, true, true};
            break;
        case 3:
            result->payload = r::request_vote_result{2, 7, true, true, 1, 65535};
            break;
        case 4: {
            r::install_snapshot value;
            value.term = 7;
            value.last_index = 4;
            value.last_term = 6;
            value.configuration_index = 1;
            check(!value.configuration.add(1, "node", r::role::voter), "snapshot membership");
            r::buffer bytes{16384};
            std::ranges::fill(bytes.bytes(), r::byte{42});
            value.data = r::shared_buffer{std::move(bytes)};
            result->payload = std::move(value);
            break;
        }
        case 5:
            result->payload = r::timeout_now{0, 7, 4, 6};
            break;
        }
        return result;
    }
    r::vector<r::byte> wire(const r::message& value) {
        r::encoded_message encoded;
        check(!r::encode_message(value, encoded), "encode test RPC");
        r::vector<r::byte> result{encoded.header.bytes().begin(), encoded.header.bytes().end()};
        for (auto& data : encoded.payload)
            result.insert(result.end(), data.bytes().begin(), data.bytes().end());
        return result;
    }
    r::string tcp_address() {
        const auto fd = ::socket(AF_INET, SOCK_STREAM, 0);
        check(fd >= 0, "reserve TCP socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        const auto bound = ::bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof address);
        socklen_t size = sizeof address;
        const auto queried = getsockname(fd, reinterpret_cast<sockaddr*>(&address), &size);
        ::close(fd);
        check(!bound && !queried, "reserve TCP adapter address");
        return "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
    }
    void tcp() {
        loop events;
        auto raw = std::make_shared<r::tcp_stream_transport>(events.value);
        r::stream_transport adapter{events.value, raw};
        r::tcp_transport peer{events.value};
        const auto first = tcp_address(), second = tcp_address();
        check(!adapter.init(1, first) && !peer.init(2, second),
              "initialize adapter/direct TCP endpoints");
        unsigned received = 0, replied = 0, completed = 0;
        check(!peer.start([&](r::message value) {
            check(value.server_id == 1 && wire(value) == wire(*message(received, 0, "")),
                  "direct TCP decodes adapter wire");
            ++received;
        }),
              "listen with direct TCP codec");
        check(!adapter.start([&](r::message value) {
            check(value.server_id == 2 && wire(value) == wire(*message(replied, 0, "")),
                  "adapter decodes direct TCP wire");
            ++replied;
        }),
              "listen with stream adapter");
        for (unsigned i = 0; i < 6; ++i) {
            check(!adapter.send(message(i, 2, second),
                                [&](auto status) {
                                    check(!status, "adapter TCP write");
                                    ++completed;
                                }),
                  "send adapter TCP RPC");
            check(!peer.send(message(i, 1, first),
                             [&](auto status) {
                                 check(!status, "direct TCP write");
                                 ++completed;
                             }),
                  "send direct TCP RPC");
        }
        events.until([&] { return completed == 12 && received == 6 && replied == 6; });
        bool a = false, b = false;
        adapter.close([&] { a = true; });
        peer.close([&] { b = true; });
        events.until([&] { return a && b; });
    }
    void exchange() {
        loop events;
        auto network = std::make_shared<bus>();
        endpoint first{events, network, 1, "first", 7}, second{events, network, 2, "second", 3};
        r::vector<r::message> received, replies;
        unsigned completed = 0;
        check(!first.messages->start([&](auto value) { replies.push_back(std::move(value)); }),
              "start first");
        check(!second.messages->start([&](auto value) { received.push_back(std::move(value)); }),
              "start second");
        r::vector<r::weak<const r::message>> owners;
        for (unsigned i = 0; i < 6; ++i) {
            auto value = message(i, 2, "second");
            owners.push_back(value);
            check(!first.messages->send(value,
                                        [&, i](auto error) {
                                            check(!error && !owners[i].expired(),
                                                  "owned message survives completion");
                                            ++completed;
                                        }),
                  "send all RPCs");
            check(!second.messages->send(message(i, 1, "first"),
                                         [&](auto error) {
                                             check(!error, "reverse RPC completion");
                                             ++completed;
                                         }),
                  "reverse all RPCs");
        }
        events.until(
            [&] { return completed == 12 && received.size() == 6 && replies.size() == 6; });
        for (unsigned i = 0; i < 6; ++i) {
            check(received[i].server_id == 1 && received[i].server_address == "first" &&
                      wire(received[i]) == wire(*message(i, 0, "")),
                  "identity/payload preserve fragmented pipe frames");
            check(wire(replies[i]) == wire(*message(i, 0, "")) && owners[i].expired(),
                  "reverse content and request release");
        }
        first.close(events);
        second.close(events);
        check(network->counts->releases == 4,
              "all independently owned pipe allocations drained before close");
    }
    void tty() {
        {
            loop events;
            auto network = std::make_shared<bus>();
            network->tty = true;
            endpoint first{events, network, 1, "first", 3}, second{events, network, 2, "second", 5};
            struct results {
                r::vector<r::message> received, replies;
                r::vector<r::weak<const r::message>> owners;
                unsigned completed = 0;
            };
            auto state = std::make_shared<results>();
            check(!first.messages->start([state](r::message value) {
                state->replies.push_back(std::move(value));
            }) && !second.messages->start([state](r::message value) {
                state->received.push_back(std::move(value));
            }),
                  "start private PTY message adapters");
            for (unsigned i = 0; i < 6; ++i) {
                auto value = message(i, 2, "second");
                state->owners.push_back(value);
                check(!first.messages->send(
                          value,
                          [state, i](r::error_code error) {
                              check(!error && !state->owners[i].expired(),
                                    "PTY adapter message owner survives write completion");
                              ++state->completed;
                          }),
                      "send each RPC over PTY master");
                check(!second.messages->send(message(i, 1, "first"),
                                             [state](r::error_code error) {
                                                 check(!error,
                                                       "reverse TTY adapter write succeeds");
                                                 ++state->completed;
                                             }),
                      "send each RPC over TTY slave");
            }
            events.until([&] {
                return state->completed == 12 && state->received.size() == 6 &&
                       state->replies.size() == 6;
            });
            for (unsigned i = 0; i < 6; ++i) {
                check(
                    state->received[i].server_id == 1 &&
                        state->received[i].server_address == "first" &&
                        state->replies[i].server_id == 2 &&
                        state->replies[i].server_address == "second" &&
                        wire(state->received[i]) == wire(*message(i, 0, "")) &&
                        wire(state->replies[i]) == wire(*message(i, 0, "")) &&
                        state->owners[i].expired(),
                    "PTY adapter preserves all RPC bytes, identity, order and final request release");
            }
            first.close(events);
            second.close(events);
            check(network->counts->ttys == 2 && network->counts->connects == 2 &&
                      network->counts->releases == 4 && !network->counts->last_tty,
                  "PTY adapter close drains both native stream types and independent deleters");
        }
        {
            loop events;
            auto network = std::make_shared<bus>();
            network->tty = true;
            endpoint sender{events, network, 2, "sender"};
            r::uv_stream unread;
            auto sink = std::make_shared<connector>(events.value, network);
            check(!sink->init(3, "sink") && !sink->listen([&](auto, auto, r::uv_stream value) {
                unread = std::move(value);
            }),
                  "prepare unread PTY receiver");
            check(!sender.messages->start([](auto) {}), "start backpressured TTY sender");
            auto value = message(0, 3, "sink");
            auto& append = std::get<r::append_entries>(value->payload);
            append.entries.clear();
            append.entries.push_back(std::make_shared<const r::entry>(
                r::entry{7, r::entry_type::command, r::shared_buffer{r::buffer{4 * 1024 * 1024}}}));
            struct results {
                r::weak<const r::message> owner;
                unsigned completed = 0;
                bool closed = false;
            };
            auto state = std::make_shared<results>();
            state->owner = value;
            check(
                !sender.messages->send(
                    value,
                    [state, counts = network->counts](r::error_code error) {
                        check(
                            error == r::errc::canceled && !state->owner.expired() &&
                                !state->closed && counts->releases == 0,
                            "pending TTY adapter write cancels with its message alive before native release/adapter close");
                        ++state->completed;
                    }),
                "submit TTY frame larger than driver buffers");
            value.reset();
            check(network->counts->last_tty && network->counts->last_tty->write_queue_size > 0,
                  "TTY adapter has an actual backpressured native write");
            sender.messages->close([state, counts = network->counts] {
                check(state->completed == 1 && counts->releases == 1,
                      "TTY sender drains native write/deleter before close callback");
                state->closed = true;
            });
            sender.messages.reset();
            sender.raw.reset();
            events.until([&] { return state->closed; });
            check(state->owner.expired() && state->completed == 1,
                  "TTY adapter wrapper destruction releases the accepted message once");
            check(!unread.close(), "close independent PTY master after sender destruction");
            sink->close();
            sink.reset();
            events.until([&] { return network->counts->releases == 2; });
        }
        {
            loop events;
            auto network = std::make_shared<bus>();
            endpoint receiver{events, network, 3, "receiver", 2};
            struct results {
                unsigned delivered = 0;
                bool written = false;
            };
            auto state = std::make_shared<results>();
            check(!receiver.messages->start([state](auto) { ++state->delivered; }),
                  "start truncated TTY receiver");
            auto streams = terminals(events.value, network->counts);
            receiver.raw->accept(9, "pty-peer", std::move(streams.second));
            const auto encoded = wire(*message(2, 0, ""));
            check(!streams.first.write(
                      r::shared_buffer::copy(r::view<const r::byte>{encoded}.first(8)),
                      [state](r::error_code error) {
                          check(!error, "partial TTY preamble write succeeds");
                          state->written = true;
                      }),
                  "write truncated PTY preamble");
            events.until([&] { return state->written; });
            check(state->delivered == 0, "partial PTY frame is not published");
            check(!streams.first.close(), "close PTY master during header assembly");
            events.until([&] {
                return receiver.messages->error_details().code == r::errc::no_connection &&
                       network->counts->releases == 2;
            });
            check(receiver.messages->error_details().cause.value() == UV_EOF &&
                      state->delivered == 0,
                  "TTY EOF preserves native cause and never delivers a partial message");
            receiver.close(events);
        }
        std::cout << "TTY message adapter: 3 cases, 12 complete RPCs\n";
    }

    void retry() {
        loop events;
        auto network = std::make_shared<bus>();
        endpoint first{events, network, 1, "first"};
        first.raw->stall = true;
        check(!first.messages->start([](auto) {}), "retry sender start");
        unsigned failed = 0, canceled = 0, delivered = 0;
        r::vector<r::error_code> results(7);
        for (unsigned i = 0; i < 7; ++i)
            check(!first.messages->send(message(2, 2, "second"),
                                        [&, i](auto error) {
                                            results[i] = error;
                                            if (error == r::errc::no_connection)
                                                ++failed;
                                            else if (error == r::errc::canceled)
                                                ++canceled;
                                            else {
                                                check(!error, "retry eventually writes");
                                                ++delivered;
                                            }
                                        }),
                  "queue pending send");
        check(first.raw->pending.size() == 1, "one outstanding connect for seven sends");
        first.raw->stall = false;
        auto callback = std::move(first.raw->pending.front());
        first.raw->pending.clear();
        first.raw->diagnostic =
            r::make_error_info(r::errc::no_connection, "pipe.refused", {},
                               std::make_error_code(std::errc::connection_refused));
        callback(r::errc::no_connection, {});
        check(failed == 4 && canceled == 0 &&
                  first.messages->error_details().cause == std::errc::connection_refused,
              "asynchronous failure evicts oldest four, retains newest three");
        endpoint second{events, network, 2, "second"};
        unsigned received = 0;
        check(!second.messages->start([&](auto) { ++received; }), "start late receiver");
        first.raw->immediate_failures = 1;
        first.messages->set_connect_retry_delay(1);
        events.until([&] { return delivered == 3 && received == 3; });
        check(first.raw->attempts == 3, "submission failure schedules retry without eviction");
        first.raw->stall = true;
        check(!first.messages->send(message(2, 2, "changed"),
                                    [&](auto error) {
                                        check(error == r::errc::canceled &&
                                                  !first.messages->error_details().cause,
                                              "address-change request canceled with fresh cause");
                                        ++canceled;
                                    }),
              "change peer address");
        first.close(events);
        second.close(events);
        check(canceled == 1, "closing drains pending raw connect");
    }
    void close_test() {
        loop events;
        auto network = std::make_shared<bus>();
        endpoint first{events, network, 1, "first"};
        first.raw->stall = true;
        check(!first.messages->start([](auto) {}), "close sender start");
        bool sent = false, closed = false;
        check(!first.messages->send(message(2, 2, "missing"),
                                    [&](auto error) {
                                        check(error == r::errc::canceled && !closed,
                                              "send cancels before close completion");
                                        sent = true;
                                    }),
              "stall send");
        r::close_callback callback = [&] {
            check(sent, "drain pending before close callback");
            closed = true;
        };
        allocation_failure::after(0);
        first.messages->close(std::move(callback));
        allocation_failure::after(-1);
        first.messages.reset();
        events.until([&] { return closed; });
        endpoint active{events, network, 2, "active"}, remote{events, network, 3, "remote"};
        unsigned received = 0;
        check(!active.messages->start([](auto) {}) &&
                  !remote.messages->start([&](auto) { ++received; }),
              "active close endpoints");
        bool completed = false;
        check(!active.messages->send(message(2, 3, "remote"),
                                     [&](auto error) {
                                         check(!error, "establish active stream");
                                         completed = true;
                                     }),
              "first send");
        events.until([&] { return completed && received == 1; });
        bool drained = false;
        r::close_callback close = [&] { drained = true; };
        allocation_failure::after(0);
        active.messages->close(std::move(close));
        allocation_failure::after(-1);
        active.messages.reset();
        events.until([&] { return drained; });
        remote.close(events);
        check(network->counts->releases == 2,
              "wrapper destruction and OOM close drain native streams");
        endpoint writer{events, network, 4, "writer"};
        auto sink = std::make_shared<connector>(events.value, network);
        check(!sink->init(5, "sink"), "initialize unread pipe sink");
        r::uv_stream unread;
        check(!sink->listen([&](auto, auto, r::uv_stream stream) { unread = std::move(stream); }),
              "retain unread accepted stream");
        check(!writer.messages->start([](auto) {}), "pending-write sender start");
        auto large = std::make_shared<r::message>();
        large->server_id = 5;
        large->server_address = "sink";
        r::append_entries payload;
        payload.term = 1;
        payload.entries.push_back(std::make_shared<r::entry>(
            r::entry{1, r::entry_type::command, r::shared_buffer{r::buffer{8 * 1024 * 1024}}}));
        large->payload = std::move(payload);
        r::weak<const r::message> owner = large;
        bool write_done = false, write_closed = false;
        check(!writer.messages->send(
                  large,
                  [&](auto status) {
                      check(status == r::errc::canceled && !owner.expired() && !write_closed,
                            "accepted native write cancels before close and retains message");
                      write_done = true;
                  }),
              "queue native write to unread stream");
        large.reset();
        r::close_callback finish = [&] {
            check(write_done && owner.expired(),
                  "close follows write callback and request release");
            write_closed = true;
        };
        allocation_failure::after(0);
        writer.messages->close(std::move(finish));
        allocation_failure::after(-1);
        writer.messages.reset();
        events.until([&] { return write_closed; });
        check(!unread.close(), "release plugin sink stream");
        sink->close();
    }
    void receive_failure() {
        loop events;
        auto network = std::make_shared<bus>();
        endpoint receiver{events, network, 1, "receiver"};
        unsigned delivered = 0;
        check(!receiver.messages->start([&](auto) {
            ++delivered;
            throw std::runtime_error("receive rejected");
        }),
              "failure receiver start");
        {
            auto streams = pipes(events.value, network->counts);
            receiver.raw->accept(9, "malformed", std::move(streams.second));
            r::buffer invalid{16};
            invalid.bytes()[0] = r::byte{1};
            check(!streams.first.write(r::shared_buffer{std::move(invalid)}),
                  "write type-1 preamble with malformed zero header size");
            events.until(
                [&] { return receiver.messages->error_details().code == r::errc::malformed; });
            check(receiver.messages->error_details().text().find("malformed") !=
                      r::string_view::npos,
                  "malformed input owns peer diagnostics");
        }
        {
            auto streams = pipes(events.value, network->counts);
            receiver.raw->accept(9, "oversized", std::move(streams.second));
            r::buffer input{16};
            r::detail::byte_writer encoded{input.bytes()};
            encoded.write<r::uint64_t>(1);
            encoded.write<r::uint64_t>(8 * 1024 * 1024 + 1);
            check(!streams.first.write(r::shared_buffer{std::move(input)}),
                  "send oversized header declaration");
            events.until(
                [&] { return receiver.messages->error_details().code == r::errc::too_big; });
        }
        {
            auto streams = pipes(events.value, network->counts);
            receiver.raw->accept(9, "EOF", std::move(streams.second));
            check(!streams.first.close(), "close remote peer before RPC");
            events.until(
                [&] { return receiver.messages->error_details().code == r::errc::no_connection; });
            check(receiver.messages->error_details().cause.value() == UV_EOF,
                  "native EOF cause propagates through adapter");
        }
        auto streams = pipes(events.value, network->counts);
        receiver.raw->accept(9, "throws", std::move(streams.second));
        const auto bytes = wire(*message(2, 0, ""));
        check(!streams.first.write(r::shared_buffer::copy(bytes)),
              "write valid RPC before throwing receive");
        events.until([&] { return delivered == 1; });
        check(receiver.messages->error_details().code == r::errc::invalid,
              "receive exception contained and closes adapter");
        receiver.close(events);
    }
    void allocations() {
        loop events;
        auto network = std::make_shared<bus>();
        endpoint first{events, network, 1, "first"}, second{events, network, 2, "second"};
        unsigned received = 0;
        check(!first.messages->start([](auto) {}) &&
                  !second.messages->start([&](auto) { ++received; }),
              "allocation endpoints");
        bool done = false;
        check(!first.messages->send(message(2, 2, "second"),
                                    [&](auto error) {
                                        check(!error, "prime stream");
                                        done = true;
                                    }),
              "prime write");
        events.until([&] { return done && received == 1; });
        auto value = message(2, 2, "second");
        unsigned failed = 0, accepted = 0;
        for (std::ptrdiff_t limit = 0; limit < 18; ++limit) {
            bool complete = false;
            r::completion callback = [&](auto error) {
                check(!error, "accepted allocation-sweep write");
                complete = true;
            };
            allocation_failure::after(limit);
            const auto error = first.messages->send(value, std::move(callback));
            allocation_failure::after(-1);
            if (error) {
                check((error == r::errc::out_of_memory || error == r::errc::io_error) && !complete,
                      "allocation rejection never invokes callback");
                ++failed;
            } else {
                ++accepted;
                events.until([&] { return complete; });
            }
        }
        check(failed > 3 && accepted > 0,
              "sweep covers adapter encoding/list and stream request storage");
        // Allocate incoming streams before injecting failure into adapter admission.
        auto extra = pipes(events.value, network->counts);
        const auto baseline = network->counts->releases;
        r::stream_accept_callback accept = second.raw->accept;
        r::string name = "allocation-failed-peer";
        allocation_failure::after(0);
        accept(4, std::move(name), std::move(extra.second));
        allocation_failure::after(-1);
        check(second.messages->error_details().code == r::errc::out_of_memory,
              "failed incoming admission reports OOM");
        bool closed = false;
        second.messages->close([&] { closed = true; });
        events.until([&] { return closed; });
        check(network->counts->releases >= baseline + 2,
              "close waits for admitted and orphaned stream deleters");
        first.close(events);
        second.messages.reset();
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-adapter-XXXXXX";
            check(mkdtemp(name.data()), "private adapter directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    struct counter final : r::state_machine {
        unsigned applied = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>& result) override {
            ++applied;
            result = std::make_shared<unsigned>(applied);
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>&) override {
            return r::errc::invalid;
        }
        r::error_code restore(r::shared_buffer) override {
            return r::errc::invalid;
        }
    };
    void uv_io() {
        loop events;
        auto network = std::make_shared<bus>();
        r::array<directory, 3> directories;
        r::array<r::shared<counter>, 3> fsms;
        r::array<r::shared<r::uv_io>, 3> backends;
        r::array<r::owned<r::node>, 3> nodes;
        r::configuration membership;
        for (unsigned i = 0; i < 3; ++i)
            check(!membership.add(i + 1, "pipe-" + std::to_string(i + 1), r::role::voter),
                  "three pipe voters");
        for (unsigned i = 0; i < 3; ++i) {
            auto connector = std::make_shared<struct connector>(events.value, network);
            auto transport = std::make_shared<r::stream_transport>(events.value, connector);
            r::uv_io_options io;
            io.segment_size = 4096;
            io.disk_retry = 1;
            backends[i] =
                std::make_shared<r::uv_io>(events.value, directories[i].path, transport, io);
            fsms[i] = std::make_shared<counter>();
            r::node_options options;
            options.election_timeout = 150;
            options.heartbeat_timeout = 30;
            options.snapshot_threshold = 100000;
            options.capacity_threshold = 0;
            nodes[i] = std::make_unique<r::node>(i + 1, "pipe-" + std::to_string(i + 1),
                                                 backends[i], fsms[i], options);
            check(!nodes[i]->bootstrap(membership) && !nodes[i]->start(),
                  "bootstrap/start native disk with pipe stream adapter");
        }
        int leader = -1;
        events.until([&] {
            for (unsigned i = 0; i < 3; ++i)
                if (nodes[i]->state() == r::node_state::leader)
                    leader = static_cast<int>(i);
            return leader >= 0;
        });
        unsigned complete = 0;
        for (unsigned i = 0; i < 4; ++i) {
            const auto status = nodes[static_cast<unsigned>(leader)]->apply(
                r::buffer{8}, [&](r::error_code error, r::shared<void> result) {
                    check(!error && result, "native consensus apply result");
                    ++complete;
                });
            check(!status, "submit replicated command: " + status.message());
        }
        events.until([&] {
            return complete == 4 && std::all_of(fsms.begin(), fsms.end(),
                                                [](const auto& f) { return f->applied == 4; });
        });
        unsigned closed = 0;
        for (auto& node : nodes)
            check(!node->close([&] { ++closed; }), "close native pipe cluster");
        events.until([&] { return closed == 3; });
        for (auto& node : nodes)
            node.reset();
        for (auto& backend : backends)
            backend.reset();
        check(network->counts->connects >= 3 &&
                  network->counts->releases == network->counts->connects * 2,
              "native I/O closes every plugin stream before node completion");
    }
}
int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        check(argc == 2, "one adapter suite");
        const r::string_view suite = argv[1];
        if (suite == "exchange")
            exchange();
        else if (suite == "tcp")
            tcp();
        else if (suite == "retry")
            retry();
        else if (suite == "close")
            close_test();
        else if (suite == "receive_failure")
            receive_failure();
        else if (suite == "allocations")
            allocations();
        else if (suite == "uv_io")
            uv_io();
        else if (suite == "tty")
            tty();
        else
            check(false, "unknown adapter suite");
        if (failure)
            std::rethrow_exception(failure);
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
