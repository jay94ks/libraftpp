# Internal SDK prerequisites, not separately enabled raftpp modules.
set(RAFTPP_MVFST_DEPENDENCIES
    boost openssl fmt fast_float gflags glog libevent libsodium zstd zlib folly fizz)

function(raftpp_mvfst_sdk_options name)
    set(extra)
    set(dependencies)
    set(subdir "")
    if(name STREQUAL "fmt")
        set(extra -DFMT_TEST=OFF -DFMT_DOC=OFF -DFMT_INSTALL=ON)
    elseif(name STREQUAL "fast_float")
        set(extra -DFASTFLOAT_TEST=OFF -DFASTFLOAT_SANITIZE=OFF)
    elseif(name STREQUAL "gflags")
        set(extra -DGFLAGS_REGISTER_INSTALL_PREFIX=OFF -DREGISTER_INSTALL_PREFIX=OFF
            -DGFLAGS_REGISTER_BUILD_DIR=OFF -DREGISTER_BUILD_DIR=OFF
            -DGFLAGS_BUILD_TESTING=OFF -DGFLAGS_BUILD_gflags_LIB=ON
            -DGFLAGS_BUILD_gflags_nothreads_LIB=OFF
            -DGFLAGS_BUILD_SHARED_LIBS=${RAFTPP_DEPS_SHARED}
            -DGFLAGS_BUILD_STATIC_LIBS=OFF -DGFLAGS_INSTALL_HEADERS=ON)
        if(NOT RAFTPP_DEPS_SHARED)
            list(APPEND extra -DGFLAGS_BUILD_STATIC_LIBS=ON)
        endif()
    elseif(name STREQUAL "glog")
        set(dependencies gflags)
        set(extra -DBUILD_TESTING=OFF -DWITH_GTEST=OFF -DWITH_GMOCK=OFF -DWITH_GFLAGS=ON -DWITH_UNWIND=none
            -DWITH_PKGCONFIG=ON)
    elseif(name STREQUAL "libevent")
        set(extra -DEVENT__DISABLE_TESTS=ON -DEVENT__DISABLE_BENCHMARK=ON
            -DEVENT__DISABLE_SAMPLES=ON -DEVENT__DISABLE_REGRESS=ON
            -DEVENT__DISABLE_OPENSSL=ON -DEVENT__LIBRARY_TYPE=SHARED
            -DCMAKE_POLICY_VERSION_MINIMUM=3.5)
        if(NOT RAFTPP_DEPS_SHARED)
            list(APPEND extra -DEVENT__LIBRARY_TYPE=STATIC)
        endif()
    elseif(name STREQUAL "folly")
        set(dependencies boost openssl fmt fast_float gflags glog libevent libsodium zstd zlib)
        set(extra -DBUILD_TESTS=OFF -DBUILD_BENCHMARKS=OFF -DPYTHON_EXTENSIONS=OFF
            "-DCMAKE_PROJECT_folly_INCLUDE=${CMAKE_CURRENT_FUNCTION_LIST_DIR}/folly-sdk.cmake"
            -DFOLLY_BUILD_CLI_PROGRAM_OPTIONS=OFF -DFOLLY_BUILD_OPENSSL=OFF
            -DFETCHCONTENT_FULLY_DISCONNECTED=ON
            -DCMAKE_DISABLE_FIND_PACKAGE_GTest=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_boost_program_options=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_BZip2=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_LibLZMA=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_Snappy=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_LZ4=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_Libiberty=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_LibAIO=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_LibUring=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_LibUnwind=TRUE
            -DBOOST_LINK_STATIC=OFF)
    elseif(name STREQUAL "fizz")
        set(subdir fizz)
        set(dependencies folly openssl fmt gflags glog libevent libsodium zstd zlib)
        set(extra -DBUILD_TESTS=OFF -DBUILD_EXAMPLES=OFF
            -DCMAKE_DISABLE_FIND_PACKAGE_liboqs=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_aegis=TRUE)
    elseif(name STREQUAL "mvfst")
        set(dependencies folly fizz boost openssl fmt gflags glog)
        set(extra -DBUILD_TESTS=OFF -DMVFST_USE_HABERDASHERY=OFF)
    endif()
    set(RAFTPP_SDK_EXTRA "${extra}" PARENT_SCOPE)
    set(RAFTPP_SDK_DEPENDENCIES "${dependencies}" PARENT_SCOPE)
    set(RAFTPP_SDK_SUBDIR "${subdir}" PARENT_SCOPE)
endfunction()
