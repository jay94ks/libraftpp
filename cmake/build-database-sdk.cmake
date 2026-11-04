# Prepare DB libraries and Tcl source-generation tools, without upstream suites/docs.
foreach(required RAFTPP_SDK_NAME RAFTPP_SDK_SOURCE RAFTPP_SDK_WORK RAFTPP_SDK_PREFIX)
    if(NOT DEFINED ${required})
        message(FATAL_ERROR "Missing ${required}")
    endif()
endforeach()
find_program(make NAMES gmake make REQUIRED)
file(MAKE_DIRECTORY "${RAFTPP_SDK_WORK}")
function(run)
    execute_process(COMMAND ${ARGV} WORKING_DIRECTORY "${RAFTPP_SDK_WORK}"
        RESULT_VARIABLE result COMMAND_ECHO STDOUT)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "${RAFTPP_SDK_NAME} preparation failed (${result})")
    endif()
endfunction()
if(RAFTPP_SDK_NAME STREQUAL "tcl")
    run(sh "${RAFTPP_SDK_SOURCE}/unix/configure" "--prefix=${RAFTPP_SDK_PREFIX}"
        --disable-shared --disable-load --disable-symbols --enable-threads)
    run("${make}" "-j${RAFTPP_SDK_JOBS}" binaries)
    run("${make}" install-binaries install-libraries install-headers)
elseif(RAFTPP_SDK_NAME STREQUAL "sqlite")
    if(RAFTPP_SDK_SHARED)
        set(link --enable-shared --disable-static)
    else()
        set(link --disable-shared --enable-static)
    endif()
    run("${CMAKE_COMMAND}" -E env "CFLAGS=-O2 -fPIC" sh "${RAFTPP_SDK_SOURCE}/configure"
        "--prefix=${RAFTPP_SDK_PREFIX}" ${link}
        "--with-tclsh=${RAFTPP_TCL_PREFIX}/bin/tclsh8.6"
        "--with-tcl=${RAFTPP_TCL_PREFIX}/lib" --disable-tcl
        --disable-readline --disable-math --disable-load-extension --disable-fts3 --disable-fts4
        --disable-fts5 --disable-rtree --disable-session)
    run("${make}" "-j${RAFTPP_SDK_JOBS}" install-lib install-dll install-headers)
else()
    message(FATAL_ERROR "Unsupported database SDK ${RAFTPP_SDK_NAME}")
endif()
include("${CMAKE_CURRENT_LIST_DIR}/install-sdk-licenses.cmake")
