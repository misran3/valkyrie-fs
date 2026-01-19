cmake_minimum_required(VERSION 3.20)
project(valkyrie-fs VERSION 0.1.0 LANGUAGES CXX)

# C++20 required for concepts, ranges
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Compiler flags
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -fsanitize=address,undefined")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -march=native")

# Output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

# AWS SDK for S3
find_package(AWSSDK REQUIRED COMPONENTS s3)

# Dependencies (will add later)
# find_package(PkgConfig REQUIRED)
# pkg_check_modules(FUSE3 REQUIRED fuse3)

# Source files
set(SOURCES
    src/main.cpp
    src/cache_manager.cpp
    src/s3_worker_pool.cpp
)

# Main executable
add_executable(valkyrie ${SOURCES})

# Include directories
target_include_directories(valkyrie PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(valkyrie
    ${AWSSDK_LINK_LIBRARIES}
    pthread
)

# Print configuration
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "C++ standard: ${CMAKE_CXX_STANDARD}")

# Simple test executable (before we have GTest)
add_executable(test_types tests/test_types.cpp)
target_include_directories(test_types PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

add_executable(test_queue tests/test_thread_safe_queue.cpp)
target_include_directories(test_queue PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_queue pthread)

add_executable(test_cache_manager tests/test_cache_manager.cpp src/cache_manager.cpp)
target_include_directories(test_cache_manager PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_cache_manager pthread)

add_executable(test_s3_mock
    tests/test_s3_mock.cpp
    src/cache_manager.cpp
    src/s3_worker_pool.cpp
)
target_include_directories(test_s3_mock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_s3_mock
    ${AWSSDK_LINK_LIBRARIES}
    pthread
)

add_executable(test_predictor
    tests/test_predictor.cpp
    src/predictor.cpp
    src/cache_manager.cpp
    src/s3_worker_pool.cpp
)
target_include_directories(test_predictor PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_predictor
    ${AWSSDK_LINK_LIBRARIES}
    pthread
)
