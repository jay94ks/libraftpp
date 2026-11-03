include_guard(GLOBAL)
include(GNUInstallDirs)

# Every raftpp module requires the mandatory core module.
set(RAFTPP_DEPS_DIR "${CMAKE_CURRENT_LIST_DIR}/../deps" CACHE PATH "Pinned dependency checkouts")
if(NOT DEFINED RAFTPP_INSTALL)
    option(RAFTPP_INSTALL "Install the selected SDK module" ON)
endif()

function(raftpp_add_dependency_module name)
    if(NOT TARGET raftpp::core)
        message(FATAL_ERROR "The ${name} module requires raftpp::core; configure the project root")
    endif()
    set_property(DIRECTORY PROPERTY RAFTPP_SDK_MODULE "${name}")
    add_library(raftpp_${name} "${CMAKE_CURRENT_SOURCE_DIR}/src/${name}.cpp")
    add_library(raftpp::${name} ALIAS raftpp_${name})
    string(TOUPPER "${name}" upper)
    raftpp_configure_module(raftpp_${name} ${name}
        "raftpp/${name}/export.hpp" "RAFTPP_${upper}_API")
    target_link_libraries(raftpp_${name} PUBLIC raftpp::core)
    if(BUILD_SHARED_LIBS)
        set_property(TARGET raftpp_${name} APPEND PROPERTY INSTALL_RPATH "$ORIGIN")
    endif()
    set(public_include "${CMAKE_CURRENT_SOURCE_DIR}/include")
    if(DEFINED RAFTPP_GENERATED_INCLUDE_DIR)
        set(public_include "${RAFTPP_GENERATED_INCLUDE_DIR}")
    endif()
    target_include_directories(raftpp_${name} PUBLIC
        "$<BUILD_INTERFACE:${public_include}>"
        "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")
    set_property(GLOBAL APPEND PROPERTY RAFTPP_DEPENDENCY_TARGETS raftpp_${name})
endfunction()

function(raftpp_dependency_package code)
    get_property(module DIRECTORY PROPERTY RAFTPP_SDK_MODULE)
    set_property(GLOBAL APPEND PROPERTY RAFTPP_DEPENDENCY_PACKAGES_${module} "${code}")
endfunction()

function(raftpp_finish_dependency_module name)
    if(COMMAND raftpp_stage_headers AND DEFINED RAFTPP_GENERATED_INCLUDE_DIR)
        # Match core/UV: consumers compile against their own build's copies.
        file(GLOB_RECURSE headers CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/include/*.hpp")
        raftpp_stage_headers(${headers})
    endif()
    if(RAFTPP_INSTALL)
        # Preserve upstream licenses even when vendoring header-only libraries.
        foreach(file LICENSE LICENSE.md LICENSE.txt LICENSE_1_0.txt NOTICE NOTICE.txt COPYING COPYING.LESSER COPYING.LIB lib/LICENSE lib/COPYING)
            if(EXISTS "${RAFTPP_DEPS_DIR}/${name}/${file}")
                # Preserve subdirectory components to avoid two COPYING collisions.
                get_filename_component(directory "${file}" DIRECTORY)
                install(FILES "${RAFTPP_DEPS_DIR}/${name}/${file}"
                    DESTINATION "${CMAKE_INSTALL_DATADIR}/raftpp/deps/${name}/${directory}")
            endif()
        endforeach()
    endif()
endfunction()
