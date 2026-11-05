#include <raftpp/bindings/bindings.hpp>
#include <filesystem>
#include <thread>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <array>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace {
    void require(bool condition, const char* message) {
        if (!condition)
            throw std::runtime_error(message);
    }
    std::string address() {
        const int fd = socket(AF_INET, SOCK_STREAM, 0);
        require(fd >= 0, "socket");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        require(bind(fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0, "bind");
        socklen_t length = sizeof(address);
        require(getsockname(fd, reinterpret_cast<sockaddr*>(&address), &length) == 0,
                "getsockname");
        close(fd);
        return "127.0.0.1:" + std::to_string(ntohs(address.sin_port));
    }
    struct context {
        raftpp_binding_node* node = nullptr;
        int applies = 0, snapshots = 0;
        bool reentrant_rejected = false;
        std::array<uint8_t, 3> value{};
    };
    int machine(void* pointer, int operation, const uint8_t* data, size_t size, void* output) {
        auto& ctx = *static_cast<context*>(pointer);
        if (operation == 0) {
            ++ctx.applies;
            require(size == 3, "arbitrary command length not preserved");
            ctx.value = {data[0], data[1], data[2]};
            ctx.reentrant_rejected = raftpp_binding_poll(ctx.node) == 17;
        } else if (operation == 1) {
            ++ctx.snapshots;
        } else {
            return 0;
        }
        return raftpp_binding_set_output(output, ctx.value.data(), ctx.value.size());
    }
    template <typename F> void until(raftpp_binding_node* node, F condition) {
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(10);
        while (!condition()) {
            require(std::chrono::steady_clock::now() < end, "timeout");
            require(raftpp_binding_poll(node) == 0, "poll");
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    }
}
int main() {
    raftpp_binding_node* node = nullptr;
    const auto root = std::filesystem::temp_directory_path() /
                      ("raftpp-native-binding-" + std::to_string(getpid()));
    try {
        std::filesystem::create_directories(root);
        require(raftpp_binding_abi_version() == 2, "ABI");
        require(raftpp_binding_create(0, "", "", nullptr, nullptr, nullptr, &node) == 20 && !node,
                "invalid create");
        context ctx;
        const auto endpoint = address();
        const raftpp_binding_options options{150, 30, 2, 2};
        require(!raftpp_binding_create(1, endpoint.c_str(), root.c_str(), &options, machine, &ctx,
                                       &node),
                "create");
        ctx.node = node;
        int wrong_thread = 0;
        std::thread thread([&] { wrong_thread = raftpp_binding_poll(node); });
        thread.join();
        require(wrong_thread == 17, "thread admission");
        raftpp_binding_server server{1, endpoint.c_str(), 1};
        require(!raftpp_binding_bootstrap(node, &server, 1, 0), "bootstrap");
        require(!raftpp_binding_start(node), "start");
        until(node, [&] {
            raftpp_binding_status status{};
            require(!raftpp_binding_get_status(node, &status), "status");
            return status.state == 3;
        });
        uint8_t input[] = {1, 2, 3};
        require(!raftpp_binding_submit(node, 0, 1, input, 3, 0, nullptr, 0), "apply");
        input[0] = 99;
        require(raftpp_binding_submit(node, 1, 1, nullptr, 0, 0, nullptr, 0) == 20,
                "duplicate request");
        require(raftpp_binding_submit(node, 3, 2, nullptr, 0, 2, nullptr, 99) == 20,
                "invalid role");
        require(raftpp_binding_submit(node, 0, 2, input, size_t(-1), 0, nullptr, 0) == 15,
                "overflow");
        raftpp_binding_completion result{};
        until(node, [&] { return raftpp_binding_peek_completion(node, &result) == 1; });
        require(result.request_id == 1 && !result.status && result.size == 3 && result.data[0] == 1,
                "result/copy");
        require(ctx.applies == 1 && ctx.reentrant_rejected, "FSM and reentrancy");
        require(!raftpp_binding_pop_completion(node), "pop");
        require(raftpp_binding_peek_completion(node, &result) == 0, "exactly once");
        require(!raftpp_binding_close(node), "close");
        until(node, [&] {
            raftpp_binding_status status{};
            require(!raftpp_binding_get_status(node, &status), "close status");
            return status.closed;
        });
        raftpp_binding_release(node);
        node = nullptr;
        // Abandon a started node: release disables foreign callbacks and drains
        // the private loop on a cleanup thread without entering this context.
        require(!raftpp_binding_create(1, endpoint.c_str(), root.c_str(), &options, machine, &ctx,
                                       &node),
                "reopen");
        ctx.node = node;
        require(!raftpp_binding_start(node), "restart");
        raftpp_binding_release(node);
        node = nullptr;
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::filesystem::remove_all(root);
        std::cout << "binding ABI contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        if (node)
            raftpp_binding_release(node);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
