# libuv I/O and Transport

[한국어](../ko/uv_io.md) · [Node API](node.md) · [TCP Transport](tcp_transport.md) · [Owned Streams](uv_stream.md) · [Message Adapter](stream_transport.md)

`uv_io` in `<raftpp/uv/uv_io.hpp>` implements `io`, connecting Linux file storage and libuv timers to a `transport`. It is declared and built only with `RAFTPP_WITH_UV=ON`. The abstract message transport in `<raftpp/transport.hpp>` is available independently of UV. Consumers link only `raftpp::raftpp`; with UV enabled, that target also propagates `<uv.h>` and libuv link requirements.

See [Errors and Owning Diagnostics](errors.md) for `error_details()` and owning initialization diagnostics.

## Construction and Execution Order

```cpp
#include <raftpp/uv/uv_io.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_io(
    uv_loop_t& loop, raftpp::fs::path directory,
    raftpp::shared<raftpp::transport> network)
{
    raftpp::uv_io_options options;
    options.disk_retry = 1000;
    return std::make_shared<raftpp::uv_io>(
        loop, std::move(directory), std::move(network), options);
}
```

The constructor borrows the event loop and shares ownership of the transport. The directory must already exist and be readable/writable. Construction can throw standard allocation exceptions. The object is neither copyable nor movable and can be passed to a node as `shared<io>`.

The order is `init` → optional `bootstrap` or `recover` → `load` → `start` → operations → `close`. A `node` calls initialization, loading, startup, and close itself. Initialization checks the ID/address and filesystem capabilities and initializes the transport. `load` recovers stored state and opens log writing. A successful load cannot be repeated on the same object.

Run methods and callbacks on the same event-loop thread. Keep the loop alive and running after destruction or `close` until shutdown completes, including cleanup after initialization/load/start failure. Finish closing failed objects rather than reusing them.

## Options

| Field | Default | Contract |
| --- | --- | --- |
| `segment_size` | 8 MiB | Prepared segment-file size. Must be at least the block size and at most the `int64_t` maximum. A large single batch may exceed it. |
| `disk_retry` | 5000 ms | Retry interval for file preparation, log writes, and snapshot preparation. Zero is permitted. |
| `block_size` | 0 | Zero uses the probe result. An override must be a power of two of at least 512 and satisfy actual filesystem alignment requirements. |
| `auto_recovery` | `true` | Permit quarantine and retry of the last damaged segment during loading. |
| `max_snapshot_size` | `size_t` maximum | Limit total submitted snapshots, loaded/read files, and decompressed payloads. |
| `tracing` | Default stderr tracer | File/timer diagnostics and native write events. `set_tracer(tracer)` updates existing/future writers. The transport's observer is independent; see [tracing](tracing.md). |

File preparation reserves space with `posix_fallocate`. If it returns `EOPNOTSUPP`, reservation falls back to one-byte writes at filesystem-block intervals capped at 4096 bytes. Native-reservation `ENOSPC` returns `no_space`; stat/write failures during emulation return `io_error`, matching the original, with the actual native cause retained in owning diagnostics. Failed preparation removes its new file and releases its descriptor.

## Changing Settings

```cpp
error_code set_block_size(size_t size) noexcept;
error_code set_segment_size(size_t size) noexcept;
void set_disk_retry(unsigned msecs) noexcept;
void set_connect_retry_delay(unsigned msecs) noexcept;
void set_auto_recovery(bool enabled) noexcept;
error_code set_snapshot_compression(bool enabled) noexcept;
```

Call setters on the owning loop thread. `set_block_size` accepts 0 to restore the filesystem probe result, or a power of two of at least 512. After initialization the segment size must fit the resulting block. Change alignment before `load`: an open file writer returns `busy` to preserve submitted buffers and offsets. Invalid values return `invalid` without changing settings; shutdown returns `shutdown`.

`set_segment_size` accepts a positive value at least the configured/probed block size and at most the `int64_t` maximum. It works before initialization and while writing. Before initialization, unknown probe requirements are checked by `init`. Future append-fit decisions and capacity reports use the new size. Existing buffers, files in the prepare pool, and already-submitted preparations retain their allocations. Later preparations round down to whole blocks. A batch larger than the size still gets its own segment. Invalid settings preserve the previous size; shutdown returns `shutdown`.

`set_disk_retry` propagates to log allocation/write/finalization retries and snapshot preparation. `set_connect_retry_delay` forwards to the transport's optional connection policy. Both affect future scheduling; already-armed timers keep their deadlines. Zero is permitted. `set_auto_recovery` changes subsequent load/recover behavior, including when set after `init`; it does not rewrite loaded state. These void setters have no effect during shutdown. The connection-policy forwarder retains internal backend/transport ownership until the virtual call returns, even if a custom policy destroys the facade during that call.

`set_snapshot_compression` does nothing and returns success for either value, including after close and with LZ4 disabled. It does not enable snapshot compression. Existing compressed snapshot reading follows the LZ4 feature setting.

Reading an on-disk LZ4 snapshot through `load` or `snapshot_get` returns a decompressed payload. When `node` uses this snapshot to catch up a lagging node, it also sends the decompressed payload; the receiver's `snapshot_put` stores the supplied bytes unchanged. This does not rewrite the sender's original compressed file. Reading the compressed file itself with LZ4 disabled returns `invalid`. The older-snapshot fallback policy for `load` follows the contract below.

## Storage and Snapshot Methods

| Method | Contract |
| --- | --- |
| `bootstrap`, `recover` | Synchronously create the initial configuration or force recovery before loading. |
| `load(io_state&)` | Load term, vote, snapshot, and log. Consider earlier snapshots if the latest cannot be read. Preserve output and initiate shutdown on error. |
| `set_term`, `set_vote` | Synchronously persist metadata. Update the cache only after a successful write. |
| `truncate(index)` | Submit a blocking barrier and worker to remove the suffix from that index. Preserve ordering with subsequent appends. Errors after submission appear through `last_error()` and subsequent request results. |
| `append(index, entries, callback)` | Write at the current next index and retain accepted batches/payloads through completion. An immediate error does not invoke the callback. |
| `snapshot_put(trailing, value, callback)` | Prepare temporary files, then publish/clean up after a barrier finishing preceding logs. Only one may be pending; a duplicate returns `busy`. |
| `snapshot_get(callback)` | Read the latest snapshot on a worker. Deliver success and `nullopt` if none exists, or an error and `nullopt` on failure. Multiple reads may be submitted together. |

`recover(configuration)` loads the local snapshot/log using the current auto-recovery policy, closes recoverable open segments, and appends the supplied configuration at the next recovered index. The new entry uses the last stored entry’s term, or the snapshot term when no log entries remain. It preserves term/vote metadata and existing valid history; a snapshot-only store is supported, while a store with neither entries nor a snapshot returns `invalid`. Repeating recovery before `load` appends another configuration entry. After loading has opened the file coordinator, `bootstrap` and `recover` return `busy`.

Disabling auto-recovery prevents quarantine-and-retry of corrupt suffix files; it does not disable salvaging a valid batch prefix from an open segment. Recovery may finalize/remove open files or quarantine corrupt files before an error is returned, so failure does not promise directory rollback. `bootstrap` writes term 1 before creating its first configuration entry; a later failure may therefore leave term metadata persisted.

Automatic recovery can quarantine an open segment or the last closed segment when loading reports `corrupt`. Later open files are quarantined with the damaged file; damage in a middle segment followed by another closed segment is rejected as `corrupt`. These conditions also apply when a snapshot is present. A short read at actual EOF while reading the first batch's format/preamble/header/data is `io_error` and is not quarantined as a corrupt suffix even with automatic recovery enabled. A format-zero file is considered an empty prepared file only when all its bytes are zero.

A closed segment’s filename declares its inclusive log-index range. Its decoded entry count must match that entire range, including when it exceeds 32 bits; an inconsistent count is `corrupt`. The auto-recovery policy above governs quarantine/retry. High starting indices with a consistent small range do not by themselves imply a large allocation.

Snapshot term/index must be positive, with index below 10¹⁶. Combined payload must be nonempty. Invalid values return `invalid`; exceeding size limits returns `too_big`. Do not modify submitted snapshots/batches/messages or their payloads until completion.

A local snapshot with `trailing > 0` permits log writes after its barrier concurrently with publication. Installation with `trailing == 0` sets the next append index to `snapshot.index + 1` and blocks subsequent writes until publication and old-segment cleanup finish. Retain the latest two snapshots and retain segments according to the trailing policy.

A local `snapshot_put` may be followed immediately by `truncate` and `append` at the new index. Requests follow barrier and file-operation ordering. Immediate truncate success does not mean truncation has finished. A successful subsequent append callback includes completion of its preceding truncation; separately wait for local snapshot publication rather than inferring the relative order of its completion callback.

Snapshot preparation failures retry until success or shutdown. Publication/cleanup failures are delivered through completion without retry. Files may already have been published on failure; do not interpret it as rollback. Success includes file/directory synchronization but does not imply an atomic multi-file transaction.

## Transport Interface

| `transport` method | Implementer's contract |
| --- | --- |
| `init(id, address)` | Initialize the local ID/address. Copy the view when retaining the address. |
| `start(receive)` | Start receiving and deliver owning `message` values identifying the sender's ID/address. |
| `send(value, callback)` | Send a message identifying the destination ID/address. Never invoke completion on immediate failure; complete accepted sends exactly once. |
| `set_connect_retry_delay(unsigned) noexcept` | Optional policy for future connection retries. The default implementation does nothing; existing custom transports need not override it. `tcp_transport` updates its connection retry delay. |
| `error_details() const noexcept` | Optional owning diagnostic query; defaults to empty. Publish matching information before returning an immediate failure or invoking its failure completion. Queries must not allocate, throw, reenter the node, or report another request’s context. |
| `close(callback)` | Stop receiving, drain accepted sends/native handles, then complete. Must also be callable after `init`/`start` failure. |

The transport manages connections, serialization, and receive-buffer lifetime. A returned error or exception from `send` must mean that the request was not accepted; do not throw a submission failure after accepting it. Messages follow the [Node API](node.md) contract. `uv_io` also retains accepted send messages through completion. The node handles replication retries after transport errors.

`uv_io` copies transport init/start failures and immediate/asynchronous send diagnostics before external callbacks. Missing or mismatched information falls back to the operation’s Raft code. If a transport rejects submission by throwing, its earlier diagnostic is not reused. Send failures retain existing results/retry policy and do not themselves become fatal disk failures.

Build the library, application, and custom transport plugins against matching C++ interface headers. Optional method defaults provide source compatibility, not a contract for mixing binaries built against different virtual-interface versions.

## Timers, Close, and Diagnostics

The interval for `start(interval, tick, receive)` must be positive. A libuv timer calls tick and the transport delivers received messages. `time()` returns the loop's cached monotonic time in milliseconds. Like the C backend, `random(minimum, maximum)` uses `std::rand` and excludes maximum. It returns minimum when `minimum >= maximum`. Initialization seeds the process-wide random state.

`capacity()` sums remaining space in prepared files and active segments using the original unsigned `size_t` arithmetic, divides by 1024, then converts to `uint16_t` KiB. It does not saturate at the maximum; large values wrap into the sixteen-bit range. For example, two prepared files with a reported size of 32 MiB each and no active segment report 65536 KiB as zero. The value also feeds no-space threshold decisions and RPC capacity fields. Changing runtime segment size changes the report even without reallocating existing files. Returns zero during shutdown. `last_error()` reports a fatal backend error and does not replace individual callback results.

New submissions during shutdown return `shutdown`. Snapshots preparing, retrying, or waiting for a barrier are canceled. Snapshots already running publication workers and accepted reads drain normally. Close completion follows request completion, release of library-held request data, and native-handle cleanup. Calling close or destroying the last `uv_io` owner inside a callback preserves internal lifetime.

Register only one outstanding close callback. Repeating close with an empty callback is permitted; after shutdown has completed, a supplied callback runs inline. Exceptions from tick, receive, or completion callbacks do not cross native boundaries and initiate backend shutdown.
