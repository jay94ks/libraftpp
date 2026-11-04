# Message Transport over Stream Plugins

[한국어](../ko/stream_transport.md) · [Owned Streams](uv_stream.md) · [libuv I/O](uv_io.md) · [Encoding](encoding.md) · [Module APIs](modules/index.md)

`stream_transport` in `<raftpp/uv/stream_transport.hpp>` implements `transport` over a caller-selected `shared<uv_stream_transport>`. It frames complete Raft messages, owns pending writes, and reconnects using the raw connector. `RAFTPP_WITH_UV=ON` is required; LZ4 and fixture are independent. Link only `raftpp::raftpp`. Calls and callbacks run serially on the supplied libuv loop; keep the loop alive through close completion.

## Construction and Integration

```cpp
#include <raftpp/uv/stream_transport.hpp>
#include <raftpp/uv/uv_io.hpp>

raftpp::shared<raftpp::uv_io> make_stream_backend(
    uv_loop_s& loop,
    raftpp::fs::path directory,
    raftpp::shared<raftpp::uv_stream_transport> connector)
{
    auto messages = std::make_shared<raftpp::stream_transport>(loop, std::move(connector));
    return std::make_shared<raftpp::uv_io>(loop, std::move(directory), std::move(messages));
}
```

The connector may be `tcp_stream_transport` or an application implementation supplying initialized TCP/pipe/TTY streams through `uv_stream`. `stream_transport` exclusively controls its connector's init/listen/connect/close lifecycle. Do not initialize, listen on, or close that connector separately, or share one connector between adapters. Retaining an additional shared pointer allows diagnostics and implementation-specific configuration before initialization; it does not grant an independent lifecycle. Handed-off native streams and the adapter must use the supplied loop.

A null connector throws `invalid_argument`. The constructor may throw on allocation failure. `stream_transport_options` has:

| Member | Default | Meaning |
| --- | --- | --- |
| `connect_retry` | `1000` | Milliseconds before retrying a failed connection. |
| `read_buffer_size` | `65536` | Positive read-chunk buffer size, at most the maximum unsigned integer; an invalid value throws `invalid_argument`. |
| `limits` | `encoding_limits{}` | Maximum decoded header, payload, and entry count; also applied during encoding. |
| `tracing` | Empty tracer | Optional received-RPC tracing. |

## Lifecycle and Completion

`init(id, address)` delegates identity/address setup to the connector. Zero IDs are rejected. Call it once; a connector-init failure still requires close and cannot be retried on the same adapter. `start(receive)` registers a listener and starts complete-message delivery. Initialization and a nonempty callback are required; a repeated successful start returns `busy`. A listen/setup failure initiates shutdown. `uv_io` calls these operations as part of its normal node lifecycle.

`send(shared<const message>, completion)` requires a started adapter and a nonzero destination ID. It owns the message through completion and encodes before submitting native writes. After an immediate error, completion is not invoked. An accepted request completes exactly once, including failure/cancellation. Keep message contents immutable while sending. Successful completion means bytes were written, not that the remote Raft node committed a command.

Each destination ID/address has an outgoing connection. Address changes retire the old connection and cancel its queued requests. Connect submission failures schedule a retry without evicting requests. An accepted failed connection evicts the oldest queued requests with `no_connection` until only the newest three remain, then retries. `set_connect_retry_delay(unsigned)` affects future retry scheduling without rearming an active timer. Asynchronous write failures report `io_error`, close the failed stream, and reconnect after native close; canceled writes during retirement report `canceled`. Immediate native-write rejection reports `io_error` without invoking completion; a write rejected while flushing an already accepted queue completes that request with `io_error`.

`close(callback)` stops delivery, cancels queued writes, closes accepted streams/timers, and closes the connector. Pending connects and writes drain before the close callback. Failed incoming admission also retains its stream until the native deleter runs. Register at most one nonempty adapter-close callback; repeated calls are idempotent, and an already-closed adapter invokes its callback inline. Destruction initiates asynchronous shutdown; it does not synchronously drive the loop. Internal state survives wrapper destruction until cleanup completes. The adapter uses the stream's first, allocation-free observer slot and reference callables to initiate close without allocating callback storage.

## Framing, Plugins, and Errors

Incoming chunks may split or combine RPCs arbitrarily. The adapter decodes the 16-byte preamble, bounded header, and bounded payload using the public [encoding](encoding.md) contracts. Received messages contain the owned ID/address supplied by the connector. Malformed or excessive frames and native read errors close that incoming stream. A throwing application receive/send callback is contained and initiates adapter shutdown. Native write/read causes and peer context are available as owning `error_info` from `error_details()` before a matching failure return or completion; copy the value at callback entry.

A connector must hand off a nonblocking stream that preserves byte order and contents. When supplying a TTY, prepare an appropriate raw mode and libuv nonblocking writes (for example `uv_stream_set_blocking(handle, 0)`) before adoption. The adapter transmits binary preambles, headers, and payloads; it does not compensate for terminal line handling or character conversion.

A connector must invoke each accepted connect callback exactly once and complete pending connections before its close callback. It may complete a connect or accept inline; the adapter preserves state during reentrant callbacks. Immediate connect errors invoke no completion. Successful handoffs must provide a live, nonclosing stream without outstanding external native operations or existing close observers. Transfer only streams whose deleters retain any required allocator/native owner independently of the connector. Closing the connector alone must not free handed-off streams. Connector methods and callbacks use the same serialized loop, and close must not throw. See [raw connection contracts](uv_stream.md) for native-handle ownership.
