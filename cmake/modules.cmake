# Keep public header staging separate from private implementation search paths.
function(raftpp_configure_module target module export_header export_macro)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES
        EXPORT_NAME ${module}
        OUTPUT_NAME raftpp-${module}
        CXX_EXTENSIONS OFF
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN YES
        POSITION_INDEPENDENT_CODE ON)
    target_include_directories(${target}
        PUBLIC
            "$<BUILD_INTERFACE:${RAFTPP_GENERATED_INCLUDE_DIR}>"
            "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>"
        PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/privates")
    generate_export_header(${target}
        EXPORT_FILE_NAME "${RAFTPP_GENERATED_INCLUDE_DIR}/${export_header}"
        EXPORT_MACRO_NAME ${export_macro})
    set_property(GLOBAL APPEND PROPERTY RAFTPP_ACTIVE_HEADERS "${export_header}")
endfunction()

function(raftpp_stage_headers)
    foreach(header IN LISTS ARGN)
        file(RELATIVE_PATH relative "${CMAKE_CURRENT_SOURCE_DIR}/include" "${header}")
        configure_file("${header}" "${RAFTPP_GENERATED_INCLUDE_DIR}/${relative}" COPYONLY)
        set_property(GLOBAL APPEND PROPERTY RAFTPP_ACTIVE_HEADERS "${relative}")
    endforeach()
endfunction()

function(raftpp_use_private_headers target)
    target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/modules/core/privates")
    if(RAFTPP_WITH_UV)
        target_include_directories(${target} PRIVATE "${PROJECT_SOURCE_DIR}/modules/uv/privates")
    endif()
endfunction()

function(raftpp_prune_headers)
    get_property(active_headers GLOBAL PROPERTY RAFTPP_ACTIVE_HEADERS)
    file(GLOB_RECURSE staged_headers "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp/*.hpp")
    foreach(header IN LISTS staged_headers)
        file(RELATIVE_PATH relative "${RAFTPP_GENERATED_INCLUDE_DIR}" "${header}")
        if(NOT relative IN_LIST active_headers)
            file(REMOVE "${header}")
        endif()
    endforeach()
    foreach(module IN ITEMS asyncpp asio mvfst msquic lz4 zstd zlib)
        string(TOUPPER "${module}" upper)
        if(NOT RAFTPP_MODULE_${upper})
            file(REMOVE_RECURSE "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp/${module}")
        endif()
    endforeach()
    if(NOT RAFTPP_WITH_UV)
        file(REMOVE_RECURSE "${RAFTPP_GENERATED_INCLUDE_DIR}/raftpp/uv")
    endif()
endfunction()

function(raftpp_mirror_headers)
    # Different build configurations may configure concurrently. Compilers never
    # read this shared mirror; it reflects the last completed configuration.
    file(LOCK "${PROJECT_SOURCE_DIR}/.raftpp-headers.lock" GUARD FUNCTION TIMEOUT 30)
    file(REMOVE_RECURSE "${PROJECT_SOURCE_DIR}/include")
    file(MAKE_DIRECTORY "${PROJECT_SOURCE_DIR}/include")
    get_property(active_headers GLOBAL PROPERTY RAFTPP_ACTIVE_HEADERS)
    foreach(header IN LISTS active_headers)
        configure_file("${RAFTPP_GENERATED_INCLUDE_DIR}/${header}"
            "${PROJECT_SOURCE_DIR}/include/${header}" COPYONLY)
    endforeach()
endfunction()
