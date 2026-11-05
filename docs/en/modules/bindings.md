# Node.js and C# Runtime Bindings

[한국어](../../ko/modules/bindings.md) · [Module index](index.md) · [I/O composition](../composition.md)

`modules/bindings` composes and runs core nodes with optional modules. Native C++ lives here; language APIs live in `bindings/nodejs` and `bindings/csharp/Raftpp`. Public headers are `<raftpp/bindings/bindings.hpp>` and `<raftpp/bindings/runtime.hpp>`; the target is `raftpp::bindings`. Node.js uses Node-API 8; C# uses P/Invoke. See [Node-API](https://nodejs.org/api/n-api.html) and [.NET interop](https://learn.microsoft.com/en-us/dotnet/standard/native-interop/best-practices).

## Configuration and Loading

`RAFTPP_MODULE_BINDINGS=ON` always builds shared `libraftpp-bindings.so`. Core is mandatory; UV is optional. `RAFTPP_BINDINGS_NODEJS=ON` adds `raftpp_nodejs.node` using `node_api.h`. Both options default OFF; Node OFF needs no Node SDK. Default composition uses UV storage/timers/TCP and requires UV, but SQLite/RocksDB + poll/Asio + custom transport, or plugin runtimes, can run without UV. Select SDK modules when configuring the library. Requesting a disabled module returns 19 (`not_found`); unknown composition selectors return 20 (`invalid`).

Use Node.js 18+ and .NET 8+. These commands assume the repository root and a single-configuration generator; use actual artifact paths for your configuration.

```sh
cmake -S . -B build/bindings -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_MODULE_BINDINGS=ON -DRAFTPP_BINDINGS_NODEJS=ON \
  -DRAFTPP_WITH_UV=ON -DRAFTPP_WITH_LZ4=OFF -DRAFTPP_WITH_FIXTURE=OFF
cmake --build build/bindings --config Release --parallel
export RAFTPP_NODE_ADDON="$PWD/build/bindings/modules/bindings/raftpp_nodejs.node"
export LD_LIBRARY_PATH="$PWD/build/bindings/modules/bindings:${LD_LIBRARY_PATH:-}"
dotnet build bindings/csharp/Raftpp/Raftpp.csproj --configuration Release
```

For SQLite/RocksDB, Asio, and LZ4/zstd/zlib, enable their `RAFTPP_MODULE_SQLITE/ROCKSDB/ASIO/LZ4/ZSTD/ZLIB=ON` options and set the prepared `RAFTPP_DEPENDENCY_PREFIX`. See [dependencies](../dependencies.md). `RaftNode.features`/`RaftNode.Features` reports enabled UV/SDK modules. Enabled asyncpp/mvfst/msquic flags describe SDK modules, not automatic connection factories; compose their executors/connected streams through plugins below.

Node.js loads `require('./bindings/nodejs')`; C# references `Raftpp.csproj` or a locally packed NuGet package. TypeScript declarations are in `index.d.ts`; consumers provide `@types/node`. Native binaries/SDKs/runtimes are not downloaded automatically. `cmake --install` installs the bridge, active public headers and optional addon. Installed consumers use `find_package(raftpp CONFIG REQUIRED COMPONENTS bindings)`. Keep addon and bridge in the same library directory for `$ORIGIN` lookup. Shared core/modules and external SDK runtime libraries must also be discoverable. Distribute managed packages separately. [LICENSE.en.md](../../../LICENSE.en.md) applies.

## Node and I/O Options

JS `NodeOptions` and C# `NodeOptions` take id/address/directory (`Id/Address/Directory`). IDs, terms and indexes use JS bigint/C# ulong. Server IDs are positive; unknown leader ID is zero. Transfer target zero selects automatically. Addresses are NUL-free UTF-8 strings. Callers create UV directories/DB parent directories; SQLite takes a file path, RocksDB a DB directory path.

| Node.js | C# | Default |
| --- | --- | --- |
| `electionTimeout` | `ElectionTimeout` | 1000 |
| `heartbeatTimeout` | `HeartbeatTimeout` | 100 |
| `installSnapshotTimeout` | `InstallSnapshotTimeout` | 30000 |
| `snapshotThreshold` / `snapshotTrailing` | `SnapshotThreshold` / `SnapshotTrailing` | 1024 / 2048 |
| `maxCatchUpRounds` / `maxCatchUpRoundDuration` | `MaxCatchUpRounds` / `MaxCatchUpRoundDuration` | 10 / 5000 |
| `maxInflightEntries` / `capacityThreshold` | `MaxInflightEntries` / `CapacityThreshold` | 32 / 4096 |
| `preVote` / `forceBarrier` | `PreVote` / `ForceBarrier` | false / false |
| `randomSeed` | `RandomSeed` | 1 |
| `payloadEncoding: 'bytes' / 'raw'` | `RawPayloads: false / true` | bytes / false |
| `pollInterval` | Not applicable | JS automatic polling 5ms |
| `io` | `IO: IOOptions` | Default composition below |

Times are milliseconds. Heartbeat must be positive and election greater than heartbeat. JS integer node options use uint32; pollInterval is positive. Capacity uses uint16. forceBarrier is selected at creation. snapshotTrailing=0 is supported: nodes distinguish local snapshots through snapshot_put_local to preserve newer logs, while remote installation uses replacement semantics.

| JS `io` name (PascalCase in C#) | Default/meaning |
| --- | --- |
| `factory` | composed; can select a registered plugin runtime |
| `storage` | uv; sqlite and rocksdb are also available |
| `timer` | uv; asio and standard monotonic-clock poll are also available |
| `network` | uv; custom when transport/Transport is supplied |
| `codec` | none; snapshot lz4/zstd/zlib |
| `configuration` | Plugin Buffer/byte[]; empty by default |
| `maxSnapshotSize` / `maxEncodedSnapshotSize` | 8MiB / 16MiB; core composition and DB/UV storage limits |
| `maxEntrySize` / `maxEntries` | DB limits 8MiB / 262144 |
| `segmentSize` / `blockSize` | UV segment 8MiB / automatic detection 0 |
| `diskRetry` / `connectRetry` / `autoRecovery` | UV 5000ms / 1000ms / true |

JS sizes are safe integers; C# uses ulong. Follow [UV](uv.md) for alignment/segment constraints and [SQLite](sqlite.md)/[RocksDB](rocksdb.md) for DB limits. Components share one serialized execution domain. DB operations/initialization/bootstrap/recovery may block the calling thread. The poll timer delivers ready ticks according to polling frequency and does not replay all missed ticks.

## Public Node Features

| Node.js | C# | Contract |
| --- | --- | --- |
| bootstrap / recover / start | Bootstrap / Recover / Start | Initialization, forced recovery and start per [node APIs](../node.md); handle existing-store bootstrap error 10 separately |
| apply / barrier | Apply / Barrier | Command submission/ordering after prior commands, completion callbacks |
| add / assign / remove | Add / Assign / Remove | Add spare/change role/remove server |
| transfer | Transfer | Zero target selects automatically; zero-status completion does not guarantee successful transfer, so inspect leader |
| state, currentTerm, votedFor, lastIndex, commitIndex, lastApplied, timeout, localRole, leader, transferee | Corresponding PascalCase properties | State/log/leader/timer queries; missing role/leader is null |
| configuration | Configuration | Independent copy of server IDs/addresses/roles |
| peerStatus / matchIndex / catchUp | GetPeerStatus / MatchIndex / CatchUp | Replication index/catch-up status with node preconditions/errors |
| error / errorMessage / errorDetails / lastMachineError | PascalCase properties | Raft code, diagnostic/cause categories, owning strings, original FSM exception |
| seed, setElectionTimeout, setHeartbeatTimeout, setInstallSnapshotTimeout, setPreVote, setSnapshotThreshold, setSnapshotTrailing, setMaxCatchUpRounds, setMaxCatchUpRoundDuration, setMaxInflightEntries, setCapacityThreshold | PascalCase methods | Corresponding C++ seed/setter behavior and ranges |
| setStepCallback / setTracer | SetStepCallback / SetTracer | Synchronous observation at native step/tracer points; null unregisters |
| poll / close / closed | Poll / Close / Closed | Ready-work dispatch/asynchronous shutdown/shutdown completion |

JS Role.standby/voter/spare and C# ServerRole.Standby/Voter/Spare are 0/1/2. NodeState unavailable/follower/candidate/leader is 0/1/2/3; CatchUpStatus none/running/aborted/finished is 0/1/2/3, with PascalCase C# members. status/Status supplies main state together. JS details supplies votedFor/timeout/transferee/localRole/leaderAddress/diagnostics/droppedEvents. droppedEvents/DroppedEvents counts losses from the bounded native copied-event queue (4096). Errors such as bootstrap rejection can remain during normal execution; distinguish them from shutdown state.

Step/trace callbacks may query state, replace observers or close. Command submission during a native step follows core busy admission. Use JS setImmediate or C# RaftHost.Post to submit afterward. Nested Poll is forbidden. Observer exceptions surface after other completions through JS microtasks/C# AggregateException. TraceEvent owns type/level/message/file/functionName/line and may be retained. The node tracer covers node diagnostics; tracing of separate native plugin components follows plugin contracts.

## FSM, Requests and Lifetimes

JS FSM supplies apply(Buffer): Buffer, snapshot(): Buffer, restore(Buffer): void. Optional snapshotAsync(Buffer): Buffer is a second synchronous snapshot phase; snapshotFinalize(): void is cleanup. C# IStateMachine uses corresponding PascalCase byte[] methods with defaults for optional hooks. Promise/Task-returning FSMs are unsupported. FSM hooks may query state or Close; core checks shutdown after returning. Reentrant submissions/start/recovery follow core busy/shutdown admission, and nested Poll is forbidden. Original exceptions remain in LastMachineError, returning native error 20; finalize does not propagate errors.

Inputs/commands/results are copied at boundaries. JS completion receives (RaftError|null, Buffer); C# uses CompletionCallback(RaftException?, byte[]). Non-apply operations return empty bytes. Immediate rejection throws with code/Code and produces no completion. Accepted requests complete once, and a throwing completion does not discard others. Native data/request state survive until completion. See [error codes](../errors.md). Timeouts do not withdraw commands.

Default bytes mode wraps commands/snapshots in an application envelope: 8-byte little-endian length and zero padding to an 8-byte boundary. File/RPC formats remain unchanged; only decoded bytes reach users. Empty arrays are supported. Raw mode shares the exact bytes with C++ FSMs, requiring native storage/transport command alignment and nonempty snapshot constraints. Do not mix modes within a cluster/store.

Direct RaftNode construction/calls/callbacks use one thread. JS start/close maintains automatic interval polling; ref/unref controls process retention. Direct C# nodes require Poll. Continue driving through Close completion and Dispose after Closed. Finalizers disable foreign callbacks and hand native drain to a cleanup thread without guaranteeing completion delivery. Use explicit orderly close. Live resources are retained on exhaustion or plugin-cleanup failure.

## Automatic C# Host

RaftHost(options, machine, bootstrap?, started?, onError?) creates/starts/polls a node on a dedicated thread. Post(Action<RaftNode>) runs every public node operation on that thread; FSM/completions use it too. Bootstrap configuration is copied; existing-store error 10 is allowed. started/onError execute on the worker. Post action exceptions reach onError. Startup failure reports onError and does not run pending work.

Close queues shutdown after preceding Posts and notifies after cleanup. Post after shutdown request throws ObjectDisposedException. Dispose requests Close without synchronously waiting on UI/event loops. Close on an already-closed host may invoke its callback immediately on the caller. The worker retains the process, so normal shutdown requires Close/Dispose. Use raw node references only inside Post, on their owning thread.

## Custom Transport

Implement JS start(receive, local), send(packet, done), close(done), or C# IPacketTransport.Start/Send/Close, and supply io.transport/IO.Transport. Packet owns peerId/PeerId, address and data. Send packets identify the destination; receive packets identify the authenticated sender. Data is one complete Raft RPC wire message. Connection/authentication/fragment reassembly/reconnect policy belongs to the transport. Partial/trailing bytes are rejected.

Send calls done once on actual completion, passing RaftError/RaftException on failure. Immediate throw is treated as send failure. Close stops receive and cleans its resources before done; native close waits for it. Late/duplicate send completions are ignored safely. JS uses the same event loop. C# transports may deliver receive/completion from other threads; Poll consumes queues on the creating thread. Direct Receive is creating-thread only. Receives after shutdown starts are discarded.

## Native Runtime Plugins

RaftNode.loadBackendPlugin(path)/LoadBackendPlugin(path) calls raftpp_bindings_register_plugin(). Plugins register names with raftpp::bindings::register_runtime(name, factory); duplicates return busy. Factories take directory/configuration bytes and return runtime. Backend is core io, poll is nonblocking, and drain completes finalizer cleanup after foreign callbacks are disabled. Owner outlives every component/callback. Optional abandon stops managed-language calls. Drain/abandon must not throw.

At this connector boundary, compose Asio/asyncpp executors, connected mvfst/MsQuic streams and framed_transport, or other SDK components. TLS/connection establishment and dispatch between execution domains are application contracts, not automatically implemented by flags. Custom core io is selectable here without omitting public I/O functions. Build plugins against the same C++ headers/ABI as the bridge and link shared libraries. Loaded plugins remain resident for process lifetime, including partially failing initialization, to preserve registered factory/component vtables. Consume configuration during creation or retain your own copy.

```cpp
#include <raftpp/bindings/runtime.hpp>
#include <raftpp/composed_io.hpp>
#include <raftpp/sqlite/sqlite.hpp>
#include <raftpp/uv/uv_timer.hpp>
#include <raftpp/uv/tcp_transport.hpp>
#include <uv.h>

namespace r = raftpp;
struct environment {
    uv_loop_t loop{};
    environment() { if (uv_loop_init(&loop)) throw r::system_error(r::errc::io_error); }
    ~environment() { uv_run(&loop, UV_RUN_DEFAULT); uv_loop_close(&loop); }
};
extern "C" int raftpp_bindings_register_plugin() {
    return r::bindings::register_runtime("application", [](r::string_view directory,
                                                         r::view<const r::byte> configuration) {
        // Parse application configuration and establish SDK connections here.
        (void)configuration;
        auto owner = std::make_shared<environment>();
        auto backend = r::make_io({
            .storage = std::make_shared<r::sqlite_storage>(r::fs::path(directory)),
            .timer = std::make_shared<r::uv_timer>(owner->loop),
            .network = std::make_shared<r::tcp_transport>(owner->loop)
        });
        return r::bindings::runtime{
            .owner = owner, .backend = std::move(backend),
            .poll = [owner] { uv_run(&owner->loop, UV_RUN_NOWAIT); },
            .drain = [owner] { uv_run(&owner->loop, UV_RUN_DEFAULT); }
        };
    }).value();
}
```

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS bindings uv sqlite)
add_library(application_backend SHARED backend.cpp)
target_link_libraries(application_backend PRIVATE raftpp::bindings raftpp::uv raftpp::sqlite)
```

Select application through io.factory/IO.Factory. Default selectors are unused by plugin factories; configuration interpretation belongs to the application.

## Native Interop

ABI version is 2. V1 create/status/completion layouts and symbols remain; create_runtime takes expanded options. Match uint64_t/size_t/pointer/struct layouts exactly. Ordinary returns are zero/Raft error; peek returns none=0/available=1/negative error. System errors normalize to io_error. raftpp_binding_read(kind=0/1/2) queries size then copies configuration/copied events/full leader address, consuming events only on successful copying. FSM operations 0..6 mean apply/snapshot/restore/snapshotAsync/finalize/step/trace. Copy output only through set_output during its callback.

After transport peek, send status=-1 for dispatch acknowledgement; actual completion uses 0..23. ID zero means transport close. Request IDs cannot repeat before pop; copy completion data before pop/release. Release transfers handle ownership; do not reuse it or race it with calls. Use close/Poll for orderly shutdown.

## Usage Examples

These examples require SQLite, zstd and UV modules enabled.

```js
const { RaftNode, Role } = require('./bindings/nodejs');
const fs = require('node:fs');
fs.mkdirSync('./data', { recursive: true });
let value = Buffer.alloc(0);
const node = new RaftNode({
  id: 1n, address: '127.0.0.1:9001', directory: './data/state.sqlite',
  snapshotThreshold: 16, snapshotTrailing: 0, preVote: true,
  io: { storage: 'sqlite', timer: 'poll', network: 'uv', codec: 'zstd' }
}, {
  apply(command) { value = Buffer.from(command); return value; },
  snapshot() { return value; },
  snapshotAsync(bytes) { return bytes; },
  snapshotFinalize() {},
  restore(bytes) { value = Buffer.from(bytes); }
});
node.bootstrap([{ id: 1n, address: '127.0.0.1:9001', role: Role.voter }]);
node.setTracer(event => console.log(event.message));
node.start();
// Access node.configuration, node.leader, node.peerStatus(peerId), and
// submit apply/barrier/membership/transfer operations with callbacks.
// During normal shutdown:
// node.close(() => console.log('closed'));
```

C# can use a host instead of direct Poll.

```csharp
using Raftpp;

Directory.CreateDirectory("./data");
using var stopped = new ManualResetEventSlim();
var options = new NodeOptions(1, "127.0.0.1:9001", "./data/state.sqlite") {
    PreVote = true, SnapshotTrailing = 0,
    IO = new IOOptions { Storage = "sqlite", Timer = "poll", Network = "uv", Codec = "zstd" }
};
using var host = new RaftHost(options, new Machine(),
    [new Server(1, options.Address)], onError: Console.Error.WriteLine);
bool sent = false;
host.Post(node => node.SetStepCallback(() => {
    if (sent || node.State != NodeState.Leader) return;
    sent = true;
    // Post executes after the current native step has finished.
    host.Post(leader => leader.Apply("hello"u8.ToArray(), (error, result) => {
        Console.WriteLine(error?.Message ?? System.Text.Encoding.UTF8.GetString(result));
        host.Close(stopped.Set);
    }));
}));
stopped.Wait();
sealed class Machine : IStateMachine {
    private byte[] _value = [];
    public byte[] Apply(byte[] command) { _value = command; return _value; }
    public byte[] Snapshot() => _value;
    public void Restore(byte[] snapshot) => _value = snapshot;
}
```
