// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/security.hpp"
#include "song/wire.hpp"
#include "song/error.hpp"
#include <cstring>
#include <cstdio>

#if defined(__APPLE__)
#include <CommonCrypto/CommonHMAC.h>
#elif defined(__linux__)
#include <openssl/hmac.h>
#include <openssl/evp.h>
#else
#error "No HMAC implementation available. Song requires CommonCrypto (macOS) or OpenSSL (Linux)."
#endif

namespace song {

HmacTag compute_hmac(const void* key, size_t key_len,
                     const void* data, size_t data_len) {
    HmacTag tag{};

#if defined(__APPLE__)
    // Use CommonCrypto HMAC-SHA256
    unsigned char full_hmac[CC_SHA256_DIGEST_LENGTH];
    CCHmac(kCCHmacAlgSHA256, key, key_len, data, data_len, full_hmac);
    // Truncate to 8 bytes
    std::memcpy(tag.data(), full_hmac, kHmacTagSize);

#elif defined(__linux__)
    // Use OpenSSL HMAC-SHA256
    unsigned char full_hmac[EVP_MAX_MD_SIZE];
    unsigned int hmac_len = 0;
    HMAC(EVP_sha256(),
         key, static_cast<int>(key_len),
         static_cast<const unsigned char*>(data), data_len,
         full_hmac, &hmac_len);
    // Truncate to 8 bytes
    std::memcpy(tag.data(), full_hmac, kHmacTagSize);

#endif

    return tag;
}

HmacTag compute_hmac(const std::string& key, const Buffer& msg) {
    return compute_hmac(key.data(), key.size(), msg.data(), msg.size());
}

bool verify_hmac(const std::string& key, const Buffer& msg, const HmacTag& tag) {
    HmacTag computed = compute_hmac(key, msg);

    // Constant-time comparison to prevent timing attacks
    volatile unsigned char result = 0;
    for (size_t i = 0; i < kHmacTagSize; ++i) {
        result |= static_cast<unsigned char>(computed[i]) ^
                  static_cast<unsigned char>(tag[i]);
    }
    return result == 0;
}

// =============================================================================
// SecureTransport Implementation
// =============================================================================

SecureTransport::SecureTransport(std::unique_ptr<Transport> inner, SecurityConfig config)
    : inner_(std::move(inner))
    , config_(std::move(config)) {}

void SecureTransport::send(const Buffer& msg) {
    if (!inner_) {
        throw ServiceError("SecureTransport: no underlying transport");
    }

    if (!config_.is_enabled()) {
        // No security - pass through directly
        inner_->send(msg);
        return;
    }

    // Compute HMAC over the raw wire bytes of the original message
    HmacTag tag = compute_hmac(config_.key(), msg);

    // Read payload_size directly from raw wire bytes at offset 8
    u32 payload_size = 0;
    std::memcpy(&payload_size, msg.data() + 8, sizeof(u32));

    // Validate payload_size won't overflow when adding HMAC tag
    if (payload_size > wire::kMaxPayloadSize - kHmacTagSize) {
        throw SecurityError("payload too large for HMAC authentication");
    }

    // Build secure message: copy original header, patch payload_size, append payload + tag
    Buffer secure_msg;
    secure_msg.write(msg.data(), 16);  // Copy raw header

    // Patch payload_size at offset 8 to include HMAC tag
    u32 new_payload_size = payload_size + static_cast<u32>(kHmacTagSize);
    std::memcpy(secure_msg.data() + 8, &new_payload_size, sizeof(u32));

    // Copy original payload
    if (payload_size > 0) {
        secure_msg.write(msg.data() + 16, payload_size);
    }

    // Append HMAC tag (now part of the "extended" payload)
    secure_msg.write(tag.data(), kHmacTagSize);

    inner_->send(secure_msg);
}

bool SecureTransport::receive(Buffer& msg, int timeout_ms) {
    if (!inner_) {
        return false;
    }

    if (!config_.is_enabled()) {
        // No security - pass through directly
        return inner_->receive(msg, timeout_ms);
    }

    // Receive the full message (with extended payload that includes HMAC)
    Buffer secure_msg;
    if (!inner_->receive(secure_msg, timeout_ms)) {
        return false;
    }

    // Read payload_size directly from raw wire bytes at offset 8
    u32 ext_payload_size = 0;
    std::memcpy(&ext_payload_size, secure_msg.data() + 8, sizeof(u32));

    // Extended payload must include at least the HMAC tag
    if (ext_payload_size < kHmacTagSize) {
        throw SecurityError("message payload too short for HMAC verification");
    }

    // Calculate original payload size
    u32 orig_payload_size = ext_payload_size - static_cast<u32>(kHmacTagSize);

    // Extract HMAC tag from end of extended payload
    HmacTag received_tag{};
    std::memcpy(received_tag.data(),
                secure_msg.data() + 16 + orig_payload_size,
                kHmacTagSize);

    // Build original message from raw wire bytes (header + payload, no tag)
    // by copying the received bytes and patching payload_size back to original
    msg.reset();
    msg.write(secure_msg.data(), 16 + orig_payload_size);

    // Patch payload_size at offset 8 to the original value
    std::memcpy(msg.data() + 8, &orig_payload_size, sizeof(u32));

    // Verify HMAC over raw wire bytes of original message
    if (!verify_hmac(config_.key(), msg, received_tag)) {
        throw SecurityError("HMAC verification failed - message may be tampered");
    }

    msg.reset_read();
    return true;
}

void SecureTransport::close() {
    if (inner_) {
        inner_->close();
    }
}

bool SecureTransport::is_connected() const {
    return inner_ && inner_->is_connected();
}

const char* SecureTransport::type_name() const {
    if (!inner_) {
        return "secure(null)";
    }

    // Return a description indicating secure wrapper
    static thread_local char name_buf[64];
    std::snprintf(name_buf, sizeof(name_buf), "secure(%s)", inner_->type_name());
    return name_buf;
}

} // namespace song
