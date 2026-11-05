# Language integration tests exercise only the bindings and their native boundary.
if(NOT CMAKE_CONFIGURATION_TYPES AND NOT CMAKE_BUILD_TYPE STREQUAL "Release")
    return()
endif()
add_executable(raftpp_bindings_tests "${PROJECT_SOURCE_DIR}/tests/bindings.cpp")
target_compile_features(raftpp_bindings_tests PRIVATE cxx_std_20)
target_link_libraries(raftpp_bindings_tests PRIVATE raftpp::bindings)
set_target_properties(raftpp_bindings_tests PROPERTIES CXX_EXTENSIONS OFF
    EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
if(RAFTPP_WITH_UV)
    add_test(NAME raftpp.bindings_native COMMAND raftpp_bindings_tests CONFIGURATIONS Release)
    set_tests_properties(raftpp.bindings_native PROPERTIES TIMEOUT 30 LABELS "binding;adapter")
endif()
add_library(raftpp_bindings_test_plugin SHARED "${PROJECT_SOURCE_DIR}/tests/bindings_plugin.cpp")
target_link_libraries(raftpp_bindings_test_plugin PRIVATE raftpp::bindings raftpp::core)
set_target_properties(raftpp_bindings_test_plugin PROPERTIES CXX_EXTENSIONS OFF
    EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
add_dependencies(raftpp_bindings_tests raftpp_bindings_test_plugin)
if(TARGET raftpp_nodejs)
    find_program(RAFTPP_NODE_EXECUTABLE node REQUIRED)
    add_test(NAME raftpp.bindings_nodejs COMMAND "${CMAKE_COMMAND}" -E env
        "RAFTPP_NODE_ADDON=$<TARGET_FILE:raftpp_nodejs>"
        "RAFTPP_BINDING_PLUGIN=$<TARGET_FILE:raftpp_bindings_test_plugin>"
        "${RAFTPP_NODE_EXECUTABLE}" --expose-gc --test
        "${CMAKE_CURRENT_SOURCE_DIR}/nodejs/test/node.test.js")
    set_tests_properties(raftpp.bindings_nodejs PROPERTIES TIMEOUT 60 CONFIGURATIONS Release LABELS "binding;adapter")
endif()
find_program(RAFTPP_DOTNET_EXECUTABLE dotnet)
if(RAFTPP_DOTNET_EXECUTABLE)
    add_test(NAME raftpp.bindings_csharp COMMAND "${CMAKE_COMMAND}" -E env
        "LD_LIBRARY_PATH=$<TARGET_FILE_DIR:raftpp_bindings>:$ENV{LD_LIBRARY_PATH}"
        "RAFTPP_BINDING_PLUGIN=$<TARGET_FILE:raftpp_bindings_test_plugin>"
        "${RAFTPP_DOTNET_EXECUTABLE}" run --project
        "${CMAKE_CURRENT_SOURCE_DIR}/csharp/tests/Raftpp.Tests.csproj" --configuration Release)
    set_tests_properties(raftpp.bindings_csharp PROPERTIES TIMEOUT 90 CONFIGURATIONS Release LABELS "binding;adapter")
endif()

add_executable(raftpp_bindings_runtime_tests "${PROJECT_SOURCE_DIR}/tests/bindings_runtime.cpp")
target_link_libraries(raftpp_bindings_runtime_tests PRIVATE raftpp::bindings)
set_target_properties(raftpp_bindings_runtime_tests PROPERTIES CXX_EXTENSIONS OFF
    EXCLUDE_FROM_DEFAULT_BUILD_DEBUG TRUE EXCLUDE_FROM_DEFAULT_BUILD_RELWITHDEBINFO TRUE
    EXCLUDE_FROM_DEFAULT_BUILD_MINSIZEREL TRUE)
add_dependencies(raftpp_bindings_runtime_tests raftpp_bindings_test_plugin)
add_test(NAME raftpp.bindings_runtime COMMAND raftpp_bindings_runtime_tests
    "$<TARGET_FILE:raftpp_bindings_test_plugin>" CONFIGURATIONS Release)
set_tests_properties(raftpp.bindings_runtime PROPERTIES TIMEOUT 30 LABELS "binding;adapter")
