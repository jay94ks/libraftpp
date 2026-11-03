cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependency-module.cmake")
raftpp_add_dependency_module(msquic)

find_package(msquic CONFIG REQUIRED)
target_link_libraries(raftpp_msquic PUBLIC msquic::msquic)
raftpp_dependency_package("find_dependency(msquic CONFIG)")

raftpp_finish_dependency_module(msquic)
