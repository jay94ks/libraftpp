#include <raftpp/bindings/bindings.hpp>
#include <raftpp/node.hpp>
#include <raftpp/bindings/runtime.hpp>
#include "runtime.hpp"
#include <dlfcn.h>
#include <mutex>
#include <map>
#include <deque>
#include <list>
#include <thread>
#include <cstring>

namespace r = raftpp;
namespace {
    int code(r::error_code error) noexcept {
        if (!error)
            return 0;
        return error.category() == r::raft_category() ? error.value() : int(r::errc::io_error);
    }
    template <typename F> int protect(F&& fn) noexcept {
        try {
            return fn();
        } catch (const std::bad_alloc&) {
            return int(r::errc::out_of_memory);
        } catch (const std::length_error&) {
            return int(r::errc::too_big);
        } catch (const std::system_error& error) {
            return code(error.code());
        } catch (...) {
            return int(r::errc::invalid);
        }
    }
    r::buffer pack(r::view<const r::byte> bytes) {
        if (bytes.size() > std::numeric_limits<size_t>::max() - 15)
            throw std::length_error("binding payload too big");
        r::buffer result{(bytes.size() + 15) & ~size_t(7)};
        for (unsigned i = 0; i < 8; ++i)
            result.data()[i] = r::byte((uint64_t(bytes.size()) >> (8 * i)) & 255);
        if (!bytes.empty())
            std::memcpy(result.data() + 8, bytes.data(), bytes.size());
        return result;
    }
    bool unpack(r::view<const r::byte>& bytes) noexcept {
        if (bytes.size() < 8 || bytes.size() % 8)
            return false;
        uint64_t length = 0;
        for (unsigned i = 0; i < 8; ++i)
            length |= uint64_t(std::to_integer<uint8_t>(bytes[i])) << (8 * i);
        if (length > bytes.size() - 8 || bytes.size() - 8 - length > 7)
            return false;
        for (size_t i = 8 + size_t(length); i < bytes.size(); ++i)
            if (bytes[i] != r::byte{})
                return false;
        bytes = bytes.subspan(8, size_t(length));
        return true;
    }
    struct machine final : r::state_machine {
        raftpp_binding_machine callback;
        void* context;
        bool inside = false, raw = false, second_phase = false;
        machine(raftpp_binding_machine fn, void* ctx) : callback(fn), context(ctx) {}
        int call(int operation, r::view<const r::byte> input, r::buffer& output) {
            if (!callback)
                return int(r::errc::shutdown);
            const bool previous = inside;
            if (operation < 5)
                inside = true;
            const auto status = protect([&] {
                return callback(context, operation, reinterpret_cast<const uint8_t*>(input.data()),
                                input.size(), &output);
            });
            inside = previous;
            return status >= 0 && status <= int(r::errc::too_many) ? status : int(r::errc::invalid);
        }
        r::error_code apply(r::view<const r::byte> input, r::shared<void>& result) override {
            // Envelope preserves arbitrary/empty byte strings on the UV wire.
            if (!raw && !unpack(input))
                return r::errc::malformed;
            auto bytes = std::make_shared<r::buffer>();
            if (auto status = call(0, input, *bytes))
                return r::errc(status);
            result = std::move(bytes);
            return {};
        }
        r::error_code snapshot(r::vector<r::shared_buffer>& output) override {
            r::buffer bytes;
            if (auto status = call(1, {}, bytes))
                return r::errc(status);
            output.emplace_back(raw ? std::move(bytes) : pack(bytes.bytes()));
            return {};
        }
        r::error_code snapshot_async(r::vector<r::shared_buffer>& buffers) override {
            if (!second_phase)
                return {};
            r::buffer bytes;
            auto input = buffers.front().bytes();
            if (!raw && !unpack(input))
                return r::errc::malformed;
            if (auto status = call(3, input, bytes))
                return r::errc(status);
            buffers.clear();
            buffers.emplace_back(raw ? std::move(bytes) : pack(bytes.bytes()));
            return {};
        }
        void snapshot_finalize(r::vector<r::shared_buffer>& buffers) noexcept override {
            if (second_phase) {
                r::buffer ignored;
                (void)call(4, {}, ignored);
            }
            buffers.clear();
        }
        r::error_code restore(r::shared_buffer input) override {
            r::buffer ignored;
            auto bytes = input.bytes();
            if (!raw && !unpack(bytes))
                return r::errc::malformed;
            return r::errc(call(2, bytes, ignored));
        }
    };
    struct request {
        uint64_t id;
        bool done = false;
        int status = 0;
        r::shared<r::buffer> result;
    };
}
struct raftpp_binding_node {
    r::bindings::runtime runtime;
    r::shared<r::bindings::detail::packet_transport> packets;
    std::deque<r::buffer> events;
    uint64_t dropped_events = 0;
    std::thread::id owner = std::this_thread::get_id();
    r::shared<machine> fsm;
    r::shared<r::io> backend;
    r::owned<r::node> value;
    std::list<request> requests;
    bool closing = false, closed = false, polling = false, observing = false;
    bool valid() const noexcept {
        return owner == std::this_thread::get_id() &&
               (!polling || observing || (fsm && fsm->inside));
    }
};
namespace {
    int close_node(raftpp_binding_node& node) {
        if (node.closing)
            return 0;
        int status = 0;
        if (node.value) {
            status = code(node.value->close([&node] { node.closed = true; }));
        } else if (node.backend) {
            node.backend->close([&node] { node.closed = true; });
        } else {
            node.closed = true;
        }
        if (!status)
            node.closing = true;
        return status;
    }
    void drain(raftpp_binding_node* node) noexcept {
        // Abandonment needs no foreign completion or allocating close observer.
        // Keep the queue/backend alive until every loop handle/worker drains.
        if (!node->closing) {
            if (node->value)
                (void)node->value->close();
            else if (node->backend)
                node->backend->close({});
            node->closing = true;
        }
        try {
            if (node->runtime.drain)
                node->runtime.drain();
        } catch (...) {
            return;
        }
        node->value.reset();
        node->backend.reset();

        try {
            if (node->runtime.drain)
                node->runtime.drain();
        } catch (...) {
            return;
        }
        node->runtime.backend.reset();
        node->runtime = {};
        delete node;
    }
}
extern "C" {
uint32_t raftpp_binding_abi_version() noexcept {
    return 2;
}
const char* raftpp_binding_error(int status) noexcept {
    if (status < 0 || status > int(r::errc::too_many))
        return "invalid status";
    return r::error_message(r::errc(status)).data();
}
int raftpp_binding_set_output(void* output, const uint8_t* data, size_t size) noexcept {
    return protect([&] {
        if (!output || (!data && size))
            return int(r::errc::invalid);
        auto copy = r::buffer::copy({reinterpret_cast<const r::byte*>(data), size});
        *static_cast<r::buffer*>(output) = std::move(copy);
        return 0;
    });
}
int raftpp_binding_create(uint64_t id, const char* address, const char* directory,
                          const raftpp_binding_options* options, raftpp_binding_machine fn,
                          void* context, raftpp_binding_node** output) noexcept {
    raftpp_binding_runtime_options settings{};
    settings.node = options ? *options : raftpp_binding_options{1000, 100, 1024, 2048};
    settings.install_snapshot_timeout = 30000;
    settings.max_catch_up_rounds = 10;
    settings.max_catch_up_round_duration = 5000;
    settings.max_inflight_entries = 32;
    settings.capacity_threshold = 4096;
    settings.factory = "composed";
    settings.storage = "uv";
    settings.timer = "uv";
    settings.network = "uv";
    settings.codec = "none";
    settings.max_snapshot_size = 8 * 1024 * 1024;
    settings.max_encoded_snapshot_size = 16 * 1024 * 1024;
    settings.random_seed = 1;
    settings.segment_size = 8 * 1024 * 1024;
    settings.max_entry_size = 8 * 1024 * 1024;
    settings.max_entries = 262144;
    settings.disk_retry = 5000;
    settings.connect_retry = 1000;
    settings.auto_recovery = 1;
    return raftpp_binding_create_runtime(id, address, directory, &settings, fn, context, output);
}
int raftpp_binding_create_runtime(uint64_t id, const char* address, const char* directory,
                                  const raftpp_binding_runtime_options* options,
                                  raftpp_binding_machine fn, void* context,
                                  raftpp_binding_node** output) noexcept {
    if (!output)
        return int(r::errc::invalid);
    *output = nullptr;
    if (!id || !address || !*address || !directory || !options || !fn || !options->factory ||
        !options->storage || !options->timer || !options->network || !options->codec ||
        (!options->configuration && options->configuration_size))
        return int(r::errc::invalid);
    auto node = new (std::nothrow) raftpp_binding_node;
    if (!node)
        return int(r::errc::out_of_memory);
    const auto status = protect([&] {
        r::node_options settings;
        settings.election_timeout = options->node.election_timeout;
        settings.heartbeat_timeout = options->node.heartbeat_timeout;
        settings.snapshot_threshold = options->node.snapshot_threshold;
        settings.snapshot_trailing = options->node.snapshot_trailing;
        settings.install_snapshot_timeout = options->install_snapshot_timeout;
        settings.max_catch_up_rounds = options->max_catch_up_rounds;
        settings.max_catch_up_round_duration = options->max_catch_up_round_duration;
        settings.max_inflight_entries = options->max_inflight_entries;
        if (options->capacity_threshold > UINT16_MAX || options->pre_vote > 1 ||
            options->force_barrier > 1 || options->raw_payloads > 1 || options->snapshot_async > 1)
            return int(r::errc::invalid);
        settings.capacity_threshold = uint16_t(options->capacity_threshold);
        settings.pre_vote = options->pre_vote;
        settings.force_barrier = options->force_barrier;
        if (!settings.heartbeat_timeout || settings.election_timeout <= settings.heartbeat_timeout)
            return int(r::errc::invalid);
        node->fsm = std::make_shared<machine>(fn, context);
        node->fsm->raw = options->raw_payloads;
        node->fsm->second_phase = options->snapshot_async;
        node->runtime = r::bindings::detail::make_runtime(directory, *options, node->packets);
        node->backend = node->runtime.backend;
        if (!node->runtime.backend || !node->runtime.poll || !node->runtime.drain)
            return int(r::errc::invalid);
        node->value = std::make_unique<r::node>(id, address, node->backend, node->fsm, settings);
        node->value->seed(options->random_seed);
        return 0;
    });
    if (status) {
        raftpp_binding_release(node);
        return status;
    }
    *output = node;
    return 0;
}
int raftpp_binding_bootstrap(raftpp_binding_node* node, const raftpp_binding_server* servers,
                             size_t size, int recover) noexcept {
    return protect([&] {
        if (!node || (!servers && size))
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        r::configuration config;
        for (size_t i = 0; i < size; ++i) {
            const auto& server = servers[i];
            if (!server.address || server.role < 0 || server.role > 2)
                return int(r::errc::invalid);
            if (auto error = config.add(server.id, server.address, r::role(server.role)))
                return code(error);
        }
        return code(recover ? node->value->recover(config) : node->value->bootstrap(config));
    });
}
int raftpp_binding_start(raftpp_binding_node* node) noexcept {
    if (!node)
        return int(r::errc::invalid);
    if (!node->valid())
        return int(r::errc::busy);
    return code(node->value->start());
}
int raftpp_binding_poll(raftpp_binding_node* node) noexcept {
    if (!node)
        return int(r::errc::invalid);
    if (!node->valid() || node->polling || node->observing || (node->fsm && node->fsm->inside))
        return int(r::errc::busy);
    node->polling = true;
    const auto status = protect([&] {
        node->runtime.poll();
        return 0;
    });
    node->polling = false;
    return status;
}
int raftpp_binding_get_status(raftpp_binding_node* node, raftpp_binding_status* output) noexcept {
    return protect([&] {
        if (!node || !output)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        const auto leader = node->value->leader();
        *output = {node->value->current_term(), node->value->last_index(),
                   node->value->commit_index(), node->value->last_applied(),
                   leader ? leader->id : 0,     int(node->value->state()),
                   int(node->closed),           code(node->value->error())};
        return 0;
    });
}
int raftpp_binding_submit(raftpp_binding_node* node, int operation, uint64_t request_id,
                          const uint8_t* data, size_t size, uint64_t id, const char* address,
                          int role) noexcept {
    return protect([&] {
        if (!node || (!data && size) || operation < 0 || operation > 5 || !request_id)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        if (node->closing)
            return int(r::errc::shutdown);
        for (const auto& pending : node->requests)
            if (pending.id == request_id)
                return int(r::errc::invalid);
        if ((operation == 2 && !address) || (operation == 3 && (role < 0 || role > 2)))
            return int(r::errc::invalid);
        node->requests.push_back({request_id});
        auto it = std::prev(node->requests.end());
        // Allocate callbacks/command before admission; on any exception the
        // reserved queue entry is removed without generating a completion.
        const int status = protect([&] {
            auto complete = [it](r::error_code error) noexcept {
                it->status = code(error);
                it->done = true;
            };
            switch (operation) {
            case 0: {
                auto input = r::view<const r::byte>{reinterpret_cast<const r::byte*>(data), size};
                auto command = node->fsm->raw ? r::buffer::copy(input) : pack(input);
                return code(node->value->apply(
                    std::move(command), [it](r::error_code error, r::shared<void> result) noexcept {
                        it->status = code(error);
                        it->result = std::static_pointer_cast<r::buffer>(result);
                        it->done = true;
                    }));
            }
            case 1:
                return code(node->value->barrier(complete));
            case 2:
                return code(node->value->add(id, address, complete));
            case 3:
                return code(node->value->assign(id, r::role(role), complete));
            case 4:
                return code(node->value->remove(id, complete));
            default:
                return code(node->value->transfer(id, [complete] { complete({}); }));
            }
        });
        if (status)
            node->requests.erase(it);
        return status;
    });
}
int raftpp_binding_peek_completion(raftpp_binding_node* node,
                                   raftpp_binding_completion* output) noexcept {
    if (!node || !output)
        return -int(r::errc::invalid);
    if (!node->valid())
        return -int(r::errc::busy);
    for (const auto& req : node->requests)
        if (req.done) {
            *output = {req.id, req.status,
                       req.result ? reinterpret_cast<const uint8_t*>(req.result->data()) : nullptr,
                       req.result ? req.result->size() : 0};
            return 1;
        }
    return 0;
}
int raftpp_binding_pop_completion(raftpp_binding_node* node) noexcept {
    if (!node)
        return int(r::errc::invalid);
    if (!node->valid())
        return int(r::errc::busy);
    for (auto it = node->requests.begin(); it != node->requests.end(); ++it)
        if (it->done) {
            node->requests.erase(it);
            return 0;
        }
    return int(r::errc::not_found);
}
int raftpp_binding_close(raftpp_binding_node* node) noexcept {
    return protect([&] {
        if (!node)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        return close_node(*node);
    });
}
void raftpp_binding_release(raftpp_binding_node* node) noexcept {
    if (!node)
        return;
    if (node->fsm) {
        node->fsm->callback = nullptr;
        node->fsm->context = nullptr;
    }
    // Called only with exclusive ownership. Cleanup never enters foreign
    // runtimes; moving a private loop to another thread is serialized.
    try {
        if (node->runtime.abandon)
            node->runtime.abandon();
    } catch (...) {
        return;
    }
    if (node->closed) {
        drain(node);
        return;
    }
    try {
        std::thread([node] { drain(node); }).detach();
    } catch (...) { /* Retain rather than freeing live handles on thread exhaustion. */
    }
}
uint64_t raftpp_binding_features() noexcept {
    return r::bindings::detail::features();
}
int raftpp_binding_configure(raftpp_binding_node* node, int key, uint64_t value) noexcept {
    return protect([&] {
        if (!node)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        if (value > UINT32_MAX || key < 0 || key > 12)
            return int(r::errc::invalid);
        auto& n = *node->value;
        switch (key) {
        case 0:
            return code(n.set_election_timeout(unsigned(value)));
        case 1:
            return code(n.set_heartbeat_timeout(unsigned(value)));
        case 2:
            return code(n.set_install_snapshot_timeout(unsigned(value)));
        case 3:
            if (value > 1)
                return int(r::errc::invalid);
            n.set_pre_vote(value);
            break;
        case 4:
            n.set_snapshot_threshold(unsigned(value));
            break;
        case 5:
            n.set_snapshot_trailing(unsigned(value));
            break;
        case 6:
            n.set_max_catch_up_rounds(unsigned(value));
            break;
        case 7:
            n.set_max_catch_up_round_duration(unsigned(value));
            break;
        case 8:
            n.set_max_inflight_entries(unsigned(value));
            break;
        case 9:
            if (value > UINT16_MAX)
                return int(r::errc::invalid);
            n.set_capacity_threshold(uint16_t(value));
            break;
        case 10:
            n.seed(uint32_t(value));
            break;
        case 11:
            if (value > 1)
                return int(r::errc::invalid);
            if (!value)
                return code(n.set_step_callback());
            return code(n.set_step_callback([node] {
                if (node->events.size() >= 4096) {
                    node->events.pop_front();
                    ++node->dropped_events;
                }
                r::buffer event(9);
                node->events.push_back(std::move(event));
                r::buffer ignored;
                const bool previous = node->observing;
                node->observing = true;
                (void)node->fsm->call(5, {}, ignored);
                node->observing = previous;
            }));
        case 12:
            if (value > 1)
                return int(r::errc::invalid);
            if (!value) {
                n.set_tracer(r::tracer::noop());
                break;
            }
            n.set_tracer(r::tracer([node](const r::trace_event& event) {
                r::string text;
                unsigned level = 0;
                if (event.type == r::trace_diagnostic && event.info) {
                    const auto& info = *static_cast<const r::diagnostic_trace*>(event.info);
                    level = unsigned(info.level);
                    text = info.message;
                    text += '\0';
                    text += info.location.file_name();
                    text += '\0';
                    text += info.location.function_name();
                    text += '\0';
                    text += std::to_string(info.location.line());
                }
                auto bytes = r::buffer(9 + text.size());
                bytes.data()[0] = r::byte{1};
                for (unsigned i = 0; i < 4; ++i) {
                    bytes.data()[1 + i] = r::byte((event.type >> (8 * i)) & 255);
                    bytes.data()[5 + i] = r::byte((level >> (8 * i)) & 255);
                }
                if (!text.empty())
                    std::memcpy(bytes.data() + 9, text.data(), text.size());
                if (node->events.size() >= 4096) {
                    node->events.pop_front();
                    ++node->dropped_events;
                }
                r::buffer ignored;
                const bool previous = node->observing;
                node->observing = true;
                (void)node->fsm->call(6, bytes.bytes(), ignored);
                node->observing = previous;
                node->events.push_back(std::move(bytes));
            }));
            break;
        }
        return 0;
    });
}
int raftpp_binding_details_get(raftpp_binding_node* node, raftpp_binding_details* output) noexcept {
    return protect([&] {
        if (!node || !output)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        *output = {};
        auto& n = *node->value;
        auto info = n.error_details();
        auto leader = n.leader();
        auto role = n.local_role();
        output->voted_for = n.voted_for();
        output->timeout = n.timeout();
        output->transferee = n.transferee();
        output->dropped_events = node->dropped_events;
        output->local_role = role ? int(*role) : -1;
        output->error_code = info.code.value();
        output->cause_code = info.cause.value();
        auto copy = [](auto& target, r::string_view text) {
            auto size = std::min(sizeof(target) - 1, text.size());
            std::memcpy(target, text.data(), size);
        };
        copy(output->error_category, info.code.category().name());
        copy(output->cause_category, info.cause.category().name());
        copy(output->message, info.text());
        if (leader)
            copy(output->leader_address, leader->address);
        return 0;
    });
}
int raftpp_binding_configuration_get(raftpp_binding_node* node, void* output) noexcept {
    return protect([&] {
        if (!node || !output)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        return code(node->value->configuration().encode(*static_cast<r::buffer*>(output)));
    });
}
int raftpp_binding_peer(raftpp_binding_node* node, uint64_t id, uint64_t* match,
                        int32_t* catch_up) noexcept {
    if (!node || !match || !catch_up)
        return int(r::errc::invalid);
    if (!node->valid())
        return int(r::errc::busy);
    if (auto error = node->value->match_index(id, *match))
        return code(error);
    r::catch_up_status status;
    if (auto error = node->value->catch_up(id, status))
        return code(error);
    *catch_up = int32_t(status);
    return 0;
}
int raftpp_binding_event(raftpp_binding_node* node, void* output) noexcept {
    if (!node || !output)
        return -int(r::errc::invalid);
    if (!node->valid())
        return -int(r::errc::busy);
    if (node->events.empty())
        return 0;
    *static_cast<r::buffer*>(output) = std::move(node->events.front());
    node->events.pop_front();
    return 1;
}
int raftpp_binding_read(raftpp_binding_node* node, int kind, uint8_t* data, size_t* size) noexcept {
    return protect([&] {
        if (!node || !size || (kind < 0 || kind > 2))
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        r::buffer config;
        if (kind == 0) {
            auto value = node->value->configuration();
            if (!value.empty())
                if (auto error = value.encode(config))
                    return code(error);
        }
        if (kind == 2) {
            auto leader = node->value->leader();
            if (leader)
                config = r::buffer::copy({reinterpret_cast<const r::byte*>(leader->address.data()),
                                          leader->address.size()});
        }
        const auto bytes = kind != 1              ? config.bytes()
                           : node->events.empty() ? r::view<const r::byte>{}
                                                  : node->events.front().bytes();
        const auto capacity = *size;
        *size = bytes.size();
        if (!data)
            return 0;
        if (capacity < bytes.size())
            return int(r::errc::too_big);
        if (!bytes.empty())
            std::memcpy(data, bytes.data(), bytes.size());
        if (kind == 1 && !node->events.empty())
            node->events.pop_front();
        return 0;
    });
}
int raftpp_binding_transport_peek(raftpp_binding_node* node,
                                  raftpp_binding_packet* output) noexcept {
    if (!node || !output)
        return -int(r::errc::invalid);
    if (!node->valid())
        return -int(r::errc::busy);
    if (!node->packets)
        return 0;
    auto& network = *node->packets;
    for (auto& packet : network.pending)
        if (!packet.dispatched) {
            *output = {packet.id,
                       packet.peer,
                       packet.address.c_str(),
                       reinterpret_cast<const uint8_t*>(packet.bytes.data()),
                       packet.bytes.size(),
                       0};
            return 1;
        }
    if (network.closing && !network.close_sent) {
        *output = {0, 0, "", nullptr, 0, 1};
        return 1;
    }
    return 0;
}
int raftpp_binding_transport_complete(raftpp_binding_node* node, uint64_t id, int status) noexcept {
    return protect([&] {
        if (!node || !node->packets || status < -1 || status > 23)
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        auto& network = *node->packets;
        if (id == 0) {
            if (!network.closing)
                return int(r::errc::invalid);
            if (status == -1)
                network.close_sent = true;
            else
                network.finish_close();
            return 0;
        }
        for (auto it = network.pending.begin(); it != network.pending.end(); ++it)
            if (it->id == id) {
                if (status == -1) {
                    it->dispatched = true;
                    return 0;
                }
                auto callback = std::move(it->done);
                network.pending.erase(it);
                if (callback)
                    callback(r::errc(status));
                return 0;
            }
        return int(r::errc::not_found);
    });
}
int raftpp_binding_receive(raftpp_binding_node* node, uint64_t id, const char* address,
                           const uint8_t* data, size_t size) noexcept {
    return protect([&] {
        if (!node || !node->packets || !id || !address || (!data && size))
            return int(r::errc::invalid);
        if (!node->valid())
            return int(r::errc::busy);
        if (!node->packets->receiver || node->packets->closing)
            return int(r::errc::shutdown);
        auto bytes = r::buffer::copy({reinterpret_cast<const r::byte*>(data), size});
        r::message message;
        size_t consumed = 0;
        if (auto error = r::decode_message(r::shared_buffer{std::move(bytes)}, message, consumed))
            return code(error);
        if (consumed != size)
            return int(r::errc::malformed);
        message.server_id = id;
        message.server_address = address;
        node->packets->receiver(std::move(message));
        return 0;
    });
}
}
