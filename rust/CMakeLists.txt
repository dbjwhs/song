# MIT License
# Copyright (c) 2026 dbjwhs

# Rust runtime (the `song` crate in this directory): ctest registration.
#
# cargo is never needed to configure or build: nothing here adds a build
# target. When cargo is found, ctest builds the crate and runs its tests,
# clippy, and the cross-language interop tests. When it is not, one test
# reports that the Rust tests were skipped.

find_program(SONG_CARGO cargo)

if(NOT SONG_CARGO)
    message(STATUS "cargo not found: Rust tests will be reported as skipped")
    add_test(NAME rust_tests
        COMMAND ${CMAKE_COMMAND} -E echo
            "SKIPPED: cargo not found, so the Rust runtime, the songc --lang rust output and the Rust interop tests were not run. Install Rust (https://rustup.rs) and re-run cmake to run them.")
    set_tests_properties(rust_tests PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED")
    return()
endif()

message(STATUS "cargo found: ${SONG_CARGO} (Rust tests run from ctest)")

# The checked-in generator output is what the current songc emits. Needs no
# cargo, but there is nothing to compile it with without one.
add_test(NAME rust_generated_is_current
    COMMAND ${CMAKE_COMMAND}
        -DSONGC=$<TARGET_FILE:songc>
        -DSOURCE_DIR=${CMAKE_SOURCE_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/generated_check
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_generated.cmake)

# Build first, as a fixture: the tests below (and the C++ and Python
# interop tests, which start the Rust service binary) run after it and are
# not run when it fails.
add_test(NAME rust_build
    COMMAND ${SONG_CARGO} build --all-targets
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(rust_build PROPERTIES FIXTURES_SETUP rust_build TIMEOUT 1200)

# Unit tests, golden vectors, generated code, Rust client to C++ services.
# SONG_REQUIRE_PEERS turns "C++ service not built" from a skip into a
# failure: under ctest the C++ tree is built.
add_test(NAME rust_cargo_test
    COMMAND ${SONG_CARGO} test
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
set_tests_properties(rust_cargo_test PROPERTIES
    FIXTURES_REQUIRED rust_build
    RESOURCE_LOCK network_ports
    ENVIRONMENT "SONG_BUILD_DIR=${CMAKE_BINARY_DIR};SONG_REQUIRE_PEERS=1"
    TIMEOUT 1200)

# The fuzz harnesses (fuzz/rust) on deterministic input. The libFuzzer
# binaries themselves are built by fuzz/rust/build.sh, not from here.
add_test(NAME rust_fuzz_smoke
    COMMAND ${SONG_CARGO} test
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/fuzz/rust)
set_tests_properties(rust_fuzz_smoke PROPERTIES TIMEOUT 1200)

# clippy is a rustup component and may be absent where cargo is present.
execute_process(COMMAND ${SONG_CARGO} clippy --version
    RESULT_VARIABLE SONG_CLIPPY_RESULT OUTPUT_QUIET ERROR_QUIET)
if(SONG_CLIPPY_RESULT EQUAL 0)
    add_test(NAME rust_clippy
        COMMAND ${SONG_CARGO} clippy --all-targets -- -D warnings
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(rust_clippy PROPERTIES FIXTURES_REQUIRED rust_build TIMEOUT 1200)
else()
    add_test(NAME rust_clippy
        COMMAND ${CMAKE_COMMAND} -E echo
            "SKIPPED: cargo clippy is not installed (rustup component add clippy)")
    set_tests_properties(rust_clippy PROPERTIES SKIP_REGULAR_EXPRESSION "SKIPPED")
endif()

# Python client to the Rust service.
find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
    add_test(NAME python_rust_interop
        COMMAND ${Python3_EXECUTABLE} -m unittest -v tests.test_rust_interop
        WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}/python)
    set_tests_properties(python_rust_interop PROPERTIES
        FIXTURES_REQUIRED rust_build
        RESOURCE_LOCK network_ports
        ENVIRONMENT "SONG_REQUIRE_PEERS=1")
endif()
