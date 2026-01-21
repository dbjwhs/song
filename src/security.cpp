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
// Fallback: simple warning and no-op (for testing only)
#warning "No HMAC implementation available - security features disabled"
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

#else
    // No HMAC available - zero tag (INSECURE, for testing only)
    (void)key;
    (void)key_len;
    (void)data;
    (void)data_len;
    std::memset(tag.data(), 0, kHmacTagSize);
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

    // Compute HMAC over the original message (before modification)
    HmacTag tag = compute_hmac(config_.key(), msg);

    // Parse the original header to get payload_size
    Buffer temp_buf;
    temp_buf.write(msg.data(), msg.size());
    temp_buf.reset_read();
    auto hdr = wire::decode_header(temp_buf);

    // Create new message with modified payload_size to include HMAC tag
    Buffer secure_msg;
    wire::Header new_hdr = hdr;
    new_hdr.payload_size = hdr.payload_size + static_cast<u32>(kHmacTagSize);
    wire::encode_header(secure_msg, new_hdr);

    // Copy original payload
    if (hdr.payload_size > 0) {
        secure_msg.write(msg.data() + 16, hdr.payload_size);
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

    // Parse header to get extended payload_size
    secure_msg.reset_read();
    auto hdr = wire::decode_header(secure_msg);

    // Extended payload must include at least the HMAC tag
    if (hdr.payload_size < kHmacTagSize) {
        throw SecurityError("message payload too short for HMAC verification");
    }

    // Calculate original payload size
    u32 orig_payload_size = hdr.payload_size - static_cast<u32>(kHmacTagSize);

    // Extract HMAC tag from end of extended payload
    HmacTag received_tag{};
    std::memcpy(received_tag.data(),
                secure_msg.data() + 16 + orig_payload_size,
                kHmacTagSize);

    // Reconstruct original message (with original payload_size)
    msg.reset();
    wire::Header orig_hdr = hdr;
    orig_hdr.payload_size = orig_payload_size;
    wire::encode_header(msg, orig_hdr);

    // Copy original payload
    if (orig_payload_size > 0) {
        msg.write(secure_msg.data() + 16, orig_payload_size);
    }

    // Verify HMAC over reconstructed original message
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
