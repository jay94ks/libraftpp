// Multiple-address contracts from cowsql/raft test/integration/test_uv_tcp_*.c;
// see LICENSE.md. Resolver injection is confined to this test executable.
#include <raftpp.hpp>
#include "byte.hpp"
#include <array>
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <iostream>
#include <mutex>
#include <netdb.h>
#include <source_location>
#include <sys/socket.h>
#include <thread>
#include <unistd.h>
#include <uv.h>

namespace r = raftpp;
namespace resolver_testing {
    struct answer {
        addrinfo value{};
        sockaddr_in address{};
    };
    std::mutex mutex;
    std::array<sockaddr_in, 2> endpoints{};
    std::array<addrinfo*, 8> allocations{};
    int result = 0;
    unsigned calls = 0, freed = 0;
    void configure(std::array<sockaddr_in, 2> values, int status = 0) {
        std::lock_guard lock{mutex};
        endpoints = values;
        result = status;
    }
    void clear(addrinfo* head) noexcept {
        while (head) {
            auto next = head->ai_next;
            std::free(reinterpret_cast<answer*>(head));
            head = next;
        }
    }
}
extern "C" int getaddrinfo(const char* node, const char* service, const addrinfo* hints,
                           addrinfo** output) {
    if (!node || std::strcmp(node, "raftpp.test") != 0) {
        static auto original =
            reinterpret_cast<decltype(&getaddrinfo)>(dlsym(RTLD_NEXT, "getaddrinfo"));
        return original(node, service, hints, output);
    }
    using namespace resolver_testing;
    std::lock_guard lock{mutex};
    ++calls;
    *output = nullptr;
    if (result)
        return result;
    addrinfo* head = nullptr;
    for (auto it = endpoints.rbegin(); it != endpoints.rend(); ++it) {
        auto item = static_cast<answer*>(std::calloc(1, sizeof(answer)));
        if (!item) {
            clear(head);
            return EAI_MEMORY;
        }
        item->address = *it;
        item->value.ai_family = AF_INET;
        item->value.ai_socktype = SOCK_STREAM;
        item->value.ai_protocol = IPPROTO_TCP;
        item->value.ai_addrlen = sizeof(sockaddr_in);
        item->value.ai_addr = reinterpret_cast<sockaddr*>(&item->address);
        item->value.ai_next = head;
        head = &item->value;
    }
    for (auto& allocation : allocations)
        if (!allocation) {
            allocation = head;
            *output = head;
            return 0;
        }
    clear(head);
    return EAI_MEMORY;
}
extern "C" void freeaddrinfo(addrinfo* value) noexcept {
    using namespace resolver_testing;
    {
        std::lock_guard lock{mutex};
        for (auto& allocation : allocations)
            if (value && allocation == value) {
                allocation = nullptr;
                ++freed;
                clear(value);
                return;
            }
    }
    static auto original =
        reinterpret_cast<decltype(&freeaddrinfo)>(dlsym(RTLD_NEXT, "freeaddrinfo"));
    original(value);
}
namespace {
    void check(bool value, r::string_view message,
               std::source_location where = std::source_location::current()) {
        if (!value)
            throw std::runtime_error(r::string{where.file_name()} + ":" +
                                     std::to_string(where.line()) + ": " + r::string{message});
    }
    struct loop {
        uv_loop_t value{};
        loop() {
            check(!uv_loop_init(&value), "loop init");
        }
        ~loop() {
            (void)uv_run(&value, UV_RUN_DEFAULT);
            if (uv_loop_close(&value))
                std::abort();
        }
        template <class F> void until(F ready) {
            const auto deadline = uv_hrtime() + 5000000000ULL;
            while (!ready() && uv_hrtime() < deadline) {
                (void)uv_run(&value, UV_RUN_NOWAIT);
                std::this_thread::sleep_for(r::chrono::milliseconds{1});
            }
            check(ready(), "operation completes before deadline");
        }
    };
    struct socket {
        int value = ::socket(AF_INET, SOCK_STREAM, 0);
        socket() {
            check(value >= 0, "socket");
        }
        ~socket() {
            close();
        }
        void close() {
            if (value >= 0)
                ::close(std::exchange(value, -1));
        }
        sockaddr_in reserve(bool listen = false) {
            sockaddr_in address{};
            address.sin_family = AF_INET;
            address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            check(!::bind(value, reinterpret_cast<sockaddr*>(&address), sizeof address),
                  "reserve endpoint");
            socklen_t size = sizeof address;
            check(!::getsockname(value, reinterpret_cast<sockaddr*>(&address), &size),
                  "query endpoint");
            if (listen)
                check(!::listen(value, 4), "listen endpoint");
            return address;
        }
    };
    struct outcome {
        int submitted = 0, completed = -1;
        unsigned callbacks = 0, accepted = 0;
        r::server_id peer = 0;
        r::array<char, 64> address{};
        r::array<r::byte, 40> handshake{};
        bool operator==(const outcome&) const = default;
    };
    struct connector {
        loop& event_loop;
        outcome observed;
        r::tcp_stream_transport transport;
        r::vector<r::uv_stream> streams;
        bool closed = false;
        connector(loop& loop, bool listening = false) : event_loop(loop), transport(loop.value) {
            check(!transport.init(7, listening ? "raftpp.test:12345" : "127.0.0.1:10001"),
                  "connector init");
        }
        ~connector() {
            close();
            event_loop.until([&] { return closed; });
        }
        void close() {
            if (closed)
                return;
            streams.clear();
            transport.close([this] { closed = true; });
        }
        void connect() {
            observed.submitted = transport
                                     .connect(99, "raftpp.test:12345",
                                              [this](auto status, auto stream) {
                                                  observed.completed = status.value();
                                                  ++observed.callbacks;
                                                  if (!status)
                                                      streams.push_back(std::move(stream));
                                              })
                                     .value();
        }
        void listen() {
            observed.submitted =
                transport
                    .listen([this](auto id, auto address, auto stream) {
                        observed.peer = id;
                        std::snprintf(observed.address.data(), observed.address.size(), "%s",
                                      address.c_str());
                        ++observed.accepted;
                        streams.push_back(std::move(stream));
                    })
                    .value();
        }
    };
    outcome connect_case(unsigned kind) {
        loop loop;
        socket server, refused;
        const auto live = server.reserve(true), offline = refused.reserve();
        resolver_testing::configure(kind == 1   ? std::array{offline, live}
                                    : kind == 2 ? std::array{offline, offline}
                                                : std::array{live, offline});
        connector transport{loop};
        transport.connect();
        if (kind == 3)
            transport.close();
        loop.until([&] { return transport.observed.callbacks == 1; });
        check(!transport.observed.submitted &&
                  transport.observed.completed == (kind == 2
                                                       ? static_cast<int>(r::errc::no_connection)
                                                   : kind == 3 ? static_cast<int>(r::errc::canceled)
                                                               : 0),
              "connect/fallback/refusal/cancel status");
        if (kind < 2) {
            const auto peer = ::accept(server.value, nullptr, nullptr);
            check(peer >= 0, "accept connector handshake");
            timeval timeout{2, 0};
            (void)::setsockopt(peer, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
            const auto count = ::recv(peer, transport.observed.handshake.data(),
                                      transport.observed.handshake.size(), MSG_WAITALL);
            ::close(peer);
            check(count == 40, "complete handshake bytes");
            r::array<r::byte, 40> expected{};
            expected[0] = r::byte{1};
            expected[8] = r::byte{7};
            expected[16] = r::byte{16};
            constexpr r::string_view identity = "127.0.0.1:10001";
            for (r::size_t i = 0; i < identity.size(); ++i)
                expected[24 + i] = static_cast<r::byte>(identity[i]);
            check(transport.observed.handshake == expected,
                  "fixed protocol/id/padded-address handshake bytes");
        }
        transport.close();
        loop.until([&] { return transport.closed; });
        return transport.observed;
    }
    outcome listen_case(unsigned kind) {
        loop loop;
        socket first, second;
        const auto a = first.reserve(), b = second.reserve();
        first.close();
        second.close();
        auto bad = a;
        check(!uv_ip4_addr("192.0.2.1", ntohs(a.sin_port), &bad), "nonlocal endpoint");
        resolver_testing::configure(kind == 1   ? std::array{a, a}
                                    : kind == 2 ? std::array{bad, b}
                                    : kind == 3 ? std::array{a, bad}
                                                : std::array{a, b},
                                    kind == 4 ? EAI_FAIL : 0);
        connector transport{loop, true};
        transport.listen();
        check(transport.observed.submitted == (kind >= 2 ? static_cast<int>(r::errc::io_error) : 0),
              "listen multiple/bind/resolver result");
        if (kind < 2) {
            r::buffer hello{40};
            r::detail::byte_writer writer{hello.bytes()};
            writer.write<r::uint64_t>(1);
            writer.write<r::uint64_t>(99);
            writer.write<r::uint64_t>(16);
            constexpr r::string_view identity = "client:777";
            writer.write({reinterpret_cast<const r::byte*>(identity.data()), identity.size()});
            for (unsigned i = 0; i < (kind == 1 ? 1U : 2U); ++i) {
                socket client;
                const auto endpoint = i == 0 ? a : b;
                check(!::connect(client.value, reinterpret_cast<const sockaddr*>(&endpoint),
                                 sizeof endpoint),
                      "connect each listener");
                check(::send(client.value, hello.data(), hello.size(), MSG_NOSIGNAL) == 40,
                      "send peer identity");
                loop.until([&] { return transport.observed.accepted == i + 1; });
            }
            check(transport.observed.peer == 99 &&
                      r::string_view{transport.observed.address.data()} == identity,
                  "handed-off peer identity");
        }
        transport.close();
        loop.until([&] { return transport.closed; });
        // Also drain handed-off stream handles before checking partial bind cleanup.
        (void)uv_run(&loop.value, UV_RUN_DEFAULT);
        socket rebound;
        int reuse = 1;
        check(!::setsockopt(rebound.value, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof reuse),
              "rebind address option");
        check(!::bind(rebound.value, reinterpret_cast<const sockaddr*>(&a), sizeof a),
              "listener sockets released");
        return transport.observed;
    }
}
int main() {
    try {
        unsigned cases = 0;
        for (unsigned kind = 0; kind < 4; ++kind) {
            (void)connect_case(kind);
            ++cases;
        }
        for (unsigned kind = 0; kind < 5; ++kind) {
            (void)listen_case(kind);
            ++cases;
        }
        std::lock_guard lock{resolver_testing::mutex};
        for (auto allocation : resolver_testing::allocations)
            check(!allocation, "all resolver results released");
        check(resolver_testing::calls >= cases - 1 &&
                  resolver_testing::freed + 1 == resolver_testing::calls,
              "all successful resolver allocations are released, including cancellation");
        std::cout << "resolver contracts: " << cases << " cases; " << resolver_testing::freed
                  << " resolver results released\n";
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
