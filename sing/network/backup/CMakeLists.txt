# MIT License
# Copyright (c) 2026 dbjwhs

# Backup Agent - Sing Network Integration Test (Comprehensive)
# Exercises: sync RPC, streaming, TLS, multi-client, property notifications, mDNS

# Service executable
add_executable(sing_network_backup_service
    backup_service.cpp
)
target_link_libraries(sing_network_backup_service song)
target_include_directories(sing_network_backup_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_network_backup_test
    backup_test.cpp
)
target_link_libraries(sing_network_backup_test
    song
    GTest::gtest_main
)
target_include_directories(sing_network_backup_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_network_backup_test sing_network_backup_service)

# Register tests with CTest
# RESOURCE_LOCK prevents parallel execution with other network tests
gtest_discover_tests(sing_network_backup_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    PROPERTIES RESOURCE_LOCK network_ports
)

# Client executable
add_executable(sing_network_backup_client
    backup_client.cpp
)
target_link_libraries(sing_network_backup_client song)
target_include_directories(sing_network_backup_client PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Convenience target
add_custom_target(sing_network_backup
    DEPENDS sing_network_backup_service sing_network_backup_test sing_network_backup_client
)
