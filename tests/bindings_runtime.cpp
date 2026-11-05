#include <raftpp/bindings/bindings.hpp>
#include <raftpp/bindings/runtime.hpp>
#include <vector>
#include <iostream>
#include <stdexcept>
#include <cstring>
namespace {
    void check(bool value, const char* message) {
        if (!value)
            throw std::runtime_error(message);
    }
    int phases = 0, finalized = 0, observations = 0;
    raftpp_binding_node* observed = nullptr;
    bool observer_failed = false, close_on_step = false;
    int machine(void*, int operation, const uint8_t* data, size_t size, void* output) {
        if (operation >= 5) {
            raftpp_binding_status status{};
            observer_failed |= raftpp_binding_get_status(observed, &status) != 0;
            observer_failed |= raftpp_binding_poll(observed) != 17;
            ++observations;
            if (operation == 5 && close_on_step)
                observer_failed |= raftpp_binding_close(observed) != 0;
            return 0;
        }
        if (operation == 4) {
            ++finalized;
            return 0;
        }
        if (operation == 2)
            return 0;
        if (operation == 1) {
            static const uint8_t bytes[]{1, 2, 3};
            return raftpp_binding_set_output(output, bytes, 3);
        }
        if (operation == 3)
            ++phases;
        return raftpp_binding_set_output(output, data, size);
    }
}
int main(int argc, char** argv) {
    raftpp_binding_node* node = nullptr;
    try {
        check(argc == 2, "plugin argument");
        check(!raftpp_binding_load_plugin(argv[1]), "load factory plugin");
        check(raftpp_binding_load_plugin(argv[1]) == 17, "duplicate factory");
        raftpp_binding_runtime_options options{};
        options.node = {150, 30, 2, 0};
        options.install_snapshot_timeout = 30000;
        options.max_catch_up_rounds = 10;
        options.max_catch_up_round_duration = 5000;
        options.max_inflight_entries = 32;
        options.capacity_threshold = 0;
        options.snapshot_async = 1;
        options.factory = "test-memory";
        options.storage = "uv";
        options.timer = "uv";
        options.network = "uv";
        options.codec = "none";
        check(!raftpp_binding_create_runtime(1, "plugin", "", &options, machine, nullptr, &node),
              "create plugin runtime");
        observed = node;
        check(!raftpp_binding_configure(node, 11, 1) && !raftpp_binding_configure(node, 12, 1),
              "observers");
        check(raftpp_binding_configure(node, 9, 65536) == 20, "invalid capacity");
        raftpp_binding_server server{1, "plugin", 1};
        check(!raftpp_binding_bootstrap(node, &server, 1, 0), "bootstrap");
        check(!raftpp_binding_start(node), "start");
        raftpp_binding_status status{};
        for (int i = 0; i < 100; ++i) {
            check(!raftpp_binding_poll(node), "poll");
            check(!raftpp_binding_get_status(node, &status), "status");
            if (status.state == 3)
                break;
        }
        check(status.state == 3, "election");
        uint8_t command[]{9, 8, 7};
        check(!raftpp_binding_submit(node, 0, 1, command, 3, 0, nullptr, 0), "submit");
        raftpp_binding_completion completion{};
        for (int i = 0; i < 100; ++i) {
            raftpp_binding_poll(node);
            if (raftpp_binding_peek_completion(node, &completion) == 1)
                break;
        }
        check(completion.request_id == 1 && !completion.status && completion.size == 3 &&
                  !std::memcmp(command, completion.data, 3),
              "FSM result");
        check(!raftpp_binding_pop_completion(node), "pop");
        check(phases > 0 && finalized > 0, "snapshot hooks");
        raftpp_binding_details details{};
        check(!raftpp_binding_details_get(node, &details), "details");
        check(details.local_role == 1 && std::string(details.leader_address) == "plugin",
              "role/leader");
        size_t size = 0;
        check(!raftpp_binding_read(node, 0, nullptr, &size) && size >= 9, "configuration query");
        std::vector<uint8_t> configuration(size);
        check(!raftpp_binding_read(node, 0, configuration.data(), &size), "configuration copy");
        check(configuration[0] == 1, "configuration format");
        size = 0;
        check(!raftpp_binding_read(node, 2, nullptr, &size) && size == 6,
              "full leader address size");
        std::vector<uint8_t> address(size);
        check(!raftpp_binding_read(node, 2, address.data(), &size), "full address copy");
        check(std::string(address.begin(), address.end()) == "plugin", "full leader address");
        size = 0;
        check(!raftpp_binding_read(node, 1, nullptr, &size) && size >= 9, "event queue");
        std::vector<uint8_t> event(size);
        check(!raftpp_binding_read(node, 1, event.data(), &size), "event copy");
        check(observations > 0 && !observer_failed,
              "synchronous observer queries and no recursive poll");
        close_on_step = true;
        for (int i = 0; i < 100; ++i) {
            raftpp_binding_poll(node);
            raftpp_binding_get_status(node, &status);
            if (status.closed)
                break;
        }
        check(status.closed && !observer_failed, "close from native step observer");
        raftpp_binding_release(node);
        node = nullptr;
        std::cout << "plugin/full node runtime contracts passed\n";
        return 0;
    } catch (const std::exception& error) {
        raftpp_binding_release(node);
        std::cerr << error.what() << '\n';
        return 1;
    }
}
