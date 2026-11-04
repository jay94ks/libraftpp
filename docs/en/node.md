# Nodes, I/O, and State Machines

[한국어](../ko/node.md) · [Event Core](core.md) · [Buffers, Entries, and Snapshots](storage.md) · [Message Encoding](encoding.md) · [Virtual-Time Fixture](fixture.md) · [Server and Cluster Examples](examples.md) · [I/O composition](composition.md)

Include `<raftpp.hpp>` and link the CMake target `raftpp::raftpp`. Names are in namespace `raftpp`. These APIs use C++20 and are available regardless of `RAFTPP_WITH_UV`, `RAFTPP_WITH_LZ4`, or `RAFTPP_WITH_FIXTURE`. Provide implementations of `io` and `state_machine` to the node.

With `RAFTPP_WITH_UV=ON`, `raftpp::raftpp` propagates libuv header search paths and link dependencies. Consumers using a libuv event loop can link that target and include `<uv.h>`. Configurations with `RAFTPP_WITH_UV=OFF` add no libuv usage requirements.

See the [uv_io API](uv_io.md) for the backend connecting file storage, libuv timers, and a transport.

See [Errors and Owning Diagnostics](errors.md) for `error_details()`, fixed message storage, native causes, and diagnostic lifetime.

## Construction and Lifetime

```cpp
node(server_id id, string address, shared<io> backend,
     shared<state_machine> fsm, node_options options = {});
error_code bootstrap(const raftpp::configuration& configuration) noexcept;
error_code recover(const raftpp::configuration& configuration) noexcept;
error_code start() noexcept;
error_code close(close_callback callback = {}) noexcept;
```

`node` is movable and noncopyable. `id` must be nonzero and `address` nonempty. The backend and FSM are required and share ownership with the node. You can also move `owned<io>` and `owned<state_machine>` into the constructor. Construction can throw `invalid_argument` for invalid arguments, `bad_alloc` for allocation failure, or `system_error` for an `io::init` error. The backend must copy the address view passed to `io::init` if retaining it after the call.

`bootstrap` records the initial configuration in a new store, returning `cannot_bootstrap` for an existing store. `recover` forcibly records a new configuration according to the backend's recovery contract. Call both before `start`. A joining node can start with an empty store without bootstrapping. `start` loads the persisted term, vote, snapshot, and log, restores the FSM snapshot, then starts receiving events. Startup failure closes the node. Calling `start` again on a started node returns `invalid` and preserves its existing execution.

While startup is in progress and shutdown has not been requested, reentering `start`, `bootstrap`, or `recover` from a user backend/FSM returns `busy` without nested loading/restoration or persisted-configuration changes. This differs from a duplicate call after startup has completed. You may close the node or destroy its handle inside `io::load`, startup FSM `restore`, or `io::start`. If that call returns successfully after requesting shutdown, `start` returns `shutdown` and does not continue subsequent startup phases or resume receive subscriptions. The explicit close itself is not recorded as a fatal startup error. Internal state/backend/FSM remain alive until the active call returns; the close callback may run inline.

`close` initiates shutdown and calls its callback after accepted I/O completes or is canceled and cleaned up. Pending client requests can complete with `leadership_lost` when shutdown is requested. The close callback can run inline. If allocating callback-registration storage fails, `close` returns `out_of_memory` without invoking that callback or initiating a new shutdown. You can call `close()` again without an argument. A closed node invokes close callbacks inline.

You may also close the node or destroy its handle within a running user I/O or FSM call. After a successful return, the driver checks shutdown again and does not start new I/O work or FSM application/restoration. It continues accepted-I/O completion/cancellation and snapshot-cleanup callbacks. A successful client submission may report `leadership_lost` through its completion, possibly inline before submission returns; that completion runs before close completion. Closing within FSM `apply`/`restore` prevents further advancement of `last_applied` after the call returns. This does not undo side effects of an active user function or a persisted command. Ordinary explicit close itself is not recorded as a new fatal error.

The destructor and move assignment initiate asynchronous shutdown of the previous node. The destructor does not wait. Internal requests, data, backend, and FSM remain alive for as long as actual cleanup requires. Keep running the event loop until shutdown completes. Destroying the node handle inside a completion callback preserves internal operation lifetime. A moved-from node is `unavailable` and returns `shutdown` for submissions.

Serialize node methods and backend/FSM callbacks on one event-loop thread. Shared ownership does not provide thread synchronization.

Nodes follow the [event core term and clock domains](core.md). Backend `time()` must fit that domain for the current election timeout. Starting beyond the clock bound returns `too_big` from `start()` and closes the node. If a timer/received event exhausts the clock or next-election term domain while running, the node records `too_big` and initiates asynchronous shutdown. Receiving the maximum term itself and single-voter self-election remain permitted.

## Commands and Completion Callbacks

```cpp
using completion = function<void(error_code)>;
using apply_callback = function<void(error_code, shared<void>)>;
using close_callback = function<void()>;

error_code apply(shared_buffer command, apply_callback callback = {}) noexcept;
error_code barrier(completion callback = {}) noexcept;
error_code add(server_id id, string_view address, completion callback = {}) noexcept;
error_code assign(server_id id, role role, completion callback = {}) noexcept;
error_code remove(server_id id, completion callback = {}) noexcept;
error_code transfer(server_id id = 0, close_callback callback = {}) noexcept;
```

A returned error means immediate submission failure; the callback is not invoked. Success means the request was accepted. A supplied completion callback runs exactly once with success or failure, possibly before the submission method returns. Common immediate errors include `not_leader`, `shutdown`, `busy` for submissions reentering from an FSM call, `out_of_memory`, and `no_space`. Do not throw from callbacks. Ordinary completion callbacks can submit new requests.

`apply` appends one command to the log and applies it to the FSM after commit. Overloads also accept `buffer` and `aligned_buffer` by value, so you can pass them directly with `std::move`. Copies of `shared_buffer` share the same read-only storage. A buffer moved into an argument is cleaned up automatically even on an immediate error and is not returned to the caller. FSM results use `shared<void>` and can be cast to the required type and retained. The library owns command bytes and callback state for as long as request completion requires.

Completion callbacks can be omitted or passed as `nullptr`. A null function pointer also omits the callback. All request methods also accept ordinary functions, function pointers, and callbacks with move-only captures. Standard `shared_ptr` maintains callback lifetime internally. Argument construction and pre-call copying of `std::function` arguments still follow standard C++ exception rules.

```cpp
#include <raftpp.hpp>

raftpp::error_code submit(raftpp::node& node, raftpp::buffer command)
{
    auto context = std::make_unique<unsigned>(42);
    return node.apply(std::move(command),
        [context = std::move(context)](raftpp::error_code status,
                                       raftpp::shared<void> result) {
            // Handle status. context and result remain alive during this call.
            (void)status;
            (void)result;
            (void)context;
        });
}
```

The node supports log/snapshot indices through `numeric_limits<log_index>::max() - 1`. A leader submission exceeding that bound is immediately rejected with `too_big` and does not invoke its completion callback. Received/startup state also follows the [event-core index range](core.md). Native storage filenames may impose a smaller limit.

`barrier` completes after its log barrier commits without applying a command to the FSM. `add` adds a server as a `spare`. `assign` selects `voter`, `standby`, or `spare`, waiting for necessary catch-up before voter promotion. Concurrent membership submissions can return `cannot_change`. For `assign`, an unknown server returns `not_found`; invalid or already assigned roles return `bad_role`. `remove` removes a server from the configuration and returns `bad_id` for a missing ID.

After common lifecycle admission checks, `add` and `remove` validate IDs/addresses in a configuration copy before checking leadership/change availability. Duplicate IDs/addresses or a missing removal target can therefore take precedence over a pending change. `assign` checks leadership/transfer first, then requested-role validity, change availability, target existence, and its current role. Immediate rejection neither changes the pending request nor invokes its completion.

`transfer` transfers leadership to another voter. ID 0 selects the first other voter in configuration order replicated through the current log end, or the last other voter if none is caught up. No other voter returns `not_found`; self, unknown IDs, and non-voters return `bad_id`. Its callback has no status argument; inspect the observed leader with `leader()` afterward. Shutdown or transfer timeout can also invoke the callback, so invocation does not establish successful transfer. New commands can be rejected while transferring.

The [event-core transfer contract](core.md) also applies: partial progress can start a transfer hint that the recipient discards if its durable log does not match; subsequent ACKs do not automatically resend it. A timeout releases the pending transfer, allowing an explicit retry.

Accepted requests that cannot finish after leadership loss or I/O failure can complete with `leadership_lost`. Inspect the underlying node failure with `error()` and `error_message()`. A failure callback or caller timeout does not guarantee withdrawal of a submitted command.

## Step Notifications

```cpp
using step_callback = function<void()>;
error_code set_step_callback(step_callback callback = {}) noexcept;
```

`set_step_callback` registers notifications corresponding to original `raft.legacy.step_cb`. Notifications are disabled by default. After each successful core step, invoke the callback on the same loop thread after state-transition/request checks and before that step's term/vote/log/snapshot/send I/O and FSM application. Consequently, a newly observed commit index may still have an older `last_applied`. Notification itself establishes neither command completion nor persistence. One external event may produce internal configuration/persistence events and multiple notifications; identical observed state is not deduplicated. Failed steps emit no notification. `bootstrap`, `recover`, queries, setters, and direct `close` do not themselves produce core-step notifications. A completed apply awaiting delivery with `no_space` suppresses that step notification so the client error can be delivered first. Skipped notifications are not replayed automatically; a subsequent successful step checks again.

Standard shared ownership retains the callable/captures. Ordinary functions, function pointers, and move-only captures are supported. Replacing/clearing during emission keeps the current call alive; replacements apply to subsequent steps. An empty function, `{}`, or `nullptr` clears without allocation. Registration allocation failure returns `out_of_memory` and preserves the previous callback. A moved-from node returns `shutdown`. A closed node permits registration/clearing but executes no new steps; captures remain until replacement/clearing/object destruction. Argument construction and pre-call `std::function` copying follow standard C++ exception rules.

An observer may query state, replace/clear itself, or close/destroy the node. Closing prevents later I/O/FSM phases while already admitted requests settle under the existing close contract. New request submissions from an observer before shutdown return `busy`. Do not throw from observers. The node event boundary translates exceptions into errors and initiates shutdown: ordinary exceptions become `io_error`, `bad_alloc` becomes `out_of_memory`, and `length_error` becomes `too_big`.

```cpp
#include <raftpp/node.hpp>

raftpp::error_code count_steps(raftpp::node& node,
                              raftpp::shared<unsigned> notifications)
{
    return node.set_step_callback(
        [notifications = std::move(notifications)] { ++*notifications; });
}
```

## Options and State Queries

| `node_options` field | Default | Meaning |
| --- | --- | --- |
| `election_timeout` | 1000 | Election timeout in ms; greater than heartbeat and at most half the maximum `uint32_t`. |
| `heartbeat_timeout` | 100 | Heartbeat interval in ms; must be positive. |
| `install_snapshot_timeout` | 30000 | Snapshot installation timeout in ms; must be positive. |
| `snapshot_threshold` | 1024 | Applied-entry count determining when to take a snapshot. |
| `snapshot_trailing` | 2048 | Log entries to retain after a local snapshot. |
| `max_catch_up_rounds` | 10 | Promotion catch-up round limit. |
| `max_catch_up_round_duration` | 5000 | Catch-up round timeout in ms. |
| `max_inflight_entries` | 32 | Per-server limit on replication entries awaiting acknowledgment. |
| `capacity_threshold` | 4096 | Voter storage-capacity threshold in KiB; 0 disables capacity checks. |
| `pre_vote` | false | Whether to use pre-vote. |
| `force_barrier` | false | Force an initial barrier on leader conversion with multiple voters. |
| `tracing` | Default stderr tracer | Diagnostic observer; see [tracing](tracing.md). |

Each option has a corresponding `set_...` method except `force_barrier`, which is set only at construction; change `tracing` with `set_tracer(tracer)`. The three timeout setters return `error_code`, reporting `invalid` when the conditions above are violated. Other setters return `void`. `seed(uint32_t)` sets the internal election random state.

`force_barrier` adds a barrier on multi-voter leader conversion even when the existing log is committed. A single voter follows its ordinary self-election/commit path without adding a barrier for this option. The library does not inspect executable names; callers express environment-specific compatibility policy explicitly through construction options.

The election setter resamples the follower/candidate deadline without resetting its start time. If the proposed timeout exceeds the representable range at the clock recorded by the core, it returns `too_big`, preserving settings, random seed, and expiration. For legacy compatibility, `set_election_timeout(150)` with heartbeat 15 ms changes election/heartbeat to 450/45 ms. Explicit constructor options remain literal. Configure heartbeat before `start()` for the backend tick interval; a later setter does not restart that timer. Names, standalone random values, and reference-version metadata are described in [Event Core and Common Queries](core.md).

| Query | Return or output |
| --- | --- |
| `state()` | `node_state::{unavailable,follower,candidate,leader}`; `follower` before startup, matching the reference `raft_init`; `unavailable` during shutdown. |
| `current_term()`, `voted_for()` | Current term and voted-for server ID. |
| `last_index()`, `commit_index()`, `last_applied()` | Last log, commit, and FSM-applied indices. |
| `timeout()` | Absolute next timeout in the backend's ms clock; 0 during shutdown or on a moved-from handle. Before startup, the unsampled default timer also returns 0. |
| `local_role()` | `optional<role>` in the current configuration; empty when absent. |
| `leader()` | Value copy of the known leader as `optional<leader_info>`. `leader_info` has `id` and `address`. |
| `configuration()` | Value copy of the current [configuration](configuration.md). |
| `match_index(id, index)` | Outputs the server's replication index known to the leader. |
| `catch_up(id, status)` | Outputs `catch_up_status::{none,running,aborted,finished}`. |
| `transferee()` | Transfer target ID, or 0 when absent. |
| `error()`, `error_message()` | Recorded node error and contextual message; success and an empty string if no error exists. |

`match_index` and `catch_up` return `error_code`: `shutdown` during shutdown, `not_leader` when not leader, and `bad_id` for an unknown ID. They preserve output arguments on failure. Queries copying strings or configurations can throw allocation exceptions. Setters without return values, including `set_pre_vote` and `seed`, also run on the same thread.

`leader_info` is an owning value from `<raftpp/types.hpp>`, shared with event-core queries. Node leader/role/replication-progress queries use the same core semantics after applying the node lifecycle checks for moved or closing handles.

## I/O Implementation Contract

`io` is an abstract class with a virtual destructor. Implement the following methods. Asynchronous submission methods returning `error_code` must not invoke callbacks on immediate failure; accepted operations complete exactly once, including shutdown and cancellation. Inline completion is allowed.

| Method | Contract |
| --- | --- |
| `init(server_id, string_view)` | Initialize storage and the address. The backend also cleans up failed initialization resources. |
| `bootstrap(const configuration&)`, `recover(const configuration&)` | Synchronously record initial configuration or forced recovery before startup. |
| `load(io_state&)` | Return term, voted_for, optional snapshot, start_index, and entries by value. The first log index defaults to 1. |
| `start(unsigned interval, function<void()> tick, function<void(message)> receive)` | Register the timer with its ms interval and the receiver. |
| `close(close_callback) noexcept` | Stop new timer and receive callbacks, complete or cancel accepted operations, then invoke the close callback. |
| `set_term(term)`, `set_vote(server_id)` | Durably record before responding. Recording a new term also resets the stored vote to 0. |
| `truncate(log_index)` | Remove the log suffix starting at that index. Order pending writes and subsequent appends correctly. |
| `append(log_index, shared<const entry_batch>, completion)` | Record entries starting at the given index, completing after durable storage. |
| `send(shared<const message>, completion)` | Retain the message and payload until transport completes. |
| `snapshot_put(unsigned trailing, shared<const snapshot>, completion)` | Store a snapshot. trailing counts covered log entries to retain; 0 requests none and is also used for installation. Replace conflicting history; a backend may retain a matching suffix. |
| `snapshot_get(snapshot_callback)` | Return the stable snapshot by `optional<snapshot>` value. The callback type is `function<void(error_code, optional<snapshot>)>`. |
| `time() const noexcept` | Monotonically increasing time in ms. |
| `random(uint32_t minimum, uint32_t maximum) noexcept` | Random value within inclusive bounds. |
| `capacity() const noexcept` | Guaranteed capacity for log storage in KiB; the default implementation returns 0. |

A backend without capacity reporting can disable checks with `capacity_threshold = 0`. Replication retries transport failures. Persistent storage failures can close the node. Backends using reference or view arguments asynchronously must obtain ownership of that data. Batches, messages, and snapshots passed as owning handles can be retained until completion.

`entry_batch` is `vector<shared<const entry>>`. `message` contains `server_id`, an owning `server_address` string, and `message_payload`. The ID and address describe the destination for sends and the sender for receives. The payload is a standard variant of `append_entries`, `append_entries_result`, `request_vote`, `request_vote_result`, `install_snapshot`, and `timeout_now`, in that order. Its `index() + 1` matches the original RPC code. Fields in [message.hpp](../../include/raftpp/message.hpp) represent message version, term, log indices and terms, vote, pre-vote, features, capacity, configuration, and payload. `message_feature_capacity` is the capacity-reporting bit. `install_snapshot::needs_data` is an internal snapshot-loading directive handled by the node driver. It must be `false` for messages the backend sends or delivers to the receiver.

| Payload type | Fields and meaning |
| --- | --- |
| `request_vote` | `version` defaults to 2. `term` is the election term and `candidate_id` is the candidate ID. `last_log_index` and `last_log_term` describe the candidate's persisted log boundary. `disrupt_leader` bypasses vote rejection based on an existing leader during leadership transfer; `pre_vote` requests a preliminary vote before recording an actual term or vote. |
| `request_vote_result` | `version` defaults to 2. `term`, `vote_granted`, and `pre_vote` describe the vote response. `features` contains capability bits; `capacity` is guaranteed log-storage capacity in KiB. |
| `append_entries` | `version` defaults to 0. `term` is the leader term; `prev_log_index` and `prev_log_term` are the matching boundary immediately before new entries; `leader_commit` is the leader's commit index. `entries` contains shared entry handles to append in order; an empty batch is a heartbeat. |
| `append_entries_result` | `version` defaults to 2. `term` is the response term. `rejected` is 0 on success, or the rejected previous log index on rejection. `last_log_index` is the acknowledged persisted matching prefix on success, or a log-position hint on rejection. `features` and `capacity` have the same units and meaning as in vote responses. |
| `install_snapshot` | `version` defaults to 0. `term` is the leader term; `last_index` and `last_term` describe the snapshot boundary. `configuration` and `configuration_index` contain the snapshot configuration and its log index; `data` is a shared buffer owning snapshot bytes. The backend contract for `needs_data` is described above. |
| `timeout_now` | `version` defaults to 0. `term`, `last_log_index`, and `last_log_term` describe the term and log boundary used for leadership transfer. It asks the receiving target to start an election immediately. |

A node that is no longer a candidate ignores version-2-or-later `request_vote_result` messages with `pre_vote=true`. A delayed response's proposed term does not change its current term/vote or request I/O persistence. Higher terms in actual vote responses and version-1 responses, which do not support the pre-vote indication, follow the usual update rules.

## State Machine Implementation Contract

```cpp
virtual error_code apply(view<const byte> command, shared<void>& result) = 0;
virtual error_code restore(shared_buffer data) = 0;
virtual error_code snapshot(vector<shared_buffer>& buffers) = 0;
virtual error_code snapshot_async(vector<shared_buffer>& buffers);
virtual void snapshot_finalize(vector<shared_buffer>& buffers) noexcept;
```

FSM methods execute synchronously on the node's event loop. `apply` applies committed commands in log order. Copy a command view if retaining it after the call. The shared object assigned to `result` is passed to that command's completion callback. `restore` restores snapshot state supplied as one compacted shared buffer. Retain the handle to keep its storage alive. Failure of `apply` or `restore` can close the node.

`snapshot` freezes state at that point as read-only buffers. Each successful preparation has exactly one matching `snapshot_finalize`, regardless of storage success, failure, or cancellation. Failed preparation itself does not count as successful work requiring finalization. The optional second phase, `snapshot_async`, also returns synchronously; its default implementation succeeds. The default `snapshot_finalize` clears the buffers. A failed `snapshot_async` or immediate snapshot-submission failure finalizes prepared state and waits for another snapshot opportunity. The FSM must not modify or release the buffers before storage completes.

If local snapshot metadata/request preparation or `snapshot` throws `bad_alloc` or `length_error`, the node skips that attempt and retries on a later event. Already committed/applied commands and their successful callbacks are preserved; these errors alone do not close the node. Failure to prepare submission after a successful freeze calls `snapshot_finalize`. Other first-phase FSM exceptions and completion errors from already accepted snapshot storage can close the node.

After successful local snapshot storage, the node does not submit the same commit index again while its completion event awaits core processing. Immediate submission failure may retry at a later snapshot opportunity. Inline completion follows the same contract.
