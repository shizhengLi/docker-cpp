# Google Test integration for Docker-CPP tests

# Find Google Test
find_package(GTest REQUIRED)

# Enable testing
enable_testing()

# Core library tests
add_executable(tests-core
    unit/core/test_error.cpp
    unit/core/test_logger.cpp
    unit/core/test_event_simple.cpp
)

target_link_libraries(tests-core
    PRIVATE
    docker-cpp-core
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-core
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Namespace library tests
add_executable(tests-namespace
    unit/namespace/test_namespace_manager.cpp
)

target_link_libraries(tests-namespace
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-namespace
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Cgroup library tests (currently just a placeholder)
add_executable(tests-cgroup
    unit/cgroup/test_cgroup_manager.cpp
)

target_link_libraries(tests-cgroup
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-cgroup
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Plugin library tests
add_executable(tests-plugin
    unit/plugin/test_plugin_system.cpp
)

target_link_libraries(tests-plugin
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-plugin
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Configuration library tests
add_executable(tests-config
    unit/config/test_config_manager.cpp
)

target_link_libraries(tests-config
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-config
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Integration tests (currently just a placeholder)
add_executable(tests-integration
    integration/test_container_runtime.cpp
)

target_link_libraries(tests-integration
    PRIVATE
    docker-cpp-lib
    GTest::gtest
    GTest::gtest_main
)

target_include_directories(tests-integration
    PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

# Performance tests (currently disabled)
# add_executable(tests-performance
#     performance/test_namespace_performance.cpp
#     performance/test_cgroup_performance.cpp
# )

# target_link_libraries(tests-performance
#     PRIVATE
#     docker-cpp-lib
#     GTest::gtest
#     GTest::gtest_main
# )

# target_include_directories(tests-performance
#     PRIVATE
#     ${CMAKE_SOURCE_DIR}/include
# )

# Register tests with CTest
add_test(NAME CoreTests COMMAND tests-core --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_core.xml)
add_test(NAME NamespaceTests COMMAND tests-namespace --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_namespace.xml)
add_test(NAME CgroupTests COMMAND tests-cgroup --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_cgroup.xml)
add_test(NAME PluginTests COMMAND tests-plugin --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_plugin.xml)
add_test(NAME ConfigTests COMMAND tests-config --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_config.xml)
add_test(NAME IntegrationTests COMMAND tests-integration --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_integration.xml)

# Set test properties
set_tests_properties(
    CoreTests
    NamespaceTests
    CgroupTests
    PluginTests
    ConfigTests
    IntegrationTests
    PROPERTIES
    TIMEOUT 300
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
)

# Add custom test target
add_custom_target(run_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure --parallel
    DEPENDS tests-core tests-namespace tests-cgroup tests-plugin tests-config tests-integration
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all unit tests"
)

# Add performance test target (currently disabled)
# add_custom_target(run_performance_tests
#     COMMAND tests-performance --gtest_output=xml:${CMAKE_BINARY_DIR}/test_results_performance.xml
#     DEPENDS tests-performance
#     WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
#     COMMENT "Running performance tests"
# )

# Add memory check with Valgrind (Linux only)
if(UNIX AND NOT APPLE)
    find_program(VALGRIND_EXECUTABLE valgrind)
    if(VALGRIND_EXECUTABLE)
        add_custom_target(memcheck
            COMMAND ${VALGRIND_EXECUTABLE}
                    --leak-check=full
                    --show-leak-kinds=all
                    --track-origins=yes
                    --verbose
                    --error-exitcode=1
                    $<TARGET_FILE:tests-core>
            DEPENDS tests-core
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running memory leak check on core tests"
        )

        add_custom_target(memcheck_all
            COMMAND ${CMAKE_CTEST_COMMAND}
                    -T memcheck
                    --output-on-failure
            DEPENDS tests-core tests-namespace tests-cgroup tests-integration
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Running memory leak check on all tests"
        )
    endif()
endif()

# Code coverage target (Debug builds only)
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    find_program(GCOV_EXECUTABLE gcov)
    find_program(LCOV_EXECUTABLE lcov)
    find_program(GENHTML_EXECUTABLE genhtml)

    if(GCOV_EXECUTABLE AND LCOV_EXECUTABLE AND GENHTML_EXECUTABLE)
        add_custom_target(coverage
            COMMAND ${LCOV_EXECUTABLE} --capture --directory . --output-file coverage.info
            COMMAND ${LCOV_EXECUTABLE} --remove coverage.info '/usr/*' --output-file coverage.info
            COMMAND ${LCOV_EXECUTABLE} --remove coverage.info '*/build/*' --output-file coverage.info
            COMMAND ${GENHTML_EXECUTABLE} -o coverage_html coverage.info
            DEPENDS tests-core tests-namespace tests-cgroup tests-integration
            WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
            COMMENT "Generating code coverage report"
        )
    endif()
endif()