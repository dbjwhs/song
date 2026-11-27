# MIT License
# Copyright (c) 2026 dbjwhs

# Fails when a module under rust/tests/generated/ is not what the built
# songc emits for its IDL: generated code is checked in and CMake does not
# rebuild it, so a generator change has to be followed by regenerating.
#
#   cmake -DSONGC=<songc> -DSOURCE_DIR=<repo> -DWORK_DIR=<scratch dir> -P check_generated.cmake

set(IDL_FILES
    sing/ipc/calculator/calculator.song
    sing/network/rust_interop/interop.song
    rust/tests/idl/coverage.song
)

file(REMOVE_RECURSE ${WORK_DIR})
file(MAKE_DIRECTORY ${WORK_DIR})

set(STALE "")
foreach(IDL ${IDL_FILES})
    get_filename_component(NAME ${IDL} NAME_WE)
    execute_process(
        COMMAND ${SONGC} ${SOURCE_DIR}/${IDL} -o ${WORK_DIR} --lang rust
        RESULT_VARIABLE RESULT OUTPUT_QUIET ERROR_VARIABLE ERRORS)
    if(NOT RESULT EQUAL 0)
        message(FATAL_ERROR "songc --lang rust failed on ${IDL}: ${ERRORS}")
    endif()
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E compare_files
            ${WORK_DIR}/${NAME}.rs ${SOURCE_DIR}/rust/tests/generated/${NAME}.rs
        RESULT_VARIABLE DIFFERENT)
    if(NOT DIFFERENT EQUAL 0)
        list(APPEND STALE "${NAME}.rs")
        message(STATUS "stale: rust/tests/generated/${NAME}.rs; regenerate with: "
                       "songc ${IDL} -o rust/tests/generated --lang rust")
    endif()
endforeach()

if(STALE)
    message(FATAL_ERROR "generated Rust modules are out of date: ${STALE}")
endif()
message(STATUS "rust/tests/generated is current")
