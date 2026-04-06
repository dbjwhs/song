# MIT License
# Copyright (c) 2026 dbjwhs

# libFuzzer harnesses -- only build when fuzzer sanitizer is available
# Build with: cmake .. -DCMAKE_BUILD_TYPE=Debug -DSONG_ENABLE_FUZZ=ON
# Requires LLVM clang (not Apple clang) with libFuzzer support

option(SONG_ENABLE_FUZZ "Build libFuzzer harnesses" OFF)

if(SONG_ENABLE_FUZZ)
    set(FUZZ_FLAGS "-fsanitize=fuzzer,address,undefined")

    add_executable(fuzz_wire_decoder fuzz_wire_decoder.cpp)
    target_link_libraries(fuzz_wire_decoder song)
    target_compile_options(fuzz_wire_decoder PRIVATE ${FUZZ_FLAGS})
    target_link_options(fuzz_wire_decoder PRIVATE ${FUZZ_FLAGS})

    add_executable(fuzz_buffer_decoder fuzz_buffer_decoder.cpp)
    target_link_libraries(fuzz_buffer_decoder song)
    target_compile_options(fuzz_buffer_decoder PRIVATE ${FUZZ_FLAGS})
    target_link_options(fuzz_buffer_decoder PRIVATE ${FUZZ_FLAGS})
endif()
