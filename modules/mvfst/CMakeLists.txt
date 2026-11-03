cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependency-module.cmake")
raftpp_add_dependency_module(mvfst)

list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/../../cmake")
find_package(mvfst CONFIG REQUIRED)
# Bind only the transport component, rather than every monolithic SDK component.
target_link_libraries(raftpp_mvfst PUBLIC mvfst::mvfst_transport)
raftpp_dependency_package("set(_raftpp_module_path \"\${CMAKE_MODULE_PATH}\")
list(PREPEND CMAKE_MODULE_PATH \"\${CMAKE_CURRENT_LIST_DIR}\")
find_dependency(mvfst CONFIG)
set(CMAKE_MODULE_PATH \"\${_raftpp_module_path}\")
unset(_raftpp_module_path)")

raftpp_finish_dependency_module(mvfst)
