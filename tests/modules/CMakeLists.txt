# These validate only the SDK boundary used by the module, never upstream suites.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    message(STATUS "raftpp: SDK module checks require Release; not built in this configuration")
    return()
endif()
foreach(module IN ITEMS asyncpp asio mvfst msquic lz4 zstd zlib rocksdb sqlite)
    if(TARGET raftpp::${module} AND (NOT DEFINED raftpp_RESOLVED_COMPONENTS
       OR module IN_LIST raftpp_RESOLVED_COMPONENTS))
        add_executable(raftpp_module_${module}_check ${module}.cpp)
        target_link_libraries(raftpp_module_${module}_check PRIVATE raftpp::${module})
        if(module STREQUAL "rocksdb")
            if(NOT TARGET RocksDB::rocksdb)
                find_package(RocksDB 11.13.2 CONFIG REQUIRED)
            endif()
            target_link_libraries(raftpp_module_${module}_check PRIVATE RocksDB::rocksdb)
        elseif(module STREQUAL "sqlite")
            if(NOT TARGET SQLite::SQLite3)
                find_package(SQLite3 3.24 REQUIRED)
            endif()
            target_link_libraries(raftpp_module_${module}_check PRIVATE SQLite::SQLite3)
        endif()
        set_target_properties(raftpp_module_${module}_check PROPERTIES CXX_EXTENSIONS OFF
            EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
            EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
        add_test(NAME raftpp.module_${module} COMMAND raftpp_module_${module}_check
            CONFIGURATIONS Release)
        set_tests_properties(raftpp.module_${module} PROPERTIES TIMEOUT 30 LABELS "module;adapter")
    endif()
endforeach()

if(TARGET raftpp_asio)
    add_executable(raftpp_composition_module_tests composition.cpp)
    target_link_libraries(raftpp_composition_module_tests PRIVATE raftpp::core raftpp::asio)
    set_target_properties(raftpp_composition_module_tests PROPERTIES CXX_EXTENSIONS OFF
        EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE
        EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
        EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
    set(composition_suites timer cluster)
    if(TARGET raftpp::uv)
        target_link_libraries(raftpp_composition_module_tests PRIVATE raftpp::uv)
        list(APPEND composition_suites uv)
        foreach(codec IN ITEMS lz4 zstd zlib)
            if(TARGET raftpp::${codec})
                target_link_libraries(raftpp_composition_module_tests PRIVATE raftpp::${codec})
                list(APPEND composition_suites ${codec})
            endif()
        endforeach()
    endif()
    foreach(suite IN LISTS composition_suites)
        add_test(NAME raftpp.composition_${suite} COMMAND raftpp_composition_module_tests ${suite}
            CONFIGURATIONS Release)
        set_tests_properties(raftpp.composition_${suite} PROPERTIES TIMEOUT 30 LABELS "module;adapter")
    endforeach()
endif()
