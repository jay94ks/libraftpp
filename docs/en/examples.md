# Native Server and Cluster Examples

[한국어](../ko/examples.md) · [Node API](node.md) · [libuv I/O](uv_io.md) · [TCP Transport](tcp_transport.md) · [Native Submit Benchmark](benchmark.md) · [I/O composition](composition.md)

The examples compose `node`, `uv_io`, `tcp_transport`, and an owning counter state machine. They use the C++ library. Each server has a separate process, event loop, and persistent directory. `RAFTPP_BUILD_EXAMPLES` defaults to OFF; enabling it requires `RAFTPP_WITH_UV=ON` on Linux. It does not require fixture, LZ4, or tests.

## Build and Run

```sh
cmake -S . -B build -DRAFTPP_WITH_UV=ON -DRAFTPP_BUILD_EXAMPLES=ON
cmake --build build --target raftpp_example_cluster --parallel
./build/examples/raftpp-example-cluster /tmp/raftpp-demo
```

The CMake targets are `raftpp_example_server` and `raftpp_example_cluster`; executable names use hyphens. Building the cluster target also builds its server. Executables remain in the build tree. The supervisor finds the server beside its own executable, independently of the working directory, or uses `--server PATH`.

The supervisor creates three directories numbered `1`, `2`, and `3`, starts one server for each, then repeatedly stops a randomly selected server with SIGINT, waits, and restarts it using its existing data. Default intervals are independently drawn from 1–15 seconds; each outage uses the same interval before stopping and before restarting. Newly created data directories use owner-only permissions. Existing data is retained on exit. Reuse the same IDs and port configuration when reusing directories.

SIGINT or SIGTERM stops the supervisor and all children. It waits for child exits and closes process handles. Children that do not exit within five seconds are killed; that run returns failure. Unexpected child exit or a spawn/startup failure stops the remaining children and returns failure.

## Server API Composition

Run three standalone servers in separate terminals:

```sh
./build/examples/raftpp-example-server /tmp/raftpp-demo/1 1
./build/examples/raftpp-example-server /tmp/raftpp-demo/2 2
./build/examples/raftpp-example-server /tmp/raftpp-demo/3 3
```

IDs are 1 through 3. Each server bootstraps the same three-voter configuration if its directory is new; `cannot_bootstrap` with existing data permits ordinary startup and recovery. A historical diagnostic is not itself a shutdown notification. The application checks the node's state when detecting an unexpected shutdown.

Servers enable pre-vote, snapshot threshold 64, and trailing retention 16. A timer proposes an increment of one every 125 ms while the node is leader. Followers apply replicated commands. The counter accepts exactly eight command bytes containing a native `uint64_t`, matching the original C counter example. Snapshots contain a separate eight-byte copy of that value, and restore validates the size before changing state. This sample value format uses host byte order; RPC and disk framing retain the library's own formats.

An FSM result uses `shared<void>` backed by the counter's shared value, remaining alive when retained after machine destruction. Results refer to that mutable counter value; snapshots own independent bytes. Command buffers and callbacks are owned through actual completion. The application logs count milestones and final count/restore/snapshot totals.

On SIGINT/SIGTERM or `--run-for`, the server stops proposing, asks to transfer leadership if it is leader, and then closes the node. The application allows at most two seconds for handoff, then closes/drains the node even if all peers have stopped and no new leader can be observed. Transfer completion is a best-effort handoff, not proof of success. The loop continues through network/file/request cleanup before application state is destroyed.

## Options

Both executables accept:

| Option | Meaning |
| --- | --- |
| `--base-port N` | Base in 1..65532; server IDs 1..3 use loopback ports N+1..N+3. Default 9000. |
| `--apply-interval MS` | Positive proposal interval in milliseconds. Default 125. |
| `--snapshot-threshold N` | Node snapshot threshold. Default 64. |
| `--snapshot-trailing N` | Node trailing retention. Default 16. |
| `--help` | Print usage without starting nodes. |

The server requires `<dir> <id>` and optionally accepts `--run-for MS`, a positive run duration. Without it, the server waits for a signal. The cluster accepts an optional directory, default `/tmp/raftpp`, and these options:

| Option | Meaning |
| --- | --- |
| `--cycles N` | Positive number of stop/restart cycles; omitted means continue until a signal. |
| `--interval MS` | Positive fixed interval instead of randomized seconds. Applies before stopping, during downtime, and while settling after restart. |
| `--server PATH` | Server executable path; arguments are passed directly without a shell. |

After the final bounded restart, the supervisor allows one more interval for the restarted node to run before shutting down all servers. For example:

```sh
./build/examples/raftpp-example-cluster /tmp/raftpp-demo \
  --cycles 1 --interval 3000 --base-port 12000 \
  --apply-interval 20 --snapshot-threshold 4 --snapshot-trailing 2
```

Use a fresh directory or matching persisted configuration. Invalid IDs, option names/values, missing values, startup failures, and child failures return a nonzero exit status. There is no network authentication or encryption; the example binds loopback addresses.
