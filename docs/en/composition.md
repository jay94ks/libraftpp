# User-selected I/O Composition

[한국어](../ko/composition.md) · [Modules](modules/index.md) · [Nodes](node.md)

`make_io(io_components)` in `<raftpp/composed_io.hpp>` assembles user-selected storage, a timer, and message transport into `shared<io>`. It is mandatory core API with no dependency on UV or a particular SDK. Pass the result as an existing `node` backend. Also link the concrete components' module targets.

```cpp
#include <raftpp/composed_io.hpp>

raftpp::shared<raftpp::io> assemble(
    raftpp::shared<raftpp::storage> disk,
    raftpp::shared<raftpp::timer> clock,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::move(disk),
        .timer = std::move(clock),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

## Components and Options

| Field | Default/contract |
| --- | --- |
| `storage` | Required `shared<storage>`. Metadata, logs, snapshots, and storage capacity. |
| `timer` | Required `shared<timer>`. Periodic ticks and a monotonic millisecond clock. |
| `network` | Required `shared<transport>`. Sends/receives Raft messages. A single node may use `framed_transport` with no peers. |
| `snapshot_codec` | Null by default. When supplied, compresses stored snapshot payloads and decodes them on reads. Does not apply to logs/RPCs. |
| `max_snapshot_size` | Default 8 MiB. Bounds original payloads before storage and logical payloads on reads. |
| `max_encoded_snapshot_size` | Default 16 MiB. Bounds compression/escape frames including their headers. Unframed raw payloads use `max_snapshot_size`. Does not configure the storage provider's file-read limit. |
| `random_seed` | Default 1. Initial state of this I/O object's reproducible random generator. Select different seeds for different nodes. |

Null storage/timer/network throws `std::invalid_argument`. Composition/internal callback creation may throw allocation exceptions. The factory does not call component init/start. Construction failure releases supplied shared references without taking ownership away from other owners.

Replace components with user objects implementing the abstract interfaces. Enabling a module does not automatically select a particular implementation/codec. `executor` is a separate callback-submission interface; it is not itself storage or a timer.

## Storage Contract

`storage` in `<raftpp/storage.hpp>` uses the following order/contracts.

| Method | Contract |
| --- | --- |
| `init(server_id, string_view)` | Initializes with local identity. Copy the address view if retaining it. Must support close after failure. |
| `bootstrap(const configuration&)`, `recover(const configuration&)` | Synchronous configuration creation/recovery. The provider defines actual file changes and failure-state preservation. |
| `load(io_state&)` | Reads term/vote/snapshot/logs. The composer uses temporary state and moves it into caller output only after loading/required decoding succeeds. |
| `set_term(term)`, `set_vote(server_id)` | Durably persist the metadata before returning success. |
| `truncate(log_index)` | Removes the suffix starting at that index, preserving storage order with later appends. |
| `append(log_index, shared<const entry_batch>, completion)` | Retains accepted batches until one completion reporting persistence results. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | Publishes a snapshot with ordering relative to earlier logs and the trailing-retention policy. |
| `snapshot_get(snapshot_callback)` | Success with `nullopt` when absent; otherwise provides an owned snapshot value. |
| `close(close_callback) noexcept` | Stops submissions and drains accepted operations/cleanup before completion. Must support closing before initialization. |
| `capacity() const noexcept` | Capacity in KiB, forwarded to `io::capacity()`. Default 0. |
| `error_details() const noexcept` | Publish matching owning diagnostics before failure returns/completions. Default empty diagnostics. |

Synchronous methods and asynchronous submissions return `error_code`. Immediate failure invokes no completion; accepted work completes once, possibly inline. Storage treats snapshot payloads as owned bytes and must not transform composer frames. `uv_storage` passes these frames through while retaining its existing legacy LZ4 snapshot-read contract.

The built-in durable provider is [UV `uv_storage`](modules/uv.md). Supply another `storage` implementation for storage without UV. Asio/QUIC stream adapters do not themselves provide file persistence.

UV log-entry payload lengths must remain multiples of eight, as required by the existing disk format. The composer does not pad commands. Snapshot payloads do not have this alignment requirement.

## Timer Contract

`timer` in `<raftpp/timer.hpp>` provides `start(unsigned interval, completion tick)`, `time() const noexcept`, `close(close_callback) noexcept`, and `error_details() const noexcept`. Interval is positive milliseconds. Normal ticks pass an empty error code. An error tick ends the subscription and causes the composer to shut down the entire I/O. Initial submission failure invokes no tick. Close prevents new ticks and waits for active callbacks/native cleanup before completion. Closing before initialization/start is supported.

Choose [UV `uv_timer`](modules/uv.md) or [Asio `asio_timer`](modules/asio.md). Retain the loop/context through native shutdown.

## Execution Domain and Lifetime

All component, node/FSM calls and callbacks use **one serialized execution domain**. The factory does not compare SDK threads or create an automatic bridge. When combining loops, drive them together on one thread or provide explicit adapters satisfying this contract. For example, co-drive an `asio_timer` and UV storage on one thread with:

```cpp
// loop and context are the execution domains supplied to the components.
uv_run(&loop, UV_RUN_NOWAIT);
if (context.stopped()) context.restart();
context.poll();
```

The composer exclusively controls each component's init/start/close lifecycle. Do not simultaneously assign that lifecycle to another node/composer. Register peers with `framed_transport` before assembly; do not separately start its reads/close. QUIC connection/TLS/peer authentication remain connector responsibilities.

Nodes call `init → bootstrap/recover(optional) → load → start → close`; direct I/O users follow the same order. Init/load/start failure starts cleanup of all components. Repeated init returns `busy`, ID 0 returns `bad_id`, empty address returns `invalid`. Repeated/reentrant load returns `busy`. Start requires a successful load, positive interval, and nonempty tick/receive callbacks. Submissions during shutdown return `shutdown`.

The composer closes storage/timer/network and completes shutdown only after accepted requests and active user callbacks finish. Retains the first nonempty pending close observer; repeated empty close is safe. Observers supplied after completion may run inline. Destruction initiates close without waiting. Continue driving loops/contexts.

Batches, messages, original/encoded snapshots, codecs, and internal request state remain alive through actual completion, including the user callback. Composer-held request payloads are released before close completion. Callback exceptions do not cross native boundaries; they initiate shutdown. Submission/synchronous `bad_alloc` maps to `out_of_memory`, length errors to `too_big`, and other exceptions to `io_error`. `error_details()` owns the latest diagnostic; submission exceptions do not reuse old native causes. After assembly, `time()` uses the timer clock; `random(min,max)` excludes the upper bound and returns min when `min>=max`.

## Snapshot Codec and Format

Selected codecs run synchronously. The input snapshot remains unchanged; only an independently owned snapshot's payload is encoded. Term/index/configuration remain intact. Decoding precedes delivery to nodes/FSMs, leaving transport RPC and log-record formats unchanged.

Stored payloads use this v1 envelope; integers are little endian.

| Offset | Size | Content |
| --- | --- | --- |
| 0 | 8 | `52 41 46 54 50 50 01 00` |
| 8 | 8 | Encoding: 0 is an uncompressed escape, 1 a selected-codec frame |
| 16 | 8 | Original payload byte count |
| 24 | Rest | Raw data or codec frame |

Without a codec, original payloads are normally stored unchanged. Payloads beginning with envelope magic use encoding 0 to preserve arbitrary-byte round trips. With a codec, encoding is 1. Existing raw snapshots remain readable unless they begin with envelope magic. Such preexisting raw payloads are interpreted as envelopes and require separate conversion before opening with this path. Reading encoding 1 without a codec returns `invalid`; truncated headers/unknown encodings/decoded-length mismatches return `corrupt`; limit violations return `too_big`. Codec errors are preserved. Failed snapshot_get delivers `nullopt`; failed load preserves caller state.

Reopen directories with the same snapshot-codec configuration. The envelope stores no codec identifier/dictionary; do not arbitrarily switch codecs. Legacy `uv_io` does not decode new envelopes, so reopen those directories through `make_io`. Existing `uv_io` raw/LZ4 reading and fallback to older snapshots are unchanged. Do not share storage/snapshot directories between incompatible format configurations. The envelope provides neither encryption nor authentication.

## Concrete Composition

```cpp
#include <raftpp/composed_io.hpp>
#include <raftpp/uv/uv_storage.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <raftpp/zstd/zstd.hpp>

raftpp::shared<raftpp::io> make_backend(uv_loop_s& loop, raftpp::fs::path directory)
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::uv_storage>(loop, std::move(directory)),
        .timer = std::make_shared<raftpp::uv_timer>(loop),
        .network = std::make_shared<raftpp::tcp_transport>(loop),
        .snapshot_codec = std::make_shared<raftpp::zstd_codec>(),
    });
}
```

Requires UV and `RAFTPP_MODULE_ZSTD=ON`. Use `find_package(raftpp CONFIG REQUIRED COMPONENTS uv zstd)` and link `raftpp::uv`/`raftpp::zstd`. Create the directory beforehand. Replace the timer with `asio_timer`, or network with `framed_transport` containing established Asio/mvfst/MsQuic streams, while satisfying the execution-domain contract.

With `RAFTPP_BUILD_EXAMPLES=ON`, `raftpp-example-composed DIRECTORY [none|lz4|zstd|zlib] [uv|asio]` uses a selected codec/timer and `uv_storage`. Enable the corresponding SDK modules at build time. This single-voter example uses a peerless transport, increments a counter once, and waits for snapshot publication/shutdown. Reopen the same directory with matching arguments to restore its value. Existing `uv_io` remains a separate API and does not require this factory.
