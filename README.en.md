# raftpp

[한국어](README.md)

raftpp is a C++20 Raft library rewritten from cowsql/raft. Compose the consensus core with storage, timers, transports, and compression modules, using RAII and standard smart pointers to express ownership.

- Provides leader election, log replication, membership changes, leadership transfer, and snapshots.
- Uses synchronous return values and request/completion callback APIs.
- Exposes mandatory core and optional adapters as independent CMake targets.
- Uses standard containers, strings, views, and error types, propagating C++20 requirements.

## Quick Start

Requires CMake 3.20 or newer and a C++20 compiler. This configuration builds core without optional SDKs.

```sh
git clone https://github.com/jay94ks/libraftpp.git
cd libraftpp

cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=OFF -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_WITH_FIXTURE=OFF
cmake --build build/core --config Release --parallel
```

`raftpp::core` is always enabled. `RAFTPP_MODULE_<NAME>` options default OFF; in the default configuration `RAFTPP_WITH_UV` follows installed libuv detection. Select required features when building the library.

## Modules

| Module | Activation | Role |
| --- | --- | --- |
| [core](docs/en/modules/core.md) | Always enabled | Consensus, nodes, value types, abstract I/O, RPC framing |
| [uv](docs/en/modules/uv.md) | `RAFTPP_WITH_UV` | Linux durable I/O, storage, timers, TCP/pipe/TTY |
| [asyncpp](docs/en/modules/asyncpp.md) | `RAFTPP_MODULE_ASYNCPP` | Callback executor |
| [asio](docs/en/modules/asio.md) | `RAFTPP_MODULE_ASIO` | Callback executor, timer, connected TCP streams |
| [mvfst](docs/en/modules/mvfst.md) | `RAFTPP_MODULE_MVFST` | Established bidirectional QUIC streams |
| [msquic](docs/en/modules/msquic.md) | `RAFTPP_MODULE_MSQUIC` | MsQuic API ownership and established QUIC streams |
| [lz4](docs/en/modules/lz4.md) | `RAFTPP_MODULE_LZ4` | LZ4 frame compression |
| [zstd](docs/en/modules/zstd.md) | `RAFTPP_MODULE_ZSTD` | Zstandard frame compression |
| [zlib](docs/en/modules/zlib.md) | `RAFTPP_MODULE_ZLIB` | zlib-wrapped DEFLATE compression |
| [rocksdb](docs/en/modules/rocksdb.md) | `RAFTPP_MODULE_ROCKSDB` | RocksDB durable storage |
| [sqlite](docs/en/modules/sqlite.md) | `RAFTPP_MODULE_SQLITE` | SQLite durable storage |
| [bindings](docs/en/modules/bindings.md) | `RAFTPP_MODULE_BINDINGS` | Shared Node.js/C# bridge and optional Node-API addon |

Each module provides a `raftpp::<module>` target and public headers. `raftpp::raftpp` and `<raftpp.hpp>` collect all active modules. To consume a subset, use a selection such as `find_package(raftpp CONFIG REQUIRED COMPONENTS sqlite)` and that module's target/header.

`RAFTPP_MODULE_LZ4` is an independent compression codec. It is separate from `RAFTPP_WITH_LZ4`, which selects UV's existing LZ4 snapshot-reading support.

## Preparing Optional SDKs

Initialize only required submodules. SDK preparation and raftpp module activation are separate steps; project configuration does not automatically download or build SDKs. For SQLite:

```sh
git submodule update --init deps/sqlite deps/tcl
cmake -S deps -B build/deps -DRAFTPP_DEPENDENCIES=sqlite -DRAFTPP_DEPS_SHARED=OFF
cmake --build build/deps --parallel 2

cmake -S . -B build/sqlite -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=OFF -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_MODULE_SQLITE=ON \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/deps/prefix"
cmake --build build/sqlite --config Release --parallel
```

Tcl is a SQLite source-generation build tool, not a runtime dependency. This preparation path uses native POSIX tools; supply installed SDKs for other environments or cross compilation. Existing SDKs can also be found through `CMAKE_PREFIX_PATH` and other package settings. See [dependencies](docs/en/dependencies.md) for other libraries and QUIC prerequisites.

## Composing Storage, Timers, and Networking

Users select an implementation for each interface. This function combines SQLite storage with a timer, transport, and optional snapshot codec, returning `shared<io>` for the backend argument of `node` construction.

```cpp
#include <raftpp/composed_io.hpp>
#include <raftpp/sqlite/sqlite.hpp>

raftpp::shared<raftpp::io> make_backend(
    raftpp::fs::path database,
    raftpp::shared<raftpp::timer> timer,
    raftpp::shared<raftpp::transport> network,
    raftpp::shared<raftpp::compression_codec> codec = {})
{
    return raftpp::make_io({
        .storage = std::make_shared<raftpp::sqlite_storage>(std::move(database)),
        .timer = std::move(timer),
        .network = std::move(network),
        .snapshot_codec = std::move(codec),
    });
}
```

This example requires linking `raftpp::sqlite`. Create the DB parent directory beforehand and pass valid timer/network objects. Users implement application state through `state_machine`.

All component calls, node/FSM calls, and callbacks share one serialized execution domain. Keep event loops alive and running until close completion. RocksDB/SQLite DB calls and commits block the calling thread and complete inline. Application connectors prepare connections adopted by stream adapters, including QUIC TLS/authentication. See [I/O composition](docs/en/composition.md) and [node APIs](docs/en/node.md) for ownership/completion/close contracts.

## Installation and CMake Consumption

Install the core build above:

```sh
cmake --install build/core --config Release --prefix "$PWD/build/install"
```

Link from the consumer project's `CMakeLists.txt` below; `main.cpp` is the consumer's application code. Pass `-DCMAKE_PREFIX_PATH=/path/to/raftpp-prefix` when configuring the consumer.

```cmake
cmake_minimum_required(VERSION 3.20)
project(app LANGUAGES CXX)

find_package(raftpp CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

When optional modules are built/installed, their required external SDKs must also be discoverable in the consumer environment. Select static/shared libraries with `BUILD_SHARED_LIBS`. See [CMake APIs](docs/en/cmake.md) for installation/component/feature conditions.

## Runnable Examples and Tests

With Linux, libuv 1.18.0 or newer, and required Linux headers, run the single-node counter example. The executable path below assumes a single-configuration generator.

```sh
cmake -S . -B build/uv -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_WITH_UV=ON -DRAFTPP_WITH_LZ4=OFF \
  -DRAFTPP_BUILD_EXAMPLES=ON
cmake --build build/uv --config Release --parallel
./build/uv/examples/raftpp-example-composed ./build/uv/demo none uv
```

Running the same command again restores the counter from its stored snapshot and increments it. This executable uses `uv_storage`. Enabling optional SDK modules allows different codec/timer arguments. See [example APIs](docs/en/examples.md) for server/cluster usage.

To add tests to the previously configured core build, use the following. Adapter validation uses Release.

```sh
cmake -S . -B build/core -DCMAKE_BUILD_TYPE=Release -DRAFTPP_BUILD_TESTS=ON
cmake --build build/core --config Release --parallel
ctest --test-dir build/core -C Release --output-on-failure
```

Follow [work instructions](AGENTS.en.md) for selecting development checks and reusing unchanged validation results.

## Language Bindings

[Node.js and C# APIs](docs/en/modules/bindings.md) are available. Native implementations live in `modules/bindings`; language packages are in `bindings/nodejs` and `bindings/csharp`. Enable `RAFTPP_MODULE_BINDINGS=ON` for the shared runtime composing selected modules and `RAFTPP_BINDINGS_NODEJS=ON` for the Node-API addon. Node.js uses callbacks and `camelCase`; C# uses callbacks and `PascalCase`. Node settings, replication queries, FSM hooks, tracing, and storage/timer/transport/codec selection are available; C# `RaftHost` owns execution-thread/polling progress.

## Documentation and Source Layout

- [Module APIs](docs/en/modules/index.md) · [I/O composition](docs/en/composition.md)
- [Nodes and state machines](docs/en/node.md) · [Build/install](docs/en/cmake.md)
- [Optional SDKs/dependencies](docs/en/dependencies.md) · [Runnable examples](docs/en/examples.md)
- [Implementation/validation records](REWRITTEN.en.md) · [Contribution instructions](AGENTS.en.md)

Public header originals live in `modules/core/include/raftpp/` and `modules/<name>/include/raftpp/<name>/`. Each module contains `privates/`, `src/`, and `CMakeLists.txt`. Root `include/` is a generated copy of active public headers, excluded from Git; edit module originals. `docs/ko/` and `docs/en/` provide Korean/English API documents.

## License

A direct derivative of cowsql/raft, provided under `LGPL-3.0-only WITH LGPL-3.0-linking-exception`. See [LICENSE.en.md](LICENSE.en.md) for upstream notices, linking exception, and original license text. External libraries under `deps/` retain their respective licenses.
