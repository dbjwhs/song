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
    src/security.cpp
    src/registry.cpp
    src/object.cpp
    src/stream.cpp
    src/subscription.cpp
    src/logging.cpp
)
target_include_directories(song PUBLIC include)

# Link dns_sd on macOS for mDNS/Bonjour support
if(APPLE)
    find_library(DNSSD_LIBRARY dns_sd)
    if(DNSSD_LIBRARY)
        target_link_libraries(song PUBLIC ${DNSSD_LIBRARY})
    endif()
endif()

# Link OpenSSL on Linux for HMAC support
if(UNIX AND NOT APPLE)
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        target_link_libraries(song PUBLIC OpenSSL::Crypto)
    endif()

    # Avahi for mDNS service discovery on Linux
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(AVAHI QUIET avahi-client)
        if(AVAHI_FOUND)
            target_link_libraries(song PUBLIC ${AVAHI_LIBRARIES})
            target_include_directories(song PUBLIC ${AVAHI_INCLUDE_DIRS})
            target_compile_definitions(song PUBLIC SONG_HAS_AVAHI=1)
        else()
            message(STATUS "Avahi not found - mDNS discovery disabled on Linux")
        endif()
    endif()
endif()

# mbedTLS for TLS transport encryption
find_package(MbedTLS QUIET)
if(MbedTLS_FOUND)
    target_sources(song PRIVATE src/tls.cpp)
    target_link_libraries(song PRIVATE MbedTLS::mbedtls_static MbedTLS::mbedx509_static MbedTLS::tfpsacrypto_static)
    target_compile_definitions(song PUBLIC SONG_HAS_TLS=1)
else()
    message(STATUS "mbedTLS not found - TLS transport disabled")
endif()

# Compiler
add_executable(songc
    compiler/main.cpp
    compiler/codegen.cpp
    compiler/python_codegen.cpp
    compiler/scaffold.cpp
    compiler/lexer.cpp
    compiler/parser.cpp
    compiler/resolver.cpp
)

# Tests, examples, integration tests, and fuzz harnesses assume top-level
# context (CMAKE_SOURCE_DIR paths, gtest fetch, enable_testing). Consumers
# embedding song via add_subdirectory get just the library and compiler.
# (Spelled without PROJECT_IS_TOP_LEVEL: that needs CMake 3.21, floor is 3.16.)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    # Tests
    enable_testing()
    add_subdirectory(test)

    # Examples
    add_subdirectory(examples)

    # Sing - Integration Tests
    add_subdirectory(sing)

    # Fuzz harnesses (optional, requires LLVM clang with libFuzzer)
    add_subdirectory(fuzz)
endif()
