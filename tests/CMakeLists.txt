set(ZAPPYLOG_TEST_WARNINGS
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /permissive->
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-Wall -Wextra>)

foreach(name queue format file core exit)
    add_executable(test_${name} test_${name}.cpp)
    target_link_libraries(test_${name} PRIVATE zappy-log)
    target_compile_options(test_${name} PRIVATE ${ZAPPYLOG_TEST_WARNINGS})
    add_test(NAME ${name} COMMAND test_${name}
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR})
    # a hang is a failure, not a stuck CI job
    set_tests_properties(${name} PROPERTIES TIMEOUT 30)
endforeach()

if(WIN32)
    add_executable(test_win32 test_win32.cpp test_win32_order.cpp
        test_win32_order2.cpp)
    target_link_libraries(test_win32 PRIVATE zappy-log)
    target_compile_options(test_win32 PRIVATE ${ZAPPYLOG_TEST_WARNINGS})
    add_test(NAME win32 COMMAND test_win32)
    set_tests_properties(win32 PROPERTIES TIMEOUT 30)
endif()

# Every header must compile on its own, without relying on what an earlier
# include happened to pull in.
file(GLOB_RECURSE ZAPPYLOG_HEADERS CONFIGURE_DEPENDS
    "${PROJECT_SOURCE_DIR}/include/zappy/*.hpp")
set(ZAPPYLOG_HEADER_TUS)
foreach(header ${ZAPPYLOG_HEADERS})
    file(RELATIVE_PATH rel "${PROJECT_SOURCE_DIR}/include" "${header}")
    string(MAKE_C_IDENTIFIER "${rel}" id)
    set(tu "${CMAKE_CURRENT_BINARY_DIR}/headers/${id}.cpp")
    file(CONFIGURE OUTPUT "${tu}" CONTENT "#include <${rel}>\n")
    list(APPEND ZAPPYLOG_HEADER_TUS "${tu}")
endforeach()
add_library(test_headers OBJECT ${ZAPPYLOG_HEADER_TUS})
target_link_libraries(test_headers PRIVATE zappy-log)
target_compile_options(test_headers PRIVATE ${ZAPPYLOG_TEST_WARNINGS})
