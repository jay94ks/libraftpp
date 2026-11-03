# Owned libuv Streams and Connection Transports

[한국어](../ko/uv_stream.md) · [TCP Message Transport](tcp_transport.md) · [libuv I/O](uv_io.md) · [Encoding](encoding.md) · [Message Adapter](stream_transport.md)

Include `<raftpp/uv/uv_stream.hpp>` and `<raftpp/uv/uv_stream_transport.hpp>`, also available through `<raftpp.hpp>`. These APIs require `RAFTPP_WITH_UV=ON`; fixture and LZ4 are independent. Calls and callbacks run serially on the owning libuv loop, which must remain alive through every close completion.

## Stream Ownership

`uv_stream` is a move-only owner of one initialized native TCP, pipe, or TTY stream. Moving its C++ value preserves the native handle's address. Default construction creates an empty owner. `operator bool` indicates an owned stream whose close has not begun; it does not establish connection health or readability.

```cpp
uv_stream(uv_stream_s& stream, stream_deleter release);
borrowed<uv_stream_s> native_handle() const noexcept;
error_code read_start(stream_read_callback callback, size_t buffer_size = 65536) noexcept;
error_code read_stop() noexcept;
error_code write(view<const shared_buffer> buffers, completion callback = {}) noexcept;
error_code write(shared_buffer bytes, completion callback = {}) noexcept;
error_code close(close_callback callback = {}) noexcept;
error_info error_details() const noexcept;
```

Adoption requires an initialized, inactive, nonclosing native stream and a nonempty deleter. Ownership transfers only when construction succeeds; on an exception the caller still owns the native allocation. While owned, native `data`, reading, writing, and closing are controlled by `uv_stream`; callers may use the borrowed handle for compatible native configuration/inspection (including TTY window-size queries) but must not replace `data`, move/free the handle, or independently call native read/write/close operations. The original `data` is restored before the deleter runs after native close. The deleter releases the original allocation and must not throw. A plugin must capture any allocator/owner needed for that release rather than borrow connector state that may already have been destroyed.

`native_handle()` is borrowed and becomes null after close completes. Destruction or move-assignment over an existing owner initiates asynchronous close. Internal ownership retains native memory and request buffers until libuv completes/cancels the operations and closes the handle. Driving the loop remains the caller's responsibility.

## Reads and Writes

`stream_read_callback` is `function<void(error_code, shared_buffer)>`. Each successful read supplies an independently owned chunk; chunks may be fragmented/coalesced and remain valid after the callback or stream ends. The read buffer size must be positive and at most the maximum unsigned integer. A second active subscription returns `busy`; `read_stop()` removes the current subscription. EOF is reported as `no_connection` with its native libuv cause. A read error stops the subscription, and restarting after such an error is rejected; the writable side can still be used where the native stream supports it. Closing stops notifications.

TTY bytes are affected by the native terminal mode selected by the caller. Adopting or moving a `uv_stream` neither selects a mode nor adds text conversion. Configure an appropriate raw mode before adoption to transmit binary data containing NUL, CR, and LF unchanged. Native mode/window-size configuration remains the caller's responsibility; the rule against independently performing native read/write/close operations while owned still applies.

For a unidirectional native pipe, use only the direction supported by that endpoint. Submitting a write to the read-only end returns `io_error` immediately without invoking completion. `error_details()` preserves the cause reported by libuv. Pipe EOF follows the ordinary stream read-termination contract, and its callback may call `close()`.

Writes retain shared owners for all nonempty buffers through completion. At least one buffer must be nonempty; counts and individual sizes must fit libuv's unsigned limits. Success means the operation was accepted and its callback runs exactly once, including cancellation during close. An immediate error invokes no completion. The input array itself need only survive the call. Buffer contents must remain immutable while writing.

`close()` stops reads, cancels/drains accepted writes, calls the native allocation deleter, and then invokes registered close callbacks. Multiple observers are allowed. Storing the first nonempty observer requires no additional allocation beyond constructing the supplied callable. Registering later observers may allocate; failure returns `out_of_memory` without registering that observer and leaves the ongoing close intact. No-argument close also requires no observer allocation. Empty/already-closed streams invoke close callbacks inline. Never synchronously wait inside a loop callback for the loop to complete close.

Callback exceptions are contained at native boundaries. A throwing read/write completion records `invalid` and initiates close. Diagnostics own their messages and preserve native causes; querying them does not extend a borrowed native handle's lifetime.

## Raw Connection Interface

`uv_stream_transport` is the connection-level plugin interface:

```cpp
error_code init(server_id id, string_view address);
error_code listen(stream_accept_callback callback);
error_code connect(server_id id, string_view address, stream_connect_callback callback);
void close(close_callback callback = {}) noexcept;
```

`stream_accept_callback` is `function<void(server_id, string, uv_stream)>`. Identity/address and stream are handed to the application as owned values. `stream_connect_callback` is `function<void(error_code, uv_stream)>`; success carries a stream, failure an empty owner. Immediate connect rejection invokes no callback; accepted requests complete once. Closing stops accepts, cancels pending connection establishment, and completes those requests before the connector-close callback. Register at most one nonempty connector-close observer. Handed-off streams are independent and must be closed by their owners; connector close does not wait for them.

`tcp_stream_transport(loop, tcp_transport_options)` implements IPv4 DNS resolution, multiple address attempts, and cowsql/raft's version-1 little-endian identity/address handshake. It shares TCP establishment machinery with `tcp_transport`. Call `init` once before listen/connect; connect does not require listening. A connect attempts the resolved addresses once and returns `no_connection` on asynchronous failure; it does not keep retrying indefinitely. The target ID is diagnostic metadata, not remote identity authentication. Native refusal/resolution/handshake errors are available from `error_details()` before completion.

`set_bind_address` must precede `init`. Default address ports, optional bind policy, and `max_handshake_address` follow [TCP message transport](tcp_transport.md). Incoming handshakes must include a nonzero sender ID, valid padded address length, terminating NUL, and zero padding. Closing cancels unfinished handshakes and DNS/connect/write requests. Already handed-off streams can remain usable after the connector object itself is destroyed.

`uv_io` takes a `shared<transport>` for complete Raft messages. Wrap a raw connector in [stream_transport](stream_transport.md) to supply framing, queues, and retries to that backend. Native stream chunks have no implicit RPC boundaries.

## Retaining an Accepted Connection

This function retains the most recently accepted connection in caller-owned shared state. The callback's captures survive the function call. Keep the returned-state owner while the connection is needed and drive the loop through shutdown.

```cpp
#include <raftpp/uv/uv_stream_transport.hpp>
#include <cstdio>

raftpp::error_code listen_for_one(
    raftpp::tcp_stream_transport& listener,
    raftpp::shared<raftpp::optional<raftpp::uv_stream>> accepted)
{
    namespace r = raftpp;
    if (auto error = listener.init(1, "127.0.0.1:8080")) return error;
    return listener.listen([accepted = std::move(accepted)](
        r::server_id, r::string, r::uv_stream stream) {
        accepted->emplace(std::move(stream));
        auto error = (*accepted)->read_start([](r::error_code status, r::shared_buffer data) {
            if (status) return;
            std::fwrite(data.bytes().data(), 1, data.size(), stdout);
        });
        if (error) (void)(*accepted)->close();
    });
}
```

Native lifetime rules follow libuv's [handle](https://docs.libuv.org/en/v1.x/handle.html) and [stream](https://docs.libuv.org/en/v1.x/stream.html) contracts.
