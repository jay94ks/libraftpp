cmake_minimum_required(VERSION 3.20)
project(raftpp_dependencies LANGUAGES NONE)

include(ExternalProject)
set(RAFTPP_DEPENDENCIES "" CACHE STRING "SDKs to prepare, separated by semicolons")
set(RAFTPP_DEPS_PREFIX "${CMAKE_BINARY_DIR}/prefix" CACHE PATH "Independent SDK installation roots")
set(RAFTPP_DEPS_PREBUILT "" CACHE STRING "Already prepared SDK names under RAFTPP_DEPS_PREFIX")
set(RAFTPP_DEPS_JOBS 2 CACHE STRING "Parallel jobs within each SDK build")
set(RAFTPP_DEPS_BUILD_TYPE Release CACHE STRING "SDK build configuration")
option(RAFTPP_DEPS_SHARED "Build shared SDK libraries" ON)
option(RAFTPP_MSQUIC_EXTERNAL_OPENSSL "Use installed OpenSSL 3.5+ instead of bundled TLS" OFF)
set(RAFTPP_MSQUIC_TLS openssl CACHE STRING "MsQuic TLS provider: openssl, quictls, or schannel")
include("${CMAKE_CURRENT_LIST_DIR}/../cmake/mvfst-dependencies.cmake")
set(_supported asyncpp asio boost openssl fmt fast_float gflags glog libevent libsodium
    lz4 zstd zlib folly fizz mvfst msquic tcl rocksdb sqlite)
if("sqlite" IN_LIST RAFTPP_DEPENDENCIES AND NOT "sqlite" IN_LIST RAFTPP_DEPS_PREBUILT)
    list(APPEND RAFTPP_DEPENDENCIES tcl)
endif()
set(pending ${RAFTPP_DEPENDENCIES})
while(pending)
    list(POP_FRONT pending name)
    if(NOT name IN_LIST _supported)
        message(FATAL_ERROR "Unknown dependency '${name}'; supported: ${_supported}")
    endif()
    raftpp_mvfst_sdk_options("${name}")
    foreach(dependency IN LISTS RAFTPP_SDK_DEPENDENCIES)
        if(NOT dependency IN_LIST RAFTPP_DEPENDENCIES)
            list(APPEND RAFTPP_DEPENDENCIES "${dependency}")
            list(APPEND pending "${dependency}")
        endif()
    endforeach()
endwhile()
set(sdk_prefixes)
foreach(name IN LISTS _supported)
    list(APPEND sdk_prefixes "${RAFTPP_DEPS_PREFIX}/${name}")
endforeach()
list(REMOVE_DUPLICATES sdk_prefixes)
string(JOIN "|" sdk_prefix_argument ${sdk_prefixes})
list(REMOVE_DUPLICATES RAFTPP_DEPENDENCIES)
foreach(name IN LISTS _supported)
    if(NOT name IN_LIST RAFTPP_DEPENDENCIES)
        continue()
    endif()
    set(prefix "${RAFTPP_DEPS_PREFIX}/${name}")
    if(name IN_LIST RAFTPP_DEPS_PREBUILT)
        if(NOT IS_DIRECTORY "${prefix}/include")
            message(FATAL_ERROR "Prebuilt SDK ${name} has no include directory at ${prefix}")
        endif()
        add_custom_target(raftpp_dep_${name})
        continue()
    endif()
    set(source "${CMAKE_CURRENT_SOURCE_DIR}/${name}")
    if(NOT EXISTS "${source}/.git")
        message(FATAL_ERROR "Missing ${name} checkout. Run git submodule update --init deps/${name}")
    endif()
    set(prefix "${RAFTPP_DEPS_PREFIX}/${name}")
    set(binary "${CMAKE_BINARY_DIR}/${name}")
    if(name STREQUAL "asyncpp" OR name STREQUAL "asio")
        ExternalProject_Add(raftpp_dep_${name}
            SOURCE_DIR "${source}" DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
            CONFIGURE_COMMAND "" BUILD_COMMAND ""
            INSTALL_COMMAND "${CMAKE_COMMAND}"
                "-DRAFTPP_SDK_NAME=${name}" "-DRAFTPP_SDK_SOURCE=${source}"
                "-DRAFTPP_SDK_PREFIX=${prefix}"
                -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/install-sdk-headers.cmake")
        continue()
    endif()
    if(name STREQUAL "tcl" OR name STREQUAL "sqlite")
        if(WIN32 OR CMAKE_CROSSCOMPILING)
            message(FATAL_ERROR "The ${name} preparation recipe requires native POSIX tools; provide an installed SDK otherwise")
        endif()
        set(prerequisites)
        if(name STREQUAL "sqlite")
            set(prerequisites raftpp_dep_tcl)
        endif()
        ExternalProject_Add(raftpp_dep_${name}
            DEPENDS ${prerequisites}
            SOURCE_DIR "${source}" DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
            CONFIGURE_COMMAND "" INSTALL_COMMAND ""
            BUILD_COMMAND "${CMAKE_COMMAND}" "-DRAFTPP_SDK_NAME=${name}"
                "-DRAFTPP_SDK_SOURCE=${source}" "-DRAFTPP_SDK_WORK=${binary}"
                "-DRAFTPP_SDK_PREFIX=${prefix}" "-DRAFTPP_TCL_PREFIX=${RAFTPP_DEPS_PREFIX}/tcl"
                "-DRAFTPP_SDK_SHARED=${RAFTPP_DEPS_SHARED}" "-DRAFTPP_SDK_JOBS=${RAFTPP_DEPS_JOBS}"
                -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/build-database-sdk.cmake"
            BUILD_ALWAYS ON USES_TERMINAL_BUILD TRUE)
        continue()
    endif()
    if(name STREQUAL "boost" OR name STREQUAL "openssl" OR name STREQUAL "libsodium")
        if(WIN32)
            message(FATAL_ERROR "The source preparation recipe for ${name} requires POSIX tools; provide an installed SDK on Windows")
        endif()
        ExternalProject_Add(raftpp_dep_${name}
            SOURCE_DIR "${source}" DOWNLOAD_COMMAND "" UPDATE_COMMAND ""
            CONFIGURE_COMMAND "" INSTALL_COMMAND ""
            BUILD_COMMAND "${CMAKE_COMMAND}" "-DRAFTPP_SDK_NAME=${name}"
                "-DRAFTPP_SDK_SOURCE=${source}" "-DRAFTPP_SDK_WORK=${binary}/source"
                "-DRAFTPP_SDK_PREFIX=${prefix}" "-DRAFTPP_SDK_JOBS=${RAFTPP_DEPS_JOBS}"
                "-DRAFTPP_SDK_SHARED=${RAFTPP_DEPS_SHARED}"
                -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/build-posix-sdk.cmake"
            BUILD_ALWAYS ON USES_TERMINAL_BUILD TRUE)
        continue()
    endif()
    raftpp_mvfst_sdk_options("${name}")
    set(extra ${RAFTPP_SDK_EXTRA})
    set(subdir "${RAFTPP_SDK_SUBDIR}")
    set(download DOWNLOAD_COMMAND "")
    if(name STREQUAL "rocksdb")
        list(APPEND extra -DROCKSDB_BUILD_SHARED=${RAFTPP_DEPS_SHARED}
            -DWITH_TESTS=OFF -DWITH_ALL_TESTS=OFF -DWITH_BENCHMARK=OFF
            -DWITH_BENCHMARK_TOOLS=OFF -DWITH_CORE_TOOLS=OFF -DWITH_TOOLS=OFF
            -DWITH_TRACE_TOOLS=OFF -DWITH_EXAMPLES=OFF -DWITH_GFLAGS=OFF
            -DWITH_LIBURING=OFF -DWITH_JEMALLOC=OFF -DWITH_SNAPPY=OFF
            -DWITH_LZ4=OFF -DWITH_ZSTD=OFF -DWITH_ZLIB=OFF -DWITH_BZ2=OFF
            -DUSE_FOLLY=OFF -DUSE_COROUTINES=OFF -DPORTABLE=ON -DUSE_RTTI=ON
            -DFAIL_ON_WARNINGS=OFF)
    elseif(name STREQUAL "lz4")
        set(subdir build/cmake)
        list(APPEND extra -DLZ4_BUILD_CLI=OFF -DLZ4_BUILD_LEGACY_LZ4C=OFF
            -DLZ4_BUNDLED_MODE=OFF -DBUILD_STATIC_LIBS=OFF)
    elseif(name STREQUAL "zstd")
        set(subdir build/cmake)
        list(APPEND extra -DZSTD_BUILD_PROGRAMS=OFF -DZSTD_BUILD_TESTS=OFF
            -DZSTD_BUILD_SHARED=${RAFTPP_DEPS_SHARED} -DZSTD_BUILD_STATIC=OFF)
    elseif(name STREQUAL "zlib")
        # Upstream CMake renames zconf.h in its source tree: isolate that mutation.
        set(source "${CMAKE_BINARY_DIR}/sources/zlib")
        set(download DOWNLOAD_COMMAND "${CMAKE_COMMAND}" -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/zlib" "${source}")
        list(APPEND extra -DZLIB_BUILD_EXAMPLES=OFF -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
    elseif(name STREQUAL "msquic")
        # MsQuic uses CMAKE_BINARY_DIR and several global cache options internally.
        # Its own CMake process keeps these out of raftpp and the other SDKs.
        if(NOT RAFTPP_MSQUIC_TLS STREQUAL "schannel" AND NOT RAFTPP_MSQUIC_EXTERNAL_OPENSSL)
            if(NOT EXISTS "${source}/submodules/${RAFTPP_MSQUIC_TLS}/Configure")
                message(FATAL_ERROR "Initialize TLS first: git -C deps/msquic submodule update --init --depth 1 submodules/${RAFTPP_MSQUIC_TLS}")
            endif()
        endif()
        list(APPEND extra -DQUIC_BUILD_SHARED=ON -DQUIC_BUILD_TOOLS=OFF
            -DQUIC_BUILD_TEST=OFF -DQUIC_BUILD_PERF=OFF -DQUIC_ENABLE_LOGGING=OFF
            -DQUIC_TLS_LIB=${RAFTPP_MSQUIC_TLS}
            -DQUIC_USE_EXTERNAL_OPENSSL=${RAFTPP_MSQUIC_EXTERNAL_OPENSSL})
    endif()
    if(NOT RAFTPP_DEPS_SHARED)
        if(name STREQUAL "lz4")
            list(APPEND extra -DBUILD_STATIC_LIBS=ON)
        elseif(name STREQUAL "zstd")
            list(APPEND extra -DZSTD_BUILD_STATIC=ON)
        endif()
    endif()
    set(toolchain)
    if(CMAKE_TOOLCHAIN_FILE)
        list(APPEND toolchain "-DCMAKE_TOOLCHAIN_FILE=${CMAKE_TOOLCHAIN_FILE}")
    endif()
    set(dependency_targets)
    foreach(dependency IN LISTS RAFTPP_SDK_DEPENDENCIES)
        list(APPEND dependency_targets raftpp_dep_${dependency})
    endforeach()
    ExternalProject_Add(raftpp_dep_${name}
        DEPENDS ${dependency_targets} LIST_SEPARATOR "|"
        SOURCE_DIR "${source}" SOURCE_SUBDIR "${subdir}"
        BINARY_DIR "${binary}" ${download} UPDATE_COMMAND ""
        CMAKE_ARGS "-DCMAKE_INSTALL_PREFIX=${prefix}" -DCMAKE_INSTALL_LIBDIR=lib
            "-DCMAKE_BUILD_TYPE=${RAFTPP_DEPS_BUILD_TYPE}"
            "-DBUILD_SHARED_LIBS=${RAFTPP_DEPS_SHARED}" -DCMAKE_POSITION_INDEPENDENT_CODE=ON
            "-DCMAKE_PREFIX_PATH=${sdk_prefix_argument}" -DCMAKE_CXX_STANDARD=20
            -DCMAKE_CXX_EXTENSIONS=OFF -DCMAKE_INSTALL_RPATH_USE_LINK_PATH=ON
            ${toolchain} ${extra}
        BUILD_COMMAND "${CMAKE_COMMAND}" --build <BINARY_DIR>
            --config "${RAFTPP_DEPS_BUILD_TYPE}" --parallel "${RAFTPP_DEPS_JOBS}"
        INSTALL_COMMAND "${CMAKE_COMMAND}" --install <BINARY_DIR>
            --config "${RAFTPP_DEPS_BUILD_TYPE}"
        BUILD_ALWAYS ON USES_TERMINAL_BUILD TRUE)
    ExternalProject_Add_Step(raftpp_dep_${name} licenses
        COMMAND "${CMAKE_COMMAND}" "-DRAFTPP_SDK_NAME=${name}"
            "-DRAFTPP_SDK_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/${name}"
            "-DRAFTPP_SDK_PREFIX=${prefix}"
            -P "${CMAKE_CURRENT_LIST_DIR}/../cmake/install-sdk-licenses.cmake"
        DEPENDEES install)
endforeach()
