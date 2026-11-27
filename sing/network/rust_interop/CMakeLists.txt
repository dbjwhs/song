# MIT License
# Copyright (c) 2026 dbjwhs

# Rust Interop - Sing Network Integration Test
#
# The interop service (interop.song) in C++, and a C++ client test against
# the same service served by the Rust runtime (rust/examples/interop_service.rs).
# The Rust tests (rust/tests/interop.rs) and the Python test
# (python/tests/test_rust_interop.py) use the C++ service built here.
#
# Nothing here needs cargo to build. The test skips when the Rust binary is
# absent; rust/CMakeLists.txt makes ctest build it first when cargo is found.

# Service executable
add_executable(sing_network_rust_interop_service
    interop_service.cpp
)
target_link_libraries(sing_network_rust_interop_service song)
target_include_directories(sing_network_rust_interop_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_network_rust_interop_test
    interop_test.cpp
)
target_link_libraries(sing_network_rust_interop_test
    song
    GTest::gtest_main
)
target_include_directories(sing_network_rust_interop_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)
target_compile_definitions(sing_network_rust_interop_test PRIVATE
    SONG_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

# Register tests with CTest
# RESOURCE_LOCK prevents parallel execution with other network tests.
# FIXTURES_REQUIRED orders the tests after the cargo build when there is one
# (the fixture is defined in rust/CMakeLists.txt only when cargo is found;
# an undefined fixture is no requirement).
gtest_discover_tests(sing_network_rust_interop_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES RESOURCE_LOCK network_ports FIXTURES_REQUIRED rust_build
)

# Convenience target
add_custom_target(sing_network_rust_interop
    DEPENDS sing_network_rust_interop_service sing_network_rust_interop_test
)
