cmake_minimum_required(VERSION 3.20)

project(raftpp LANGUAGES CXX)

set(RAFTPP_DEPENDENCY_MODULES asyncpp asio mvfst msquic lz4 zstd zlib rocksdb sqlite)
set(RAFTPP_DEPENDENCY_PREFIX "" CACHE PATH "Prefix containing independently built SDK directories")
if(RAFTPP_DEPENDENCY_PREFIX)
    foreach(module IN LISTS RAFTPP_DEPENDENCY_MODULES)
        list(APPEND CMAKE_PREFIX_PATH "${RAFTPP_DEPENDENCY_PREFIX}/${module}")
    endforeach()
    file(GLOB _raftpp_sdk_prefixes LIST_DIRECTORIES TRUE "${RAFTPP_DEPENDENCY_PREFIX}/*")
    foreach(prefix IN LISTS _raftpp_sdk_prefixes)
        if(IS_DIRECTORY "${prefix}")
            list(APPEND CMAKE_PREFIX_PATH "${prefix}")
        endif()
    endforeach()
endif()
foreach(module IN LISTS RAFTPP_DEPENDENCY_MODULES)
    string(TOUPPER "${module}" upper)
    option(RAFTPP_MODULE_${upper} "Build the ${module} core-interface adapter module" OFF)
endforeach()

# Keep the original optional dependency versions.
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(RAFTPP_UV QUIET IMPORTED_TARGET "libuv>=1.18.0")
endif()

option(RAFTPP_WITH_UV "Build the libuv-based Raft I/O backend" ${RAFTPP_UV_FOUND})
option(RAFTPP_WITH_FIXTURE "Build the in-memory Raft test fixture" ON)
option(RAFTPP_WITH_BACKTRACE "Print GNU libbacktrace frames on debug assertion failure" OFF)
option(RAFTPP_BUILD_TESTS "Build the C++ test suite" OFF)
option(RAFTPP_BUILD_EXAMPLES "Build the native C++ server and cluster examples" OFF)
option(RAFTPP_BUILD_BENCHMARKS "Build the C++ native benchmark tool" OFF)
set(_raftpp_install_default OFF)
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(_raftpp_install_default ON)
endif()
option(RAFTPP_INSTALL "Install the C++ library, headers, and CMake package" ${_raftpp_install_default})

if(RAFTPP_BUILD_BENCHMARKS AND NOT RAFTPP_WITH_UV)
    message(FATAL_ERROR "RAFTPP_BUILD_BENCHMARKS requires RAFTPP_WITH_UV=ON")
endif()

if(RAFTPP_BUILD_EXAMPLES AND NOT RAFTPP_WITH_UV)
    message(FATAL_ERROR "RAFTPP_BUILD_EXAMPLES requires RAFTPP_WITH_UV=ON")
endif()

if(RAFTPP_WITH_UV)
    if(NOT CMAKE_SYSTEM_NAME STREQUAL "Linux")
        message(FATAL_ERROR "The Raft libuv backend requires Linux")
    endif()
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(RAFTPP_UV REQUIRED IMPORTED_TARGET "libuv>=1.18.0")
    pkg_check_modules(RAFTPP_LZ4 QUIET IMPORTED_TARGET "liblz4>=1.7.1")
endif()

set(_raftpp_lz4_default OFF)
if(RAFTPP_WITH_UV AND RAFTPP_LZ4_FOUND)
    set(_raftpp_lz4_default ON)
endif()
option(RAFTPP_WITH_LZ4 "Enable LZ4 compression in the libuv backend" ${_raftpp_lz4_default})

if(RAFTPP_WITH_LZ4)
    if(NOT RAFTPP_WITH_UV)
        message(FATAL_ERROR "RAFTPP_WITH_LZ4 requires RAFTPP_WITH_UV=ON")
    endif()
    if(NOT RAFTPP_MODULE_LZ4)
        pkg_check_modules(RAFTPP_LZ4 REQUIRED IMPORTED_TARGET "liblz4>=1.7.1")
    endif()
endif()

if(RAFTPP_WITH_BACKTRACE)
    find_path(RAFTPP_BACKTRACE_INCLUDE_DIR backtrace.h
        HINTS ${CMAKE_CXX_IMPLICIT_INCLUDE_DIRECTORIES})
    find_library(RAFTPP_BACKTRACE_LIBRARY backtrace
        HINTS ${CMAKE_CXX_IMPLICIT_LINK_DIRECTORIES})
    if(NOT RAFTPP_BACKTRACE_INCLUDE_DIR OR NOT EXISTS "${RAFTPP_BACKTRACE_INCLUDE_DIR}/backtrace.h"
       OR NOT RAFTPP_BACKTRACE_LIBRARY OR NOT EXISTS "${RAFTPP_BACKTRACE_LIBRARY}")
        message(FATAL_ERROR "RAFTPP_WITH_BACKTRACE requires GNU libbacktrace headers and library")
    endif()
    include(CMakePushCheckState)
    include(CheckCXXSourceCompiles)
    cmake_push_check_state(RESET)
    set(CMAKE_REQUIRED_INCLUDES "${RAFTPP_BACKTRACE_INCLUDE_DIR}")
    set(CMAKE_REQUIRED_LIBRARIES "${RAFTPP_BACKTRACE_LIBRARY}" ${CMAKE_DL_LIBS})
    # Reuse a probe only for the same dependency/toolchain inputs. Changing a
    # cached library path must not reuse success from a different library.
    file(SHA256 "${RAFTPP_BACKTRACE_INCLUDE_DIR}/backtrace.h" _raftpp_backtrace_header_hash)
    file(SHA256 "${RAFTPP_BACKTRACE_LIBRARY}" _raftpp_backtrace_library_hash)
    string(SHA256 _raftpp_backtrace_probe_hash
        "${RAFTPP_BACKTRACE_INCLUDE_DIR};${_raftpp_backtrace_header_hash};${RAFTPP_BACKTRACE_LIBRARY};${_raftpp_backtrace_library_hash};${CMAKE_CXX_COMPILER};${CMAKE_CXX_COMPILER_VERSION};${CMAKE_CXX_COMPILER_TARGET};${CMAKE_SYSROOT};${CMAKE_CXX_FLAGS};${CMAKE_CXX_FLAGS_DEBUG};${CMAKE_CXX_FLAGS_RELEASE};${CMAKE_CXX_FLAGS_RELWITHDEBINFO};${CMAKE_CXX_FLAGS_MINSIZEREL};${CMAKE_EXE_LINKER_FLAGS};${CMAKE_BUILD_TYPE};${CMAKE_TRY_COMPILE_CONFIGURATION};${CMAKE_DL_LIBS}")
    set(_raftpp_backtrace_probe "RAFTPP_HAVE_GNU_BACKTRACE_${_raftpp_backtrace_probe_hash}")
    check_cxx_source_compiles("
        #undef NDEBUG
        #include <assert.h>
        #include <stdio.h>
        #include <backtrace.h>
        int main(void) {
            struct backtrace_state *state = backtrace_create_state(NULL, 0, NULL, NULL);
            if (state == NULL) __assert_fail(\"state\", __FILE__, __LINE__, \"main\");
            backtrace_print(state, 0, stderr);
            return 0;
        }" ${_raftpp_backtrace_probe})
    cmake_pop_check_state()
    if(NOT ${_raftpp_backtrace_probe})
        message(FATAL_ERROR "RAFTPP_WITH_BACKTRACE requires working GNU libbacktrace and __assert_fail")
    endif()
    add_library(raftpp::backtrace UNKNOWN IMPORTED)
    set_target_properties(raftpp::backtrace PROPERTIES
        IMPORTED_LOCATION "${RAFTPP_BACKTRACE_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${RAFTPP_BACKTRACE_INCLUDE_DIR}"
        INTERFACE_LINK_LIBRARIES "${CMAKE_DL_LIBS}")
endif()

# Native I/O and optional benchmark paths depend on Linux kernel headers.
if(RAFTPP_WITH_UV)
    include(CheckIncludeFileCXX)
    include(CheckCXXSymbolExists)
    check_include_file_cxx(linux/aio_abi.h RAFTPP_HAVE_LINUX_AIO_ABI_H)
    check_include_file_cxx(linux/io_uring.h RAFTPP_HAVE_LINUX_IO_URING_H)
    check_include_file_cxx(linux/perf_event.h RAFTPP_HAVE_LINUX_PERF_EVENT_H)
    check_cxx_symbol_exists(RWF_NOWAIT "linux/aio_abi.h" RAFTPP_HAVE_RWF_NOWAIT)
    if(NOT RAFTPP_HAVE_LINUX_AIO_ABI_H OR NOT RAFTPP_HAVE_RWF_NOWAIT)
        message(FATAL_ERROR "The Raft libuv backend requires Linux headers with RWF_NOWAIT support")
    endif()
endif()

include(GNUInstallDirs)
include(GenerateExportHeader)
include(cmake/modules.cmake)

set(RAFTPP_GENERATED_INCLUDE_DIR "${PROJECT_BINARY_DIR}/include")
# Each build owns its compiler inputs. The source-tree include/ is only a mirror.
set_property(GLOBAL PROPERTY RAFTPP_ACTIVE_HEADERS raftpp.hpp raftpp/config.hpp)
configure_file(cmake/config.hpp.in
    "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp/config.hpp" @ONLY)
configure_file(cmake/raftpp.hpp.in
    "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp.hpp" COPYONLY)

add_subdirectory(modules/core)

foreach(module IN LISTS RAFTPP_DEPENDENCY_MODULES)
    string(TOUPPER "${module}" upper)
    if(RAFTPP_MODULE_${upper})
        add_subdirectory("modules/${module}")
    endif()
endforeach()

if(RAFTPP_WITH_UV)
    add_subdirectory(modules/uv)
endif()

add_library(raftpp INTERFACE "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp.hpp")
add_library(raftpp::raftpp ALIAS raftpp)
target_link_libraries(raftpp INTERFACE raftpp::core)
get_property(dependency_targets GLOBAL PROPERTY RAFTPP_DEPENDENCY_TARGETS)
if(dependency_targets)
    target_link_libraries(raftpp INTERFACE ${dependency_targets})
    add_dependencies(raftpp ${dependency_targets})
endif()
add_dependencies(raftpp raftpp_core)
if(RAFTPP_WITH_UV)
    target_link_libraries(raftpp INTERFACE raftpp::uv)
    add_dependencies(raftpp raftpp_uv)
endif()
raftpp_prune_headers()
raftpp_mirror_headers()

if(RAFTPP_INSTALL)
    include(cmake/install.cmake)
else()
    file(REMOVE "${CMAKE_CURRENT_BINARY_DIR}/raftpp-config.cmake")
endif()

if(RAFTPP_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(RAFTPP_BUILD_BENCHMARKS)
    add_subdirectory(tools/benchmark)
endif()

if(RAFTPP_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()

message(STATUS "raftpp: libuv=${RAFTPP_WITH_UV}, LZ4=${RAFTPP_WITH_LZ4}, fixture=${RAFTPP_WITH_FIXTURE}, backtrace=${RAFTPP_WITH_BACKTRACE}, examples=${RAFTPP_BUILD_EXAMPLES}, benchmarks=${RAFTPP_BUILD_BENCHMARKS}, install=${RAFTPP_INSTALL}")

set(_raftpp_active_modules core)
if(RAFTPP_WITH_UV)
    list(APPEND _raftpp_active_modules uv)
endif()
foreach(module IN LISTS RAFTPP_DEPENDENCY_MODULES)
    if(TARGET raftpp::${module})
        list(APPEND _raftpp_active_modules "${module}")
    endif()
endforeach()
message(STATUS "raftpp modules: ${_raftpp_active_modules}")
