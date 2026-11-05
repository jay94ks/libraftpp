#define NAPI_VERSION 8
#include <node_api.h>
#include <raftpp/bindings/bindings.hpp>
#include <raftpp/bindings/runtime.hpp>
#include <array>
#include <string>
#include <vector>
#include <stdexcept>
#include <limits>
#include <memory>

namespace {
    void check(napi_status status) {
        if (status != napi_ok)
            throw std::runtime_error("invalid native binding argument");
    }
    struct instance {
        napi_env env;
        napi_ref machine = nullptr;
        raftpp_binding_node* node = nullptr;
        void release() noexcept {
            raftpp_binding_release(node);
            node = nullptr;
            if (machine) {
                napi_delete_reference(env, machine);
                machine = nullptr;
            }
        }
        ~instance() {
            release();
        }
    };
    napi_value undefined(napi_env env) {
        napi_value value;
        check(napi_get_undefined(env, &value));
        return value;
    }
    napi_value integer(napi_env env, int value) {
        napi_value result;
        check(napi_create_int32(env, value, &result));
        return result;
    }
    napi_value bigint(napi_env env, uint64_t value) {
        napi_value result;
        check(napi_create_bigint_uint64(env, value, &result));
        return result;
    }
    int number(napi_env env, napi_value value) {
        int32_t result;
        check(napi_get_value_int32(env, value, &result));
        return result;
    }
    uint64_t id(napi_env env, napi_value value) {
        uint64_t result;
        bool lossless;
        check(napi_get_value_bigint_uint64(env, value, &result, &lossless));
        if (!lossless)
            throw std::runtime_error("ID must be an unsigned 64-bit bigint");
        return result;
    }
    std::string text(napi_env env, napi_value value) {
        size_t size;
        check(napi_get_value_string_utf8(env, value, nullptr, 0, &size));
        std::string result(size + 1, '\0');
        check(napi_get_value_string_utf8(env, value, result.data(), result.size(), &size));
        result.resize(size);
        if (result.find('\0') != std::string::npos)
            throw std::runtime_error("embedded NUL");
        return result;
    }
    napi_value get(napi_env env, napi_value object, const char* key) {
        napi_value result;
        check(napi_get_named_property(env, object, key, &result));
        return result;
    }
    void set(napi_env env, napi_value object, const char* key, napi_value value) {
        check(napi_set_named_property(env, object, key, value));
    }
    void status(int value) {
        if (value)
            throw std::runtime_error(std::to_string(value) + ": " + raftpp_binding_error(value));
    }
    instance& unwrap(napi_env env, napi_value value) {
        void* data;
        check(napi_get_value_external(env, value, &data));
        auto& result = *static_cast<instance*>(data);
        if (!result.node)
            throw std::runtime_error("node is closed");
        return result;
    }
    struct args {
        napi_env env;
        std::array<napi_value, 9> values{};
        size_t size = values.size();
        args(napi_env e, napi_callback_info info) : env(e) {
            check(napi_get_cb_info(env, info, &size, values.data(), nullptr, nullptr));
        }
        napi_value operator[](size_t i) {
            if (i >= size)
                throw std::runtime_error("missing argument");
            return values[i];
        }
    };
    template <typename F> napi_value guard(napi_env env, F&& fn) noexcept {
        try {
            return fn();
        } catch (const std::exception& error) {
            bool pending = false;
            napi_is_exception_pending(env, &pending);
            if (!pending)
                napi_throw_error(env, nullptr, error.what());
        } catch (...) {
            napi_throw_error(env, nullptr, "native binding failure");
        }
        return nullptr;
    }
    int machine(void* context, int operation, const uint8_t* input, size_t size,
                void* output) noexcept {
        auto& self = *static_cast<instance*>(context);
        try {
            napi_value fn, data, result;
            check(napi_get_reference_value(self.env, self.machine, &fn));
            check(napi_create_buffer_copy(self.env, size, input, nullptr, &data));
            napi_value argv[] = {integer(self.env, operation), data};
            check(napi_call_function(self.env, undefined(self.env), fn, 2, argv, &result));
            if (operation >= 2 && operation != 3)
                return 0;
            void* bytes;
            size_t length;
            check(napi_get_buffer_info(self.env, result, &bytes, &length));
            return raftpp_binding_set_output(output, static_cast<uint8_t*>(bytes), length);
        } catch (...) {
            bool pending = false;
            napi_is_exception_pending(self.env, &pending);
            if (pending) {
                napi_value error;
                napi_get_and_clear_last_exception(self.env, &error);
            }
            return 20;
        }
    }
    napi_value create(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto self = std::make_unique<instance>();
            self->env = env;
            const auto options = a[0];
            const auto address = text(env, get(env, options, "address"));
            const auto directory = text(env, get(env, options, "directory"));
            raftpp_binding_runtime_options settings{};
            auto numeric = [&](const char* key) {
                double value;
                check(napi_get_value_double(env, get(env, options, key), &value));
                if (!(value >= 0 && value <= UINT32_MAX) || value != uint32_t(value))
                    throw std::runtime_error("invalid option");
                return uint32_t(value);
            };
            settings.node = {numeric("electionTimeout"), numeric("heartbeatTimeout"),
                             numeric("snapshotThreshold"), numeric("snapshotTrailing")};
            settings.install_snapshot_timeout = numeric("installSnapshotTimeout");
            settings.max_catch_up_rounds = numeric("maxCatchUpRounds");
            settings.max_catch_up_round_duration = numeric("maxCatchUpRoundDuration");
            settings.max_inflight_entries = numeric("maxInflightEntries");
            settings.capacity_threshold = numeric("capacityThreshold");
            settings.pre_vote = numeric("preVote");
            settings.force_barrier = numeric("forceBarrier");
            settings.raw_payloads = numeric("rawPayloads");
            settings.snapshot_async = numeric("snapshotAsync");
            auto io = get(env, options, "io");
            auto factory = text(env, get(env, io, "factory")),
                 storage = text(env, get(env, io, "storage")),
                 timer = text(env, get(env, io, "timer"));
            auto network = text(env, get(env, io, "network")),
                 codec = text(env, get(env, io, "codec"));
            settings.factory = factory.c_str();
            settings.storage = storage.c_str();
            settings.timer = timer.c_str();
            settings.network = network.c_str();
            settings.codec = codec.c_str();
            void* data;
            size_t size;
            check(napi_get_buffer_info(env, get(env, io, "configuration"), &data, &size));
            settings.configuration = static_cast<const uint8_t*>(data);
            settings.configuration_size = size;
            settings.max_snapshot_size = id(env, get(env, io, "maxSnapshotSize"));
            settings.max_encoded_snapshot_size = id(env, get(env, io, "maxEncodedSnapshotSize"));
            settings.random_seed = numeric("randomSeed");
            settings.segment_size = id(env, get(env, io, "segmentSize"));
            settings.block_size = id(env, get(env, io, "blockSize"));
            settings.max_entry_size = id(env, get(env, io, "maxEntrySize"));
            settings.max_entries = id(env, get(env, io, "maxEntries"));
            settings.disk_retry = uint32_t(number(env, get(env, io, "diskRetry")));
            settings.connect_retry = uint32_t(number(env, get(env, io, "connectRetry")));
            settings.auto_recovery = uint32_t(number(env, get(env, io, "autoRecovery")));
            check(napi_create_reference(env, a[1], 1, &self->machine));
            status(raftpp_binding_create_runtime(id(env, get(env, options, "id")), address.c_str(),
                                                 directory.c_str(), &settings, machine, self.get(),
                                                 &self->node));
            napi_value result;
            check(napi_create_external(
                env, self.get(),
                [](napi_env, void* ptr, void*) { delete static_cast<instance*>(ptr); }, nullptr,
                &result));
            self.release();
            return result;
        });
    }
    napi_value bootstrap(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto& self = unwrap(env, a[0]);
            uint32_t length;
            check(napi_get_array_length(env, a[1], &length));
            std::vector<std::string> addresses;
            addresses.reserve(length);
            std::vector<raftpp_binding_server> servers;
            servers.reserve(length);
            for (uint32_t i = 0; i < length; ++i) {
                napi_value server;
                check(napi_get_element(env, a[1], i, &server));
                addresses.push_back(text(env, get(env, server, "address")));
                servers.push_back({id(env, get(env, server, "id")), addresses.back().c_str(),
                                   number(env, get(env, server, "role"))});
            }
            return integer(env, raftpp_binding_bootstrap(self.node, servers.data(), servers.size(),
                                                         number(env, a[2])));
        });
    }
    napi_value start(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            return integer(env, raftpp_binding_start(unwrap(env, a[0]).node));
        });
    }
    napi_value close(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            return integer(env, raftpp_binding_close(unwrap(env, a[0]).node));
        });
    }
    napi_value dispose(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            unwrap(env, a[0]).release();
            return undefined(env);
        });
    }
    napi_value submit(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto& self = unwrap(env, a[0]);
            void* data;
            size_t size;
            check(napi_get_buffer_info(env, a[3], &data, &size));
            const auto address = text(env, a[5]);
            return integer(env,
                           raftpp_binding_submit(self.node, number(env, a[1]), id(env, a[2]),
                                                 static_cast<uint8_t*>(data), size, id(env, a[4]),
                                                 address.c_str(), number(env, a[6])));
        });
    }
    napi_value poll(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto& self = unwrap(env, a[0]);
            status(raftpp_binding_poll(self.node));
            napi_value result;
            check(napi_create_array(env, &result));
            raftpp_binding_completion completion;
            uint32_t i = 0;
            int found;
            while ((found = raftpp_binding_peek_completion(self.node, &completion)) == 1) {
                napi_value item, data;
                check(napi_create_object(env, &item));
                set(env, item, "id", bigint(env, completion.request_id));
                set(env, item, "status", integer(env, completion.status));
                check(
                    napi_create_buffer_copy(env, completion.size, completion.data, nullptr, &data));
                set(env, item, "data", data);
                check(napi_set_element(env, result, i++, item));
                status(raftpp_binding_pop_completion(self.node));
            }
            if (found < 0)
                status(-found);
            return result;
        });
    }
    napi_value state(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            raftpp_binding_status state;
            status(raftpp_binding_get_status(unwrap(env, a[0]).node, &state));
            napi_value result;
            check(napi_create_object(env, &result));
            set(env, result, "term", bigint(env, state.term));
            set(env, result, "lastIndex", bigint(env, state.last_index));
            set(env, result, "commitIndex", bigint(env, state.commit_index));
            set(env, result, "lastApplied", bigint(env, state.last_applied));
            set(env, result, "leaderId", bigint(env, state.leader_id));
            set(env, result, "state", integer(env, state.state));
            set(env, result, "closed", integer(env, state.closed));
            set(env, result, "errorCode", integer(env, state.error_code));
            return result;
        });
    }
    napi_value features(napi_env env, napi_callback_info) {
        return guard(env, [&] { return bigint(env, raftpp_binding_features()); });
    }
    napi_value load_plugin(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto path = text(env, a[0]);
            return integer(env, raftpp_binding_load_plugin(path.c_str()));
        });
    }
    napi_value configure(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            return integer(env, raftpp_binding_configure(unwrap(env, a[0]).node, number(env, a[1]),
                                                         id(env, a[2])));
        });
    }
    napi_value read(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            auto node = unwrap(env, a[0]).node;
            auto kind = number(env, a[1]);
            size_t size = 0;
            status(raftpp_binding_read(node, kind, nullptr, &size));
            napi_value result;
            void* bytes;
            check(napi_create_buffer(env, size, &bytes, &result));
            if (size)
                status(raftpp_binding_read(node, kind, static_cast<uint8_t*>(bytes), &size));
            return result;
        });
    }
    napi_value details(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            raftpp_binding_details value;
            status(raftpp_binding_details_get(unwrap(env, a[0]).node, &value));
            napi_value result;
            check(napi_create_object(env, &result));
            set(env, result, "votedFor", bigint(env, value.voted_for));
            set(env, result, "timeout", bigint(env, value.timeout));
            set(env, result, "transferee", bigint(env, value.transferee));
            set(env, result, "droppedEvents", bigint(env, value.dropped_events));
            set(env, result, "localRole", integer(env, value.local_role));
            set(env, result, "errorCode", integer(env, value.error_code));
            set(env, result, "causeCode", integer(env, value.cause_code));
            auto string = [&](const char* key, const char* data) {
                napi_value text;
                check(napi_create_string_utf8(env, data, NAPI_AUTO_LENGTH, &text));
                set(env, result, key, text);
            };
            string("errorCategory", value.error_category);
            string("causeCategory", value.cause_category);
            string("message", value.message);
            string("leaderAddress", value.leader_address);
            return result;
        });
    }
    napi_value peer(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            uint64_t index;
            int32_t catch_up;
            status(raftpp_binding_peer(unwrap(env, a[0]).node, id(env, a[1]), &index, &catch_up));
            napi_value result;
            check(napi_create_object(env, &result));
            set(env, result, "matchIndex", bigint(env, index));
            set(env, result, "catchUpStatus", integer(env, catch_up));
            return result;
        });
    }
    napi_value transport(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            raftpp_binding_packet packet;
            const auto found = raftpp_binding_transport_peek(unwrap(env, a[0]).node, &packet);
            if (found < 0)
                status(-found);
            if (!found)
                return undefined(env);
            napi_value result, data, address;
            check(napi_create_object(env, &result));
            check(napi_create_buffer_copy(env, packet.size, packet.data, nullptr, &data));
            check(napi_create_string_utf8(env, packet.address, NAPI_AUTO_LENGTH, &address));
            set(env, result, "requestId", bigint(env, packet.request_id));
            set(env, result, "peerId", bigint(env, packet.server_id));
            set(env, result, "address", address);
            set(env, result, "data", data);
            set(env, result, "closing", integer(env, packet.closing));
            return result;
        });
    }
    napi_value transport_complete(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            return integer(env, raftpp_binding_transport_complete(
                                    unwrap(env, a[0]).node, id(env, a[1]), number(env, a[2])));
        });
    }
    napi_value receive(napi_env env, napi_callback_info info) {
        return guard(env, [&] {
            args a(env, info);
            void* bytes;
            size_t size;
            check(napi_get_buffer_info(env, a[3], &bytes, &size));
            auto address = text(env, a[2]);
            return integer(env, raftpp_binding_receive(unwrap(env, a[0]).node, id(env, a[1]),
                                                       address.c_str(),
                                                       static_cast<const uint8_t*>(bytes), size));
        });
    }

}
NAPI_MODULE_INIT() {
    if (raftpp_binding_abi_version() != 2) {
        napi_throw_error(env, nullptr, "Unsupported raftpp bindings ABI");
        return nullptr;
    }
    const napi_property_descriptor properties[] = {
        {"features", nullptr, features, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"loadPlugin", nullptr, load_plugin, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"configure", nullptr, configure, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"read", nullptr, read, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"details", nullptr, details, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"peer", nullptr, peer, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"transport", nullptr, transport, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"transportComplete", nullptr, transport_complete, nullptr, nullptr, nullptr, napi_default,
         nullptr},
        {"receive", nullptr, receive, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"create", nullptr, create, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"bootstrap", nullptr, bootstrap, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"start", nullptr, start, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"close", nullptr, close, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"dispose", nullptr, dispose, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"submit", nullptr, submit, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"poll", nullptr, poll, nullptr, nullptr, nullptr, napi_default, nullptr},
        {"status", nullptr, state, nullptr, nullptr, nullptr, napi_default, nullptr}};
    if (napi_define_properties(env, exports, std::size(properties), properties) != napi_ok)
        return nullptr;
    return exports;
}
