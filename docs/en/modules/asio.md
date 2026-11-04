# Asio Module API

[한국어](../../ko/modules/asio.md) · [Modules](index.md)

`RAFTPP_MODULE_ASIO=ON` provides `raftpp::asio` and `<raftpp/asio/asio.hpp>`. Uses standalone Asio; the target propagates core, Threads, and `ASIO_STANDALONE` usage requirements. Do not pass Boost.Asio types.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio)
target_link_libraries(app PRIVATE raftpp::asio)
```

## Executor

```cpp
explicit asio_executor(asio::any_io_executor executor);
error_code post(function<void()> callback) noexcept override;
```

Stores the executor value and uses `asio::post` for one later callback invocation. Empty callbacks return `invalid`, submission allocation failure returns `out_of_memory`, and other submission exceptions return `io_error`. Immediate failure executes no callback. Accepted work belongs to the native queue and is not canceled by adapter destruction. Holding the executor value does not extend the `io_context` lifetime: the caller must retain the context and drive its queue. Callbacks must not throw.

```cpp
#include <raftpp/asio/asio.hpp>
#include <asio/io_context.hpp>

int main()
{
    asio::io_context context;
    raftpp::asio_executor scheduler{context.get_executor()};
    int count = 0;
    if (auto error = scheduler.post([&count] { ++count; }))
        return 1;
    context.run();
    return count == 1 ? 0 : 1;
}
```

## TCP Stream Construction and Ownership

```cpp
explicit asio_stream(asio::ip::tcp::socket socket);
```

Pass an already connected socket using `std::move`. The by-value argument consumes the socket, which is not restored to the caller even if construction fails. A closed socket throws `std::invalid_argument("closed asio socket")`; state allocation failure throws an allocation exception. Checking that the socket is open does not establish connectivity; connection setup is the caller's responsibility. `asio_stream` is noncopyable/nonmovable; use `shared<asio_stream>` when shared ownership is needed.

Run every method, destruction, and callback in the same serialized domain of the socket's executor. Drive the context on one thread, or construct the socket with a strand executor and submit calls on that strand too. The adapter creates no strand itself. It owns the native socket; do not perform competing native read/write/close operations.

## Stream Methods

All methods below are `noexcept`. Operations return an empty `error_code` on success; `error_details()` returns `error_info`. Default `buffer_size` is 65536.

| Method | Inputs/behavior | Main failures |
| --- | --- | --- |
| `read_start(stream_read_callback callback, size_t buffer_size = 65536)` | Starts reading. Successful data is owned `shared_buffer` storage. Resume may deliver buffered data inline. | Empty callback/size 0: `invalid`; active subscription: `busy`; closing/after a terminal read failure: `shutdown` |
| `read_stop()` | Removes the subscription and cancels only that read. Writes continue; already consumed native bytes remain buffered until resume. | Normal stop/closed state succeeds |
| `write(view<const shared_buffer> buffers, completion callback = {})` | Writes all buffers in array order, retaining their owners through completion. Empty writes are allowed. | Another active write: `busy`; closing: `shutdown` |
| `write(shared_buffer bytes, completion callback = {})` | Inherited single-buffer convenience overload with the same contract. | Same as array write |
| `close(close_callback callback = {})` | Stops reads, closes the native socket, drains/cancels accepted work, then completes. | A new nonempty observer returns `busy` while a close observer is pending |
| `error_details() const` | Returns owning diagnostics for the most recently recorded native error. | Not every returned error updates separate diagnostics. |

Allocation failure can return `out_of_memory`; native/other submission failures can return `io_error`. Native EOF/reset/broken pipe maps to `no_connection`; operation_aborted maps to `canceled`. Recorded diagnostics retain the native cause in `error_info::cause`. Immediate write failure invokes no completion; accepted writes complete exactly once. The input array may be released after submission, but do not mutate its data. Completion means local Asio write completion, not remote acknowledgement/commit.

A read error ends that subscription. Fragments do not follow write/RPC boundaries. Close/destruction requested from read/write callbacks retains internal state and defers close completion until the active callback returns. Close callbacks may run inline when no work is pending or the stream is already closed. Destruction initiates close without waiting. All user callbacks must not throw.

## Raft Transport Composition

```cpp
#include <raftpp/asio/asio.hpp>
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_asio_transport(
    asio::ip::tcp::socket connected_socket)
{
    auto stream = std::make_shared<raftpp::asio_stream>(std::move(connected_socket));
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(stream)))
        throw raftpp::system_error(error);
    return network;
}
```

Complete connection/peer authentication and match the socket's execution domain to node I/O. Let the I/O backend own the returned transport's init/start/close lifecycle. There is no automatic bridge connecting a separate context directly to UV I/O. This module provides neither a separate durable I/O backend nor a TCP listener API. Also follow the [common stream contract](../streams.md).
