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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-core
    PUBLIC
    $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
    $<$<BOOL:${nlohmann_json_FOUND}>:nlohmann_json::nlohmann_json>
)

# Add compile definitions
target_compile_definitions(docker-cpp-core
    PRIVATE
        DOCKER_CPP_CORE_EXPORTS
)

# Namespace library
add_library(docker-cpp-namespace
    namespace/namespace_manager.cpp
    namespace/process_manager.cpp
)

target_include_directories(docker-cpp-namespace
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
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
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-cgroup
    PUBLIC
    docker-cpp-core
)

# Plugin library
add_library(docker-cpp-plugin
    plugin/plugin_registry.cpp
)

target_include_directories(docker-cpp-plugin
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-plugin
    PUBLIC
    docker-cpp-core
)

# Configuration library
add_library(docker-cpp-config
    config/config_manager.cpp
)

target_include_directories(docker-cpp-config
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(docker-cpp-config
    PUBLIC
    docker-cpp-core
    $<$<BOOL:${nlohmann_json_FOUND}>:nlohmann_json::nlohmann_json>
)

# Add compile definitions for config library
target_compile_definitions(docker-cpp-config
    PRIVATE
        HAS_NLOHMANN_JSON=$<$<BOOL:${nlohmann_json_FOUND}>:1>$<$<NOT:$<BOOL:${nlohmann_json_FOUND}>>:0>
)

# Consolidated library
add_library(docker-cpp-lib STATIC
    core/error.cpp
    core/logger.cpp
    core/config.cpp
    core/event.cpp
    namespace/namespace_manager.cpp
    namespace/process_manager.cpp
    cgroup/cgroup_manager.cpp
    cgroup/resource_monitor.cpp
    plugin/plugin_registry.cpp
    config/config_manager.cpp
)

target_link_libraries(docker-cpp-lib
    PUBLIC
    $<$<BOOL:${fmt_FOUND}>:fmt::fmt>
    $<$<BOOL:${spdlog_FOUND}>:spdlog::spdlog>
    $<$<BOOL:${nlohmann_json_FOUND}>:nlohmann_json::nlohmann_json>
    GTest::gtest
)

target_include_directories(docker-cpp-lib
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Add compile definitions for consolidated library
target_compile_definitions(docker-cpp-lib
    PRIVATE
        HAS_NLOHMANN_JSON=$<$<BOOL:${nlohmann_json_FOUND}>:1>$<$<NOT:$<BOOL:${nlohmann_json_FOUND}>>:0>
)

# Main executable
add_executable(docker-cpp
    main.cpp
    cli/docker_cli.cpp
)

target_link_libraries(docker-cpp
    PRIVATE
    docker-cpp-lib
)