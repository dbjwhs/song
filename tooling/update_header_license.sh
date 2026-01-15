#!/bin/bash
# update_license.sh
# Script to add MIT License header to CMakeLists.txt files and all C++ source files

# Define the exact strings we're looking for
CMAKE_MIT_LINE="# MIT License"
CMAKE_COPYRIGHT_LINE="# Copyright (c) 2026 dbjwhs"
CPP_MIT_LINE="// MIT License"
CPP_COPYRIGHT_LINE="// Copyright (c) 2026 dbjwhs"

function validate_file_path() {
    local file="$1"
    if [[ ! -f "$file" ]]; then
        echo "Error: File does not exist: $file"
        return 1
    fi
    if [[ ! -r "$file" ]]; then
        echo "Error: Cannot read file: $file"
        return 1
    fi
    if [[ ! -w "$file" ]]; then
        echo "Error: Cannot write to file: $file"
        return 1
    fi
    return 0
}

function check_and_update_file() {
    local file="$1"
    local is_cpp="$2"

    # Validate file access
    if ! validate_file_path "$file"; then
        return 1
    fi

    # Set appropriate comment style based on file type
    local mit_line="${CMAKE_MIT_LINE}"
    local copyright_line="${CMAKE_COPYRIGHT_LINE}"
    if [[ "$is_cpp" == "true" ]]; then
        mit_line="${CPP_MIT_LINE}"
        copyright_line="${CPP_COPYRIGHT_LINE}"
    fi

    # Read first two lines for comparison
    local first_line=$(head -n 1 "$file" | tr -d '\r\n')
    local second_line=$(head -n 2 "$file" | tail -n 1 | tr -d '\r\n')

    if [[ "$first_line" == "$mit_line" ]] && [[ "$second_line" == "$copyright_line" ]]; then
        echo "License header already exists in $file"
        return 0
    fi

    # Create temporary file with proper permissions
    local temp_file=$(mktemp)
    if [[ ! -f "$temp_file" ]]; then
        echo "Error: Failed to create temporary file"
        return 1
    fi

    # Preserve original file permissions
    local orig_perms=$(stat -f %A "$file")

    # Add license header and original content
    echo "$mit_line" > "$temp_file"
    echo "$copyright_line" >> "$temp_file"
    echo >> "$temp_file"
    cat "$file" >> "$temp_file"

    # Apply original permissions to temp file
    chmod "$orig_perms" "$temp_file"

    # Replace original file with updated content
    if ! mv "$temp_file" "$file"; then
        echo "Error: Failed to update $file"
        rm -f "$temp_file"
        return 1
    fi

    echo "Successfully updated $file"
    return 0
}

# Get the song repository root (parent of tooling directory)
SONG_ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# Process all CMakeLists.txt files in song directory only
while IFS= read -r -d '' cmake_file; do
    check_and_update_file "$cmake_file" "false"
done < <(find "$SONG_ROOT" -name "CMakeLists.txt" -type f -not -path "*/build/*" -not -path "*/cmake-build-debug/*" -print0)

# Process all .cpp and .hpp files in song directory only (no name matching required)
while IFS= read -r -d '' cpp_file; do
    check_and_update_file "$cpp_file" "true"
done < <(find "$SONG_ROOT" \( -name "*.cpp" -o -name "*.hpp" -o -name "*.h" \) -type f -not -path "*/build/*" -not -path "*/cmake-build-debug/*" -print0)
