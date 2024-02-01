cmake_minimum_required(VERSION 3.20)

project(zappy-log)

add_library(zappy-log INTERFACE)

target_compile_features(zappy-log INTERFACE cxx_std_20)

target_include_directories(zappy-log INTERFACE "${CMAKE_CURRENT_SOURCE_DIR}/include")

if(UNIX)
    find_package(Threads REQUIRED)
    target_link_libraries(zappy-log INTERFACE "Threads::Threads")
endif()


option(ZAPPYLOG_BUILD_EXAMPLE "Build zappy-log example" ON)
if (ZAPPYLOG_BUILD_EXAMPLE)
    add_subdirectory("example")
endif()