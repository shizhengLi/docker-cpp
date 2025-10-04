# Source libraries

# Core library
add_library(docker-cpp-core
    core/error.cpp
    core/logger.cpp
    core/config.cpp
    core/event.cpp
)

target_include_directories(docker-cpp-core
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Namespace library
add_library(docker-cpp-namespace
    namespace/namespace_manager.cpp
    namespace/process_manager.cpp
)

target_include_directories(docker-cpp-namespace
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-namespace
    PUBLIC
    docker-cpp-core
)

# Cgroup library
add_library(docker-cpp-cgroup
    cgroup/cgroup_manager.cpp
    cgroup/resource_monitor.cpp
)

target_include_directories(docker-cpp-cgroup
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-cgroup
    PUBLIC
    docker-cpp-core
)

# Main executable
add_executable(docker-cpp
    main.cpp
    cli/docker_cli.cpp
)

target_link_libraries(docker-cpp
    PRIVATE
    docker-cpp-core
    docker-cpp-namespace
    docker-cpp-cgroup
)

# Test runner executable
add_executable(docker-cpp-test-runner
    test_runner.cpp
)

target_link_libraries(docker-cpp-test-runner
    PRIVATE
    docker-cpp-core
    docker-cpp-namespace
)