# MIT License
# Copyright (c) 2026 dbjwhs

include(FetchContent)

# Fetch GoogleTest
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# For Windows: Prevent overriding the parent project's compiler/linker settings
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(googletest)

# Enable testing
include(GoogleTest)

# Test executable
add_executable(song_tests
    buffer_test.cpp
    wire_test.cpp
    pipe_test.cpp
    process_test.cpp
    manager_test.cpp
)

target_link_libraries(song_tests
    song
    GTest::gtest_main
)

target_include_directories(song_tests PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Discover tests for CTest
gtest_discover_tests(song_tests)
