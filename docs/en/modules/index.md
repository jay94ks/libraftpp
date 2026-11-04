# Module APIs

[한국어](../../ko/modules/index.md) · [I/O composition](../composition.md)

All types use namespace `raftpp`. Core is mandatory; optional modules depend on it. Linking a module target propagates C++20, public headers, and required link conditions.

| Module | CMake target | Activation | Public entry header | Role |
| --- | --- | --- | --- | --- |
| [core](core.md) | `raftpp::core` | Always enabled | `<raftpp/raft.hpp>` | Consensus, nodes, values, abstract I/O/streams/executors/compression, RPC framing |
| [uv](uv.md) | `raftpp::uv` | `RAFTPP_WITH_UV` | `<raftpp/uv/uv.hpp>` | Linux durable I/O/timers, TCP/pipe/TTY streams and transports |
| [asyncpp](asyncpp.md) | `raftpp::asyncpp` | `RAFTPP_MODULE_ASYNCPP` | `<raftpp/asyncpp/asyncpp.hpp>` | Dispatcher-backed callback executor |
| [asio](asio.md) | `raftpp::asio` | `RAFTPP_MODULE_ASIO` | `<raftpp/asio/asio.hpp>` | Standalone Asio executor/timer and TCP stream |
| [mvfst](mvfst.md) | `raftpp::mvfst` | `RAFTPP_MODULE_MVFST` | `<raftpp/mvfst/mvfst.hpp>` | Established mvfst bidirectional QUIC stream |
| [msquic](msquic.md) | `raftpp::msquic` | `RAFTPP_MODULE_MSQUIC` | `<raftpp/msquic/msquic.hpp>` | MsQuic API ownership and established QUIC stream |
| [lz4](lz4.md) | `raftpp::lz4` | `RAFTPP_MODULE_LZ4` | `<raftpp/lz4/lz4.hpp>` | LZ4 frame codec |
| [zstd](zstd.md) | `raftpp::zstd` | `RAFTPP_MODULE_ZSTD` | `<raftpp/zstd/zstd.hpp>` | Zstandard frame codec |
| [zlib](zlib.md) | `raftpp::zlib` | `RAFTPP_MODULE_ZLIB` | `<raftpp/zlib/zlib.hpp>` | zlib-wrapped DEFLATE codec |
| [RocksDB](rocksdb.md) | `raftpp::rocksdb` | `RAFTPP_MODULE_ROCKSDB` | `<raftpp/rocksdb/rocksdb.hpp>` | `rocksdb_storage : storage` |
| [SQLite](sqlite.md) | `raftpp::sqlite` | `RAFTPP_MODULE_SQLITE` | `<raftpp/sqlite/sqlite.hpp>` | `sqlite_storage : storage` |

## Selection and Installed Consumption

The seven `RAFTPP_MODULE_<NAME>` options default OFF. UV initially defaults according to libuv detection. Select options when building the library. Disabled modules provide no headers/targets; consumers cannot activate them by redefining feature macros.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio zstd)
target_link_libraries(app PRIVATE raftpp::asio raftpp::zstd)
```

Here `app` is a target created by the consumer. `COMPONENTS` resolves only required dependencies among installed active modules; core is provided even when omitted. Without components, discovery requires SDKs for every module enabled at installation. Consumers selecting a subset use individual module headers. `raftpp::raftpp` and `<raftpp.hpp>` aggregate every installed active module and require all their SDKs.

Generated `<raftpp/config.hpp>` macros `RAFTPP_MODULE_<NAME>` and `raftpp::features::modules::<name>` describe library-build settings. UV/fixture/backtrace flags use `raftpp::features`. See [CMake](../cmake.md) and [optional adapters/dependencies](../dependencies.md) for source builds, SDK preparation, and installation paths. Separately installed shared SDKs must also be visible to the runtime loader.

## Composition and Execution Contract

`asio_stream`, `mvfst_stream`, `msquic_stream`, and `uv_stream` implement core `byte_stream`. Register established peer streams with `framed_transport` to provide a Raft `transport`. UV's `stream_transport` is a separate API accepting a `uv_stream_transport` connector and managing connections/retries. Their inputs and lifecycle contracts differ.

Streams and I/O callbacks belonging to one node/transport must share a serialized execution domain. Adapters provide neither automatic event-loop transfer nor node synchronization. QUIC connection/TLS/peer authentication/reconnect belong to the application connector. UV provides disk/timer `io`. Executors submit callbacks and do not add coroutine/future-based Raft APIs.

Write success means the SDK's local acceptance/completion, not Raft commit or remote application acknowledgement. Immediate submission failure invokes no completion. Keep the execution domain running after close until accepted work and native cleanup finish. See each module and the [common stream contract](../streams.md) for differences.

Use `make_io(io_components)` to assemble storage/timer/network and an optional snapshot codec into a node backend. UV provides `uv_storage`/`uv_timer`; Asio provides `asio_timer`. See [I/O composition](../composition.md) for concrete combinations.
