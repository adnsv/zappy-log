cmake_minimum_required(VERSION 3.20)

project(zappy-log LANGUAGES CXX)

add_library(zappy-log INTERFACE)

target_compile_features(zappy-log INTERFACE cxx_std_20)

target_include_directories(zappy-log INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

find_package(Threads REQUIRED)
target_link_libraries(zappy-log INTERFACE "Threads::Threads")

# Build the example and tests only when zappy-log is the top-level project,
# not when a consumer pulls it in with add_subdirectory or FetchContent.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
    set(ZAPPYLOG_TOP_LEVEL ON)
else()
    set(ZAPPYLOG_TOP_LEVEL OFF)
endif()

option(ZAPPYLOG_BUILD_EXAMPLE "Build zappy-log example" ${ZAPPYLOG_TOP_LEVEL})
if (ZAPPYLOG_BUILD_EXAMPLE)
    add_subdirectory("example")
endif()

option(ZAPPYLOG_BUILD_TESTS "Build zappy-log tests" ${ZAPPYLOG_TOP_LEVEL})
if (ZAPPYLOG_BUILD_TESTS)
    enable_testing()
    add_subdirectory("tests")
endif()
