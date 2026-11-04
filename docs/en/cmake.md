# CMake Builds and Installed Packages

[한국어](../ko/cmake.md) · [Event Core](core.md) · [libuv I/O](uv_io.md) · [Tracing](tracing.md) · [Module APIs](modules/index.md) · [I/O composition](composition.md)

`raftpp::raftpp` is the interface target aggregating enabled module libraries and their C++20 usage requirements. Use CMake 3.20 or newer. Source builds, tests, examples, tools, and installed-package consumers require only a C++ compiler. Module libraries are `raftpp::core` (always enabled), `raftpp::uv` (with `RAFTPP_WITH_UV=ON`), and selected `RAFTPP_MODULE_<NAME>` adapters. Link the aggregate target to use all enabled modules, or a module target for its API; UV transitively links core.

Use `#include <raftpp.hpp>` for all enabled modules, `<raftpp/raft.hpp>` for core, and `<raftpp/uv/uv.hpp>` for UV. Concrete headers are under `<raftpp/uv/...>`, such as `<raftpp/uv/uv_io.hpp>`. Names remain in namespace `raftpp`. UV headers are absent when UV is disabled; `fixture.hpp` is absent when fixture support is disabled. UV compression helpers return `invalid` when LZ4 support is disabled. See [module APIs](modules/index.md) for separate `lz4`/`zstd`/`zlib` codec modules and core abstract `compression_codec`.

## Using the Source

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
add_subdirectory(path/to/raftpp raftpp-build)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

The target propagates `cxx_std_20` and required include/link conditions. Each build has its own generated header copies under `<build>/include/`. The source-root `include/` is an ignored mirror of the last configured active modules. Edit originals in `modules/core/include/raftpp/` or `modules/uv/include/raftpp/uv/`; never edit copies or depend on their shared mirror when compiling. Private headers in `modules/<module>/privates/` are not installed.

`RAFTPP_INSTALL` initially defaults ON at top level and OFF under `add_subdirectory`. A parent project may select it explicitly to control installation. Cached options persist across later configuration runs.

## Installing and Consuming

```sh
cmake -S path/to/raftpp -B raftpp-build -DCMAKE_BUILD_TYPE=Release -DRAFTPP_INSTALL=ON
cmake --build raftpp-build --config Release --parallel
cmake --install raftpp-build --config Release --prefix /path/to/raftpp-prefix
cmake -S consumer -B consumer-build -DCMAKE_PREFIX_PATH=/path/to/raftpp-prefix
cmake --build consumer-build --config Release --parallel
```

The consumer's `CMakeLists.txt` is:

```cmake
cmake_minimum_required(VERSION 3.20)
project(consumer LANGUAGES CXX)
find_package(raftpp CONFIG REQUIRED)
add_executable(app main.cpp)
target_link_libraries(app PRIVATE raftpp::raftpp)
```

Default destinations are `lib/` for libraries, `include/raftpp.hpp` for the umbrella header, `include/raftpp/` for core headers and `include/raftpp/uv/` for enabled UV headers, including generated configuration/export headers, `lib/cmake/raftpp/` for package configuration, and `share/raftpp/LICENSE.md` for the upstream license. Actual directories follow `GNUInstallDirs` variables `CMAKE_INSTALL_LIBDIR`, `CMAKE_INSTALL_INCLUDEDIR`, `CMAKE_INSTALL_BINDIR`, and `CMAKE_INSTALL_DATADIR`. With relative destinations, relocate the install prefix and select its new location using `CMAKE_PREFIX_PATH` or `raftpp_DIR`. Explicit absolute install directories retain their absolute locations.

The installed package provides `raftpp::raftpp`, `raftpp::core`, optional `raftpp::uv`, and active public headers. Module library files are named `raftpp-core` and `raftpp-uv` with platform-specific prefixes/suffixes. Consumers need not manually add producer build paths or library files. Generated `config.hpp` guards and the installed header set match the build configuration; disabled features are not exposed. Select features when building the library rather than redefining consumer macros.

Use versionless `find_package(raftpp CONFIG)`. `reference_version` describes the reference C implementation, not the C++ ABI or a CMake-package version requirement.

## Feature Options and Dependencies

| Option | Contract |
| --- | --- |
| `RAFTPP_WITH_UV` | Enables the UV module; initially defaults to libuv detection. ON requires Linux, libuv 1.18.0 or newer, and necessary Linux headers. Propagates libuv usage requirements to installed consumers. |
| `RAFTPP_WITH_LZ4` | Initially ON when UV and liblz4 1.7.1 or newer are detected. Cannot be enabled without UV. |
| `RAFTPP_WITH_FIXTURE` | ON by default. OFF excludes fixture APIs/implementation/related test dependencies. |
| `RAFTPP_WITH_BACKTRACE` | OFF by default. Producer builds require GNU libbacktrace headers/library and `__assert_fail`; independent of UV. Follows the assertion conditions in [Tracing](tracing.md). |
| `RAFTPP_BUILD_TESTS` | OFF by default. Builds CTest targets for selected features. |
| `RAFTPP_BUILD_EXAMPLES` | OFF by default. Requires UV; independent of tests/fixture/LZ4. |
| `RAFTPP_BUILD_BENCHMARKS` | OFF by default. Requires UV; independent of examples/tests/fixture/LZ4. |
| `BUILD_SHARED_LIBS` | Static unless configured otherwise; ON builds a shared library. |
| `RAFTPP_INSTALL` | Initially ON at top level/OFF in subprojects. Generates install rules and the CMake package. |

UV-enabled installed packages rediscover libuv through PkgConfig in the consumer environment. Static LZ4 builds also rediscover liblz4; static backtrace builds find GNU libbacktrace in the consumer compiler's search paths. Producer dependency absolute paths are not copied. Shared-library private LZ4/backtrace links do not impose consumer development-header/PkgConfig requirements. Runtime lookup of external shared libraries follows platform-loader rules.

Packages with both UV/LZ4 disabled do not find PkgConfig. Disabled backtrace does not require its dependency either. Necessary private static links propagate through the target, so consumers need not manually add `-llz4`/`-lbacktrace`.

## Discovery Errors

Missing consumer dependencies for enabled features set `raftpp_FOUND` false and record the cause in `raftpp_NOT_FOUND_MESSAGE`. Use `find_package(raftpp CONFIG QUIET)` to check availability; `REQUIRED` turns it into a configuration error. `raftpp::raftpp` is defined after required dependencies are found. Unsupported required components also fail discovery.

Installed and source builds share [event-core](core.md)/[callback-node](node.md) contracts. Public shared-library symbols and feature selection follow the installed binary/generated-header combination.

See [Optional Adapter Modules](dependencies.md) for SDK targets, independent dependency preparation, and module options.
