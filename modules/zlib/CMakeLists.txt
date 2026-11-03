cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependency-module.cmake")
raftpp_add_dependency_module(zlib)

find_package(ZLIB REQUIRED)
target_link_libraries(raftpp_zlib PUBLIC ZLIB::ZLIB)
raftpp_dependency_package("find_dependency(ZLIB)")

raftpp_finish_dependency_module(zlib)
