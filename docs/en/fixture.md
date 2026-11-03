# Virtual-Time Cluster Fixture

[한국어](../ko/fixture.md) · [Nodes](node.md) · [Memory](memory.md)

Include `<raftpp/fixture.hpp>` and link `raftpp::raftpp`. `fixture` and its event/message enums are available only with `RAFTPP_WITH_FIXTURE=ON`. They require neither UV nor LZ4. The fixture runs the public callback-based `node` with an independent in-memory `io` implementation; it uses no sockets, files, background threads, or wall-clock sleeps.

## Ownership and Setup

`fixture` is move-only and owns up to `fixture::max_servers` (eight) nodes. `grow(shared<state_machine>, node_options = {})` adds a node with ID `index + 1` and its decimal ID as its address. Indices are zero-based. The fixture retains each FSM; callers can also retain their own shared handles for inspection. Moving the fixture preserves node/backend addresses. `get(index)` returns a borrowed node reference, invalidated by closing or destroying the fixture. `backend(index)` returns a shared `io` handle for direct simulation operations; a retained backend rejects new work with `shutdown` after closure or fixture destruction.

You can call `grow` on a running cluster. The new node has simulated connections to existing nodes but is neither started nor automatically added to the Raft configuration. Obtain its index as `size() - 1`, start only that node through `get(index).start()`, then change membership through the current leader's `add(id, address, callback)` and `assign(id, role, callback)`. Cluster-wide `start()` applies to every node; do not use it to restart already running nodes. Wait for each asynchronous configuration change to complete before submitting the next. Voter promotion includes any necessary log catch-up. Removing a Raft member retains its fixture node/FSM/index; it neither reuses the index nor renumbers other IDs.

| Operation | Contract |
| --- | --- |
| `grow(machine, options)` | Return `invalid` for a null FSM or invalid node options, `too_many` at the server limit, or `busy` during a step or close. Failure preserves cluster size and existing links. |
| `configuration(voters, output)` | Build all current members, with the first `voters` as voters and the rest as standbys. Require `1 <= voters <= size()`. Preserve output on error. |
| `bootstrap(configuration)` / `start()` | Apply to nodes in index order. Require at least one node. Stop on the first error; preceding successful nodes are not rolled back. |
| `size()` / `time()` | Number of nodes and shared virtual time in milliseconds. Both begin at zero. |
| `leader_index()` / `commit_index()` | Last step's stable-leader index, or `size()` for none; largest observed stable-leader commit index. Network controls update these observations on the next step. |
| `alive(index)` / `voted_for(index)` | Server's simulation liveness and persisted vote, respectively. |
| `persisted(index, output)` | Deep copy the server's persisted term, vote, snapshot, start index, and entries. Preserve output on error. |
| `error()` | Failure recorded when a direct I/O completion or close callback throws. Inspect each node's `error()` for Raft/driver failures. |

The normal node timeouts apply: heartbeat 100 ms, election 1000 ms, snapshot install 30000 ms. Backend capacity is 4096 KiB. Default randomized election timeouts are 1000, 1100, … ms. With a custom election timeout, the fixture uses `timeout + index * (timeout / 10)`. It seeds the existing Raft generator before starting and stepping to reproduce the configured timeout. `set_randomized_election_timeout(index, msecs)` requires a value in the node's inclusive `[election_timeout, 2 * election_timeout]` range. It controls subsequent randomized timer draws; `start_elect` directly changes current timers. Node tracing is selected through `node_options::tracing` or `get(index).set_tracer(...)`.

## Stepping and Observations

`step()` executes one event and returns `optional<fixture_event>` by value. Empty means no pending event, or a closed/moved-from fixture. `fixture_event` contains `server_index`, `type`, and virtual `time`. Event types preserve the C numeric codes: `tick = 1`, `network = 2`, `disk = 3`, `work = 4`. `work` is reserved; this memory backend schedules no work events.

The earliest due event wins. At equal times, lower server indices win; within the same server a tick precedes requests, and requests retain submission order. A send first completes at the current time; successful completion creates a separate delivery event after the sender's network latency. Disk requests use their submission-time disk latency. A transmit's latency is captured when its send completes. Defaults are 15 ms network and 10 ms disk. Each step processes only one tick or request, including when several share the same timestamp. Dead servers' tick slots advance without invoking their tick callbacks; their pending disk requests can still complete.

After an event, the fixture checks election safety: two leaders cannot have the same term. The highest-term live leader is stable when all applicable live, unsaturated followers acknowledge it and the C fixture's non-spare-member threshold is met. A disconnected link is distinct from saturation for this observation rule. For an unchanged stable leader, cached entries must match the preceding observed log unless compacted into a snapshot. Violations throw `system_error` with `errc::corrupt`. Safety comparisons include entry type, term, size, and bytes, and run in Release builds too. Observation preparation can allocate; allocation errors propagate from `step()` after the event has already occurred.

| Method | Contract |
| --- | --- |
| `step_n(count)` | Execute up to `count` events and return the last one; stop if none remains. Zero count is invalid. |
| `step_until(stop, max_msecs)` | Check `stop(const fixture&)` before each step. Return true only when stopped strictly before the virtual deadline, matching the C fixture. A condition reached exactly at the deadline returns false. A final event can overshoot the deadline. Empty predicates are invalid. |
| `step_until_elapsed(msecs)` | Advance events until at least this much virtual time elapses or no event remains. |
| `step_until_has_leader(limit)` / `step_until_has_no_leader(limit)` | Wait for the cached stable-leader observation. |
| `step_until_applied(index, entry, limit)` | Wait for `last_applied() >= entry`. Passing `size()` as index waits for every server. |
| `step_until_state_is(index, state, limit)` / `step_until_term_is(index, term, limit)` | Wait for the specified node state or current term. |
| `step_until_voted_for(index, candidate, limit)` | Wait for the node's current vote for server ID `candidate + 1`. |
| `step_until_delivered(from, to, limit)` | Wait until no send or transmit from `from` to `to` remains queued. Delivery or discard both remove a transmit. |
| `hook(callback)` | Invoke `callback(fixture&, const fixture_event&)` after each completed observation. The event reference is borrowed for that invocation; copy it to retain it. An empty callback disables the hook. |

Use the fixture and its backends on one thread. Recursive stepping throws `invalid_argument`. A hook may close the fixture, and exceptions from hooks/predicates propagate to their caller. Invalid query/wait indices throw `out_of_range`; invalid message enums throw `invalid_argument`. Controls returning `error_code` instead report `invalid` for invalid indices/arguments and `shutdown` after close. Moving from a fixture leaves size/time zero and no events; indexed queries on it are invalid.

## Elections and Network Controls

`start_elect(index)` requires no stable leader, all nodes in follower state, and a voting target. It minimizes that follower's current election timer while maximizing the others. `elect(index, limit = 20000)` invokes it and waits for that target to become stable. `depose(limit = 3000)` requires a stable leader, maximizes followers' current election timers, and temporarily drops their AppendEntries results until the leader steps down. Previous drop settings are restored even if stepping throws. `elect` and `depose` return false on timeout; invalid preconditions throw `system_error` or `invalid_argument` as appropriate.

`disconnect(from, to)`, `reconnect(from, to)`, `saturate(from, to)`, and `desaturate(from, to)` change **one direction**. Self links are invalid. A disconnected destination produces asynchronous `no_connection` when a send completes. A saturated connection still completes sends successfully, but drops deliveries. Saturating/desaturating a disconnected link returns `no_connection`. `saturated(from, to)` queries that flag.

`kill(index)` suppresses that node's tick callbacks and saturates both directions to every other node. It keeps the node/FSM and persisted data alive; it is not a crash/restart operation. `revive(index)` resumes callbacks and desaturates both directions to other live nodes. Explicit disconnections remain disconnected. Use `set_network_latency(index, msecs)` and `set_disk_latency(index, msecs)` for subsequent send completions/disk submissions; zero is allowed. Already scheduled events retain their due times.

`n_send(index, fixture_message_type)` counts successfully completed sends; `n_recv(...)` counts deliveries. Enum codes are AppendEntries 1, AppendEntriesResult 2, RequestVote 3, RequestVoteResult 4, InstallSnapshot 5, and TimeoutNow 6. Counters do not count discarded deliveries as received messages.

## Persistent State and Faults

`set_term(index, term)` changes only the persisted term. `set_snapshot(index, const snapshot&)` and `add_entry(index, const entry&)` deep-copy supplied data. With an empty persisted log, setting a snapshot sets the suffix start index to `snapshot.index + 1`. These methods do not alter an already running node's in-memory state. A backend `load` deep-copies data. With a snapshot, it returns every stored entry as a suffix starting at `snapshot.index + 1`, regardless of injection order. It does not filter entries using the previous start index, so injected entries must form the suffix corresponding to that snapshot. Repeated loads do not change the stored snapshot or entries; allocation failure preserves the caller output. A snapshot put with zero trailing discards the old log and starts its suffix after the snapshot; nonzero trailing retains the persisted entries. Snapshot get returns an independent compacted snapshot, or an empty optional. Memory-backend `recover` returns `io_error`, matching the original fixture's unsupported recovery operation.

`io_fault(index, delay, repeat)` counts eligible I/O calls: load, bootstrap, start, term/vote writes, effective truncations, send submission, append submission, and append completion. Negative delay disables faults; delay zero fails immediately. Positive delay counts successful eligible calls first. Negative repeat fails indefinitely; positive repeat fails that many times, and zero disables further failures. Injected errors are `io_error`. Snapshot put/get submission and snapshot-get completion do not consume this counter. Completing a zero-trailing snapshot calls `truncate(1)`: it consumes a tick if the stored log is nonempty or its suffix start index exceeds one, matching the C fixture’s internal truncation. Nonzero trailing keeps the log and does not consume a truncation tick. If the internal truncate fails, completion reports `io_error` and the previous snapshot/log remain unchanged; a later retry may succeed after a finite fault is exhausted. An immediate submission failure invokes no completion; an accepted request completes exactly once. Allocation failures likewise report their documented immediate/completion errors. Valueless message variants are rejected with `invalid` before queueing. Accepted message and entry contents must remain unchanged through their completion.

`close()` is idempotent. Following the C fixture, it first completes pending disk and send requests in server/FIFO order without advancing virtual time and discards queued transmits. It then closes all nodes and releases nodes/FSMs after callbacks drain. Completions can observe flushed durable data. Stop submitting new work before closing; completions must not create an endless request stream. Directly closing a node or its backend instead cancels that backend's accepted requests with `canceled`. No cancellation queue is allocated and no wall-clock I/O is awaited. The destructor calls fixture close. An in-progress event retains its own backend/request until it returns, so closing from a callback is safe. A retained closed backend can keep an allocator resource alive until its last shared handle is released. See [memory resource boundaries](memory.md).

## Example

```cpp
#include <raftpp/fixture.hpp>

struct counter final : raftpp::state_machine {
    unsigned count = 0;
    raftpp::error_code apply(raftpp::view<const raftpp::byte>,
                            raftpp::shared<void>& result) override
    { ++count; result.reset(); return {}; }
    raftpp::error_code restore(raftpp::shared_buffer) override
    { return raftpp::errc::io_error; }
    raftpp::error_code snapshot(raftpp::vector<raftpp::shared_buffer>&) override
    { return raftpp::errc::io_error; }
};

int main()
{
    raftpp::fixture cluster;
    auto first = std::make_shared<counter>();
    if (cluster.grow(first) ||
        cluster.grow(std::make_shared<counter>()) ||
        cluster.grow(std::make_shared<counter>())) return 1;
    raftpp::configuration members;
    if (cluster.configuration(3, members) ||
        cluster.bootstrap(members) || cluster.start()) return 2;
    if (!cluster.elect(0)) return 3;
    bool done = false;
    if (cluster.get(0).apply(raftpp::buffer{8},
        [&](raftpp::error_code error, raftpp::shared<void>) {
            done = !error;
        })) return 4;
    const auto index = cluster.get(0).last_index();
    if (!cluster.step_until_applied(cluster.size(), index, 2000)) return 5;
    return done && first->count == 1 ? 0 : 6;
}
```
