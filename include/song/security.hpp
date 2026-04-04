// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "transport.hpp"
#include <string>
#include <array>
#include <memory>
#include <cstring>

namespace song {

/// HMAC tag size (truncated SHA-256)
constexpr size_t kHmacTagSize = 8;

/// Minimum recommended key size (256 bits)
constexpr size_t kMinKeySize = 32;

/// Maximum key size
constexpr size_t kMaxKeySize = 64;

/// Security level for transport
enum class SecurityLevel : u8 {
    none = 0,           // No security (trusted environment)
    shared_secret = 1,  // HMAC validation with shared key
    tls = 2,            // Full TLS encryption
};

/// HMAC tag type (8 bytes)
using HmacTag = std::array<std::byte, kHmacTagSize>;

/// Compute HMAC-SHA256 over data, truncated to 8 bytes
/// @param key Secret key (should be at least 32 bytes)
/// @param key_len Length of key
/// @param data Data to authenticate
/// @param data_len Length of data
/// @return 8-byte HMAC tag
HmacTag compute_hmac(const void* key, size_t key_len,
                     const void* data, size_t data_len);

/// Compute HMAC-SHA256 over a buffer
/// @param key Secret key
/// @param msg Message buffer
/// @return 8-byte HMAC tag
HmacTag compute_hmac(const std::string& key, const Buffer& msg);

/// Verify HMAC tag
/// @param key Secret key
/// @param msg Message buffer (without HMAC tag)
/// @param tag Expected HMAC tag
/// @return true if tag is valid
bool verify_hmac(const std::string& key, const Buffer& msg, const HmacTag& tag);

/// Security configuration
class SecurityConfig {
    std::string key_;
    SecurityLevel level_ = SecurityLevel::none;

public:
    SecurityConfig() = default;

    /// Create configuration with shared secret
    /// @param key Shared secret key (should be at least 32 bytes)
    explicit SecurityConfig(std::string key)
        : key_(std::move(key))
        , level_(SecurityLevel::shared_secret) {}

    /// Destructor zeroes key material before deallocation
    ~SecurityConfig() {
        if (!key_.empty()) {
            volatile char* p = key_.data();
            std::memset(const_cast<char*>(static_cast<const volatile char*>(p)), 0, key_.size());
        }
    }

    // Move-only: prevent key material from lingering in copied memory
    SecurityConfig(SecurityConfig&&) noexcept = default;
    SecurityConfig& operator=(SecurityConfig&&) noexcept = default;
    SecurityConfig(const SecurityConfig&) = delete;
    SecurityConfig& operator=(const SecurityConfig&) = delete;

    /// Check if security is enabled
    bool is_enabled() const { return level_ != SecurityLevel::none; }

    /// Get security level
    SecurityLevel level() const { return level_; }

    /// Get the key (for internal use)
    const std::string& key() const { return key_; }

    /// Set security level
    void set_level(SecurityLevel level) { level_ = level; }

    /// Set key
    void set_key(std::string key) {
        key_ = std::move(key);
        if (!key_.empty()) {
            level_ = SecurityLevel::shared_secret;
        }
    }

    /// Disable security
    void disable() {
        level_ = SecurityLevel::none;
    }
};

/// Secure transport wrapper
/// Adds HMAC authentication to any underlying transport
class SecureTransport : public Transport {
    std::unique_ptr<Transport> inner_;
    SecurityConfig config_;

public:
    /// Create secure transport wrapping an existing transport
    /// @param inner Underlying transport (takes ownership)
    /// @param config Security configuration
    SecureTransport(std::unique_ptr<Transport> inner, SecurityConfig config);

    // Non-copyable, movable
    SecureTransport(const SecureTransport&) = delete;
    SecureTransport& operator=(const SecureTransport&) = delete;
    SecureTransport(SecureTransport&&) noexcept = default;
    SecureTransport& operator=(SecureTransport&&) noexcept = default;

    /// Send a message with HMAC authentication
    /// If security is enabled, appends HMAC tag to the message
    void send(const Buffer& msg) override;

    /// Receive a message and verify HMAC
    /// If security is enabled, verifies and strips HMAC tag
    /// @throws SecurityError if HMAC verification fails
    bool receive(Buffer& msg, int timeout_ms = -1) override;

    void close() override;
    bool is_connected() const override;
    const char* type_name() const override;

    /// Get security configuration
    const SecurityConfig& config() const { return config_; }

    /// Get underlying transport
    Transport* inner() { return inner_.get(); }
    const Transport* inner() const { return inner_.get(); }
};

/// Security error exception
class SecurityError : public std::runtime_error {
public:
    explicit SecurityError(const std::string& msg)
        : std::runtime_error("Security error: " + msg) {}
};

#if defined(SONG_HAS_TLS)

/// TLS configuration for encrypted transport
/// Supports both certificate-based and PSK (pre-shared key) modes
class TlsConfig {
public:
    enum class Mode : u8 {
        certificate,  // X.509 certificate-based authentication
        psk,          // Pre-shared key (lighter weight, aligns with SharedSecret)
    };

    enum class VerifyMode : u8 {
        none,      // Do not verify peer (testing only)
        optional,  // Verify if peer presents a certificate
        required,  // Peer must present a valid certificate (default for cert mode)
    };

private:
    std::string cert_path_;      // PEM certificate file
    std::string key_path_;       // PEM private key file
    std::string ca_path_;        // PEM CA certificate for peer verification
    std::string psk_;            // Pre-shared key bytes
    std::string psk_identity_;   // PSK identity string
    Mode mode_ = Mode::certificate;
    VerifyMode verify_ = VerifyMode::required;
    bool is_server_ = false;

public:
    TlsConfig() = default;

    /// Certificate-based TLS configuration
    /// @param cert_path Path to PEM certificate file
    /// @param key_path Path to PEM private key file
    /// @param ca_path Path to PEM CA certificate (for peer verification)
    TlsConfig(std::string cert_path, std::string key_path, std::string ca_path)
        : cert_path_(std::move(cert_path))
        , key_path_(std::move(key_path))
        , ca_path_(std::move(ca_path))
        , mode_(Mode::certificate)
        , verify_(VerifyMode::required) {}

    /// PSK-based TLS configuration
    /// @param psk Pre-shared key (raw bytes)
    /// @param identity PSK identity string
    TlsConfig(std::string psk, std::string identity, Mode /* tag */)
        : psk_(std::move(psk))
        , psk_identity_(std::move(identity))
        , mode_(Mode::psk)
        , verify_(VerifyMode::none) {}

    /// Destructor zeroes sensitive material
    ~TlsConfig() {
        if (!psk_.empty()) {
            volatile char* p = psk_.data();
            std::memset(const_cast<char*>(static_cast<const volatile char*>(p)), 0, psk_.size());
        }
        if (!key_path_.empty()) {
            // Key path itself is not secret, but clear for consistency
            key_path_.clear();
        }
    }

    // Move-only: prevent sensitive material from lingering in copies
    TlsConfig(TlsConfig&&) noexcept = default;
    TlsConfig& operator=(TlsConfig&&) noexcept = default;
    TlsConfig(const TlsConfig&) = delete;
    TlsConfig& operator=(const TlsConfig&) = delete;

    // Accessors
    Mode mode() const { return mode_; }
    VerifyMode verify_mode() const { return verify_; }
    bool is_server() const { return is_server_; }
    const std::string& cert_path() const { return cert_path_; }
    const std::string& key_path() const { return key_path_; }
    const std::string& ca_path() const { return ca_path_; }
    const std::string& psk() const { return psk_; }
    const std::string& psk_identity() const { return psk_identity_; }

    // Setters
    void set_server(bool is_server) { is_server_ = is_server; }
    void set_verify_mode(VerifyMode mode) { verify_ = mode; }
};

#endif // SONG_HAS_TLS

} // namespace song
