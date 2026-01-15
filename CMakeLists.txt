# MIT License
# Copyright (c) 2026 dbjwhs

cmake_minimum_required(VERSION 3.16)
project(song VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler flags for speed
add_compile_options(-O3 -march=native -flto)

# Runtime library
add_library(song STATIC
    src/buffer.cpp
    src/pipe.cpp
    src/wire.cpp
    src/process.cpp
    src/manager.cpp
    src/runtime.cpp
)
target_include_directories(song PUBLIC include)

# Compiler
add_executable(songc
    compiler/main.cpp
    compiler/lexer.cpp
    compiler/parser.cpp
    compiler/resolver.cpp
    compiler/codegen.cpp
)

# Tests
enable_testing()
add_subdirectory(test)

# Examples
add_subdirectory(examples)
