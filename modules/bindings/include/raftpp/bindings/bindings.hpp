#ifndef RAFTPP_BINDINGS_BINDINGS_HPP
#define RAFTPP_BINDINGS_BINDINGS_HPP

#include <raftpp/bindings/export.hpp>
#include <cstddef>
#include <cstdint>

// C linkage with fixed-width statuses/IDs. This header is consumed by C++;
// managed declarations mirror these layouts. All entry points contain exceptions.
extern "C" {
struct raftpp_binding_node;
// operation: 0 apply, 1 snapshot, 2 restore, 3 snapshot_async, 4 finalize,
// 5 step observation, 6 trace observation. Return a Raft error number.
// output is valid only during this call; set_output copies its input.
using raftpp_binding_machine = int (*)(void* context, int operation, const uint8_t* data,
                                       size_t size, void* output);
struct raftpp_binding_options {
    uint32_t election_timeout;
    uint32_t heartbeat_timeout;
    uint32_t snapshot_threshold;
    uint32_t snapshot_trailing;
};
struct raftpp_binding_server {
    uint64_t id;
    const char* address;
    int32_t role;
};
struct raftpp_binding_status {
    uint64_t term;
    uint64_t last_index;
    uint64_t commit_index;
    uint64_t last_applied;
    uint64_t leader_id;
    int32_t state;
    int32_t closed;
    int32_t error_code;
};
struct raftpp_binding_completion {
    uint64_t request_id;
    int32_t status;
    const uint8_t* data;
    size_t size;
};
// ABI v2 creation options. Existing v1 entry points/layouts remain available.
struct raftpp_binding_runtime_options {
    raftpp_binding_options node;
    uint32_t install_snapshot_timeout, max_catch_up_rounds, max_catch_up_round_duration;
    uint32_t max_inflight_entries, capacity_threshold, pre_vote, force_barrier;
    uint32_t raw_payloads, snapshot_async;
    const char* factory; // "composed" or a registered plugin runtime name
    const char* storage; // "uv", "sqlite", "rocksdb"
    const char* timer;   // "uv", "asio", "poll"
    const char* network; // "uv", "custom"
    const char* codec;   // "none", "lz4", "zstd", "zlib"
    const uint8_t* configuration;
    size_t configuration_size;
    uint64_t max_snapshot_size, max_encoded_snapshot_size;
    uint32_t random_seed;
    uint64_t segment_size, block_size, max_entry_size, max_entries;
    uint32_t disk_retry, connect_retry, auto_recovery;
};
struct raftpp_binding_details {
    uint64_t voted_for, timeout, transferee, dropped_events;
    int32_t local_role, error_code, cause_code;
    char error_category[64], cause_category[64], message[256], leader_address[2048];
};
struct raftpp_binding_packet {
    uint64_t request_id, server_id;
    const char* address;
    const uint8_t* data;
    size_t size;
    int32_t closing;
};
RAFTPP_BINDINGS_API int raftpp_binding_read(raftpp_binding_node*, int kind, uint8_t* data,
                                            size_t* size) noexcept;
RAFTPP_BINDINGS_API uint64_t raftpp_binding_features() noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_create_runtime(uint64_t id, const char* address,
                                                      const char* directory,
                                                      const raftpp_binding_runtime_options* options,
                                                      raftpp_binding_machine machine, void* context,
                                                      raftpp_binding_node** output) noexcept;
// keys 0..11: election, heartbeat, install snapshot timeout, pre vote, snapshot
// threshold/trailing, catchup rounds/duration, inflight, capacity, random seed,
// step observation. key 12 toggles tracing. Values are range checked.
RAFTPP_BINDINGS_API int raftpp_binding_configure(raftpp_binding_node*, int key,
                                                 uint64_t value) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_details_get(raftpp_binding_node*,
                                                   raftpp_binding_details*) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_configuration_get(raftpp_binding_node*,
                                                         void* output) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_peer(raftpp_binding_node*, uint64_t id, uint64_t* match,
                                            int32_t* catch_up) noexcept;
// Events are copied to output: type byte (0 step, 1 trace), LE u32 trace type,
// LE u32 level, then UTF-8 diagnostic text. Each call consumes one event.
RAFTPP_BINDINGS_API int raftpp_binding_event(raftpp_binding_node*, void* output) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_transport_peek(raftpp_binding_node*,
                                                      raftpp_binding_packet*) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_transport_complete(raftpp_binding_node*, uint64_t request_id,
                                                          int status) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_receive(raftpp_binding_node*, uint64_t id,
                                               const char* address, const uint8_t* data,
                                               size_t size) noexcept;
RAFTPP_BINDINGS_API uint32_t raftpp_binding_abi_version() noexcept;
RAFTPP_BINDINGS_API const char* raftpp_binding_error(int status) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_set_output(void* output, const uint8_t* data,
                                                  size_t size) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_create(uint64_t id, const char* address,
                                              const char* directory,
                                              const raftpp_binding_options* options,
                                              raftpp_binding_machine machine, void* context,
                                              raftpp_binding_node** output) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_bootstrap(raftpp_binding_node* node,
                                                 const raftpp_binding_server* servers, size_t size,
                                                 int recover) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_start(raftpp_binding_node* node) noexcept;
// Every call except release must run on the creating thread. Nested calls
// from a state-machine callback return busy. poll never waits for I/O.
RAFTPP_BINDINGS_API int raftpp_binding_poll(raftpp_binding_node* node) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_get_status(raftpp_binding_node* node,
                                                  raftpp_binding_status* output) noexcept;
// operation: 0 apply, 1 barrier, 2 add, 3 assign, 4 remove, 5 transfer.
// A nonzero return rejects submission: no completion is enqueued.
RAFTPP_BINDINGS_API int raftpp_binding_submit(raftpp_binding_node* node, int operation,
                                              uint64_t request_id, const uint8_t* data, size_t size,
                                              uint64_t server_id, const char* address,
                                              int role) noexcept;
// 0: no completion; 1: completion available; negative: invalid/busy.
// Data remains valid until pop_completion or release; copy before popping.
RAFTPP_BINDINGS_API int raftpp_binding_peek_completion(raftpp_binding_node* node,
                                                       raftpp_binding_completion* output) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_pop_completion(raftpp_binding_node* node) noexcept;
RAFTPP_BINDINGS_API int raftpp_binding_close(raftpp_binding_node* node) noexcept;
// Transfers ownership, disables foreign callbacks immediately, and drains
// close on a cleanup thread. Never call from inside a machine callback.
RAFTPP_BINDINGS_API void raftpp_binding_release(raftpp_binding_node* node) noexcept;
}
#endif
