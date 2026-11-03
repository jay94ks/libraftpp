cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependency-module.cmake")
raftpp_add_dependency_module(asyncpp)

find_package(Threads REQUIRED)
# Header-only upstream; use the pinned checkout without configuring its tests.
if(EXISTS "${RAFTPP_DEPS_DIR}/asyncpp/include/asyncpp/dispatcher.h")
    target_include_directories(raftpp_asyncpp SYSTEM PUBLIC
        "$<BUILD_INTERFACE:${RAFTPP_DEPS_DIR}/asyncpp/include>")
    if(RAFTPP_INSTALL)
        install(DIRECTORY "${RAFTPP_DEPS_DIR}/asyncpp/include/asyncpp"
            DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.h")
    endif()
else()
    find_package(asyncpp CONFIG REQUIRED)
    target_link_libraries(raftpp_asyncpp PUBLIC asyncpp::asyncpp)
    raftpp_dependency_package("find_dependency(asyncpp CONFIG)")
endif()
target_link_libraries(raftpp_asyncpp PUBLIC Threads::Threads)
raftpp_dependency_package("find_dependency(Threads)")

raftpp_finish_dependency_module(asyncpp)
