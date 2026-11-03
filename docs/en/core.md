# Event Core and Common Queries

[한국어](../ko/core.md) · [Callback Nodes](node.md) · [Buffers, Entries, and Snapshots](storage.md) · [CMake Builds/Installation](cmake.md)

Include `<raftpp/core.hpp>`, `<raftpp/random.hpp>`, or `<raftpp/version.hpp>` for these APIs, also included by `<raftpp.hpp>`. Link only `raftpp::raftpp`. All are available with any UV/LZ4/fixture setting.

## State and Events

```cpp
error_code step(core_state& core, const core_event& event) noexcept;
uint64_t next_timeout(const core_state& core) noexcept;
```

The event API uses the same consensus engine as `node` and performs no I/O or FSM application. Its driver owns persistence, transport, timers, application ordering, and shutdown. Use the [callback node](node.md) for library-coordinated I/O instead.

`core_state` is an owning, noncopyable, nonmovable value. Before startup set its nonzero `id`, nonempty `address`, optional `random` seed, and settings. `state` is a standard variant of `follower_state`, `candidate_state`, and `leader_state`, initially follower. Inspect it with `std::holds_alternative`/`std::get_if`; do not manually replace active state. Term, vote, configurations, commit/log boundaries, and settings are fields of the core.

`core_event` contains monotonic `time` in ms, storage `capacity` in KiB, and variant `payload`. The capacity threshold defaults to 0; `capacity_threshold` enables admission checks. Serialize calls/completions. Do not reenter `step`, mutate the core from a tracer, or destroy it during a call.

The supported `time` upper bound is `numeric_limits<uint64_t>::max() - 2 * uint64_t(core.election_timeout)`, reserving room for the entire randomized election duration and heartbeat/quorum expiration. An event exceeding that bound returns `too_big` before changing the clock, capacity, previous update, or core state. Use the provided setters for runtime timeout changes.

The maximum `term` itself is valid for persisted state, received RPCs, and the last representable election. Starting a new election, pre-vote, or transfer election that requires a subsequent term returns `too_big` without resetting the election role, term, vote, timer, or random seed, or sending vote requests. An existing last-term election can still win through valid responses. Single-voter startup self-election does not increment the term and remains possible at the maximum. Election errors may follow replacement of the event clock, capacity, and update.

The highest actual log/snapshot index is `numeric_limits<log_index>::max() - 1`. The maximum value itself represents the next index. Startup metadata/suffixes or new submissions exceeding that bound return `too_big`; receive-side log/snapshot range validation rejects references to the maximum itself with `malformed`. RPCs ignored by earlier term/state handling may not reach that validation. When a snapshot is at the supported upper bound with an empty suffix, `start_index` may be the maximum value. These range checks precede log insertion/snapshot replacement; they do not promise rollback of all state, including the event clock or leader/term updates.

| Payload | Input |
| --- | --- |
| `start_event` | Persisted `term`, `voted_for`, optional snapshot `metadata`, log `start_index`, and `entries`. Empty state has term 0 and no vote/snapshot/entries. A bootstrap log starts with a configuration entry at index 1. Exactly one successful startup. |
| `receive_event` | Owning `rpc` with sender ID/address and fully loaded snapshot data. |
| `persisted_entries_event` | `index`: last index of a successfully persisted contiguous log prefix. |
| `persisted_snapshot_event` | Metadata of a successfully installed incoming snapshot; `offset`/`last` describe its chunk. The engine installs complete snapshots. |
| `configuration_event` | Notify application of a committed configuration at `index`, with decoded `configuration`. |
| `snapshot_event` | Notify durable publication of a local snapshot described by `metadata`, retaining `trailing` log entries. |
| `timeout_event` | Deliver expiration with the event's current time/capacity. |
| `submit_event` | Owning `entries` for commands, barriers, or encoded membership changes, with the current term. |
| `catch_up_event` | Start promotion catch-up for server `id`. |
| `transfer_event` | Request transfer to another voter `id`; ID 0 selects a target automatically. |

`snapshot_metadata` owns `index`, `term`, `configuration`, and `configuration_index`. See [cluster configuration](configuration.md) for value modification and byte format. Input events use owning values/shared payload handles. `step` borrows the event during the call and retains required data through owners. Keep the borrowed input unchanged during that call; there is no manual C-allocation cleanup.

A version-2-or-later `request_vote_result` with `pre_vote=true` may contain a proposed future term. A node that is no longer a candidate ignores this delayed response without changing or requesting persistence of its term/vote. Higher terms in actual vote responses and version-1 responses, which do not support the pre-vote indication, follow the usual term-update rules. Candidates filter and tally responses according to their current election phase.

Request transfer only as leader. ID 0 selects the first other voter, in configuration order, replicated through the current log end; otherwise it selects the last other voter inspected. No other voter returns `not_found`; specifying self, an unknown ID, or a non-voter returns `bad_id`. A nonleader or an already pending transfer returns `not_leader` before target validation. While transferring, `leader(core)` is empty and new submissions return `not_leader`.

An already caught-up target receives `TimeoutNow` immediately. A lagging target can also trigger `TimeoutNow` when a response acknowledges new partial progress. The RPC advertises the leader's full log end, and the recipient starts an election bypassing pre-vote only when that boundary matches its durable log. After transmission starts, subsequent ACKs alone do not retransmit the same transfer hint. Accepting a transfer or sending its RPC therefore does not establish that another leader was elected. The event driver must continue processing elections/state changes and deliver expiration after `election_timeout`. Expiration clears the target, allowing the caller to request transfer again. Stepping down to follower on a higher term also clears the target.

## Updates, Ownership, and Ordering

Each accepted step replaces `core.update`, an owning `core_update`. Use only fields selected by its flags, whose numeric values match C.

| Flag | Value | Driver action |
| --- | --- | --- |
| `update_current_term` | 1 | Durably record `core.current_term`, resetting the persisted vote. |
| `update_voted_for` | 2 | Durably record `core.voted_for`. |
| `update_entries` | 4 | Persist `entries.batch` at `entries.index`, ordering replacement/truncation before append. |
| `update_snapshot` | 8 | Persist incoming `snapshot.metadata`, `offset`, `chunk`, and `last`. |
| `update_messages` | 16 | Send owning RPCs in `messages` in order. |
| `update_state` | 32 | Observe the new `core.state`. |
| `update_commit_index` | 64 | Apply newly committed log entries in order through `core.commit_index`. |
| `update_timeout` | 128 | Recompute the absolute deadline with `next_timeout(core)`. |

Persist term/vote before sending dependent replies. Deliver persisted events only after successful durable writes. Apply commands in order and track the applied index in the driver; apply committed configurations and deliver `configuration_event`. Publish a local snapshot before submitting `snapshot_event`. Transport errors are not persistence acknowledgements. Snapshot-loading directives in outgoing messages require loading the durable snapshot before transport, as in the [node I/O contract](node.md).

`persisted_entries_event::index` must exceed the current `last_stored` and must not exceed `trail.last_index()`. Do not forward late completions of writes discarded by log replacement or snapshot installation unchanged. Compare the indices/terms retained by each request with the current log, report only a still-valid contiguous prefix, and omit completions of previous writes during snapshot installation. The public `node` performs this filtering. An index committed by a remote quorum can exceed the leader's own `last_stored`. A follower's successful reply cannot exceed either the prefix matched with its current leader or its durable prefix; write completion sends no successful reply when no leader is known.

During incoming snapshot installation, data from duplicate snapshots and nonempty AppendEntries with the current or a higher term is not persisted again, and no election starts. Heartbeats and term/leader updates are still processed; installation completion replies to the current leader using the current term. A sender can retry at its installation deadline while the peer remains responsive; an unresponsive peer returns to probe mode. Catch-up aborts when the duration of a round **exceeds** `max_catch_up_round_duration`. Equality alone does not trigger this condition, and callers can request catch-up again after an abort.

Move `core.update` into pending work before the next `step` if asynchronous operations need its data. Updates own message addresses, batches, snapshots, and shared payloads; they can outlive subsequent steps, compaction, and the core itself. Keep driver/core state until operations using that state complete or are canceled and cleaned up. Owning an update does not extend the core's lifetime.

Errors include `invalid`, `out_of_memory`, `too_big`, `not_leader`, and operation-specific errors. Pre-call time/start/reentrancy checks preserve the previous update; later errors may follow state mutation and do not guarantee whole-state rollback. Handle failures rather than assuming submitted work was withdrawn.

`next_timeout` returns an absolute ms timestamp in the event clock domain, combining election or heartbeat/quorum expiration; it does not schedule a timer. Before startup the default randomized duration is unsampled and the result is 0, but setting an election timeout can make it nonzero. Recompute after timeout-setting changes even without a timeout flag.

## Event-Core Queries

```cpp
node_state state(const core_state&) noexcept;
optional<role> local_role(const core_state&) noexcept;
optional<leader_info> leader(const core_state&);
error_code match_index(const core_state&, server_id, log_index&) noexcept;
error_code catch_up(const core_state&, server_id, catch_up_status&) noexcept;
server_id transferee(const core_state&) noexcept;
```

These functions inspect the core without I/O, transitions, or new events. They preserve the clock, random seed, and existing update. Read term/vote/commit/log boundaries through existing `core.current_term`, `core.voted_for`, `core.commit_index`, and `core.trail.last_index()`. Serialize queries and `step` on the same thread; do not mutate the core concurrently.

| Query | Contract |
| --- | --- |
| `state(core)` | Current follower/candidate/leader variant. A newly constructed core is follower; a valueless variant is unavailable. |
| `local_role(core)` | Role of core.id in the current configuration; empty optional when absent. |
| `leader(core)` | Value describing the follower's known leader or the leader itself. Empty for candidates, unknown leaders, and transferring leaders. |
| `match_index(core, id, index)` | Outputs that member's replication index known to the leader. |
| `catch_up(core, id, status)` | Outputs that member's none/running/aborted/finished state. |
| `transferee(core)` | Leader's transfer-target ID, or zero for a nonleader/no target. |

`leader_info`, declared in `<raftpp/types.hpp>`, contains `id` and an owning `string address`. Its result remains valid after changing core addresses/configurations or destroying the core. Copying the address in `leader` can throw allocation exceptions; other queries do not allocate.

Match/catch-up check leadership first, returning `not_leader`; a leader with an unknown configuration ID returns `bad_id`. A manually malformed core whose configuration/progress disagree returns `invalid`. All failures preserve output arguments. Progress remains queryable during transfer while state is leader, although `leader(core)` hides the leader during that transfer. Use these value results instead of reading borrowed internal representations concurrently with core mutation/destruction.

## Settings and Common Queries

```cpp
error_code set_election_timeout(core_state&, unsigned msecs) noexcept;
error_code set_heartbeat_timeout(core_state&, unsigned msecs) noexcept;
error_code set_install_snapshot_timeout(core_state&, unsigned msecs) noexcept;
```

Defaults are 1000, 100, and 30000 ms. Election must exceed heartbeat and be at most half the maximum `uint32_t`; heartbeat must be positive and below election; snapshot timeout must be positive. Invalid settings return `invalid`, preserving values/random seed. If `core.now` does not fit the clock domain above for a proposed election timeout, the setter returns `too_big`, preserving settings, random seed, and the existing deadline. The election setter resamples follower/candidate duration without resetting `election_timer_start`; leaders do not resample. Setters do not change update flags or run I/O. Set other core fields, such as `pre_vote`, catch-up limits, capacity threshold, and `force_barrier`, when driver sequencing permits.

The event API preserves literal 150/15 ms settings. The callback `node::set_election_timeout(150)` preserves the reference legacy workaround: with heartbeat 15 ms, election/heartbeat become 450/45 ms. Explicit `node_options` constructor values remain literal. Configure node heartbeat before `start()` for its backend tick interval; later changes do not restart that timer.

`state_name(node_state)` and `role_name(role)` in `<raftpp/types.hpp>` return static `string_view` values: `follower`/`candidate`/`leader` and `stand-by`/`voter`/`spare`. Unknown values and `unavailable` return an empty view, matching the reference's absent name.

`random_within_range(uint32_t& state, uint32_t minimum, uint32_t maximum) noexcept` updates the caller-owned seed and returns an inclusive value. Its precondition is `minimum <= maximum`. Identical non-singleton bounds/seeds preserve the reference sequence; singleton ranges return their value without advancing the seed. This deterministic consensus/test generator is not cryptographic.

`reference_version` is `array<unsigned, 3>{0, 22, 1}`; `reference_version_number` is 2201 (`major * 10000 + minor * 100 + release`). They identify the behavioral compatibility baseline, not the C++ library release or ABI.

## Single-Voter Example

Replace the marked point with an actual durable write before delivering its acknowledgement.

```cpp
#include <raftpp/core.hpp>

int main()
{
    namespace r = raftpp;
    r::core_state core;
    core.id = 1;
    core.address = "node-1";
    r::configuration members;
    if (members.add(1, "node-1", r::role::voter)) return 1;
    r::buffer encoded;
    if (members.encode(encoded)) return 2;
    r::start_event start;
    start.term = 1;
    start.entries.push_back({1, r::entry_type::change,
                             r::shared_buffer{std::move(encoded)}});
    if (r::step(core, {0, 65535, std::move(start)})) return 3;
    const r::array<r::byte, 1> bytes{r::byte{42}};
    r::submit_event command;
    command.entries.push_back({core.current_term, r::entry_type::command,
                               r::shared_buffer::copy(bytes)});
    if (r::step(core, {1, 65535, std::move(command)})) return 4;
    auto pending = std::move(core.update);
    if (!(pending.flags & r::update_entries)) return 5;
    // Persist pending.entries here; acknowledge only after durable success.
    const auto last = pending.entries.index + pending.entries.batch.size() - 1;
    if (r::step(core, {2, 65535, r::persisted_entries_event{last}})) return 6;
    return core.commit_index == last ? 0 : 7;
}
```
