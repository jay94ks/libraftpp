# RocksDB Storage Module API

[한국어](../../ko/modules/rocksdb.md) · [Modules](index.md) · [I/O composition](../composition.md)

`RAFTPP_MODULE_ROCKSDB=ON` provides `raftpp::rocksdb` and `<raftpp/rocksdb/rocksdb.hpp>`. Core is mandatory and linked transitively. Requires an installed RocksDB 11.13.2 or newer CMake package and thread support.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS rocksdb)
target_link_libraries(app PRIVATE raftpp::rocksdb)
```

## Construction and Composition

```cpp
#include <raftpp/rocksdb/rocksdb.hpp>
#include <raftpp/composed_io.hpp>

raftpp::shared<raftpp::io> assemble(
    raftpp::fs::path database,
    raftpp::shared<raftpp::timer> timer,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::rocksdb_storage>(std::move(database)),
        .timer = std::move(timer),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

The constructor of `rocksdb_storage final : storage` is `explicit rocksdb_storage(fs::path path, storage_limits limits = {})`. path denotes a RocksDB directory. Create parent directories beforehand. Initialization failure may leave newly created empty DB files/schema. Construction does not open the DB and may throw allocation/path-conversion exceptions. Empty/NUL-containing paths or unsupported limits throw `std::invalid_argument`. Objects are neither copyable nor movable.

Users choose timers/transports/optional snapshot codecs; no UV dependency is required. `make_io`/the node manages storage lifetime. One object exclusively owns the file database; do not share lifecycle control across nodes/composers or modify through another connection. Existing UV file directories are not migrated automatically.

## Methods

Every method implements the [storage contract](../composition.md).

| Method | Arguments/results/behavior |
| --- | --- |
| `init(server_id id, string_view address)` | Zero ID returns `bad_id`, empty address `invalid`, repetition after success `busy`. Opens/creates the DB and validates stored format/limits/log continuity. Another owner's lock fails. Close a failed instance and reopen with a new object. |
| `bootstrap(const configuration&)` | Requires a voter. Atomically stores term 1/vote 0 and a configuration entry at index 1 in an empty DB. Existing term returns `cannot_bootstrap`. |
| `recover(const configuration&)` | Empty history returns `invalid`. Appends a configuration entry using the last log/snapshot term. Current term/vote are preserved. |
| `load(io_state&)` | Reads term/vote/snapshot/contiguous entries, owning raw payloads. Preserves output on failure. Returns the starting index including trailing entries. |
| `set_term(term)` | Must be positive and at least the current term. Synchronously commits and clears vote to 0. |
| `set_vote(server_id)` | Requires a term. Vote 0 is allowed. Persists before returning success. |
| `truncate(log_index)` | Must be within `[start,next]` and above snapshot index. Atomically removes the suffix from that index. |
| `append(log_index, shared<const entry_batch>, completion)` | Appends a nonempty batch at the current log end. Null entries, zero/greater-than-current terms, and unsupported types return `invalid`. Validates the whole batch before one transaction. Log payloads need no eight-byte alignment. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | Requires positive index/term, a voter configuration, and `1 <= configuration_index <= index`. Index must be below `UINT64_MAX` and at least the previous snapshot index; term cannot exceed current term. Reusing an existing snapshot index requires the same term. A matching index/term log entry or existing snapshot retains the suffix and up to trailing entries covered by the snapshot. Otherwise replaces the entire log and sets the next append index to `index+1`. Snapshot/start index/log deletion are atomic. |
| `snapshot_get(snapshot_callback)` | Success with `nullopt` if absent; otherwise delivers an owned value. Storage does not decode codec envelopes. |
| `capacity() const noexcept` | Available DB-filesystem KiB capped at 65535. Returns 0 before initialization/during close/on query failure. |
| `error_details() const noexcept` | Copyable most recent failure diagnostic. Includes the RocksDB status string; no native cause-code alias is provided. |
| `close(close_callback = {}) noexcept` | New work returns `shutdown`. Releases the DB after active callbacks and invokes the first nonempty observer. Completes inline if already closed. |

DB calls and synchronous commits block the calling thread. No worker/executor is created and completions are not transferred to another thread. All calls/destruction/node/FSM/completions must share one serialized execution domain. DB latency can delay timer/network servicing.

Async-shaped methods complete exactly once **inline**, after successful commit. Immediate failure has no callback. Batches/snapshots survive through callback return. Callbacks may close/destroy the facade; close completion waits until outside the callback. Callback exceptions record `io_error` and close without undoing the commit. Destruction starts close. This type does not synchronize concurrent access.

## Storage Format, Limits, and Failures

Uses `WriteBatch` with WAL enabled and `WriteOptions::sync=true`. SDK preparation disables optional compression and the adapter uses `kNoCompression`. Completion reports only successful synchronous commits. Restart recovery uses DB journal/WAL recovery. Durability depends on filesystem/device synchronization guarantees; no separate hardware guarantee against power loss is provided.

Uses a DB-specific v1 keyspace. `meta` contains `52 41 46 54 44 42 01 00` followed by little-endian u64 term/vote/start. Keys `e`+big-endian u64 index contain little-endian u64 term/type/payload length and payload. `snapshot` contains little-endian u64 index/term/configuration_index/configuration length/payload length, existing core configuration encoding, and raw payload. External RPC/UV-log formats are unchanged. Optional `make_io` codecs envelope the raw snapshot; reopen with the same codec settings.

Core `storage_limits` defaults: `max_entry_size=8 MiB`, `max_snapshot_size=16 MiB`, `max_entries=262144`. Size fields bound stored raw entry/snapshot payloads respectively; with a codec the storage snapshot bound includes the whole envelope. Configuration encoding has a separate 8 MiB limit. All three fields must be at most `INT_MAX/2`; max_entries must be positive. These are not total-DB-byte bounds; configure memory consumption for large logs too. Reopening with smaller bounds may return `too_big`.

Bounds/index overflow return `too_big`; malformed keys/format/lengths/log gaps `corrupt`; invalid requests `invalid`; allocation failure `out_of_memory`; other native/exception failures `io_error`. RocksDB busy/timeout maps to `busy`, exhausted storage to `no_space`. Validation/serialization/pre-commit preparation failures leave stored state unchanged; cached state is updated only after a successful commit. Native write/synchronization failures can leave the durable outcome uncertain; close/reopen and inspect state before further operations.
