# These validate only the SDK boundary used by the module, never upstream suites.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "raftpp: SDK module checks require Release; not built in this configuration")
    return()
endif()
foreach(module IN ITEMS asyncpp asio mvfst msquic lz4 zstd zlib)
    if(TARGET raftpp::${module} AND (NOT DEFINED raftpp_RESOLVED_COMPONENTS
       OR module IN_LIST raftpp_RESOLVED_COMPONENTS))
        add_executable(raftpp_module_${module}_check ${module}.cpp)
        target_link_libraries(raftpp_module_${module}_check PRIVATE raftpp::${module})
        set_target_properties(raftpp_module_${module}_check PROPERTIES CXX_EXTENSIONS OFF
            EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
        add_test(NAME raftpp.module_${module} COMMAND raftpp_module_${module}_check
            CONFIGURATIONS Release)
        set_tests_properties(raftpp.module_${module} PROPERTIES TIMEOUT 30 LABELS "module;adapter")
    endif()
endforeach()
