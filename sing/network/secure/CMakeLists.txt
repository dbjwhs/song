# MIT License
# Copyright (c) 2026 dbjwhs

# Secure - Sing Network Integration Test (HMAC Authentication)

# Service executable
add_executable(sing_network_secure_service
    secure_service.cpp
)
target_link_libraries(sing_network_secure_service song)
target_include_directories(sing_network_secure_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_network_secure_test
    secure_test.cpp
)
target_link_libraries(sing_network_secure_test
    song
    GTest::gtest_main
)
target_include_directories(sing_network_secure_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_network_secure_test sing_network_secure_service)

# Register tests with CTest
gtest_discover_tests(sing_network_secure_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Convenience target
add_custom_target(sing_network_secure
    DEPENDS sing_network_secure_service sing_network_secure_test
)
