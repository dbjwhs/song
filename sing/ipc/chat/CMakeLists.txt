# MIT License
# Copyright (c) 2026 dbjwhs

# Chat - Sing IPC Integration Test

# Service executable
add_executable(sing_ipc_chat_service
    chat_service.cpp
)
target_link_libraries(sing_ipc_chat_service song)
target_include_directories(sing_ipc_chat_service PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Test executable
add_executable(sing_ipc_chat_test
    chat_test.cpp
)
target_link_libraries(sing_ipc_chat_test
    song
    GTest::gtest_main
)
target_include_directories(sing_ipc_chat_test PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_SOURCE_DIR}/include
)

# Make test depend on service being built
add_dependencies(sing_ipc_chat_test sing_ipc_chat_service)

# Register tests with CTest
gtest_discover_tests(sing_ipc_chat_test
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

# Convenience target
add_custom_target(sing_ipc_chat
    DEPENDS sing_ipc_chat_service sing_ipc_chat_test
)
