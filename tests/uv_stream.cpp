#include <raftpp.hpp>
#include "byte.hpp"
#include <algorithm>
#include <csignal>
#include <exception>
#include <fcntl.h>
#include <iostream>
#include <thread>
#include <termios.h>
#include <uv.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/ioctl.h>

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
            check(!uv_loop_init(&value), "initialize stream loop");
        }
        ~loop() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (uv_loop_close(&value))
                std::abort();
        }
        template <typename F> void until(F done) {
            const auto deadline = uv_hrtime() + 8000000000ULL;
            while (!done() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                if (failure)
                    std::rethrow_exception(failure);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(done(), "stream operation completed before deadline");
        }
    };
    r::string address() {
        const int fd = ::socket(AF_INET, SOCK_STREAM, 0);
        check(fd >= 0, "reserve socket");
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (::bind(fd, reinterpret_cast<sockaddr*>(&endpoint), sizeof endpoint)) {
            ::close(fd);
            check(false, "reserve port");
        }
        socklen_t size = sizeof endpoint;
        const auto status = getsockname(fd, reinterpret_cast<sockaddr*>(&endpoint), &size);
        ::close(fd);
        check(!status, "get reserved port");
        return "127.0.0.1:" + std::to_string(ntohs(endpoint.sin_port));
    }
    r::shared_buffer bytes(r::string_view text) {
        return r::shared_buffer::copy({reinterpret_cast<const r::byte*>(text.data()), text.size()});
    }
    struct pair {
        loop events;
        r::owned<r::tcp_stream_transport> server =
            std::make_unique<r::tcp_stream_transport>(events.value);
        r::owned<r::tcp_stream_transport> client =
            std::make_unique<r::tcp_stream_transport>(events.value);
        r::uv_stream incoming, outgoing;
        r::string endpoint = address();
        bool server_closed = false, client_closed = false;
        pair() {
            check(!server->init(2, endpoint) && !client->init(1, "sender:8080"),
                  "initialize stream endpoints");
            check(!server->listen([this](r::server_id id, r::string name, r::uv_stream stream) {
                check(id == 1 && name == "sender:8080" && static_cast<bool>(stream),
                      "owned handshake identity and stream");
                incoming = std::move(stream);
            }),
                  "listen for raw streams");
            check(!client->connect(2, endpoint,
                                   [this](r::error_code error, r::uv_stream stream) {
                                       check(!error && static_cast<bool>(stream),
                                             "connect hands off a live stream");
                                       outgoing = std::move(stream);
                                   }),
                  "submit raw connect");
            events.until([this] { return incoming && outgoing; });
        }
        void connectors_close() {
            if (server)
                server->close([this] { server_closed = true; });
            if (client)
                client->close([this] { client_closed = true; });
            events.until([this] { return server_closed && client_closed; });
            server.reset();
            client.reset();
        }
        void close() {
            check(!incoming.close() && !outgoing.close(), "close handed-off streams");
            connectors_close();
            while (uv_run(&events.value, UV_RUN_NOWAIT)) {
            }
        }
    };
    void exchange() {
        pair values;
        values.connectors_close();
        check(values.incoming && values.outgoing,
              "connector close preserves handed-off stream lifetime");
        const auto old = values.outgoing.native_handle();
        r::uv_stream moved{std::move(values.outgoing)};
        check(!values.outgoing && moved.native_handle() == old,
              "moving wrapper keeps native handle stationary");
        values.outgoing = std::move(moved);
        r::vector<r::shared_buffer> received;
        r::string text, reply;
        unsigned completed = 0;
        check(!values.incoming.read_start(
                  [&](r::error_code error, r::shared_buffer data) {
                      check(!error, "read data after connector destruction");
                      text.append(reinterpret_cast<const char*>(data.bytes().data()), data.size());
                      received.push_back(std::move(data));
                  },
                  3),
              "start owned chunk reads");
        check(values.incoming.read_start([](auto, auto) {}) == r::errc::busy,
              "duplicate read subscription rejected");
        check(!values.outgoing.read_start([&](r::error_code error, r::shared_buffer data) {
            check(!error, "full duplex reply");
            reply.append(reinterpret_cast<const char*>(data.bytes().data()), data.size());
        }),
              "read replies");
        auto first = bytes("hello "), second = bytes("stream");
        r::weak<const r::byte> owner = first.storage();
        r::array<r::shared_buffer, 2> data{first, second};
        check(!values.outgoing.write(data,
                                     [&](r::error_code error) {
                                         check(!error && !owner.expired(),
                                               "write retains buffers through completion");
                                         ++completed;
                                     }),
              "scatter write accepted");
        first = {};
        second = {};
        data = {};
        check(!values.incoming.write(bytes("reply"),
                                     [&](r::error_code error) {
                                         check(!error, "reverse write completion");
                                         ++completed;
                                     }),
              "reverse write accepted");
        values.events.until(
            [&] { return text == "hello stream" && reply == "reply" && completed == 2; });
        check(owner.expired(), "write releases payload after callback");
        check(!values.incoming.read_stop() && !values.outgoing.read_stop(), "stop subscriptions");
        values.close();
        r::string retained;
        for (const auto& item : received)
            retained.append(reinterpret_cast<const char*>(item.bytes().data()), item.size());
        check(retained == "hello stream", "read chunks outlive connector and native streams");
    }
    void cancellation() {
        pair values;
        unsigned writes = 0, closes = 0;
        bool close_done = false;
        r::shared_buffer payload{r::buffer{8 * 1024 * 1024}};
        r::weak<const r::byte> owner = payload.storage();
        check(!values.outgoing.write(payload,
                                     [&](r::error_code error) {
                                         check(!close_done && !owner.expired() &&
                                                   (!error || error == r::errc::canceled),
                                               "accepted writes drain before close callback");
                                         ++writes;
                                     }),
              "queue pending stream write");
        payload = {};
        check(!values.outgoing.close([&] {
            close_done = true;
            ++closes;
        }),
              "register stream close");
        check(!values.outgoing.close([&] { ++closes; }), "additional close observer");
        values.events.until([&] { return close_done && closes == 2; });
        check(writes == 1 && owner.expired(), "write completes once and releases bytes");
        check(values.outgoing.write(bytes("late"), [&](auto) { ++writes; }) == r::errc::shutdown &&
                  writes == 1,
              "immediate failure invokes no callback");
        values.close();
        loop events;
        auto target = std::make_unique<r::tcp_stream_transport>(events.value);
        check(!target->init(5, "cancel:8080"), "initialize canceled connector");
        unsigned callbacks = 0;
        bool connector_closed = false;
        check(!target->connect(99, "localhost:1",
                               [&](r::error_code error, r::uv_stream stream) {
                                   check(error == r::errc::canceled && !stream && !connector_closed,
                                         "pending DNS/connect canceled before connector close");
                                   ++callbacks;
                               }),
              "submit cancelable connection");
        target->close([&] { connector_closed = true; });
        target.reset();
        events.until([&] { return connector_closed; });
        check(callbacks == 1, "canceled connect completes once after wrapper destruction");
    }
    void failure_cases() {
        loop events;
        r::tcp_stream_transport value{events.value};
        unsigned callbacks = 0;
        check(value.connect(1, "localhost:1", [&](auto, auto) { ++callbacks; }) == r::errc::invalid,
              "connect requires init");
        check(value.init(0, "bad") == r::errc::bad_id && !value.init(1, "sender:8080"),
              "failed init can be retried");
        check(value.set_bind_address("127.0.0.1:0") == r::errc::busy,
              "bind policy freezes at init");
        check(!value.connect(0, "127.0.0.1:1",
                             [&](r::error_code error, r::uv_stream stream) {
                                 check(error == r::errc::no_connection && !stream,
                                       "asynchronous refused connection reports failure once");
                                 ++callbacks;
                             }),
              "connect accepts diagnostic-only remote ID zero as in C");
        events.until([&] { return callbacks == 1; });
        check(value.error_details().cause == std::errc::connection_refused,
              "native refusal cause preserved");
        bool closed = false;
        value.close([&] { closed = true; });
        events.until([&] { return closed; });
        check(value.connect(1, "localhost:1", [&](auto, auto) { ++callbacks; }) ==
                      r::errc::shutdown &&
                  callbacks == 1,
              "closed connector has no completion for immediate rejection");
    }
    void adoption() {
        loop events;
        struct probe {
            unsigned releases = 0;
            int tag = 0;
        };
        auto result = std::make_shared<probe>();
        auto native = std::make_unique<uv_tcp_t>();
        check(!uv_tcp_init(&events.value, native.get()), "initialize adoptable native stream");
        native->data = &result->tag;
        r::stream_deleter release = [result](uv_stream_s* stream) {
            check(stream->data == &result->tag,
                  "native data restored for the original allocation deleter");
            ++result->releases;
            delete reinterpret_cast<uv_tcp_t*>(stream);
        };
        auto forwarded = release;
        allocation_failure::after(0);
        bool rejected = false;
        try {
            r::uv_stream invalid{*reinterpret_cast<uv_stream_s*>(native.get()),
                                 std::move(forwarded)};
        } catch (const std::bad_alloc&) {
            rejected = true;
        }
        allocation_failure::after(-1);
        check(rejected && native->data == &result->tag && result->releases == 0,
              "failed adoption retains caller ownership");
        r::uv_stream owned{*reinterpret_cast<uv_stream_s*>(native.get()), release};
        native.release();
        unsigned callbacks = 0;
        check(owned.write(bytes("not connected"), [&](auto) { ++callbacks; }) != r::error_code{} &&
                  callbacks == 0,
              "native immediate write failure does not complete");
        unsigned observed = 0;
        r::close_callback done = [&] { ++observed; };
        allocation_failure::after(0);
        const auto status = owned.close(std::move(done));
        allocation_failure::after(-1);
        check(!status && !owned, "first close observer requires no additional allocation");
        r::close_callback additional = [&] { observed += 100; };
        allocation_failure::after(0);
        const auto second = owned.close(std::move(additional));
        allocation_failure::after(-1);
        check(second == r::errc::out_of_memory,
              "additional observer allocation failure preserves the original close");
        check(!owned.close(), "close without allocating observer");
        events.until([&] { return result->releases == 1; });
        check(observed == 1, "only accepted close observer completes once");
        check(!owned.native_handle() && !owned && !owned.close(),
              "closed stream is inert and idempotent");
    }
    void pipes() {
        struct pipe_pair {
            loop events;
            int tags[2]{};
            unsigned releases = 0;
            bool restored = true;
            r::uv_stream reader, writer;
            pipe_pair() {
                int descriptors[2];
                check(!::pipe2(descriptors, O_CLOEXEC), "create native anonymous pipe");
                for (unsigned i = 0; i < 2; ++i) {
                    auto native = std::make_unique<uv_pipe_t>();
                    check(!uv_pipe_init(&events.value, native.get(), 0),
                          "initialize native pipe stream");
                    check(!uv_pipe_open(native.get(), descriptors[i]),
                          "transfer pipe descriptor into libuv");
                    native->data = &tags[i];
                    r::uv_stream owned{*reinterpret_cast<uv_stream_s*>(native.get()),
                                       [this, i](uv_stream_s* stream) {
                                           restored = restored && stream->data == &tags[i];
                                           ++releases;
                                           delete reinterpret_cast<uv_pipe_t*>(stream);
                                       }};
                    native.release();
                    if (i == 0)
                        reader = std::move(owned);
                    else
                        writer = std::move(owned);
                }
            }
            ~pipe_pair() {
                (void)reader.close();
                (void)writer.close();
                (void)uv_run(&events.value, UV_RUN_DEFAULT);
            }
            void close() {
                check(!reader.close() && !writer.close(), "close native pipe streams");
                events.until([&] { return releases == 2; });
                check(restored && !reader.native_handle() && !writer.native_handle(),
                      "pipe deleters restore data and release each allocation once");
            }
        };
        {
            pipe_pair values;
            r::vector<r::shared_buffer> chunks;
            r::string text;
            unsigned completed = 0;
            check(!values.reader.read_start(
                      [&](r::error_code error, r::shared_buffer data) {
                          check(!error, "read native pipe chunk");
                          text.append(reinterpret_cast<const char*>(data.bytes().data()),
                                      data.size());
                          chunks.push_back(std::move(data));
                      },
                      3),
                  "start fragmented native pipe reads");
            auto first = bytes("pipe "), last = bytes("payload");
            r::weak<const r::byte> owner = first.storage();
            r::array<r::shared_buffer, 3> scatter{first, {}, last};
            check(!values.writer.write(
                      scatter,
                      [&](r::error_code error) {
                          check(!error && !owner.expired() && values.releases == 0,
                                "native pipe write retains ownership through completion");
                          ++completed;
                      }),
                  "submit pipe scatter with empty middle buffer");
            first = {};
            last = {};
            scatter = {};
            r::uv_stream moved{std::move(values.writer)};
            values.writer = std::move(moved);
            values.events.until([&] { return text == "pipe payload" && completed == 1; });
            check(owner.expired(), "pipe write releases source storage after completion");
            check(!values.reader.read_stop(), "stop native pipe subscription");
            values.close();
            r::string saved;
            for (const auto& chunk : chunks)
                saved.append(reinterpret_cast<const char*>(chunk.bytes().data()), chunk.size());
            check(saved == "pipe payload", "pipe read chunks outlive both native allocations");
        }
        {
            pipe_pair values;
            uv_os_fd_t descriptor;
            check(!uv_fileno(reinterpret_cast<uv_handle_t*>(values.writer.native_handle()),
                             &descriptor),
                  "query pipe descriptor");
            const auto capacity = ::fcntl(descriptor, F_GETPIPE_SZ);
            check(capacity > 0, "query pipe capacity");
            r::shared_buffer data{r::buffer{static_cast<r::size_t>(capacity) * 2 + 4096}};
            r::weak<const r::byte> owner = data.storage();
            unsigned completed = 0, observed = 0;
            check(
                !values.writer.write(
                    data,
                    [&](r::error_code error) {
                        check(
                            error == r::errc::canceled && !owner.expired() &&
                                values.releases == 0 && observed == 0,
                            "backpressured pipe write cancels before native release/close observers");
                        ++completed;
                    }),
                "queue pipe write larger than kernel capacity");
            data = {};
            check(values.writer.native_handle()->write_queue_size > 0,
                  "pipe has an actual pending native write");
            check(!values.writer.close([&] {
                check(values.releases == 1 && completed == 1,
                      "pipe release precedes close observer");
                ++observed;
            }) && !values.writer.close([&] { ++observed; }),
                  "register two pipe close observers");
            values.events.until([&] { return observed == 2; });
            check(completed == 1 && owner.expired(),
                  "pending pipe payload releases after exactly one completion");
            values.close();
        }
        {
            pipe_pair values;
            unsigned eof = 0;
            check(!values.reader.read_start([&](r::error_code error, r::shared_buffer data) {
                check(error == r::errc::no_connection && data.empty() &&
                          values.reader.error_details().cause.value() == UV_EOF,
                      "pipe EOF preserves the native cause");
                ++eof;
                check(values.reader.read_start([](auto, auto) {}) == r::errc::shutdown,
                      "pipe EOF makes subscription terminal");
                check(!values.reader.close(), "close native pipe reentrantly from EOF");
            }),
                  "subscribe to native pipe EOF");
            check(!values.writer.close(), "close pipe write end to generate EOF");
            values.events.until([&] { return values.releases == 2; });
            check(eof == 1, "native pipe EOF notifies exactly once");
            values.close();
        }
        {
            pipe_pair values;
            unsigned rejected_release = 0;
            check(!values.reader.read_start([](auto, auto) {}), "make adopted pipe reader active");
            const auto native = values.reader.native_handle();
            const auto data = native->data;
            bool rejected = false;
            try {
                r::uv_stream invalid{*native, [&](auto) { ++rejected_release; }};
            } catch (const r::invalid_argument&) {
                rejected = true;
            }
            check(rejected && rejected_release == 0 && native->data == data && values.releases == 0,
                  "active pipe rejection leaves existing owner/data/deleter untouched");
            check(!values.reader.read_stop(), "stop active pipe after rejected adoption");
            values.close();
        }
        {
            pipe_pair values;
            unsigned completed = 0;
            const auto status =
                values.reader.write(bytes("wrong direction"), [&](auto) { ++completed; });
            check(status == r::errc::io_error && completed == 0 &&
                      values.reader.error_details().cause == std::errc::broken_pipe,
                  "read-only pipe rejects native write immediately without a completion");
            values.close();
        }
        std::cout << "native pipe streams: 5 cases\n";
    }

    void terminals() {
        struct descriptor {
            int value = -1;
            descriptor() = default;
            descriptor(const descriptor&) = delete;
            descriptor& operator=(const descriptor&) = delete;
            ~descriptor() {
                reset();
            }
            void reset() noexcept {
                if (value >= 0)
                    (void)::close(std::exchange(value, -1));
            }
        };
        struct observation {
            unsigned releases = 0;
            bool restored = false;
            int tag = 0;
        };
        struct terminal {
            loop& events;
            descriptor master;
            r::shared<observation> observed = std::make_shared<observation>();
            r::uv_stream stream;
            explicit terminal(loop& events) : events(events) {
                master.value = ::posix_openpt(O_RDWR | O_NOCTTY | O_NONBLOCK | O_CLOEXEC);
                check(master.value >= 0, "create a private PTY master");
                check(!::grantpt(master.value) && !::unlockpt(master.value),
                      "prepare private PTY slave");
                r::array<char, 128> path{};
                check(!::ptsname_r(master.value, path.data(), path.size()),
                      "resolve private PTY slave");
                descriptor slave;
                slave.value = ::open(path.data(), O_RDWR | O_NOCTTY | O_CLOEXEC);
                check(slave.value >= 0, "open private PTY slave without controlling terminal");
                termios mode{};
                check(!::tcgetattr(slave.value, &mode), "read private PTY attributes");
                ::cfmakeraw(&mode);
                check(!::tcsetattr(slave.value, TCSANOW, &mode),
                      "configure only the new private PTY for binary data");
                winsize size{};
                size.ws_col = 73;
                size.ws_row = 21;
                check(!::ioctl(slave.value, TIOCSWINSZ, &size), "set private PTY window size");
                auto native = std::make_unique<uv_tty_t>();
                check(!uv_tty_init(&events.value, native.get(), slave.value, 1),
                      "initialize readable native TTY");
                uv_os_fd_t owned_fd;
                check(!uv_fileno(reinterpret_cast<uv_handle_t*>(native.get()), &owned_fd),
                      "query libuv TTY descriptor ownership");
                if (owned_fd == slave.value)
                    slave.value = -1;
                native->data = &observed->tag;
                try {
                    stream = r::uv_stream{*reinterpret_cast<uv_stream_s*>(native.get()),
                                          [owner = observed](uv_stream_s* value) {
                                              owner->restored = value->data == &owner->tag;
                                              ++owner->releases;
                                              delete reinterpret_cast<uv_tty_t*>(value);
                                          }};
                    native.release();
                } catch (...) {
                    uv_close(reinterpret_cast<uv_handle_t*>(native.release()),
                             [](uv_handle_t* value) { delete reinterpret_cast<uv_tty_t*>(value); });
                    throw;
                }
            }
            ~terminal() {
                (void)stream.close();
            }
            void send(r::string_view value) {
                check(::write(master.value, value.data(), value.size()) ==
                          static_cast<ssize_t>(value.size()),
                      "write small binary payload to private PTY master");
            }
            void collect(r::string& output) {
                r::array<char, 64> data{};
                for (;;) {
                    const auto n = ::read(master.value, data.data(), data.size());
                    if (n > 0) {
                        output.append(data.data(), static_cast<r::size_t>(n));
                        continue;
                    }
                    if (n < 0 && errno == EINTR)
                        continue;
                    check(n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK),
                          "private PTY master read reaches nonblocking boundary");
                    return;
                }
            }
            void close() {
                check(!stream.close(), "close owned native TTY");
                events.until([&] { return observed->releases == 1; });
                check(observed->restored && !stream.native_handle(),
                      "TTY data restored before exactly one native allocation release");
            }
        };
        {
            loop events;
            terminal peer{events};
            check(peer.stream.native_handle()->type == UV_TTY,
                  "adopt actual TTY rather than a pipe or TCP handle");
            int width = 0, height = 0;
            check(!uv_tty_get_winsize(reinterpret_cast<uv_tty_t*>(peer.stream.native_handle()),
                                      &width, &height) &&
                      width == 73 && height == 21,
                  "borrowed TTY permits compatible native configuration queries");
            r::string received, reply;
            r::vector<r::shared_buffer> chunks;
            unsigned completed = 0;
            check(!peer.stream.read_start(
                      [&](r::error_code error, r::shared_buffer chunk) {
                          check(!error, "read binary TTY data");
                          received.append(reinterpret_cast<const char*>(chunk.bytes().data()),
                                          chunk.size());
                          chunks.push_back(std::move(chunk));
                      },
                      2),
                  "subscribe to fragmented TTY input");
            const r::string input{"a\0b\n\r?", 6}, output{"x\0y\n\r!", 6};
            peer.send(input);
            auto payload = bytes(output);
            r::weak<const r::byte> owner = payload.storage();
            r::array<r::shared_buffer, 3> scatter{payload.slice(0, 2), {}, payload.slice(2)};
            check(!peer.stream.write(scatter,
                                     [&](r::error_code error) {
                                         check(!error && !owner.expired() &&
                                                   peer.observed->releases == 0,
                                               "TTY scatter retains bytes through completion");
                                         ++completed;
                                     }),
                  "write binary TTY scatter");
            payload = {};
            scatter = {};
            events.until([&] {
                peer.collect(reply);
                return received == input && reply == output && completed == 1;
            });
            check(owner.expired(), "TTY write releases payload after completion");
            check(!peer.stream.read_stop(), "stop binary TTY reads");
            peer.close();
            r::string retained;
            for (const auto& chunk : chunks)
                retained.append(reinterpret_cast<const char*>(chunk.bytes().data()), chunk.size());
            check(retained == input,
                  "TTY read chunks survive native close without text translation");
        }
        {
            loop events;
            terminal peer{events};
            unsigned first = 0;
            r::string received;
            check(!peer.stream.read_start([&](auto, auto) { ++first; }) && !peer.stream.read_stop(),
                  "stop a live TTY subscription");
            peer.send("held");
            for (unsigned i = 0; i < 3; ++i)
                (void)uv_run(&events.value, UV_RUN_NOWAIT);
            check(first == 0, "stopped TTY subscription receives no callbacks");
            check(!peer.stream.read_start([&](r::error_code error, r::shared_buffer chunk) {
                check(!error, "resumed TTY read succeeds");
                received.append(reinterpret_cast<const char*>(chunk.bytes().data()), chunk.size());
            }),
                  "restart a stopped TTY subscription");
            events.until([&] { return received == "held"; });
            peer.close();
        }
        {
            loop events;
            terminal peer{events};
            unsigned ended = 0, closed = 0;
            check(!peer.stream.read_start([&](r::error_code error, r::shared_buffer chunk) {
                check(error == r::errc::no_connection && chunk.empty() &&
                          peer.stream.error_details().cause.value() == UV_EOF,
                      "private PTY master close reports the same EOF cause as independent libuv");
                ++ended;
                check(peer.stream.read_start([](auto, auto) {}) == r::errc::shutdown,
                      "TTY EOF subscription cannot restart");
                check(!peer.stream.close([&] {
                    check(peer.observed->releases == 1, "TTY EOF deleter precedes close observer");
                    ++closed;
                }),
                      "close reentrantly from TTY EOF");
            }),
                  "subscribe to actual TTY EOF");
            peer.master.reset();
            events.until([&] { return ended == 1 && closed == 1; });
            check(peer.observed->restored, "TTY EOF close restores original data");
        }
        {
            loop events;
            terminal peer{events};
            unsigned rejected_release = 0;
            check(!peer.stream.read_start([](auto, auto) {}), "make adopted TTY active");
            const auto native = peer.stream.native_handle();
            const auto data = native->data;
            bool rejected = false;
            try {
                r::uv_stream invalid{*native, [&](auto) { ++rejected_release; }};
            } catch (const r::invalid_argument&) {
                rejected = true;
            }
            check(rejected && !rejected_release && native->data == data && !peer.observed->releases,
                  "active TTY adoption rejection preserves existing owner and native data");
            check(!peer.stream.read_stop(), "stop TTY after failed adoption");
            peer.close();
        }
        {
            loop events;
            terminal first{events}, second{events};
            const auto native = second.stream.native_handle();
            first.stream = std::move(second.stream);
            events.until([&] { return first.observed->releases == 1; });
            check(
                first.observed->restored && !second.stream &&
                    first.stream.native_handle() == native && !second.observed->releases,
                "TTY move assignment closes the previous allocation and preserves the incoming native address");
            r::string received;
            bool completed = false;
            r::string reply;
            check(!first.stream.read_start([&](r::error_code error, r::shared_buffer chunk) {
                check(!error, "moved TTY remains readable");
                received.append(reinterpret_cast<const char*>(chunk.bytes().data()), chunk.size());
            }),
                  "read through moved TTY owner");
            second.send("moved");
            check(!first.stream.write(bytes("reply"),
                                      [&](r::error_code error) {
                                          check(!error, "moved TTY remains writable");
                                          completed = true;
                                      }),
                  "write through moved TTY owner");
            events.until([&] {
                second.collect(reply);
                return received == "moved" && reply == "reply" && completed;
            });
            check(!first.stream.close(), "close moved TTY");
            events.until([&] { return second.observed->releases == 1; });
            check(second.observed->restored,
                  "moved TTY preserves its original allocator/deleter data");
        }
        std::cout << "native TTY streams: 5 cases\n";
    }

    void allocations() {
        pair values;
        unsigned read = 0;
        check(!values.incoming.read_start([&](r::error_code error, r::shared_buffer) {
            check(!error, "allocation-case read");
            ++read;
        }),
              "start allocation reader");
        unsigned rejected = 0, accepted = 0;
        const auto payload = bytes("allocation");
        for (std::ptrdiff_t limit = 0; limit < 12; ++limit) {
            unsigned callbacks = 0;
            r::completion callback = [&](r::error_code error) {
                check(!error, "accepted write completion after allocation sweep");
                ++callbacks;
            };
            allocation_failure::after(limit);
            const auto status = values.outgoing.write(payload, std::move(callback));
            allocation_failure::after(-1);
            if (status) {
                check(status == r::errc::out_of_memory && callbacks == 0,
                      "immediate allocation failure has no write callback");
                ++rejected;
            } else {
                ++accepted;
                values.events.until([&] { return callbacks == 1; });
            }
        }
        check(rejected >= 3 && accepted > 0,
              "allocation sweep covers write request/storage/list ownership");
        check(!values.incoming.read_stop(), "stop allocation reader");
        unsigned connect_rejected = 0, connect_accepted = 0;
        for (std::ptrdiff_t limit = 0; limit < 8; ++limit) {
            r::tcp_stream_transport connector{values.events.value};
            check(!connector.init(1, "sender:8080"), "initialize allocation connector");
            unsigned callbacks = 0;
            r::stream_connect_callback callback = [&](r::error_code error, r::uv_stream stream) {
                check(!error && static_cast<bool>(stream),
                      "accepted connect after setup allocation sweep");
                ++callbacks;
            };
            allocation_failure::after(limit);
            const auto status = connector.connect(2, values.endpoint, std::move(callback));
            allocation_failure::after(-1);
            if (status) {
                check(status == r::errc::out_of_memory && callbacks == 0,
                      "connect setup allocation failure has no callback");
                ++connect_rejected;
            } else {
                ++connect_accepted;
                values.events.until([&] { return callbacks == 1; });
            }
            bool done = false;
            connector.close([&] { done = true; });
            values.events.until([&] { return done; });
        }
        check(connect_rejected > 0 && connect_accepted > 0,
              "connect allocation sweep includes rejection and real handoff");
        values.close();
    }
    void interop() {
        loop events;
        const auto endpoint = address();
        r::tcp_stream_transport transport{events.value};
        check(!transport.init(99, endpoint), "raw connector identity");
        r::uv_stream incoming;
        r::string received;
        check(!transport.listen([&](auto id, auto identity, auto stream) {
            check(id == 100 && identity == "native-peer", "decode native handshake identity");
            incoming = std::move(stream);
            check(!incoming.read_start([&](auto status, auto data) {
                check(!status, "read native payload");
                received.append(reinterpret_cast<const char*>(data.bytes().data()), data.size());
            }),
                  "start owning stream read");
        }),
              "raw stream listener");
        const int descriptor = ::socket(AF_INET, SOCK_STREAM, 0);
        check(descriptor >= 0, "native socket");
        struct native_owner {
            int value;
            ~native_owner() {
                ::close(value);
            }
        } native{descriptor};
        sockaddr_in target{};
        check(
            !uv_ip4_addr("127.0.0.1", std::stoi(endpoint.substr(endpoint.find(':') + 1)), &target),
            "native endpoint");
        check(!::connect(descriptor, reinterpret_cast<sockaddr*>(&target), sizeof target),
              "native connects to raw listener");
        r::array<unsigned char, 40> hello{};
        hello[0] = 1;
        hello[8] = 100;
        hello[16] = 16;
        constexpr r::string_view identity = "native-peer";
        std::copy(identity.begin(), identity.end(), hello.begin() + 24);
        check(::send(descriptor, hello.data(), hello.size(), MSG_NOSIGNAL) ==
                  static_cast<ssize_t>(hello.size()),
              "native sends fixed handshake");
        events.until([&] { return static_cast<bool>(incoming); });
        constexpr r::string_view payload = "native-to-cpp";
        check(::send(descriptor, payload.data(), payload.size(), MSG_NOSIGNAL) ==
                  static_cast<ssize_t>(payload.size()),
              "native payload write");
        events.until([&] { return received == payload; });
        bool written = false;
        check(!incoming.write(bytes("cpp-to-native"),
                              [&](auto status) {
                                  check(!status, "owning write completes");
                                  written = true;
                              }),
              "owning stream writes to native peer");
        events.until([&] { return written; });
        timeval timeout{2, 0};
        check(!setsockopt(descriptor, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout),
              "bound native read");
        r::array<char, 13> response{};
        check(::recv(descriptor, response.data(), response.size(), MSG_WAITALL) ==
                      static_cast<ssize_t>(response.size()) &&
                  r::string_view{response.data(), response.size()} == "cpp-to-native",
              "native peer receives exact owning-write bytes");
        check(!incoming.read_stop() && !incoming.close(), "close handed-off stream");
        bool closed = false;
        transport.close([&] { closed = true; });
        events.until([&] { return closed; });
    }
}
int main(int argc, char** argv) {
    std::signal(SIGPIPE, SIG_IGN);
    try {
        check(argc == 2, "stream suite argument");
        const r::string_view suite{argv[1]};
        if (suite == "exchange")
            exchange();
        else if (suite == "cancellation")
            cancellation();
        else if (suite == "failure")
            failure_cases();
        else if (suite == "adoption")
            adoption();
        else if (suite == "allocations")
            allocations();
        else if (suite == "interop")
            interop();
        else if (suite == "pipe")
            pipes();
        else if (suite == "tty")
            terminals();
        else
            check(false, "unknown stream suite");
    } catch (const std::exception& error) {
        allocation_failure::after(-1);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
