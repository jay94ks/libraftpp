# MsQuic Module API

[한국어](../../ko/modules/msquic.md) · [Modules](index.md)

`RAFTPP_MODULE_MSQUIC=ON` provides `raftpp::msquic` and `<raftpp/msquic/msquic.hpp>`. Links core and native target `msquic::msquic`. See [dependencies](../dependencies.md) for SDK preparation/TLS build selection.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS msquic)
target_link_libraries(app PRIVATE raftpp::msquic)
```

## API Table Ownership

```cpp
shared<const QUIC_API_TABLE> open_msquic_api();
```

Opens the API table using `MsQuicOpen2` and calls `MsQuicClose` when the last shared owner is released. Failure throws `std::system_error` whose code uses category `msquic`. Shared-owner construction may throw an allocation exception. Do not separately call `MsQuicClose` on the returned table.

## Adopting an Established Stream

```cpp
msquic_stream(shared<const QUIC_API_TABLE> api, HQUIC stream,
              shared<void> connection_owner);
```

All arguments must be valid, and `stream` must be an established bidirectional stream on a connected session. Null arguments throw `std::invalid_argument("msquic requires API, stream and connection ownership")`. The constructor does not validate direction, complete connection establishment, or authenticate peers. `connection_owner` is an application owner retaining the native connection, registration, and any required configuration. Wrapping a borrowed pointer in a shared value does not meet that requirement.

Construction disables receive before installing the adapter's native callback. Receive-disable failure throws `std::system_error`. On failure, the caller retains native stream ownership and must allow for possible changes to native receive settings during cleanup. After success, the adapter exclusively owns the native stream handle/callback. Do not externally call `StreamClose` or replace callbacks. The API table and connection owner are retained through native cleanup. The object is noncopyable/nonmovable.

Perform every method and destruction in the connection's serialized MsQuic callback domain. Different connections are not guaranteed to share a worker thread. The adapter does not transfer callbacks onto an arbitrary application loop.

## Public Methods

All methods below are `noexcept`. Operations return an empty `error_code` on success; `error_details()` returns `error_info`.

| Method | Inputs/results/callback contract |
| --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | Enables receives. Empty callback/size 0: `invalid`; active subscription: `busy`; closing/closed: `shutdown`. Size is only checked for positivity; it does not bound native RECEIVE events or allocations. |
| `read_stop()` | Removes the subscription and disables native receive. Already closed succeeds. |
| `write(view<const shared_buffer> buffers, completion callback = {})` | Retains owners/QUIC_BUFFER arrays through SEND_COMPLETE. Buffer count and each length must fit `uint32_t`, otherwise `too_big`. Empty writes are allowed. Closing/closed returns `shutdown`. |
| `write(shared_buffer bytes, completion callback = {})` | Inherited single-buffer convenience overload. |
| `close(close_callback callback = {})` | Stops reading and submits native abort shutdown. A new nonempty observer returns `busy` while one is pending. |
| `error_details() const` | Latest recorded owning diagnostics. Native status is preserved in `cause` under category `msquic`. |

Native OOM returns `out_of_memory`; other native failures return `io_error`. Write request/buffer-array allocation failure returns `out_of_memory`, length errors return `too_big`. Immediate write failure invokes no callback. Accepted writes complete once on SEND_COMPLETE; native Canceled maps to `errc::canceled`. The input array may be released after submission, but shared data must remain unmodified until completion. Success is local SDK completion, not remote application acknowledgement/commit.

RECEIVE data is copied into owned `shared_buffer` storage before returning to the SDK. Successful buffers may outlive the callback. Peer send shutdown/abort or connection shutdown delivers `no_connection` and an empty buffer to the reader. Receive allocation failure reports `out_of_memory`. Fragments do not follow write/RPC boundaries.

Close waits for all SEND_COMPLETE events, SHUTDOWN_COMPLETE, and completion of active user callbacks/nested native-event handling before calling `StreamClose`, releasing the connection owner, and invoking the close observer. Already closed completes inline. If initial shutdown submission reports OOM, it tries allocation-free inline abort on the same native worker. Final submission failure clears the close observer and returns an error, allowing the caller to retain the execution domain and retry close. Destruction attempts close without waiting. Use explicit close/completion when shutdown errors must be handled. All user callbacks must not throw.

## Example

```cpp
#include <raftpp/msquic/msquic.hpp>

raftpp::shared<raftpp::byte_stream> adopt_msquic_stream(
    raftpp::shared<const QUIC_API_TABLE> api, HQUIC connected_stream,
    raftpp::shared<void> connection_owner)
{
    return std::make_shared<raftpp::msquic_stream>(
        std::move(api), connected_stream, std::move(connection_owner));
}
```

Obtain `auto api = raftpp::open_msquic_api();` in the application connector and use that same table to create native connections/streams. Prepare an authenticated connection/registration owner and an established bidirectional stream, then invoke the helper in that connection's callback domain. Keep caller-side API/connection owner copies until successful adoption or failed-construction cleanup finishes. Clean up the native stream on construction failure; after success, let the adapter close it. Composing with [core `framed_transport`](core.md) requires the same serialized domain as I/O/node callbacks. The connector/backend handles listeners/TLS configuration/reconnect/separate durable I/O.
