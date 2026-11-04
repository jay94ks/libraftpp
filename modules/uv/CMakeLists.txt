add_library(raftpp_uv
    src/disk.cpp
    src/file.cpp
    src/writer.cpp
    src/segment_buffer.cpp
    src/probe.cpp
    src/log_writer.cpp
    src/file_backend.cpp
    src/uv_io.cpp
    src/uv_storage.cpp
    src/uv_timer.cpp
    src/tcp_transport.cpp
    src/uv_stream.cpp
    src/stream_transport.cpp
    src/compression.cpp
)
add_library(raftpp::uv ALIAS raftpp_uv)
raftpp_configure_module(raftpp_uv uv raftpp/uv/export.hpp RAFTPP_UV_API)
# Shared implementation helpers are private to the build, never installed.
raftpp_use_private_headers(raftpp_uv)
target_compile_definitions(raftpp_uv PRIVATE _GNU_SOURCE _FILE_OFFSET_BITS=64)
target_link_libraries(raftpp_uv PUBLIC raftpp::core PkgConfig::RAFTPP_UV)
if(BUILD_SHARED_LIBS)
    # UV may be the consumer's only direct dependency after relocation.
    set_property(TARGET raftpp_uv APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
endif()
if(RAFTPP_WITH_LZ4)
    target_sources(raftpp_uv PRIVATE src/compression_lz4.cpp)
    if(TARGET raftpp_lz4)
        target_link_libraries(raftpp_uv PRIVATE raftpp::lz4)
    else()
        target_link_libraries(raftpp_uv PRIVATE PkgConfig::RAFTPP_LZ4)
    endif()
endif()

file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
raftpp_stage_headers(${public_headers})
