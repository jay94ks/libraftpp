cmake_minimum_required(VERSION 3.20)
foreach(required IN ITEMS RAFTPP_BINARY_DIR RAFTPP_SOURCE_DIR RAFTPP_INSTALL_CMAKE_DIR)
    if(NOT DEFINED ${required} OR "${${required}}" STREQUAL "")
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
set(work "${RAFTPP_BINARY_DIR}/tests/package-install/${RAFTPP_CONFIG}")
file(REMOVE_RECURSE "${work}")
file(MAKE_DIRECTORY "${work}")
function(run)
    execute_process(COMMAND ${ARGV} RESULT_VARIABLE result)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Package check command failed (${result}): ${ARGV}")
    endif()
endfunction()
run("${CMAKE_COMMAND}" --install "${RAFTPP_BINARY_DIR}" --config "${RAFTPP_CONFIG}" --prefix "${work}/prefix")
file(RENAME "${work}/prefix" "${work}/relocated")
set(configure "${CMAKE_COMMAND}" -S "${RAFTPP_SOURCE_DIR}/tests/package" -B "${work}/consumer"
    "-Draftpp_DIR=${work}/relocated/${RAFTPP_INSTALL_CMAKE_DIR}"
    "-DCMAKE_BUILD_TYPE=${RAFTPP_CONFIG}"
    "-DCMAKE_CXX_COMPILER=${RAFTPP_CXX_COMPILER}")
if(DEFINED RAFTPP_CONSUMER_FLAGS)
    list(APPEND configure "-DCMAKE_CXX_FLAGS=${RAFTPP_CONSUMER_FLAGS}")
endif()
if(NOT RAFTPP_PACKAGE_NEEDS_PKGCONFIG)
    list(APPEND configure -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE)
endif()
if(RAFTPP_DEPENDENCY_PREFIX)
    set(prefixes)
    foreach(module IN ITEMS asyncpp asio mvfst msquic lz4 zstd zlib rocksdb sqlite)
        list(APPEND prefixes "${RAFTPP_DEPENDENCY_PREFIX}/${module}")
    endforeach()
    file(GLOB mvfst_prefixes LIST_DIRECTORIES TRUE "${RAFTPP_DEPENDENCY_PREFIX}/*")
    foreach(prefix IN LISTS mvfst_prefixes)
        if(IS_DIRECTORY "${prefix}")
            list(APPEND prefixes "${prefix}")
        endif()
    endforeach()
    file(WRITE "${work}/sdk-prefixes.cmake"
        "set(CMAKE_PREFIX_PATH [==[${prefixes}]==] CACHE STRING \"SDK prefixes\")\n")
    list(APPEND configure -C "${work}/sdk-prefixes.cmake")
    # Package discovery does not configure the loader for transitive SDK DSOs.
    # Keep preparation-machine paths in this test process, never in the package.
    if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Linux")
        foreach(prefix IN LISTS prefixes)
            set(ENV{LD_LIBRARY_PATH} "${prefix}/lib:${prefix}/lib64:$ENV{LD_LIBRARY_PATH}")
        endforeach()
    endif()
endif()
foreach(license IN ITEMS LICENSE.md LICENSE.en.md)
    if(NOT EXISTS "${work}/relocated/${RAFTPP_DATADIR}/raftpp/${license}")
        message(FATAL_ERROR "Project license was not installed: ${license}")
    endif()
endforeach()
run(${configure})
run("${CMAKE_COMMAND}" --build "${work}/consumer" --config "${RAFTPP_CONFIG}" --parallel 2)
run("${CMAKE_CTEST_COMMAND}" --test-dir "${work}/consumer" --build-config "${RAFTPP_CONFIG}" --output-on-failure)

# Prove selective consumption does not require unrelated active SDKs.
set(component_checks core)
foreach(module IN ITEMS asio mvfst rocksdb sqlite)
    if(EXISTS "${work}/relocated/include/raftpp/${module}/${module}.hpp")
        list(APPEND component_checks "${module}")
    endif()
endforeach()
foreach(component IN LISTS component_checks)
    set(component_configure "${CMAKE_COMMAND}" -S "${RAFTPP_SOURCE_DIR}/tests/package/component"
        -B "${work}/component-${component}"
        "-Draftpp_DIR=${work}/relocated/${RAFTPP_INSTALL_CMAKE_DIR}"
        "-DRAFTPP_COMPONENT=${component}" "-DCMAKE_BUILD_TYPE=${RAFTPP_CONFIG}"
        "-DCMAKE_CXX_COMPILER=${RAFTPP_CXX_COMPILER}"
        -DCMAKE_DISABLE_FIND_PACKAGE_msquic=TRUE)
    if(NOT component STREQUAL "mvfst")
        list(APPEND component_configure -DCMAKE_DISABLE_FIND_PACKAGE_mvfst=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_zstd=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_PkgConfig=TRUE)
    endif()
    if(component STREQUAL "core")
        list(APPEND component_configure -DCMAKE_DISABLE_FIND_PACKAGE_Threads=TRUE
            -DCMAKE_DISABLE_FIND_PACKAGE_asyncpp=TRUE)
    endif()
    if(EXISTS "${work}/sdk-prefixes.cmake")
        list(APPEND component_configure -C "${work}/sdk-prefixes.cmake")
    endif()
    run(${component_configure})
    run("${CMAKE_COMMAND}" --build "${work}/component-${component}" --config "${RAFTPP_CONFIG}" --parallel 2)
    run("${CMAKE_CTEST_COMMAND}" --test-dir "${work}/component-${component}"
        --build-config "${RAFTPP_CONFIG}" --output-on-failure)
endforeach()
