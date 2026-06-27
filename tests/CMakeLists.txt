include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/heads/main.zip
)

# For Windows: prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

add_executable(unit_tests
  test_oddprog.cpp
  ../protocol.c
  ../device.c
  ../prog.c
)

set_target_properties(unit_tests PROPERTIES
    CXX_STANDARD 17
    CXX_STANDARD_REQUIRED ON
    CXX_EXTENSIONS OFF
)

set_target_properties(gtest gtest_main PROPERTIES
    INTERFACE_SYSTEM_INCLUDE_DIRECTORIES ""
)

target_include_directories(unit_tests PRIVATE
    ${googletest_SOURCE_DIR}/googletest/include
    ${CMAKE_SOURCE_DIR}
)
target_link_libraries(unit_tests PRIVATE GTest::gtest GTest::gtest_main)

add_test(NAME unit_tests COMMAND unit_tests)
