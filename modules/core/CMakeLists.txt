add_library(raftpp_core
    src/composed_io.cpp
    src/framed_transport.cpp
    src/memory.cpp
    src/tracing.cpp
    src/metadata.cpp
    src/checksum.cpp
    src/encoding.cpp
    src/node.cpp
    src/core.cpp
    src/core_api.cpp
    src/client.cpp
    src/convert.cpp
    src/membership.cpp
    src/replication.cpp
    src/receive_core.cpp
    src/timeout.cpp
    src/election.cpp
    src/message.cpp
    src/progress.cpp
    src/random.cpp
    src/receive.cpp
    src/buffer.cpp
    src/entry.cpp
    src/log.cpp
    src/snapshot.cpp
    src/error.cpp
    src/configuration.cpp
    src/trail.cpp
)
add_library(raftpp::core ALIAS raftpp_core)
raftpp_configure_module(raftpp_core core raftpp/export.hpp RAFTPP_API)

if(RAFTPP_WITH_BACKTRACE)
    target_sources(raftpp_core PRIVATE src/assert.cpp)
    target_link_libraries(raftpp_core PRIVATE raftpp::backtrace)
endif()
if(RAFTPP_WITH_FIXTURE)
    target_sources(raftpp_core PRIVATE src/fixture.cpp)
endif()

file(GLOB_RECURSE public_headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
if(NOT RAFTPP_WITH_FIXTURE)
    list(FILTER public_headers EXCLUDE REGEX "/fixture[.]hpp$")
endif()
raftpp_stage_headers(${public_headers})
