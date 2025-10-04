cmake_minimum_required(VERSION 3.20)

# Project configuration
project(docker-cpp
    VERSION 1.0.0
    DESCRIPTION "High-performance container runtime in modern C++"
    HOMEPAGE_URL "https://github.com/shizhengLi/docker-cpp"
    LANGUAGES CXX
)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build configuration
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler-specific flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic -Werror")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra")
set(CMAKE_CXX_FLAGS_RELWITHDEBINFO "-O2 -g -Wall -Wextra")

# Include Conan-generated toolchain
list(APPEND CMAKE_MODULE_PATH ${CMAKE_BINARY_DIR})
list(APPEND CMAKE_PREFIX_PATH ${CMAKE_BINARY_DIR})

# Check for Conan dependencies
if(EXISTS ${CMAKE_BINARY_DIR}/conan_toolchain.cmake)
    include(${CMAKE_BINARY_DIR}/conan_toolchain.cmake)
endif()

# Find packages - make some optional for development
find_package(PkgConfig QUIET)

# Try to find Google Test first
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Fall back to FetchContent if not found
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)
    set(GTest_FOUND TRUE)
endif()

# Try to find other packages, make them optional for now
find_package(fmt QUIET)
if(NOT fmt_FOUND)
    message(STATUS "fmt library not found - will use fallback implementation")
    set(FALLBACK_FMT ON)
endif()

find_package(spdlog QUIET)
if(NOT spdlog_FOUND)
    message(STATUS "spdlog library not found - will use fallback implementation")
    set(FALLBACK_SPDLOG ON)
endif()

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
    message(STATUS "nlohmann_json library not found - will use fallback implementation")
    set(FALLBACK_JSON ON)
endif()

# Project-wide includes
include_directories(${CMAKE_SOURCE_DIR}/include)

# Compiler-specific configuration
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /EHsc")
endif()

# Code coverage for Debug builds
if(CMAKE_BUILD_TYPE STREQUAL "Debug" AND CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Enable testing
enable_testing()

# Installation configuration
include(GNUInstallDirs)

install(TARGETS docker-cpp
    EXPORT docker-cpp-targets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(DIRECTORY include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.hpp"
)

# Export configuration
install(EXPORT docker-cpp-targets
    FILE docker-cpp-targets.cmake
    NAMESPACE docker-cpp::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/docker-cpp
)

# Generate config file
include(CMakePackageConfigHelpers)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/docker-cpp-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

configure_package_config_file(
    ${CMAKE_SOURCE_DIR}/cmake/docker-cpp-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/docker-cpp-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/docker-cpp
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/docker-cpp-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/docker-cpp-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/docker-cpp
)

# Package configuration
set(CPACK_PACKAGE_NAME "docker-cpp")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_VENDOR "Docker-CPP Community")
set(CPACK_GENERATOR "TGZ;DEB;RPM")

include(CPack)