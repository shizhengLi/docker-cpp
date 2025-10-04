cmake_minimum_required(VERSION 3.20)
project(docker-cpp VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Compiler flags
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -Wall -Wextra")

# Try to find packages - for now use basic system packages
find_package(PkgConfig QUIET)

# For testing, try to find Google Test
find_package(GTest QUIET)
if(NOT GTest_FOUND)
    # Try to find with pkg-config
    if(PkgConfig_FOUND)
        pkg_check_modules(GTEST QUIET gtest)
        pkg_check_modules(GTEST_MAIN QUIET gtest_main)
        if(GTEST_FOUND AND GTEST_MAIN_FOUND)
            set(GTest_FOUND TRUE)
        endif()
    endif()

    # If still not found, download Google Test
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            URL https://github.com/google/googletest/archive/v1.14.0.zip
        )
        FetchContent_MakeAvailable(googletest)
        set(GTest_FOUND TRUE)
    endif()
endif()

# Basic system includes
include_directories(${CMAKE_SOURCE_DIR}/include)

# Add subdirectories
add_subdirectory(src)
add_subdirectory(tests)

# Enable testing
enable_testing()

# Installation
install(TARGETS docker-cpp
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ DESTINATION include)