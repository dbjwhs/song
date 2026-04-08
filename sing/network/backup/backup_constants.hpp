// MIT License
// Copyright (c) 2026 dbjwhs

// Hand-written constants for backup agent features not yet supported by songc:
// streaming methods and property notification IDs.
// Shared between service, test, and client to prevent drift.

#pragma once

#include <song/types.hpp>

namespace song::backup {

// Streaming service ID (separate from sync RPC service ID to avoid
// stream dispatcher taking priority over sync dispatcher)
constexpr u16 kService_BackupAgent_Stream = 2;
constexpr u16 kMethod_BackupAgent_stream_file = 1;

// Property notification IDs for progress reporting
constexpr u32 kType_BackupAgent = 1;
constexpr i32 kObject_BackupAgent = 0;
constexpr u16 kProp_progress = 1;

} // namespace song::backup
