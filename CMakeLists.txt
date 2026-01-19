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

# FUSE dependency (platform-specific)
if(APPLE)
    # macFUSE on macOS (can be installed as framework or via pkg-config)
    find_library(FUSE_LIBRARIES
        NAMES osxfuse macfuse
        PATHS /usr/local/lib /Library/Frameworks/macFUSE.framework
        NO_DEFAULT_PATH
    )
    find_path(FUSE_INCLUDE_DIRS
        NAMES fuse/fuse.h
        PATHS /usr/local/include /Library/Frameworks/macFUSE.framework/Headers
        NO_DEFAULT_PATH
    )

    # If not found in standard locations, try framework approach
    if(NOT FUSE_LIBRARIES)
        set(FUSE_LIBRARIES "/Library/Frameworks/macFUSE.framework/macFUSE")
    endif()
    if(NOT FUSE_INCLUDE_DIRS)
        set(FUSE_INCLUDE_DIRS "/usr/local/include")
    endif()

    if(NOT EXISTS "${FUSE_LIBRARIES}" OR NOT EXISTS "${FUSE_INCLUDE_DIRS}/fuse/fuse.h")
        message(FATAL_ERROR "macFUSE not found. Install with: brew install macfuse")
    endif()

    message(STATUS "Found macFUSE: ${FUSE_LIBRARIES}")
    message(STATUS "FUSE include dir: ${FUSE_INCLUDE_DIRS}")
else()
    # libfuse3 on Linux
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(FUSE3 REQUIRED fuse3)
    set(FUSE_LIBRARIES ${FUSE3_LIBRARIES})
    set(FUSE_INCLUDE_DIRS ${FUSE3_INCLUDE_DIRS})
    message(STATUS "Found FUSE3: ${FUSE_LIBRARIES}")
endif()

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
    ${FUSE_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(valkyrie
    ${AWSSDK_LINK_LIBRARIES}
    ${FUSE_LIBRARIES}
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

add_executable(test_config tests/test_config.cpp src/config.cpp)
target_include_directories(test_config PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(test_config ${AWSSDK_LINK_LIBRARIES})
