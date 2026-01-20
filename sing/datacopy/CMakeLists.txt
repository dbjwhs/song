# MIT License
# Copyright (c) 2026 dbjwhs

# Data Copy - Sing Integration Test

# Service executable
add_executable(sing_datacopy_service
    datacopy_service.cpp
)
target_link_libraries(sing_datacopy_service song)
target_include_directories(sing_datacopy_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_datacopy_test
    datacopy_test.cpp
)
target_link_libraries(sing_datacopy_test
    song
    GTest::gtest_main
)
target_include_directories(sing_datacopy_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_datacopy_test sing_datacopy_service)

# Register tests with CTest
gtest_discover_tests(sing_datacopy_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Convenience target
add_custom_target(sing_datacopy
    DEPENDS sing_datacopy_service sing_datacopy_test
)
