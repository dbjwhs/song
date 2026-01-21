# MIT License
# Copyright (c) 2026 dbjwhs

# Echo example
add_executable(echo_service echo/echo_service.cpp)
target_link_libraries(echo_service song)
target_include_directories(echo_service PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(echo_client echo/echo_client.cpp)
target_link_libraries(echo_client song)
target_include_directories(echo_client PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Crash test example (for testing auto-restart)
add_executable(crash_service crash/crash_service.cpp)
target_link_libraries(crash_service song)
target_include_directories(crash_service PRIVATE ${CMAKE_SOURCE_DIR}/include)

add_executable(crash_test crash/crash_test.cpp)
target_link_libraries(crash_test song)
target_include_directories(crash_test PRIVATE ${CMAKE_SOURCE_DIR}/include)

# Registry service
add_executable(registry_service registry/registry_service.cpp)
target_link_libraries(registry_service song)
target_include_directories(registry_service PRIVATE ${CMAKE_SOURCE_DIR}/include)
