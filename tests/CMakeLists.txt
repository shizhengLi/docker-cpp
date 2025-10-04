# Tests configuration

if(GTest_FOUND)
    # Test executable
    add_executable(docker-cpp-tests
        unit/core/test_error.cpp
        unit/namespace/test_namespace_manager.cpp
        unit/cgroup/test_cgroup_manager.cpp
        integration/test_container_runtime.cpp
    )

    target_link_libraries(docker-cpp-tests
        PRIVATE
        gtest
        gtest_main
        docker-cpp-core
        docker-cpp-namespace
        docker-cpp-cgroup
    )

    # Add tests to CTest
    add_test(NAME UnitTests COMMAND docker-cpp-tests --gtest_filter=Unit.*)
    add_test(NAME IntegrationTests COMMAND docker-cpp-tests --gtest_filter=Integration.*)
else()
    message(WARNING "Google Test not found, skipping tests")
endif()

# Enable testing
enable_testing()