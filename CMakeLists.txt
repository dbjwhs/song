# MIT License
# Copyright (c) 2026 dbjwhs

cmake_minimum_required(VERSION 3.16)
project(song VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Warning flags - NO WARNINGS ALLOWED
add_compile_options(-Wall -Wextra -Werror)

# Compiler flags - use Release flags only in Release mode
if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_compile_options(-O3 -march=native -flto)
endif()

# Runtime library
add_library(song STATIC
    src/buffer.cpp
    src/pipe.cpp
    src/wire.cpp
    src/process.cpp
    src/manager.cpp
    src/runtime.cpp
    src/transport.cpp
    src/discovery.cpp
)
target_include_directories(song PUBLIC include)

# Link dns_sd on macOS for mDNS/Bonjour support
if(APPLE)
    find_library(DNSSD_LIBRARY dns_sd)
    if(DNSSD_LIBRARY)
        target_link_libraries(song PUBLIC ${DNSSD_LIBRARY})
    endif()
endif()

# Compiler
add_executable(songc
    compiler/main.cpp
    compiler/codegen.cpp
    compiler/lexer.cpp
    compiler/parser.cpp
    compiler/resolver.cpp
)

# Tests
enable_testing()
add_subdirectory(test)

# Examples
add_subdirectory(examples)

# Sing - Integration Tests
add_subdirectory(sing)
