# UV Module API

[한국어](../../ko/modules/uv.md) · [Modules](index.md) · [I/O composition](../composition.md)

`RAFTPP_WITH_UV=ON` provides `raftpp::uv` and `<raftpp/uv/uv.hpp>`. Requires Linux, libuv 1.18.0 or newer, and the necessary Linux headers. Core is linked transitively.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS uv)
target_link_libraries(app PRIVATE raftpp::uv)
```

## APIs by Type

| Public header | Types/role | Detailed contract |
| --- | --- | --- |
| `uv_io.hpp` | `uv_io : io`, `uv_io_options`; durable files/logs/snapshots/timers | [I/O API](../uv_io.md) |
| `tcp_transport.hpp` | `tcp_transport : transport`, `tcp_transport_options`; IPv4 TCP/RPC | [TCP API](../tcp_transport.md) |
| `uv_stream.hpp` | `uv_stream : byte_stream`, `stream_deleter`; native TCP/pipe/TTY handle ownership | [Stream API](../uv_stream.md) |
| `uv_stream_transport.hpp` | `uv_stream_transport`, `tcp_stream_transport`, accept/connect callbacks | [Connection API](../uv_stream.md) |
| `stream_transport.hpp` | `stream_transport : transport`, `stream_transport_options`; framing/queues/retries over raw connectors | [Message adapter](../stream_transport.md) |
| `uv_storage.hpp` | `uv_storage : storage`; composable durable file storage | [I/O composition](../composition.md) |
| `uv_timer.hpp` | `uv_timer : timer`; independent periodic timer/clock | [I/O composition](../composition.md) |
| `compression.hpp` | `is_compressed`, `decompress`; LZ4 snapshot detection/reading | Compression helpers below |

All headers are under `<raftpp/uv/...>`. `uv_stream` is move-only and default-constructs empty. The other concrete I/O/transport objects above are noncopyable. `uv_stream_transport` is an abstract interface for application connectors.

## Ownership and Shutdown

`uv_io` and TCP/stream transports borrow the libuv loop. It must remain alive and running through every close completion. `uv_io` shares the supplied `shared<transport>`; `stream_transport` exclusively controls the supplied connector's init/listen/connect/close lifecycle. All calls and callbacks execute serially on the same loop.

`uv_stream` adopts an initialized inactive native stream and release function after successful construction. The caller retains native ownership on failure. The library controls native `data` and read/write/close. `native_handle()` is borrowed and must not be used for competing read/write/close or memory release. After native close, the original `data` is restored before invoking the deleter.

Accepted writes/disk requests retain data through completion. Immediate submission failure invokes no corresponding completion. Close/destruction initiates asynchronous cleanup; never block the loop waiting for it. `uv_stream::close` supports multiple observers, with additional observer registration potentially allocating. I/O/connectors/message transports follow their detailed single-observer contracts. Write tick/receive/completion/deleter callbacks so they do not throw.

## Compression Helpers

```cpp
bool is_compressed(view<const byte> input) noexcept;
error_code decompress(view<const byte> input, buffer& output,
                      size_t max_size = numeric_limits<size_t>::max()) noexcept;
```

`is_compressed` checks only the first four bytes `04 22 4d 18`, not frame validity. Detection remains available with LZ4 disabled.

`decompress` synchronously restores the first LZ4 frame's payload. `max_size` bounds decoded bytes. Excess expansion returns `too_big`; invalid/truncated input returns `io_error`; allocation failure returns `out_of_memory`. Failure preserves existing output. Success does not reject bytes following the first frame and does not guarantee preservation of the output allocator. Use the [LZ4 codec](lz4.md) when exact single-frame decoding and allocator preservation are required.

With `RAFTPP_WITH_LZ4=OFF`, `decompress` returns `invalid` for any input. Enabling it requires UV and liblz4 1.7.1 or newer. Separate `RAFTPP_MODULE_LZ4` selects a codec module usable without UV. With both options ON, UV uses that module's LZ4 link dependency. `uv_io::set_snapshot_compression(bool)` is a successful no-op and does not enable compression on writes.

## Example

```cpp
#include <raftpp/uv/uv.hpp>
#include <uv.h>

raftpp::shared<raftpp::uv_io> make_backend(uv_loop_t& loop,
                                        raftpp::fs::path directory)
{
    auto network = std::make_shared<raftpp::tcp_transport>(loop);
    return std::make_shared<raftpp::uv_io>(loop, std::move(directory),
                                         std::move(network));
}
```

Create a readable/writable directory beforehand. Pass the result to a node as `shared<io>` so the node controls initialization/load/start/close. Retain the loop through node shutdown. Constructors may throw allocation exceptions. See [I/O](../uv_io.md), [TCP](../tcp_transport.md), and [errors](../errors.md) for configuration/native failure codes and `error_details()`.

## Composable Storage and Timer

`uv_storage final : storage` in `<raftpp/uv/uv_storage.hpp>` provides existing disk operations, log barriers, and snapshot publication/recovery without networking or a node periodic timer.

```cpp
uv_storage(uv_loop_s& loop, fs::path directory, uv_io_options options = {});
```

`init`, `bootstrap`, `recover`, `load`, `set_term`, `set_vote`, `truncate`, `append`, `snapshot_put`, `snapshot_get`, `capacity`, `error_details`, and `close` implement the [storage contract](../composition.md), preserving [existing I/O](../uv_io.md) disk options/formats/native errors. Reuses `uv_io_options` for segment/block size, disk retries, auto recovery, snapshot-read bounds, and tracing. Methods preserve the return-value/callback model. Noncopyable/nonmovable; borrows the loop and requires calls on its loop thread. Construction may throw allocation exceptions; create the directory before init. File operations may run on libuv workers, with completions returning to the owning loop. Close drains accepted storage work/native cleanup.

`uv_timer final : timer` in `<raftpp/uv/uv_timer.hpp>` provides an independent node timer.

```cpp
explicit uv_timer(uv_loop_s& loop);
error_code start(unsigned interval, completion tick) noexcept override;
void close(close_callback callback = {}) noexcept override;
uint64_t time() const noexcept override;
error_info error_details() const noexcept override;
```

Borrows the loop and initializes a native timer during construction. Allocation/native initialization failure throws. Requires positive interval/nonempty tick, otherwise `invalid`; repeated start returns `busy`; start after closing returns `shutdown`. Callback-owner allocation failure returns `out_of_memory`; native start failure returns `io_error`; failed submission invokes no tick. Normal ticks carry an empty error code. `time()` is libuv's cached monotonic millisecond time, updated while driving the loop.

Noncopyable/nonmovable. Methods/destruction/callbacks use the loop thread. Close waits for native close/active ticks before invoking the first nonempty observer; already closed completes inline. Destruction initiates asynchronous close. Callback exceptions record diagnostics and initiate shutdown. Release the loop after both storage and timer close.

Pass both objects to `make_io` to form a node backend with user-selected transport/snapshot codec. Existing `uv_io` construction/timer/network settings remain a separate path.
