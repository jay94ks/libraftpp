#include <raftpp.hpp>
#include "byte.hpp"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <csignal>
#include <exception>
#include <iostream>
#include <source_location>
#include <thread>
#include <uv.h>
#include <unistd.h>
#include <sys/socket.h>
#include <fcntl.h>
#include "tcp_failure.hpp"
#include "memory_resource.hpp"

namespace r = raftpp;
namespace allocation_failure {
    void after(std::ptrdiff_t) noexcept;
}
namespace {
    std::exception_ptr failure;
    void check(bool condition, r::string_view text,
               std::source_location where = std::source_location::current()) {
        if (condition)
            return;
        auto error = std::make_exception_ptr(std::runtime_error(r::string{where.file_name()} + ":" +
                                                                std::to_string(where.line()) +
                                                                ": " + r::string{text}));
        if (!failure)
            failure = error;
        std::rethrow_exception(error);
    }
    struct loop {
        uv_loop_t value{};
        r::function<void()> on_timeout;
        loop() {
            check(!uv_loop_init(&value), "loop init");
        }
        ~loop() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (uv_loop_close(&value))
                std::abort();
        }
        template <typename F>
        void until(F condition, std::source_location where = std::source_location::current()) {
            const auto deadline = uv_hrtime() + 12000000000ULL;
            while (!condition() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                if (failure)
                    std::rethrow_exception(failure);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            const bool completed = condition();
            if (!completed && on_timeout)
                on_timeout();
            check(completed, "operation completed before timeout", where);
        }
        void run() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (failure)
                std::rethrow_exception(failure);
        }
    };
    struct socket {
        int value = -1;
        socket() {
            value = ::socket(AF_INET, SOCK_STREAM, 0);
            check(value >= 0, "socket");
        }
        ~socket() {
            if (value >= 0)
                ::close(value);
        }
        void close() {
            if (value >= 0)
                ::close(std::exchange(value, -1));
        }
    };
    r::string available_address() {
        socket reserved;
        sockaddr_in endpoint{};
        endpoint.sin_family = AF_INET;
        endpoint.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(!::bind(reserved.value, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)),
              "reserve port");
        socklen_t size = sizeof(endpoint);
        check(!::getsockname(reserved.value, reinterpret_cast<sockaddr*>(&endpoint), &size),
              "get reserved port");
        return "127.0.0.1:" + std::to_string(ntohs(endpoint.sin_port));
    }
    void connect(socket& peer, const r::string& address) {
        sockaddr_in endpoint{};
        check(
            !uv_ip4_addr("127.0.0.1", std::stoi(address.substr(address.find(':') + 1)), &endpoint),
            "numeric endpoint");
        check(!::connect(peer.value, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)),
              "raw connect");
    }
    void write(socket& peer, r::view<const r::byte> bytes) {
        while (!bytes.empty()) {
            const auto count = ::send(peer.value, bytes.data(), bytes.size(), MSG_NOSIGNAL);
            check(count > 0, "raw write");
            bytes = bytes.subspan(static_cast<r::size_t>(count));
        }
    }
    void close(loop& event_loop, r::tcp_transport& value) {
        bool done = false;
        value.close([&] { done = true; });
        event_loop.until([&] { return done; });
    }
    r::buffer handshake(r::server_id id, r::string_view address) {
        const auto padded = (address.size() + 8) / 8 * 8;
        r::buffer result{24 + padded};
        r::detail::byte_writer writer{result.bytes()};
        writer.write<r::uint64_t>(1);
        writer.write<r::uint64_t>(id);
        writer.write<r::uint64_t>(padded);
        writer.write({reinterpret_cast<const r::byte*>(address.data()), address.size()});
        return result;
    }
    r::shared<r::message> message(unsigned kind, r::server_id id, const r::string& address) {
        auto result = std::make_shared<r::message>();
        result->server_id = id;
        result->server_address = address;
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
            check(!value.configuration.add(99, "sender:8080", r::role::voter), "snapshot member");
            r::buffer bytes{65537};
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
        check(!r::encode_message(value, encoded), "encode message");
        r::vector<r::byte> result(encoded.header.bytes().begin(), encoded.header.bytes().end());
        for (auto& bytes : encoded.payload)
            result.insert(result.end(), bytes.bytes().begin(), bytes.bytes().end());
        return result;
    }
    void same_message(const r::message& actual, unsigned kind) {
        auto expected = message(kind, 0, "");
        check(wire(actual) == wire(*expected), "all fields/payload survive socket framing");
    }
    void exchange() {
        loop event_loop;
        r::tcp_transport left{event_loop.value}, right{event_loop.value};
        const auto first = available_address();
        check(!left.init(99, first), "first init");
        r::vector<r::message> replies, received;
        check(!left.start([&](auto value) { replies.push_back(std::move(value)); }), "first start");
        const auto second = available_address();
        check(!right.init(100, second), "second init");
        check(!right.start([&](auto value) { received.push_back(std::move(value)); }),
              "second start");
        unsigned completed = 0;
        r::vector<r::weak<const r::message>> owners;
        for (unsigned i = 0; i < 24; ++i) {
            auto value = message(i, 100, second);
            owners.push_back(value);
            r::weak<const r::message> owner = value;
            check(!left.send(value,
                             [&, owner](auto status) {
                                 check(!status && !owner.expired(),
                                       "write completion retains accepted message");
                                 ++completed;
                             }),
                  "queue socket send");
        }
        event_loop.until([&] { return received.size() == 24 && completed == 24; });
        for (unsigned i = 0; i < received.size(); ++i) {
            check(received[i].server_id == 99 && received[i].server_address == first,
                  "sender comes from handshake");
            same_message(received[i], i);
        }
        for (const auto& owner : owners)
            check(owner.expired(), "send releases request after completion");
        for (unsigned i = 0; i < 6; ++i)
            check(!right.send(message(i, 99, first),
                              [&](auto status) {
                                  check(!status, "reverse send");
                                  ++completed;
                              }),
                  "reverse accepted");
        event_loop.until([&] { return replies.size() == 6 && completed == 30; });
        close(event_loop, left);
        close(event_loop, right);
        for (unsigned i = 0; i < 6; ++i) {
            same_message(replies[i], i);
            check(replies[i].server_id == 100, "reverse identity");
        }
        // Retained receive values still own payloads after native connections close.
        for (unsigned i = 0; i < received.size(); ++i)
            same_message(received[i], i);
    }
    void fragments() {
        loop event_loop;
        r::tcp_transport receiver{event_loop.value};
        const auto address = available_address();
        check(!receiver.init(1, address), "init");
        r::vector<r::message> received;
        check(!receiver.start([&](auto value) { received.push_back(std::move(value)); }), "start");
        socket peer;
        connect(peer, address);
        auto hello = handshake(99, "fragmented:8080");
        auto bytes = wire(*message(2, 1, address));
        for (auto value : hello.bytes()) {
            write(peer, {&value, 1});
            (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
        }
        for (auto value : bytes) {
            write(peer, {&value, 1});
            (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
        }
        // Coalesced messages are bounded by each stage's exact read window.
        r::vector<r::byte> together;
        for (unsigned i : {5U, 0U, 4U, 1U, 3U}) {
            auto part = wire(*message(i, 1, address));
            together.insert(together.end(), part.begin(), part.end());
        }
        write(peer, together);
        event_loop.until([&] { return received.size() == 6; });
        same_message(received[0], 2);
        for (unsigned i = 0; i < 5; ++i)
            same_message(received[i + 1], r::array<unsigned, 5>{5, 0, 4, 1, 3}[i]);
        peer.close();
        close(event_loop, receiver);
    }
    void malformed() {
        loop event_loop;
        r::tcp_transport_options options;
        options.max_handshake_address = 64;
        options.limits.max_header_size = 128;
        options.limits.max_payload_size = 128;
        r::tcp_transport receiver{event_loop.value, options};
        const auto address = available_address();
        check(!receiver.init(1, address), "init");
        unsigned received = 0;
        check(!receiver.start([&](auto) { ++received; }), "start");
        for (unsigned kind = 0; kind < 8; ++kind) {
            socket peer;
            connect(peer, address);
            auto hello = handshake(99, "peer");
            if (kind < 4) {
                if (kind == 0)
                    hello.bytes()[0] = r::byte{2};
                if (kind == 1)
                    std::ranges::fill(hello.bytes().subspan(8, 8), r::byte{});
                if (kind == 2)
                    std::ranges::fill(hello.bytes().subspan(16, 8), r::byte{255});
                if (kind == 3)
                    std::ranges::fill(hello.bytes().subspan(24), r::byte{1});
                write(peer, hello.bytes());
            } else {
                write(peer, hello.bytes());
                auto bytes = wire(*message(kind == 7 ? 0 : 2, 1, address));
                if (kind == 4)
                    bytes[0] = r::byte{255};
                if (kind == 5)
                    std::ranges::fill(r::view{bytes}.subspan(8, 8), r::byte{});
                if (kind == 6)
                    std::ranges::fill(r::view{bytes}.subspan(8, 8), r::byte{255});
                write(peer, bytes);
            }
            event_loop.until([&] {
                char byte;
                const auto status = ::recv(peer.value, &byte, 1, MSG_DONTWAIT);
                return status == 0 || (status < 0 && errno == ECONNRESET);
            });
            check(received == 0 && receiver.last_error(), "malformed peer closed without delivery");
        }
        socket partial;
        connect(partial, address);
        const auto hello = handshake(99, "partial");
        write(partial, hello.bytes().first(7));
        partial.close();
        close(event_loop, receiver);
    }
    void runtime_settings() {
        loop event_loop;
        r::tcp_transport_options options;
        options.connect_retry = 60000;
        r::tcp_transport sender{event_loop.value, options};
        auto advertised = available_address(), bound = available_address(),
             replacement = available_address();
        check(!sender.set_bind_address(bound), "set bind override");
        r::string oversized(4096, 'x');
        check(sender.set_bind_address(oversized) == r::errc::name_too_long,
              "bad bind preserves valid override");
        allocation_failure::after(0);
        const auto status = sender.set_bind_address(r::string_view{oversized}.substr(0, 200));
        allocation_failure::after(-1);
        check(status == r::errc::out_of_memory, "bind replacement allocation failure is reported");
        check(!sender.set_bind_address(replacement) && !sender.set_bind_address(bound),
              "repeated overrides release previous strings");
        check(!sender.init(99, advertised) &&
                  sender.set_bind_address(replacement) == r::errc::busy &&
                  !sender.start([](auto) {}),
              "bind is frozen at initialization");
        socket incoming;
        connect(incoming, bound); // Prove setter-selected listener exists.
        sender.set_connect_retry_delay(1);
        const auto offline = available_address();
        unsigned failed = 0, sent = 0, received = 0;
        for (unsigned i = 0; i < 4; ++i)
            check(!sender.send(message(2, 100, offline),
                               [&](auto error) {
                                   if (error == r::errc::no_connection)
                                       ++failed;
                                   else {
                                       check(!error, "retained configured send succeeds");
                                       ++sent;
                                   }
                               }),
                  "configured pending send accepted");
        event_loop.until([&] { return failed == 1; });
        r::tcp_transport receiver{event_loop.value};
        check(!receiver.init(100, offline) && !receiver.start([&](auto value) {
            check(value.server_address == advertised,
                  "bind setter does not alter advertised identity");
            ++received;
        }),
              "late configured peer start");
        event_loop.until([&] { return sent == 3 && received == 3; });
        incoming.close();
        close(event_loop, sender);
        close(event_loop, receiver);
        check(sender.set_bind_address(bound) == r::errc::shutdown, "closed bind rejection");
        sender.set_connect_retry_delay(0);
    }
    void retry_and_close() {
        loop event_loop;
        r::tcp_transport_options options;
        options.connect_retry = 5;
        r::tcp_transport sender{event_loop.value, options};
        auto first = available_address();
        check(!sender.init(99, first) && !sender.start([](auto) {}), "sender start");
        const auto second = available_address();
        unsigned failed = 0, completed = 0, received = 0;
        for (unsigned i = 0; i < 8; ++i)
            check(!sender.send(message(2, 100, second),
                               [&](auto status) {
                                   if (status == r::errc::no_connection)
                                       ++failed;
                                   else {
                                       check(!status, "retained request succeeds after retry");
                                       ++completed;
                                   }
                               }),
                  "offline queue accepted");
        event_loop.until([&] { return failed == 5; });
        r::tcp_transport receiver{event_loop.value};
        check(!receiver.init(100, second) && !receiver.start([&](auto) { ++received; }),
              "late peer start");
        event_loop.until([&] { return completed == 3 && received == 3; });
        // Same identity changes destination address: retire old native client.
        r::tcp_transport replacement{event_loop.value};
        const auto third = available_address();
        check(!replacement.init(100, third) && !replacement.start([&](auto) { ++received; }),
              "replacement start");
        check(!sender.send(message(5, 100, third),
                           [&](auto status) {
                               check(!status, "changed address send");
                               ++completed;
                           }),
              "changed address accepted");
        event_loop.until([&] { return completed == 4 && received == 4; });
        const auto offline = available_address();
        unsigned canceled = 0;
        auto value = message(0, 101, offline);
        r::weak<const r::message> owner = value;
        check(!sender.send(value,
                           [&, owner](auto status) {
                               check(status == r::errc::canceled && !owner.expired(),
                                     "queued close retains request until callback");
                               ++canceled;
                           }),
              "pending DNS send");
        value.reset();
        bool closed = false;
        sender.close([&] {
            check(canceled == 1 && owner.expired(), "all sends drain before close");
            closed = true;
        });
        event_loop.until([&] { return closed; });
        close(event_loop, receiver);
        close(event_loop, replacement);
    }
    void lifetime() {
        loop event_loop;
        const auto address = available_address();
        unsigned received = 0;
        auto receiver = std::make_unique<r::tcp_transport>(event_loop.value);
        check(!receiver->init(1, address), "init");
        bool closed = false;
        auto token = std::make_shared<unsigned>(7);
        r::weak<unsigned> observer = token;
        check(!receiver->start([&, token](auto) {
            ++received;
            receiver->close([&] {
                check(observer.expired(), "receive captures released before closed callback");
                closed = true;
            });
            receiver.reset();
            check(*token == 7, "active callback survives facade destruction");
        }),
              "receive start");
        token.reset();
        socket peer;
        connect(peer, address);
        auto hello = handshake(99, "raw");
        write(peer, hello.bytes());
        auto bytes = wire(*message(2, 1, address));
        write(peer, bytes);
        event_loop.until([&] { return closed; });
        check(received == 1, "close in receive exactly once");
        peer.close();
        // Bind failure retains initialized handles through native close.
        r::tcp_transport first{event_loop.value}, duplicate{event_loop.value};
        check(!first.init(1, address) && !first.start([](auto) {}), "reuse port after close");
        check(!duplicate.init(2, address) && duplicate.start([](auto) {}) == r::errc::io_error,
              "occupied bind rejected");
        close(event_loop, duplicate);
        close(event_loop, first);
        r::tcp_transport uninitialized{event_loop.value};
        close(event_loop, uninitialized);
    }
    void allocations() {
        for (unsigned kind = 0; kind < 4; ++kind) {
            bool failed = false, succeeded = false;
            for (std::ptrdiff_t position = 0; position < 40; ++position) {
                loop event_loop;
                r::tcp_transport value{event_loop.value};
                const auto address = available_address();
                if (kind)
                    check(!value.init(1, address), "init before failure");
                if (kind >= 2)
                    check(!value.start([](auto) {}), "start before failure");
                auto outgoing = message(0, 2, address);
                unsigned calls = 0;
                r::completion callback = [&](auto status) {
                    check(status == r::errc::canceled, "unrun send canceled");
                    ++calls;
                };
                r::error_code status;
                if (kind == 3) {
                    // Exercise allocations in incoming accept and staged decoding.
                    socket peer;
                    connect(peer, address);
                    auto hello = handshake(2, "incoming");
                    write(peer, hello.bytes());
                    auto bytes = wire(*message(0, 1, address));
                    write(peer, bytes);
                    allocation_failure::after(position);
                    for (unsigned step = 0; step < 20; ++step)
                        (void)uv_run(&event_loop.value, UV_RUN_NOWAIT);
                    allocation_failure::after(-1);
                    status = value.last_error();
                } else {
                    allocation_failure::after(position);
                    if (kind == 0)
                        status = value.init(1, address);
                    if (kind == 1)
                        status = value.start([](auto) {});
                    if (kind == 2)
                        status = value.send(outgoing, callback);
                    allocation_failure::after(-1);
                }
                if (status) {
                    check(status == r::errc::out_of_memory, "allocation error mapping");
                    failed = true;
                    check(calls == 0, "immediate failure no callback");
                } else
                    succeeded = true;
                bool closed = false;
                r::close_callback done = [&] { closed = true; };
                allocation_failure::after(0);
                value.close(std::move(done));
                allocation_failure::after(-1);
                event_loop.run();
                check(closed && calls == (kind == 2 && !status ? 1U : 0U),
                      "allocation-free close drains all state");
            }
            check(failed && succeeded, "allocation sweep covers failure and success");
        }
    }
    void native_failures() {
        namespace fault = tcp_failure;
        for (auto operation : {fault::operation::tcp_init, fault::operation::listen}) {
            loop event_loop;
            r::tcp_transport value{event_loop.value};
            check(!value.init(1, available_address()), "init before native error");
            fault::after(operation, 0, UV_ENOMEM);
            check(value.start([](auto) {}) == r::errc::out_of_memory,
                  "partial listener native error");
            close(event_loop, value);
        }
        for (auto operation : {fault::operation::accept, fault::operation::read}) {
            loop event_loop;
            r::tcp_transport value{event_loop.value};
            const auto address = available_address();
            check(!value.init(1, address) && !value.start([](auto) {}),
                  "listen before accept failure");
            fault::after(operation, 0, UV_ENOMEM);
            socket peer;
            connect(peer, address);
            event_loop.until([&] { return value.last_error() == r::errc::out_of_memory; });
            close(event_loop, value);
        }
        for (auto operation :
             {fault::operation::resolve, fault::operation::connect, fault::operation::write}) {
            loop event_loop;
            r::tcp_transport_options options;
            options.connect_retry = 1;
            r::tcp_transport sender{event_loop.value, options}, receiver{event_loop.value};
            const auto first = available_address();
            check(!sender.init(99, first) && !sender.start([](auto) {}), "fault sender");
            const auto second = available_address();
            unsigned received = 0, completed = 0;
            check(!receiver.init(100, second) && !receiver.start([&](auto) { ++received; }),
                  "fault receiver");
            fault::after(operation, 0, UV_ENOMEM);
            check(!sender.send(message(2, 100, second),
                               [&](auto status) {
                                   check(!status, "native connection failure retries");
                                   ++completed;
                               }),
                  "native retry accepted");
            event_loop.until([&] { return completed == 1 && received == 1; });
            // Immediate write failure on a ready client is not accepted.
            unsigned rejected_calls = 0;
            fault::after(fault::operation::write, 0, UV_ENOMEM);
            check(sender.send(message(2, 100, second), [&](auto) { ++rejected_calls; }) ==
                          r::errc::io_error &&
                      rejected_calls == 0,
                  "ready write submission failure has no callback");
            check(!sender.send(message(2, 100, second),
                               [&](auto status) {
                                   check(!status,
                                         "ready connection survives immediate submission failure");
                                   ++completed;
                               }),
                  "send after failed write");
            event_loop.until([&] { return completed == 2 && received == 2; });
            close(event_loop, sender);
            close(event_loop, receiver);
        }
        {
            loop event_loop;
            r::tcp_transport sender{event_loop.value}, receiver{event_loop.value};
            const auto first = available_address();
            check(!sender.init(99, first) && !sender.start([](auto) {}), "queued fault sender");
            const auto second = available_address();
            unsigned received = 0, errors = 0, completed = 0;
            check(!receiver.init(100, second) && !receiver.start([&](auto) { ++received; }),
                  "queued fault receiver");
            fault::after(fault::operation::write, 1,
                         UV_ENOMEM); // Handshake succeeds, first queued RPC fails.
            for (unsigned i = 0; i < 3; ++i)
                check(!sender.send(message(2, 100, second),
                                   [&](auto status) {
                                       if (status == r::errc::io_error)
                                           ++errors;
                                       else {
                                           check(!status, "other queued writes succeed");
                                           ++completed;
                                       }
                                   }),
                      "queued failure accepted");
            event_loop.until([&] { return errors == 1 && completed == 2 && received == 2; });
            close(event_loop, sender);
            close(event_loop, receiver);
        }
        {
            loop event_loop;
            r::tcp_transport sender{event_loop.value};
            const auto first = available_address();
            check(!sender.init(99, first) && !sender.start([](auto) {}), "timer failure sender");
            fault::after(fault::operation::timer_init, 0, UV_ENOMEM);
            unsigned calls = 0;
            check(sender.send(message(2, 100, first), [&](auto) { ++calls; }) ==
                          r::errc::out_of_memory &&
                      calls == 0,
                  "timer initialization failure is not accepted");
            close(event_loop, sender);
        }
    }
    void diagnostics() {
        namespace fault = tcp_failure;
        for (auto operation : {fault::operation::tcp_init, fault::operation::listen}) {
            loop event_loop;
            r::tcp_transport value{event_loop.value};
            const auto address = available_address();
            check(!value.init(1, address), "diagnostic listener init");
            fault::after(operation, 0, UV_ENOMEM);
            check(value.start([](auto) {}) == r::errc::out_of_memory,
                  "diagnostic native listener failure");
            auto info = value.error_details();
            check(info.code == r::errc::out_of_memory &&
                      info.cause == std::errc::not_enough_memory &&
                      info.cause.value() == UV_ENOMEM &&
                      r::string_view{info.cause.category().name()} == "libuv" &&
                      info.text().find(address) != r::string_view::npos,
                  "listener diagnostic preserves native code/category/path");
            r::error_info copy;
            allocation_failure::after(0);
            copy = value.error_details();
            allocation_failure::after(-1);
            check(copy.text() == info.text(), "transport diagnostic copying allocates nothing");
            close(event_loop, value);
            check(value.error_details().text() == info.text(),
                  "startup diagnostic survives native cleanup");
        }
        loop event_loop;
        r::tcp_transport sender{event_loop.value};
        r::array<r::string, 2> addresses{available_address(), available_address()};
        r::array<r::owned<r::tcp_transport>, 2> receivers;
        unsigned received = 0, warm = 0;
        check(!sender.init(99, available_address()) && !sender.start([](auto) {}),
              "diagnostic sender");
        for (unsigned i = 0; i < 2; ++i) {
            receivers[i] = std::make_unique<r::tcp_transport>(event_loop.value);
            check(!receivers[i]->init(100 + i, addresses[i]) &&
                      !receivers[i]->start([&](auto) { ++received; }),
                  "diagnostic receivers");
            check(!sender.send(message(2, 100 + i, addresses[i]),
                               [&](auto status) {
                                   check(!status, "warm diagnostic connection");
                                   ++warm;
                               }),
                  "warm send accepted");
        }
        event_loop.until([&] { return warm == 2 && received == 2; });
        unsigned rejected = 0;
        fault::after(fault::operation::write, 0, UV_ENOBUFS);
        check(sender.send(message(2, 100, addresses[0]), [&](auto) { ++rejected; }) ==
                      r::errc::io_error &&
                  rejected == 0,
              "immediate write rejection has no callback");
        const auto immediate = sender.error_details();
        check(immediate.cause == std::errc::no_buffer_space &&
                  immediate.text().find("tcp.write.submit") == 0 &&
                  immediate.text().find(addresses[0]) != r::string_view::npos,
              "immediate write captures its own destination/cause");
        unsigned completed = 0;
        r::array<r::error_info, 2> results;
        for (unsigned i = 0; i < 2; ++i) {
            const auto native = i ? UV_EPIPE : UV_EIO;
            fault::after(fault::operation::write_result, 0, native);
            check(
                !sender.send(
                    message(2, 100 + i, addresses[i]),
                    [&, i, native](auto status) {
                        results[i] = sender.error_details();
                        check(
                            status == r::errc::io_error && results[i].code == status &&
                                results[i].cause.value() == native &&
                                results[i].text().find(addresses[i]) != r::string_view::npos &&
                                results[i].text().find("server " + std::to_string(100 + i)) !=
                                    r::string_view::npos,
                            "concurrent peer completions retain their own native cause/address/id");
                        check(sender.send({}, {}) == r::errc::bad_id &&
                                  !sender.error_details().cause,
                              "reentrant validation failure has fresh context");
                        check(results[i].cause.value() == native,
                              "owned callback copy survives replacement");
                        ++completed;
                    }),
                "forced asynchronous write accepted");
        }
        event_loop.until([&] { return completed == 2 && received == 4; });
        close(event_loop, sender);
        for (auto& value : receivers)
            close(event_loop, *value);
        check(results[0].cause == std::errc::io_error && results[1].cause == std::errc::broken_pipe,
              "owned peer diagnostics survive reconnect/shutdown");
    }
    void peer_diagnostics() {
        namespace fault = tcp_failure;
        {
            loop event_loop;
            r::tcp_transport_options options;
            options.connect_retry = 1000;
            r::tcp_transport sender{event_loop.value, options};
            check(!sender.init(99, available_address()) && !sender.start([](auto) {}),
                  "resolver diagnostic sender");
            const auto destination = available_address();
            unsigned canceled = 0;
            fault::after(fault::operation::resolve, 0, UV_EAI_NONAME);
            check(!sender.send(message(2, 100, destination),
                               [&](auto status) {
                                   const auto info = sender.error_details();
                                   check(status == r::errc::canceled && info.code == status &&
                                             !info.cause &&
                                             info.text().find(destination) != r::string_view::npos,
                                         "cancel does not reuse DNS failure cause");
                                   ++canceled;
                               }),
                  "immediate resolver failure keeps accepted request pending");
            const auto info = sender.error_details();
            check(info.code == r::errc::no_connection && info.cause.value() == UV_EAI_NONAME &&
                      r::string_view{info.cause.category().name()} == "libuv" &&
                      info.cause.default_error_condition().category() == info.cause.category() &&
                      info.text().find("tcp.resolve.submit") == 0,
                  "DNS code retains libuv category instead of masquerading as errno");
            close(event_loop, sender);
            check(canceled == 1 && info.cause.value() == UV_EAI_NONAME,
                  "DNS diagnostic copy survives cancellation");
        }
        {
            loop event_loop;
            r::tcp_transport_options options;
            options.connect_retry = 1000;
            r::tcp_transport sender{event_loop.value, options};
            check(!sender.init(99, available_address()) && !sender.start([](auto) {}),
                  "refused diagnostic sender");
            const r::array<r::string, 2> addresses{available_address(), available_address()};
            unsigned failed = 0, canceled = 0;
            r::array<r::error_info, 2> observed;
            for (unsigned i = 0; i < 2; ++i)
                for (unsigned n = 0; n < 4; ++n) {
                    check(
                        !sender.send(
                            message(2, 100 + i, addresses[i]),
                            [&, i](auto status) {
                                const auto info = sender.error_details();
                                if (status == r::errc::no_connection) {
                                    observed[i] = info;
                                    check(
                                        info.code == status &&
                                            info.cause == std::errc::connection_refused &&
                                            info.text().find(addresses[i]) !=
                                                r::string_view::npos &&
                                            info.text().find("tcp.connect.complete") == 0,
                                        "real refused connections retain per-peer errno through queue eviction");
                                    ++failed;
                                } else {
                                    check(status == r::errc::canceled && info.code == status &&
                                              !info.cause,
                                          "remaining queued sends cancel with fresh diagnostics");
                                    ++canceled;
                                }
                            }),
                        "refused peer queued send");
                }
            event_loop.until([&] { return failed == 2; });
            close(event_loop, sender);
            check(canceled == 6 && observed[0].cause == std::errc::connection_refused &&
                      observed[1].cause == std::errc::connection_refused,
                  "eviction retains newest three per peer and drains once");
        }
        {
            loop event_loop;
            r::tcp_transport receiver{event_loop.value};
            const auto address = available_address();
            check(!receiver.init(100, address) && !receiver.start([](auto) {}),
                  "read diagnostic receiver");
            socket peer;
            connect(peer, address);
            auto greeting = handshake(99, "peer.example:9000");
            write(peer, greeting.bytes());
            peer.close();
            event_loop.until([&] { return receiver.error_details().cause.value() == UV_EOF; });
            const auto info = receiver.error_details();
            check(!receiver.last_error() &&
                      r::string_view{info.cause.category().name()} == "libuv" &&
                      info.text().find("peer.example:9000") != r::string_view::npos,
                  "EOF is observable without changing legacy transport status");
            close(event_loop, receiver);
        }
    }
    void shutdown_paths() {
        {
            loop event_loop;
            r::tcp_transport sender{event_loop.value}, receiver{event_loop.value};
            const auto first = available_address();
            check(!sender.init(99, first) && !sender.start([](auto) {}), "shutdown sender");
            const auto second = available_address();
            unsigned received = 0;
            check(!receiver.init(100, second) && !receiver.start([&](auto) { ++received; }),
                  "shutdown receiver");
            bool ready = false;
            check(!sender.send(message(2, 100, second),
                               [&](auto status) {
                                   check(!status, "warm connection");
                                   ready = true;
                               }),
                  "warm send");
            event_loop.until([&] { return ready && received == 1; });
            auto value = message(4, 100, second);
            r::buffer bytes{4 * 1024 * 1024};
            std::get<r::install_snapshot>(value->payload).data = r::shared_buffer{std::move(bytes)};
            r::weak<const r::message> observer = value;
            unsigned completed = 0, canceled = 0;
            for (unsigned i = 0; i < 3; ++i)
                check(!sender.send(
                          value,
                          [&, observer](auto status) {
                              check(!observer.expired() && (!status || status == r::errc::canceled),
                                    "in-flight write data retained through native cancellation");
                              if (status == r::errc::canceled)
                                  ++canceled;
                              ++completed;
                          }),
                      "large write queued");
            value.reset();
            bool closed = false;
            sender.close([&] {
                check(completed == 3 && observer.expired(),
                      "close follows every native write and buffer release");
                closed = true;
            });
            event_loop.until([&] { return closed; });
            check(canceled > 0, "actual partial socket writes were canceled");
            close(event_loop, receiver);
        }
        for (bool receive_exception : {false, true}) {
            loop event_loop;
            auto sender = std::make_unique<r::tcp_transport>(event_loop.value);
            r::tcp_transport receiver{event_loop.value};
            const auto first = available_address();
            check(!sender->init(99, first) && !sender->start([](auto) {}), "exception sender");
            const auto second = available_address();
            check(!receiver.init(100, second) && !receiver.start([&](auto) {
                if (receive_exception)
                    throw std::runtime_error("receive callback");
            }),
                  "exception receiver");
            check(!sender->send(message(2, 100, second),
                                [&](auto status) {
                                    check(!status, "exception test send succeeds");
                                    if (!receive_exception)
                                        throw std::runtime_error("completion callback");
                                }),
                  "exception send accepted");
            event_loop.until([&] {
                return (receive_exception ? receiver.last_error() : sender->last_error()) ==
                       r::errc::invalid;
            });
            close(event_loop, *sender);
            close(event_loop, receiver);
        }
        {
            loop event_loop;
            r::tcp_transport_options options;
            options.bind_address = available_address();
            r::tcp_transport sender{event_loop.value, options};
            check(!sender.init(99, "advertised.example:8080") && !sender.start([](auto) {}),
                  "bind override does not resolve advertised hostname");
            r::tcp_transport receiver{event_loop.value};
            const auto second = available_address();
            bool received = false, sent = false;
            check(!receiver.init(100, second) && !receiver.start([&](auto value) {
                check(value.server_address == "advertised.example:8080",
                      "handshake preserves advertised identity with bind override");
                received = true;
            }),
                  "override receiver");
            check(!sender.send(message(2, 100, second),
                               [&](auto status) {
                                   check(!status, "override send");
                                   sent = true;
                               }),
                  "override accepted");
            event_loop.until([&] { return received && sent; });
            close(event_loop, sender);
            close(event_loop, receiver);
        }
    }
    struct directory {
        r::fs::path path;
        directory() {
            r::string name = "/tmp/raftpp-tcp-XXXXXX";
            check(mkdtemp(name.data()), "temporary directory");
            path = name;
        }
        ~directory() {
            r::error_code ignored;
            r::fs::remove_all(path, ignored);
        }
    };
    struct machine : r::state_machine {
        r::uint64_t count = 0;
        r::uint64_t finalized_snapshot = 0;
        unsigned restores = 0, finalized = 0;
        r::error_code apply(r::view<const r::byte>, r::shared<void>&) override {
            ++count;
            return {};
        }
        r::error_code restore(r::shared_buffer data) override {
            if (data.size() != sizeof(count))
                return r::errc::corrupt;
            std::memcpy(&count, data.bytes().data(), sizeof(count));
            ++restores;
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>& output) override {
            r::buffer bytes{sizeof(count)};
            std::memcpy(bytes.data(), &count, sizeof(count));
            output.push_back(r::shared_buffer{std::move(bytes)});
            return {};
        }
        void snapshot_finalize(r::vector<r::shared_buffer>& values) noexcept override {
            if (values.size() == 1 && values[0].size() == sizeof(finalized_snapshot))
                std::memcpy(&finalized_snapshot, values[0].bytes().data(),
                            sizeof(finalized_snapshot));
            values.clear();
            ++finalized;
        }
    };
    void backend_diagnostics() {
        namespace fault = tcp_failure;
        struct throwing_transport final : r::transport {
            bool fail_start = false;
            r::error_code init(r::server_id, r::string_view) override {
                return {};
            }
            r::error_code start(r::function<void(r::message)>) override {
                if (fail_start)
                    throw std::runtime_error("start failure");
                return {};
            }
            r::error_code send(r::shared<const r::message>, r::completion) override {
                throw std::runtime_error("send failure");
            }
            void close(r::close_callback callback) noexcept override {
                if (callback)
                    callback();
            }
            r::error_info error_details() const noexcept override {
                return r::make_error_info(r::errc::io_error, "stale transport failure", {},
                                          std::make_error_code(std::errc::permission_denied));
            }
        };
        for (bool fail_start : {false, true}) {
            loop event_loop;
            directory dir;
            auto network = std::make_shared<throwing_transport>();
            network->fail_start = fail_start;
            r::uv_io backend{event_loop.value, dir.path, network};
            r::io_state loaded;
            check(!backend.init(1, "127.0.0.1:8080") && !backend.load(loaded),
                  "throwing transport backend");
            const auto started = backend.start(1000, [] {}, [](auto) {});
            if (fail_start)
                check(started == r::errc::io_error && !backend.error_details().cause,
                      "throwing startup does not inherit stale transport diagnosis");
            else
                check(!started &&
                          backend.send(message(2, 100, "127.0.0.1:8081"), {}) ==
                              r::errc::io_error &&
                          !backend.error_details().cause,
                      "throwing send does not inherit stale transport diagnosis");
            bool closed = false;
            backend.close([&] { closed = true; });
            event_loop.until([&] { return closed; });
        }
        {
            loop event_loop;
            directory dir;
            r::tcp_transport blocker{event_loop.value};
            const auto address = available_address();
            check(!blocker.init(50, address) && !blocker.start([](auto) {}), "occupied listener");
            auto network = std::make_shared<r::tcp_transport>(event_loop.value);
            auto backend = std::make_shared<r::uv_io>(event_loop.value, dir.path, network);
            auto fsm = std::make_shared<machine>();
            r::node value{1, address, backend, fsm};
            r::configuration members;
            check(!members.add(1, address, r::role::voter), "diagnostic node member");
            check(!value.bootstrap(members), "diagnostic node bootstrap");
            check(value.start() == r::errc::io_error,
                  "actual occupied TCP listener fails node start");
            const auto info = value.error_details();
            check(info.code == r::errc::io_error && info.cause == std::errc::address_in_use &&
                      info.text().find(address) != r::string_view::npos &&
                      info.text().find("uv.transport.start") != r::string_view::npos,
                  "native listen failure propagates through UV to node");
            bool closed = false;
            check(!value.close([&] { closed = true; }), "close failed startup node");
            event_loop.until([&] { return closed; });
            check(value.error_details().text() == info.text(),
                  "node retains startup cause after file/network cleanup");
            close(event_loop, blocker);
        }
        {
            loop event_loop;
            directory dir;
            auto network = std::make_shared<r::tcp_transport>(event_loop.value);
            r::uv_io backend{event_loop.value, dir.path, network};
            r::io_state loaded;
            check(!backend.init(99, available_address()) && !backend.load(loaded) &&
                      !backend.start(
                          1000, [] {}, [](auto) {}),
                  "UV diagnostic backend");
            r::tcp_transport receiver{event_loop.value};
            const auto destination = available_address();
            unsigned received = 0;
            check(!receiver.init(100, destination) && !receiver.start([&](auto) { ++received; }),
                  "UV diagnostic receiver");
            unsigned completed = 0;
            check(!backend.send(message(2, 100, destination),
                                [&](auto status) {
                                    check(!status, "UV warm send");
                                    ++completed;
                                }),
                  "UV warm accepted");
            event_loop.until([&] { return completed == 1 && received == 1; });
            fault::after(fault::operation::write, 0, UV_EIO);
            check(backend.send(message(2, 100, destination), {}) == r::errc::io_error &&
                      backend.error_details().cause == std::errc::io_error &&
                      backend.error_details().text().find("uv.send.submit") == 0,
                  "immediate TCP send context reaches UV");
            fault::after(fault::operation::write_result, 0, UV_EPIPE);
            r::error_info observed;
            check(!backend.send(
                      message(2, 100, destination),
                      [&](auto status) {
                          observed = backend.error_details();
                          check(status == r::errc::io_error && observed.code == status &&
                                    observed.cause == std::errc::broken_pipe &&
                                    observed.text().find(destination) != r::string_view::npos &&
                                    observed.text().find("uv.send.complete") == 0,
                                "asynchronous TCP diagnostic is captured at UV callback entry");
                          ++completed;
                      }),
                  "UV asynchronous failure accepted");
            event_loop.until([&] { return completed == 2 && received == 2; });
            bool closed = false;
            backend.close([&] { closed = true; });
            event_loop.until([&] { return closed; });
            close(event_loop, receiver);
            check(observed.cause == std::errc::broken_pipe,
                  "UV diagnostic copy survives backend shutdown");
        }
        {
            loop event_loop;
            r::tcp_transport value{event_loop.value};
            check(!value.init(1, "127.0.0.1:invalid-service"),
                  "numeric service validated during listen");
            check(value.start([](auto) {}) == r::errc::io_error &&
                      r::string_view{value.error_details().cause.category().name()} ==
                          "getaddrinfo" &&
                      value.error_details().text().find("tcp.listen.resolve") == 0,
                  "synchronous resolver preserves its own category/code");
            close(event_loop, value);
        }
    }
    void cluster(bool trace = false, r::function<void()> switch_memory = {}) {
        loop event_loop;
        r::array<directory, 3> directories;
        unsigned core_traces = 0, io_traces = 0, tcp_traces = 0, submitted = 0, completed = 0,
                 replacement = 0;
        unsigned new_submitted = 0, new_completed = 0, new_tcp = 0;
        auto diagnostic = [&](const r::trace_event& event, unsigned& counter) {
            check(event.type == r::trace_diagnostic && event.info, "typed layer diagnostic");
            const auto& value = *static_cast<const r::diagnostic_trace*>(event.info);
            check(value.location.line() != 0, "layer diagnostic source location");
            ++counter;
        };
        r::tracer core_trace =
            trace ? r::tracer{[&](const auto& event) { diagnostic(event, core_traces); }}
                  : r::tracer::noop();
        r::tracer io_trace = trace ? r::tracer{[&](const auto& event) {
            if (event.type == r::trace_write_submit) {
                check(!event.info, "submit event has no payload");
                ++submitted;
            } else if (event.type == r::trace_write_complete) {
                check(!event.info, "complete event has no payload");
                ++completed;
            } else
                diagnostic(event, io_traces);
        }}
                                   : r::tracer::noop();
        r::tracer tcp_trace =
            trace ? r::tracer{[&](const auto& event) { diagnostic(event, tcp_traces); }}
                  : r::tracer::noop();
        r::array<r::string, 3> addresses;
        for (unsigned i = 0; i < 3; ++i) {
            do {
                addresses[i] = available_address();
            } while (std::find(addresses.begin(), addresses.begin() + i, addresses[i]) !=
                     addresses.begin() + i);
        }
        r::configuration members;
        for (unsigned i = 0; i < 3; ++i)
            check(!members.add(i + 1, addresses[i], r::role::voter), "TCP cluster member");
        r::array<r::shared<r::uv_io>, 3> backends;
        r::array<r::shared<machine>, 3> machines;
        r::array<r::owned<r::node>, 3> nodes;
        event_loop.on_timeout = [&] {
            for (unsigned i = 0; i < nodes.size(); ++i)
                if (nodes[i]) {
                    std::cerr << "node=" << i + 1
                              << " state=" << static_cast<unsigned>(nodes[i]->state())
                              << " term=" << nodes[i]->current_term()
                              << " last=" << nodes[i]->last_index()
                              << " commit=" << nodes[i]->commit_index()
                              << " applied=" << nodes[i]->last_applied()
                              << " count=" << machines[i]->count
                              << " snapshot=" << machines[i]->finalized_snapshot
                              << " node_error=" << nodes[i]->error()
                              << " io_error=" << backends[i]->last_error() << '\n';
                }
        };
        auto open = [&](unsigned i, bool bootstrap) {
            r::tcp_transport_options transport_options;
            transport_options.connect_retry = 10;
            transport_options.tracing = tcp_trace;
            auto network = std::make_shared<r::tcp_transport>(event_loop.value, transport_options);
            if (trace && !bootstrap)
                network->set_tracer(
                    r::tracer{[&](const auto& event) { diagnostic(event, new_tcp); }});
            r::uv_io_options io_options;
            io_options.disk_retry = 1;
            io_options.segment_size = 8192;
            io_options.tracing = io_trace;
            backends[i] = std::make_shared<r::uv_io>(event_loop.value, directories[i].path, network,
                                                     io_options);
            machines[i] = std::make_shared<machine>();
            r::node_options options;
            options.heartbeat_timeout = 20;
            options.election_timeout = 1000;
            options.capacity_threshold = 0;
            options.snapshot_threshold = 4;
            options.snapshot_trailing = 1;
            options.tracing = core_trace;
            nodes[i] =
                std::make_unique<r::node>(i + 1, addresses[i], backends[i], machines[i], options);
            if (bootstrap)
                check(!nodes[i]->bootstrap(members), "bootstrap TCP cluster");
            check(!nodes[i]->start(), "start TCP node");
        };
        auto leader = [&] {
            for (unsigned i = 0; i < 3; ++i)
                if (nodes[i] && nodes[i]->state() == r::node_state::leader) {
                    const auto info = nodes[i]->leader();
                    if (info && info->id == i + 1)
                        return i;
                }
            return 3U;
        };
        auto healthy = [&] {
            for (const auto& node : nodes)
                if (node)
                    check(!node->error(), "TCP cluster node healthy");
        };
        auto await_leader = [&](std::source_location where = std::source_location::current()) {
            event_loop.until(
                [&] {
                    healthy();
                    return leader() < nodes.size();
                },
                where);
            const auto primary = leader();
            check(primary < nodes.size(), "TCP cluster has an available leader", where);
            return primary;
        };
        auto apply = [&] {
            const auto primary = await_leader();
            bool done = false;
            check(!nodes[primary]->apply(r::buffer{8},
                                         [&](auto status, auto) {
                                             check(!status, "TCP cluster commit");
                                             done = true;
                                         }),
                  "submit TCP command");
            event_loop.until([&] {
                healthy();
                return done;
            });
        };
        for (unsigned i = 0; i < 3; ++i)
            open(i, true);
        for (unsigned i = 0; i < 8; ++i)
            apply();
        event_loop.until([&] {
            return std::ranges::all_of(machines, [](auto& value) { return value->count == 8; });
        });
        if (switch_memory)
            switch_memory();
        const auto primary = await_leader(), lagger = (primary + 1) % 3;
        bool stopped = false;
        check(!nodes[lagger]->close([&] { stopped = true; }), "stop follower");
        event_loop.until([&] { return stopped; });
        nodes[lagger].reset();
        backends[lagger].reset();
        for (unsigned i = 0; i < 16; ++i)
            apply();
        // Both possible majority leaders must have persisted and compacted well
        // beyond the stopped node. A callback count alone can describe only old
        // snapshots and accidentally permit log-only catch-up after transfer.
        event_loop.until([&] {
            healthy();
            for (unsigned i = 0; i < 3; ++i)
                if (nodes[i] && (machines[i]->count != 24 || machines[i]->finalized_snapshot < 20))
                    return false;
            return true;
        });
        const auto transfer_from = await_leader();
        unsigned target = 0;
        while (target == transfer_from || target == lagger)
            ++target;
        check(target < nodes.size() && nodes[target], "transfer target is an available node");
        check(!nodes[transfer_from]->transfer(target + 1), "transfer over actual TCP");
        event_loop.until([&] {
            healthy();
            return leader() == target;
        });
        open(lagger, false);
        const auto restored_at_start = machines[lagger]->restores;
        event_loop.until([&] {
            healthy();
            return std::ranges::all_of(machines, [](auto& value) { return value->count == 24; });
        });
        check(machines[lagger]->restores > restored_at_start,
              "lagging disk node installs snapshot received over TCP");
        apply();
        event_loop.until([&] {
            return std::ranges::all_of(machines, [](auto& value) { return value->count == 25; });
        });
        auto stop_all = [&] {
            unsigned closed = 0;
            for (auto& node : nodes)
                check(!node->close([&] { ++closed; }), "close TCP node");
            event_loop.until([&] { return closed == 3; });
            for (unsigned i = 0; i < 3; ++i) {
                check(!backends[i]->last_error(), "disk backend healthy");
                nodes[i].reset();
                backends[i].reset();
            }
        };
        stop_all();
        for (unsigned i = 0; i < 3; ++i)
            open(i, false);
        if (trace)
            for (unsigned i = 0; i < 3; ++i) {
                nodes[i]->set_tracer(
                    r::tracer{[&](const auto& event) { diagnostic(event, replacement); }});
                // Propagates to existing log segments as well as future writers.
                backends[i]->set_tracer(r::tracer{[&](const auto& event) {
                    if (event.type == r::trace_write_submit)
                        ++new_submitted;
                    else if (event.type == r::trace_write_complete)
                        ++new_completed;
                    else
                        diagnostic(event, io_traces);
                }});
            }
        apply();
        event_loop.until([&] {
            healthy();
            return std::ranges::all_of(machines, [](auto& value) { return value->count == 26; });
        });
        for (auto& value : machines)
            check(value->restores > 0, "restarted TCP nodes restore disk snapshots");
        stop_all();
        if (trace)
            check(
                core_traces && replacement && io_traces && tcp_traces && new_tcp && submitted &&
                    new_submitted && new_completed &&
                    submitted + new_submitted == completed + new_completed,
                "enabled node/file/TCP tracing and native write event pairing survive transfer/snapshot/restart");
    }
    void memory_cluster() {
        auto previous = r::get_memory_resource();
        struct reset {
            r::shared<r::pmr::memory_resource> previous;
            ~reset() {
                (void)r::set_memory_resource(std::move(previous));
            }
        } restore{previous};
        auto stats = std::make_shared<memory_testing::statistics>();
        r::weak<r::pmr::memory_resource> observer;
        {
            auto resource = std::make_shared<memory_testing::resource>(stats);
            observer = resource;
            (void)r::set_memory_resource(resource);
        }
        cluster(false, [&] {
            check(stats->allocated > 50 && stats->live > 0,
                  "actual node/file/TCP allocations reach custom resource");
            r::reset_memory_resource();
            check(!observer.expired(),
                  "native operations keep resource alive after selection reset");
        });
        check(
            observer.expired() && stats->destroyed && stats->live == 0 &&
                stats->allocated == stats->freed && !stats->mismatch,
            "custom resource drains exactly after native close, snapshot transfer, and cluster restart");
    }

    void interoperability() {
        loop event_loop;
        const auto endpoint = available_address();
        r::tcp_transport transport{event_loop.value};
        check(!transport.init(99, endpoint), "message transport init");
        r::vector<r::message> received;
        check(!transport.start([&](auto value) { received.push_back(std::move(value)); }),
              "message listener");
        socket native;
        connect(native, endpoint);
        auto hello = handshake(100, "native-peer");
        write(native, hello.bytes());
        for (unsigned i = 0; i < 6; ++i) {
            const auto frame = wire(*message(i, 99, endpoint));
            write(native, frame);
        }
        event_loop.until([&] { return received.size() == 6; });
        for (unsigned i = 0; i < 6; ++i) {
            same_message(received[i], i);
            check(received[i].server_id == 100 && received[i].server_address == "native-peer",
                  "native handshake identifies all six received RPCs");
        }

        socket listener;
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        check(!::bind(listener.value, reinterpret_cast<sockaddr*>(&address), sizeof(address)) &&
                  !::listen(listener.value, 1),
              "native return listener");
        socklen_t address_size = sizeof(address);
        check(!::getsockname(listener.value, reinterpret_cast<sockaddr*>(&address), &address_size),
              "native return address");
        check(::fcntl(listener.value, F_SETFL, O_NONBLOCK) == 0, "nonblocking accept");
        const auto destination = "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
        auto outgoing_hello = handshake(99, endpoint);
        r::vector<r::byte> expected(outgoing_hello.bytes().begin(), outgoing_hello.bytes().end());
        unsigned completed = 0;
        for (unsigned i = 0; i < 6; ++i) {
            auto value = message(i, 100, destination);
            const auto frame = wire(*value);
            expected.insert(expected.end(), frame.begin(), frame.end());
            check(!transport.send(std::move(value),
                                  [&](auto status) {
                                      check(!status, "C++ RPC send completion");
                                      ++completed;
                                  }),
                  "send all six RPCs to native peer");
        }
        socket accepted;
        accepted.close();
        event_loop.until([&] {
            if (accepted.value >= 0)
                return true;
            accepted.value = ::accept(listener.value, nullptr, nullptr);
            check(accepted.value >= 0 || errno == EAGAIN || errno == EWOULDBLOCK,
                  "accept native return connection");
            return accepted.value >= 0;
        });
        r::vector<r::byte> bytes;
        event_loop.until([&] {
            r::array<r::byte, 8192> chunk;
            const auto count = ::recv(accepted.value, chunk.data(), chunk.size(), MSG_DONTWAIT);
            check(count > 0 || (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)),
                  "native receive remains open until all frames arrive");
            if (count > 0)
                bytes.insert(bytes.end(), chunk.begin(), chunk.begin() + count);
            return bytes.size() >= expected.size() && completed == 6;
        });
        check(bytes == expected, "C++ handshake and six RPC frames match prescribed wire bytes");
        accepted.close();
        listener.close();
        native.close();
        close(event_loop, transport);
    }
}
int main(int argc, char** argv) {
    // A closed peer must surface as a write error rather than terminate the test process.
    std::signal(SIGPIPE, SIG_IGN);
    try {
        check(argc == 2, "suite argument");
        const r::string_view suite = argv[1];
        if (suite == "tcp_exchange")
            exchange();
        else if (suite == "tcp_fragments")
            fragments();
        else if (suite == "tcp_malformed")
            malformed();
        else if (suite == "tcp_retry")
            retry_and_close();
        else if (suite == "tcp_settings")
            runtime_settings();
        else if (suite == "tcp_lifetime")
            lifetime();
        else if (suite == "tcp_allocation")
            allocations();
        else if (suite == "tcp_interop")
            interoperability();
        else if (suite == "tcp_cluster")
            cluster();
        else if (suite == "tcp_tracing")
            cluster(true);
        else if (suite == "tcp_memory")
            memory_cluster();
        else if (suite == "tcp_failure")
            native_failures();
        else if (suite == "tcp_diagnostics")
            diagnostics();
        else if (suite == "tcp_peer_diagnostics")
            peer_diagnostics();
        else if (suite == "tcp_backend_diagnostics")
            backend_diagnostics();
        else if (suite == "tcp_shutdown")
            shutdown_paths();
        else
            check(false, "unknown suite");
        return 0;
    } catch (const std::exception& e) {
        allocation_failure::after(-1);
        std::cerr << e.what() << '\n';
        return 1;
    }
}
