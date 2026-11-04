# Core Module API

[한국어](../../ko/modules/core.md) · [Modules](index.md) · [I/O composition](../composition.md)

`raftpp::core` is always enabled and mandatory. `<raftpp/raft.hpp>` collects core public APIs, which use the standard aliases and ownership meanings in `<raftpp/common.hpp>`. Core is usable without an external networking SDK.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS core)
target_link_libraries(app PRIVATE raftpp::core)
```

## Public API Map

| Headers | Main APIs | Contract |
| --- | --- | --- |
| `core.hpp`, `types.hpp` | `core_state`, events/updates, `step`, state/leader queries | [Event core](../core.md) |
| `node.hpp`, `io.hpp`, `state_machine.hpp` | `node`, `io`, `state_machine`, requests/callbacks | [Node API](../node.md) |
| `configuration.hpp` | `configuration`, member IDs/addresses/roles | [Configuration](../configuration.md) |
| `buffer.hpp`, `entry.hpp`, `snapshot.hpp` | `buffer`, `aligned_buffer`, `shared_buffer`, batches/snapshots | [Values and ownership](../storage.md) |
| `common.hpp`, `memory.hpp` | Standard aliases, `owned`/`shared`/`borrowed`/`view`, allocators | [Memory](../memory.md) |
| `message.hpp`, `encoding.hpp`, `checksum.hpp` | RPC/disk encoding and limits, CRC32/SHA-1 | [Encoding](../encoding.md) |
| `error.hpp` | `errc`, `error_code`, owning `error_info` | [Errors](../errors.md) |
| `tracing.hpp` | `tracer`, event observation | [Tracing](../tracing.md) |
| `stream.hpp`, `transport.hpp`, `framed_transport.hpp` | Byte streams, message transport, framing | [Streams and framing](../streams.md) |
| `executor.hpp`, `compression.hpp` | Abstract callback executor/synchronous frame codec | [Executors and codecs](../streams.md) |
| `fixture.hpp` | Virtual-time cluster fixture | [Fixture](../fixture.md), available only with `RAFTPP_WITH_FIXTURE=ON` |

Public `detail/` support headers define types required by public values. Applications composing implementations should use the entry APIs above.

## Common Abstract Interfaces

```cpp
using stream_read_callback = function<void(error_code, shared_buffer)>;

error_code executor::post(function<void()> callback) noexcept;
error_code compression_codec::compress(view<const byte> input, buffer& output) const noexcept;
error_code compression_codec::decompress(view<const byte> input, buffer& output,
                                       size_t max_size) const noexcept;
```

These are API signatures in namespace `raftpp`. `post` executes an accepted callback exactly once, later. An empty callback returns `invalid`. Keep driving the concrete executor's queue; callbacks must not throw. Codecs are synchronous and `max_size` bounds decoded bytes. Failure preserves output and its allocator. Optional modules provide concrete implementations.

`byte_stream` provides `read_start`, `read_stop`, two `write` overloads, `close`, and `error_details`. Read data is owned `shared_buffer` storage; fragments do not imply message boundaries. Concrete write-completion timing and close-observer policies are specified in the module pages.

## `framed_transport`

| Method | Inputs/results/state contract |
| --- | --- |
| `framed_transport(encoding_limits limits = {})` | May allocate during construction. Uses the same limits for parsing/encoding. Noncopyable. |
| `attach(server_id peer, string_view address, shared<byte_stream> stream)` | Register before `start`. ID 0/empty address/null returns `invalid`; duplicate ID returns `duplicate_id`; reused stream or attachment after start returns `busy`. Copies the address and shares the stream. Allocation failure returns `out_of_memory`. |
| `init(server_id id, string_view address)` | ID 0/empty address returns `invalid`; repeated initialization returns `busy`. |
| `start(function<void(message)> receive)` | Before initialization or with an empty callback returns `invalid`; repeated start returns `busy`. Starts reads on registered streams. |
| `send(shared<const message> value, completion callback)` | Selects a peer using `value->server_id`. Before start/null returns `invalid`; missing/failed peer returns `no_connection`. Retains accepted messages through completion. |
| `close(close_callback callback = {}) noexcept` | Stops reading and closes registered streams before completion. Retains the first nonempty pending close observer. A callback supplied after completion runs inline. |
| `error_details() const noexcept` | Copyable owning diagnostics. Malformed frames stop that peer's reader. |

After closing begins, `attach`/`init`/`start`/`send` return `shutdown`. Operations returning `error_code` return an empty code on success. Encode/parse errors follow [encoding](../encoding.md). Constructors may throw allocation exceptions; subsequent public operations follow their declared `noexcept` boundaries. Callbacks must not throw.

The transport exclusively controls stream read/write/close. Authenticate and establish connections before registration; do not perform competing native operations. Received messages use registered peer IDs/addresses. No automatic transfer between event loops occurs. When supplied to an I/O backend, let that backend own `init`/`start`/`close`.

## Example

```cpp
#include <raftpp/framed_transport.hpp>

raftpp::shared<raftpp::framed_transport> make_transport(
    raftpp::shared<raftpp::byte_stream> connected_peer)
{
    auto network = std::make_shared<raftpp::framed_transport>();
    if (auto error = network->attach(2, "peer-2", std::move(connected_peer)))
        throw raftpp::system_error(error);
    return network;
}
```

The caller supplies an authenticated connected stream in the same serialized execution domain and connects the returned transport to an I/O backend. Node/log/configuration lifetime and persistence ordering follow the detailed contracts linked above.

`version.hpp` constants `reference_version`/`reference_version_number` identify the algorithm/format source version, not a C++ ABI or package-version requirement. `random.hpp` exposes `random_within_range` declared in `detail/random.hpp`. Fixture and backtrace are independently selected by `RAFTPP_WITH_FIXTURE` and `RAFTPP_WITH_BACKTRACE`, without requiring UV.

## Random Helper

```cpp
uint32_t random_within_range(uint32_t& state, uint32_t minimum,
                           uint32_t maximum) noexcept;
```

Available from `<raftpp/random.hpp>`. Updates caller-owned `state` and returns a value in the inclusive range. Requires `minimum <= maximum`. Equal seeds reproduce the same sequence; a singleton range returns its value without advancing state. This is not a cryptographic random API; callers serialize concurrent state access.

`storage.hpp`, `timer.hpp`, and `composed_io.hpp` provide `make_io(io_components)` for `shared<io>` built from user-selected storage/timer/transport/snapshot-codec components. See [I/O composition](../composition.md) for construction validation, options, init/close, and format contracts.
