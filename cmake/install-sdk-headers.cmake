if(RAFTPP_SDK_NAME STREQUAL "asio")
    if(EXISTS "${RAFTPP_SDK_SOURCE}/asio/include/asio.hpp")
        set(headers "${RAFTPP_SDK_SOURCE}/asio/include")
    else()
        set(headers "${RAFTPP_SDK_SOURCE}/include")
    endif()
    file(INSTALL "${headers}/asio" "${headers}/asio.hpp"
        DESTINATION "${RAFTPP_SDK_PREFIX}/include")
elseif(RAFTPP_SDK_NAME STREQUAL "asyncpp")
    file(INSTALL "${RAFTPP_SDK_SOURCE}/include/asyncpp"
        DESTINATION "${RAFTPP_SDK_PREFIX}/include")
    set(package "${RAFTPP_SDK_PREFIX}/lib/cmake/asyncpp")
    file(MAKE_DIRECTORY "${package}")
    file(WRITE "${package}/asyncpp-config.cmake"
        "include(CMakeFindDependencyMacro)\nfind_dependency(Threads)\nif(NOT TARGET asyncpp::asyncpp)\nadd_library(asyncpp::asyncpp INTERFACE IMPORTED)\nget_filename_component(_asyncpp_prefix \"\${CMAKE_CURRENT_LIST_DIR}/../../..\" ABSOLUTE)\nset_target_properties(asyncpp::asyncpp PROPERTIES INTERFACE_INCLUDE_DIRECTORIES \"\${_asyncpp_prefix}/include\" INTERFACE_LINK_LIBRARIES Threads::Threads INTERFACE_COMPILE_FEATURES cxx_std_20)\nendif()\n")
else()
    message(FATAL_ERROR "Unsupported header SDK: ${RAFTPP_SDK_NAME}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/install-sdk-licenses.cmake")
