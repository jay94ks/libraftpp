# Optional Adapter Modules

[한국어](../ko/dependencies.md) · [Module APIs](modules/index.md) · [I/O composition](composition.md)

`raftpp::core` is mandatory. Optional SDK modules also depend on core. `deps/` provides upstream sources and an independent SDK preparation build; `modules/<name>/` provides concrete adapters, public headers and CMake rules for raftpp modules. SDK preparation itself is not a raftpp module.

## Targets and Headers

| Option (default OFF) | Target | Public header | Integration scope |
| --- | --- | --- | --- |
| [`RAFTPP_MODULE_ASYNCPP`](modules/asyncpp.md) | `raftpp::asyncpp` | `<raftpp/asyncpp/asyncpp.hpp>` | `asyncpp_executor : executor` |
| [`RAFTPP_MODULE_ASIO`](modules/asio.md) | `raftpp::asio` | `<raftpp/asio/asio.hpp>` | `asio_stream : byte_stream`, `asio_executor : executor`, `asio_timer : timer` |
| [`RAFTPP_MODULE_MVFST`](modules/mvfst.md) | `raftpp::mvfst` | `<raftpp/mvfst/mvfst.hpp>` | `mvfst_stream : byte_stream` |
| [`RAFTPP_MODULE_MSQUIC`](modules/msquic.md) | `raftpp::msquic` | `<raftpp/msquic/msquic.hpp>` | `msquic_stream : byte_stream`, `open_msquic_api()` |
| [`RAFTPP_MODULE_LZ4`](modules/lz4.md) | `raftpp::lz4` | `<raftpp/lz4/lz4.hpp>` | `lz4_codec : compression_codec`, liblz4 ≥ 1.7.1 |
| [`RAFTPP_MODULE_ZSTD`](modules/zstd.md) | `raftpp::zstd` | `<raftpp/zstd/zstd.hpp>` | `zstd_codec : compression_codec` |
| [`RAFTPP_MODULE_ZLIB`](modules/zlib.md) | `raftpp::zlib` | `<raftpp/zlib/zlib.hpp>` | `zlib_codec : compression_codec` |

These modules contain concrete implementations of core abstractions, linked with their native SDKs. [Streams, framing, executors and codecs](streams.md) describe their API contracts. `uv_stream` also implements `byte_stream`; `framed_transport` implements the core `transport` over any compatible established peer streams. Enabling a module does not select a node backend automatically. [UV](uv_io.md) continues to provide disk persistence and timers; these optional stream adapters do not provide separate durable `io` implementations. QUIC connection establishment, certificates, authentication and reconnect policy remain with the application connector. All public asynchronous APIs use callbacks.

`raftpp::raftpp` and `<raftpp.hpp>` collect active modules. To use a particular SDK, link its module target and include its header. Core is always linked alongside it. `raftpp::features::modules::<name>` and generated `RAFTPP_MODULE_<NAME>` macros indicate SDK module activation at library build time; consumer component selection does not change them. Disabled modules expose no headers, targets, or dependencies.

## Preparing and Selecting SDKs

Pinned submodule commits are recorded in Git gitlinks and `deps/revisions.json`. Checkouts are required only when building the corresponding SDK. The default core/UV build does not download or configure new SDKs.

```sh
git submodule update --init deps/asyncpp deps/asio deps/lz4 deps/zstd deps/zlib
cmake -S deps -B build/deps '-DRAFTPP_DEPENDENCIES=asyncpp;asio;lz4;zstd;zlib'
cmake --build build/deps --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/deps/prefix" \
  -DRAFTPP_MODULE_ASIO=ON -DRAFTPP_MODULE_LZ4=ON \
  -DRAFTPP_MODULE_ZSTD=ON -DRAFTPP_MODULE_ZLIB=ON
cmake --build build --parallel 2
```

`RAFTPP_DEPENDENCIES` selects SDK preparation; `RAFTPP_MODULE_<NAME>` selects raftpp modules. List only needed SDKs or build `--target raftpp_dep_<name>`. Each SDK uses its own CMake process/build directory and `<prefix>/<name>` installation. Configure preparation with `RAFTPP_DEPS_PREFIX`, `RAFTPP_DEPS_JOBS`, `RAFTPP_DEPS_BUILD_TYPE` (default Release), and `RAFTPP_DEPS_SHARED`. If the same SDK is already prepared under `<prefix>/<name>`, list its name in semicolon-separated `RAFTPP_DEPS_PREBUILT` to skip rebuilding. Explicit reuse requires matching commits, build options, tools, and installed files. Upstream tests, examples, benchmarks, and documentation builds are disabled. Because zlib's CMake changes an original header, it uses a separate source copy within the build directory.

`RAFTPP_DEPENDENCY_PREFIX` is the SDK installation root. Existing SDK installations can also be supplied through `CMAKE_PREFIX_PATH`, `PKG_CONFIG_PATH`, or individual package `_DIR` settings. Asio/asyncpp use the needed headers from the default `RAFTPP_DEPS_DIR` checkouts. Without those checkouts, installed Asio headers or an asyncpp package are located. Binary SDK packages are resolved at configure time; a missing package fails configuration for the selected module. Configuration does not automatically download or build SDKs.

## QUIC SDK Dependencies

MsQuic preparation builds an isolated shared library. Initialize only the submodule needed for the OpenSSL backend.

```sh
git submodule update --init deps/msquic
git -C deps/msquic submodule update --init --depth 1 submodules/openssl
cmake -S deps -B build/quic-deps -DRAFTPP_DEPENDENCIES=msquic
cmake --build build/quic-deps --target raftpp_dep_msquic --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/quic-deps/prefix" -DRAFTPP_MODULE_MSQUIC=ON
```

`RAFTPP_MSQUIC_TLS` selects `openssl` (default), `quictls`, or `schannel`. quictls requires its corresponding submodule; schannel is used on Windows. `RAFTPP_MSQUIC_EXTERNAL_OPENSSL=ON` uses installed OpenSSL 3.5 or newer, as required by upstream. MsQuic tests, tools, perf, and logging are disabled.

Required mvfst dependency sources are also pinned submodules in `deps/`. `RAFTPP_DEPENDENCIES=mvfst` automatically selects only the layers below for building/installation; `raftpp_dep_mvfst` enforces completion order. It does not automatically fetch the entire upstream getdeps list or test-only dependencies. liboqs, aegis, haberdashery, and optional compression/profiling layers are disabled.

| Dependency | Scope |
| --- | --- |
| `folly`, `fizz` | mvfst transport and TLS foundation |
| `boost` | Build required headers and only context/regex binaries |
| `openssl` | Shared crypto/TLS SDK, independent of MsQuic's internal TLS checkout |
| `fmt`, `fast_float`, `gflags`, `glog` | Upstream-required formatting, numeric conversion, flags, and logging |
| `libevent`, `libsodium` | Event foundation and Fizz crypto support |
| `zstd`, `zlib` | Reuse compression SDKs required by Fizz |

```sh
git submodule update --init deps/mvfst deps/folly deps/fizz deps/boost deps/openssl \
  deps/fmt deps/fast_float deps/gflags deps/glog deps/libevent deps/libsodium deps/zstd deps/zlib
git -C deps/boost submodule update --init --recursive --depth 1 --jobs 4
cmake -S deps -B build/mvfst-deps -DRAFTPP_DEPENDENCIES=mvfst
cmake --build build/mvfst-deps --target raftpp_dep_mvfst --parallel 2
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release \
  -DRAFTPP_DEPENDENCY_PREFIX="$PWD/build/mvfst-deps/prefix" -DRAFTPP_MODULE_MVFST=ON
```

The prepared Folly SDK profile excludes native Linux AIO and SimpleAsyncIO, which this adapter does not use. This also avoids linking a host-detected libaio dependency. Use a separate full Folly installation when an application needs those APIs.

Boost headers span multiple sub-repositories requiring source initialization; its tests and additional binary libraries are not built. Preparing Boost/OpenSSL/libsodium requires POSIX shell, make, Perl, and autoconf/automake/libtool; builds use separate source copies to leave original submodules unchanged. Provide installed SDKs on Windows. Each package installs to `<prefix>/<name>` and is located through `RAFTPP_DEPENDENCY_PREFIX`. An already prepared SDK can instead supply installation paths resolvable by `find_package(mvfst CONFIG)`.

## UV Compression and Validation Scope

`RAFTPP_MODULE_LZ4` can be selected without UV. The existing `RAFTPP_WITH_LZ4` separately enables UV snapshot LZ4 decoding and requires UV. With both options enabled, UV uses `raftpp::lz4`; without the module option it uses the existing liblz4 package. Selecting Zstandard/zlib SDKs does not change disk/network formats or snapshot compression.

With `RAFTPP_BUILD_TESTS=ON`, selected adapter contract checks use the `adapter` label and are built/run only in Release. Upstream suites, examples and benchmarks remain disabled.

```cmake
find_package(raftpp CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE raftpp::asio)
```

Installed consumers must also be able to locate the same binary SDK packages. Shared SDKs installed separately must also be visible to the runtime loader. On Linux, configure the system loader or add the required SDK `lib` directories to `LD_LIBRARY_PATH`. `CMAKE_PREFIX_PATH` does not configure runtime library discovery. Selected Asio/asyncpp headers are copied into the raftpp installation with their licenses preserved. The project license is [LICENSE.md](../../LICENSE.md); external SDKs retain their respective licenses.

## Selecting Installed Components

Select required components with, for example, `find_package(raftpp CONFIG REQUIRED COMPONENTS core asio)` to resolve only core and the selected modules' dependencies. Core remains mandatory even when omitted from the list. Omitting components resolves SDKs for every module enabled at installation. Requesting a required component disabled at installation fails package discovery.

Consumers selecting a subset use the corresponding module targets/headers. Aggregate `raftpp::raftpp` and `<raftpp.hpp>` collect all installed active modules and require their SDKs. Actual transitive link requirements, such as LZ4 for a static UV component, are resolved together.

```cmake
find_package(raftpp CONFIG REQUIRED COMPONENTS asio)
target_link_libraries(my_app PRIVATE raftpp::asio)
```
