# MIT License
# Copyright (c) 2026 dbjwhs

# TCP Calculator - Sing Network Integration Test

# Service executable
add_executable(sing_network_tcp_calculator_service
    tcp_calculator_service.cpp
)
target_link_libraries(sing_network_tcp_calculator_service song)
target_include_directories(sing_network_tcp_calculator_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_network_tcp_calculator_test
    tcp_calculator_test.cpp
)
target_link_libraries(sing_network_tcp_calculator_test
    song
    GTest::gtest_main
)
target_include_directories(sing_network_tcp_calculator_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_network_tcp_calculator_test sing_network_tcp_calculator_service)

# Register tests with CTest
gtest_discover_tests(sing_network_tcp_calculator_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Convenience target
add_custom_target(sing_network_tcp_calculator
    DEPENDS sing_network_tcp_calculator_service sing_network_tcp_calculator_test
)
