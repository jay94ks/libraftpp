cmake_minimum_required(VERSION 3.20)
include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/dependency-module.cmake")
raftpp_add_dependency_module(lz4)

find_package(PkgConfig REQUIRED)
pkg_check_modules(RAFTPP_MODULE_LZ4 REQUIRED IMPORTED_TARGET GLOBAL "liblz4>=1.7.1")
target_link_libraries(raftpp_lz4 PUBLIC PkgConfig::RAFTPP_MODULE_LZ4)
raftpp_dependency_package("find_dependency(PkgConfig)
pkg_check_modules(RAFTPP_MODULE_LZ4 REQUIRED IMPORTED_TARGET GLOBAL \"liblz4>=1.7.1\")")

raftpp_finish_dependency_module(lz4)
