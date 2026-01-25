# MIT License
# Copyright (c) 2026 dbjwhs

# Discovery - Sing Network Integration Test (mDNS)

# Service executable
add_executable(sing_network_discovery_service
    discovery_service.cpp
)
target_link_libraries(sing_network_discovery_service song)
target_include_directories(sing_network_discovery_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_network_discovery_test
    discovery_test.cpp
)
target_link_libraries(sing_network_discovery_test
    song
    GTest::gtest_main
)
target_include_directories(sing_network_discovery_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_network_discovery_test sing_network_discovery_service)

# Register tests with CTest
# RESOURCE_LOCK prevents parallel execution with other network tests
gtest_discover_tests(sing_network_discovery_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES RESOURCE_LOCK network_ports
)

# Client executable
add_executable(sing_network_discovery_client
    discovery_client.cpp
)
target_link_libraries(sing_network_discovery_client song)
target_include_directories(sing_network_discovery_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Convenience target
add_custom_target(sing_network_discovery
    DEPENDS sing_network_discovery_service sing_network_discovery_test sing_network_discovery_client
)
