include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# Private dependencies propagate only when their owning module is static.
get_target_property(_raftpp_core_type raftpp_core TYPE)
set(RAFTPP_PACKAGE_NEEDS_UV ${RAFTPP_WITH_UV})
set(RAFTPP_PACKAGE_NEEDS_LZ4 OFF)
set(RAFTPP_PACKAGE_NEEDS_BACKTRACE OFF)
if(_raftpp_core_type STREQUAL "STATIC_LIBRARY")
    set(RAFTPP_PACKAGE_NEEDS_BACKTRACE ${RAFTPP_WITH_BACKTRACE})
endif()
if(TARGET raftpp_uv)
    get_target_property(_raftpp_uv_type raftpp_uv TYPE)
    if(_raftpp_uv_type STREQUAL "STATIC_LIBRARY")
        if(RAFTPP_WITH_LZ4 AND NOT RAFTPP_MODULE_LZ4)
            set(RAFTPP_PACKAGE_NEEDS_LZ4 ON)
        endif()
    endif()
endif()
set(RAFTPP_AVAILABLE_COMPONENTS core)
if(TARGET raftpp_uv)
    list(APPEND RAFTPP_AVAILABLE_COMPONENTS uv)
endif()
set(RAFTPP_PACKAGE_DEPENDENCIES "")
foreach(target IN LISTS dependency_targets)
    get_target_property(module ${target} EXPORT_NAME)
    list(APPEND RAFTPP_AVAILABLE_COMPONENTS "${module}")
    get_property(packages GLOBAL PROPERTY RAFTPP_DEPENDENCY_PACKAGES_${module})
    list(REMOVE_DUPLICATES packages)
    string(JOIN "\n" code ${packages})
    string(APPEND RAFTPP_PACKAGE_DEPENDENCIES
        "if(\"${module}\" IN_LIST raftpp_RESOLVED_COMPONENTS)\n${code}\nendif()\n")
endforeach()
set(RAFTPP_PACKAGE_UV_NEEDS_LZ4_MODULE OFF)
if(TARGET raftpp_uv AND _raftpp_uv_type STREQUAL "STATIC_LIBRARY"
   AND RAFTPP_WITH_LZ4 AND RAFTPP_MODULE_LZ4)
    set(RAFTPP_PACKAGE_UV_NEEDS_LZ4_MODULE ON)
endif()
set(_raftpp_package_dir "${CMAKE_INSTALL_LIBDIR}/cmake/raftpp")
if(TARGET raftpp_mvfst)
    install(FILES "${PROJECT_SOURCE_DIR}/cmake/FindSodium.cmake"
        DESTINATION "${_raftpp_package_dir}")
endif()
configure_package_config_file("${CMAKE_CURRENT_SOURCE_DIR}/cmake/raftpp-config.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/raftpp-config.cmake"
    INSTALL_DESTINATION "${_raftpp_package_dir}")

set(_raftpp_install_targets raftpp raftpp_core ${dependency_targets})
if(TARGET raftpp_uv)
    list(APPEND _raftpp_install_targets raftpp_uv)
endif()
install(TARGETS ${_raftpp_install_targets} EXPORT raftpp-targets
    ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
    RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}")
# Install exactly this configuration's active public/generated headers.
install(DIRECTORY "${RAFTPP_GENERATED_INCLUDE_DIR}/"
    DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}" FILES_MATCHING PATTERN "*.hpp")
install(EXPORT raftpp-targets NAMESPACE raftpp::
    FILE raftpp-targets.cmake DESTINATION "${_raftpp_package_dir}")
install(FILES "${CMAKE_CURRENT_BINARY_DIR}/raftpp-config.cmake"
    DESTINATION "${_raftpp_package_dir}")
install(FILES "${PROJECT_SOURCE_DIR}/LICENSE.md" "${PROJECT_SOURCE_DIR}/LICENSE.en.md"
    DESTINATION "${CMAKE_INSTALL_DATADIR}/raftpp")
