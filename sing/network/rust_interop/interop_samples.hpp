// MIT License
// Copyright (c) 2026 dbjwhs

// The values the interop service produces, shared by the C++ service and
// the C++ test. rust/tests/common/interop_impl.rs builds the same values.

#pragma once

#include "interop.hpp"
#include <string>

namespace song::interop {

// Sample number ndx of a samples(n, label) stream. Every field depends on
// ndx.
inline Sample make_sample(i32 ndx, const std::string& label) {
    bool odd = (ndx % 2) == 1;
    Sample s;
    s.id = ndx;
    s.label = label;
    s.value = static_cast<f64>(ndx) * 0.5;
    s.status = odd ? Status::failed : Status::retry;
    s.access = static_cast<Access>(static_cast<u32>(ndx) & 7u);
    s.note_id = odd ? static_cast<i64>(ndx) * 1000000007LL : -1;
    s.note = odd ? std::string() : "note " + std::to_string(ndx);
    for (i32 v = 0; v < ndx; ++v) {
        s.blob.push_back(static_cast<std::byte>(v));
    }
    for (i32 v = 1; v <= ndx; ++v) {
        s.counts.push_back(v);
    }
    for (i32 v = 0; v < ndx % 3; ++v) {
        s.tags.push_back(label + "-" + std::to_string(v));
    }
    return s;
}

inline bool same_sample(const Sample& a, const Sample& b) {
    return a.id == b.id && a.label == b.label && a.value == b.value && a.status == b.status &&
           a.access == b.access && a.note_id == b.note_id && a.note == b.note && a.blob == b.blob &&
           a.counts == b.counts && a.tags == b.tags;
}

} // namespace song::interop
