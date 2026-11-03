# TCP Transport

[한국어](../ko/tcp_transport.md) · [libuv I/O](uv_io.md) · [Node API](node.md) · [Owned Streams](uv_stream.md)

`tcp_transport` in `<raftpp/uv/tcp_transport.hpp>` implements the message-level `transport` interface with libuv IPv4 TCP sockets. It is declared and built only with `RAFTPP_WITH_UV=ON`, independently of LZ4 and fixture options. Link `raftpp::raftpp`; it propagates the libuv usage requirements.

## Connecting to a Node

```cpp
#include <raftpp.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_backend(
    uv_loop_t& loop, raftpp::fs::path directory)
{
    raftpp::tcp_transport_options options;
    options.connect_retry = 100;
    auto network = std::make_shared<raftpp::tcp_transport>(
        loop, std::move(options));
    return std::make_shared<raftpp::uv_io>(
        loop, std::move(directory), std::move(network));
}
```

Pass the resulting backend to `node` along with its ID, advertised address, and FSM. The node and `uv_io` initialize, start, and close the transport. The directory must already exist. Run node/backend methods and all callbacks on the same loop thread, and keep the loop running until close completes. Construction may throw standard allocation exceptions. `tcp_transport` is neither copyable nor movable; its destructor initiates asynchronous close.

On Linux, configure the application to handle or ignore `SIGPIPE` before running the loop, for example `std::signal(SIGPIPE, SIG_IGN)` from `<csignal>` during process startup. Like the C libuv backend, a write to a disconnected peer can raise this signal. The library does not change process-wide signal handlers.

## Options and Addresses

| `tcp_transport_options` field | Default | Meaning |
| --- | --- | --- |
| `bind_address` | Empty | Use the advertised address for listening. A nonempty override changes only the bind endpoint. |
| `connect_retry` | 1000 ms | Delay between failed connection attempts. Zero is permitted. |
| `max_handshake_address` | 2048 bytes | Bound on the advertised address including its terminating NUL and padding to eight bytes, for local initialization and incoming handshakes. |
| `limits` | `encoding_limits{}` | Header, payload, and entry-count limits for outgoing encoding and incoming decoding. See [encoding](encoding.md). |
| `tracing` | Default stderr tracer | Connection/message/retry diagnostics. Change with `set_tracer(tracer)`; see [tracing](tracing.md). |

Addresses use `host:service`. Missing or empty service uses `8080`. Hostnames resolve to IPv4; IPv6 literal syntax is unsupported. Listening requires a numeric service and resolves the host synchronously. An empty bind host listens on all IPv4 interfaces. Outgoing resolution is asynchronous and permits named services. Host and service lengths must fit the platform's `NI_MAXHOST` and `NI_MAXSERV` buffers including NUL. Embedded NULs return `invalid`; excessive component lengths return `name_too_long`.

An advertised address must be reachable by peers; binding to a wildcard address or port zero does not rewrite it. For example, advertise `node.example:8080` and set `bind_address` to `0.0.0.0:8080` to listen on every interface. Bind overrides never change the identity advertised in handshakes.

Listening binds each distinct resolved IPv4 endpoint, skipping repeated IP/port pairs. A bind/listen failure at any endpoint fails startup and asynchronously closes listeners already opened. Error return does not itself mean cleanup has completed: run the loop through `close` completion before reusing the ports/loop. Outgoing connections attempt the address list in order and advance to the next address after asynchronous connection failure.

## Changing Settings

```cpp
error_code set_bind_address(string_view address) noexcept;
void set_connect_retry_delay(unsigned msecs) noexcept;
```

Call `set_bind_address` before `init`, including before constructing a node that initializes this transport. It replaces an owning copy of the bind override; an empty value restores the advertised address. The address syntax/length rules above apply. Allocation failure returns `out_of_memory`, excessive length `name_too_long`/`too_big`, and embedded NUL `invalid`, preserving the previous setting. Initialization freezes the setting (`busy`); shutdown returns `shutdown`. Bind/listen-time resolution errors remain startup errors. Changing the bind override never changes the advertised identity.

`set_connect_retry_delay` is also available through `transport` and `uv_io`. It updates scheduling for future connection retries, including existing peers whose next failure has not yet scheduled a timer. Already-armed timers keep their deadline. Zero is permitted; shutdown ignores the change. Setters run on the owning event-loop thread and do not reconnect existing healthy sockets.

## Direct Transport Methods

| Method | Contract |
| --- | --- |
| `init(id, address)` | Copy the local identity and prepare its handshake. Zero ID returns `bad_id`; a successful initialization cannot be repeated (`busy`). Size-limit violations return `too_big`. Validation/allocation failure preserves uninitialized state and can be retried. |
| `start(receive)` | Bind/listen and subscribe to owning received messages. Requires successful initialization and a nonempty callback; otherwise `invalid`. Repeating a successful start returns `busy`. Startup failure initiates shutdown; call `close` to observe cleanup. |
| `send(shared<const message>, completion)` | Require successful start. The message's ID/address identify the destination. Null message or zero destination ID returns `bad_id`. Encode before acceptance; retain the message and encoded buffers through completion. |
| `close(callback = {})` | Stop receives, cancel queued sends, and drain DNS/connect/write requests and TCP/timer handles. Completion follows request completion and release of library-held messages and receive captures. Valid before initialization and after failed start. |
| `last_error()` | Last recorded asynchronous transport/peer error. Transient connection failures and malformed incoming peers can set it; it is not a fatal-node status or a substitute for send results. |
| `error_details() const noexcept` | Owning diagnostic of the most recently observed failure, with operation/peer ID/address/available native cause; allocates nothing. Distinct from return/completion codes; copy at callback entry. See [error contracts](errors.md). |

An immediate `send` error means no acceptance and no completion callback. Accepted sends complete exactly once, possibly during a reentrant call. Do not mutate messages or their payloads before completion. Successful completion means that the socket write completed; it does not prove remote receipt, persistence, or Raft commitment.

Received messages carry the sender ID/address from the connection handshake, not the local destination. Entry/snapshot payloads remain valid when retained after the callback and after connection/transport closure. The sender identity is peer-supplied; the protocol provides no authentication or encryption.

## Connections, Failures, and Shutdown

Connections are unidirectional for RPC traffic. Each outgoing connection sends the cowsql/raft protocol-1 handshake: little-endian 64-bit protocol version, sender ID, padded address length, and NUL-terminated address with zero padding. RPC framing uses the existing [message format](encoding.md). Incoming handshakes require a positive ID, protocol 1, a nonzero length divisible by eight within the limit, and a NUL-terminated address with zero padding. Invalid/truncated streams close independently; other peers and listeners continue.

Outgoing clients are keyed by destination ID. A changed address retires the old client and cancels its queued sends. Resolution tries IPv4 results in order, closing failed sockets before the next address. Failed connection/handshake attempts retry. After an asynchronous failed attempt, keep the newest three queued messages and complete older messages with `no_connection`. An immediate resolution-submission failure schedules a retry without eviction. This is a failed-connect queue policy, not a fixed limit on healthy in-flight writes.

An immediate RPC write failure on a ready connection returns `io_error` without completion. If a previously queued RPC cannot be submitted after connection establishment, complete it with `io_error` and continue with the remaining queue. Asynchronous RPC write failure completes with `io_error` and reconnects after closing the old socket. An operation canceled by native shutdown completes with `canceled`; already completed writes can still report success. Accepted sends waiting for a connection are canceled during shutdown.

Calling close or destroying the last facade owner from a receive/send callback is permitted. The active callback and internal native state survive until they return and drain. New sends during shutdown return `shutdown`. Register one outstanding close callback; repeating close without a callback is permitted, and a callback supplied after shutdown completes runs inline. Exceptions from receive/send completion callbacks initiate shutdown and set `invalid`. Exceptions never cross native callback boundaries; a close-callback exception is recorded as `invalid`.
