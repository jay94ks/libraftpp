# mvfst Module API

[한국어](../../ko/modules/mvfst.md) · [Modules](index.md)

`mvfst_stream final : byte_stream` exposes an established bidirectional QUIC stream as a core byte stream. Select `RAFTPP_MODULE_MVFST=ON`, link `raftpp::mvfst`, and include `<raftpp/mvfst/mvfst.hpp>`. The native dependency target is `mvfst::mvfst_transport`; see [dependencies](../dependencies.md) for SDK/Folly/Fizz preparation.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS mvfst)
target_link_libraries(app PRIVATE raftpp::mvfst)
```

## Construction and Ownership

```cpp
mvfst_stream(shared<quic::QuicSocketLite> connection, quic::StreamId stream);
```

Arguments are a shared owner of an established connection and a valid bidirectional stream ID. Null connection or a unidirectional ID throws `std::invalid_argument("mvfst requires a bidirectional stream and connection")`. Internal state allocation may throw. Construction does not validate actual stream existence, establish the connection, or authenticate peers.

The adapter exclusively controls this stream's read callbacks and read/write/reset/stop lifecycle. Do not install competing native callbacks or externally reset the same stream. Other streams may share the connection. Keep the connection/event base available through stream use and native cleanup. Perform all calls and destruction on the connection's event-base thread. The object is noncopyable/nonmovable.

## Public Methods

All methods below are `noexcept`. Operations return an empty `error_code` on success; `error_details()` returns `error_info`.

| Method | Inputs/results/callback contract |
| --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | Requires a callback and positive read size. Uses `setReadCallback` for initial subscription, `resumeRead` for resume. Empty callback/size 0: `invalid`; active subscription: `busy`; closed: `shutdown`. |
| `read_stop()` | Removes the subscription and pauses native reads. Succeeds when unregistered or closed. |
| `write(view<const shared_buffer> buffers, completion callback = {})` | Copies buffers in order into one SDK-owned `IOBuf` and passes it to `writeChain`. Successful completion may execute inline inside submission. |
| `write(shared_buffer bytes, completion callback = {})` | Inherited single-buffer convenience overload. |
| `close(close_callback callback = {})` | Detaches the reader, resets/stops this stream, releases connection ownership, then completes. A new nonempty observer returns `busy` while one is pending. |
| `error_details() const` | Returns owning diagnostics for the latest recorded error. Native local errors use category `mvfst` in `cause`; native read errors carry their message. |

Reads copy native `IOBuf` data into owned `shared_buffer` storage that may outlive the callback/stream. EOF delivers `no_connection` with an empty buffer and removes the subscription. Native read failure also removes the subscription. Fragment sizes follow read requests/native data, not RPC boundaries.

Immediate write failure invokes no completion. Accepted write success means the SDK has taken ownership of the copy, not actual transmission completion, peer acknowledgement, or commit. Original owners may be released after successful return. SDK flow-control/native failures return `io_error` with the native cause. Aggregate-size overflow/length errors return `too_big`, allocation failure returns `out_of_memory`, and other exceptions return `io_error`. Writes on a closed stream return `shutdown`.

Close is local cleanup and does not await remote responses to native reset/stopSending. Errors from those native calls are not propagated in its return value. Close requested during a user callback defers its observer until that callback returns; otherwise completion may be inline. Destruction initiates close. Read/write/close callbacks must not throw.

## Example

```cpp
#include <raftpp/mvfst/mvfst.hpp>
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_mvfst_transport(
    raftpp::shared<quic::QuicSocketLite> connection, quic::StreamId stream_id)
{
    auto stream = std::make_shared<raftpp::mvfst_stream>(std::move(connection), stream_id);
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(stream)))
        throw raftpp::system_error(error);
    return network;
}
```

The application connector establishes QUIC/TLS, authenticates the peer, and obtains the stream ID before calling on the event-base thread. I/O/node callbacks must use the same serialized domain; a separate UV loop is not automatically bridged to the event base. Let the I/O backend own the returned transport's init/start/close lifecycle. Listeners/reconnect/durable I/O are outside this stream API's role. Also follow the [common contract](../streams.md).
