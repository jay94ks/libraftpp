// Test-only native submission failures. Normal calls go to unchanged libuv.
#include "tcp_failure.hpp"
#include <dlfcn.h>
#include <array>
#include <cstdlib>
#include <utility>
#include <uv.h>
namespace tcp_failure {
    namespace {
        operation selected = operation::none;
        unsigned remaining = 0;
        int result = 0;
        struct forced_write {
            uv_write_t* request = nullptr;
            uv_write_cb callback = nullptr;
            int result = 0;
        };
        std::array<forced_write, 16> writes{};
        int take(operation value) noexcept {
            if (selected != value)
                return 0;
            if (remaining) {
                --remaining;
                return 0;
            }
            selected = operation::none;
            return result;
        }
    }
    void after(operation operation, unsigned skip, int status) noexcept {
        selected = operation;
        remaining = skip;
        result = status;
    }
}
extern "C" {
int uv_tcp_init(uv_loop_t* loop, uv_tcp_t* handle) {
    static auto original =
        reinterpret_cast<decltype(&uv_tcp_init)>(dlsym(RTLD_NEXT, "uv_tcp_init"));
    if (auto status = tcp_failure::take(tcp_failure::operation::tcp_init))
        return status;
    return original(loop, handle);
}
int uv_listen(uv_stream_t* stream, int backlog, uv_connection_cb callback) {
    static auto original = reinterpret_cast<decltype(&uv_listen)>(dlsym(RTLD_NEXT, "uv_listen"));
    if (auto status = tcp_failure::take(tcp_failure::operation::listen))
        return status;
    return original(stream, backlog, callback);
}
int uv_getaddrinfo(uv_loop_t* loop, uv_getaddrinfo_t* request, uv_getaddrinfo_cb callback,
                   const char* node, const char* service, const addrinfo* hints) {
    static auto original =
        reinterpret_cast<decltype(&uv_getaddrinfo)>(dlsym(RTLD_NEXT, "uv_getaddrinfo"));
    if (auto status = tcp_failure::take(tcp_failure::operation::resolve))
        return status;
    return original(loop, request, callback, node, service, hints);
}
int uv_tcp_connect(uv_connect_t* request, uv_tcp_t* handle, const sockaddr* address,
                   uv_connect_cb callback) {
    static auto original =
        reinterpret_cast<decltype(&uv_tcp_connect)>(dlsym(RTLD_NEXT, "uv_tcp_connect"));
    if (auto status = tcp_failure::take(tcp_failure::operation::connect))
        return status;
    return original(request, handle, address, callback);
}
int uv_write(uv_write_t* request, uv_stream_t* stream, const uv_buf_t buffers[], unsigned count,
             uv_write_cb callback) {
    static auto original = reinterpret_cast<decltype(&uv_write)>(dlsym(RTLD_NEXT, "uv_write"));
    if (auto status = tcp_failure::take(tcp_failure::operation::write))
        return status;
    if (auto result = tcp_failure::take(tcp_failure::operation::write_result)) {
        for (auto& slot : tcp_failure::writes)
            if (!slot.request) {
                slot = {request, callback, result};
                const auto submitted =
                    original(request, stream, buffers, count, [](uv_write_t* request, int status) {
                        for (auto& slot : tcp_failure::writes)
                            if (slot.request == request) {
                                const auto saved = std::exchange(slot, {});
                                saved.callback(request, status ? status : saved.result);
                                return;
                            }
                        std::abort();
                    });
                if (submitted)
                    slot = {};
                return submitted;
            }
        std::abort();
    }
    return original(request, stream, buffers, count, callback);
}
int uv_accept(uv_stream_t* server, uv_stream_t* client) {
    static auto original = reinterpret_cast<decltype(&uv_accept)>(dlsym(RTLD_NEXT, "uv_accept"));
    if (auto status = tcp_failure::take(tcp_failure::operation::accept))
        return status;
    return original(server, client);
}
int uv_read_start(uv_stream_t* stream, uv_alloc_cb allocate, uv_read_cb read) {
    static auto original =
        reinterpret_cast<decltype(&uv_read_start)>(dlsym(RTLD_NEXT, "uv_read_start"));
    if (auto status = tcp_failure::take(tcp_failure::operation::read))
        return status;
    return original(stream, allocate, read);
}
int uv_timer_init(uv_loop_t* loop, uv_timer_t* handle) {
    static auto original =
        reinterpret_cast<decltype(&uv_timer_init)>(dlsym(RTLD_NEXT, "uv_timer_init"));
    if (auto status = tcp_failure::take(tcp_failure::operation::timer_init))
        return status;
    return original(loop, handle);
}
}
