// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/transport.hpp"
#include "song/security.hpp"
#include "song/wire.hpp"
#include "song/error.hpp"

#if defined(SONG_HAS_TLS)

#include <mbedtls/ssl.h>
#include <mbedtls/ssl_cache.h>
#include <mbedtls/error.h>
#include <mbedtls/net_sockets.h>
#include <mbedtls/x509_crt.h>
#include <mbedtls/pk.h>
#include <psa/crypto.h>

#include <unistd.h>
#include <poll.h>
#include <netinet/tcp.h>
#include <cstring>
#include <cerrno>
#include <cstdint>

#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

namespace song {

// =============================================================================
// Helper: format mbedTLS error code as string
// =============================================================================

static std::string tls_error_string(int ret) {
    char buf[256];
    mbedtls_strerror(ret, buf, sizeof(buf));
    return std::string(buf);
}

// =============================================================================
// TlsTransport PIMPL
// =============================================================================

struct TlsTransport::Impl {
    mbedtls_ssl_context ssl;
    mbedtls_ssl_config conf;
    mbedtls_x509_crt cacert;
    mbedtls_x509_crt cert;
    mbedtls_pk_context pkey;

    int sock = -1;
    std::string peer_addr;
    u16 peer_port = 0;
    bool connected = false;
    bool handshake_done = false;
    bool is_server = false;
    bool is_cert_mode = false;      // certificate (vs PSK) authentication
    bool verify_required = false;   // peer certificate must validate
    bool verify_optional = false;   // verify if presented; result must be checked
    std::string expected_hostname;  // server name a client must find in the cert

    Impl() {
        mbedtls_ssl_init(&ssl);
        mbedtls_ssl_config_init(&conf);
        mbedtls_x509_crt_init(&cacert);
        mbedtls_x509_crt_init(&cert);
        mbedtls_pk_init(&pkey);
    }

    ~Impl() {
        if (handshake_done) {
            mbedtls_ssl_close_notify(&ssl);
        }
        mbedtls_ssl_free(&ssl);
        mbedtls_ssl_config_free(&conf);
        mbedtls_x509_crt_free(&cacert);
        mbedtls_x509_crt_free(&cert);
        mbedtls_pk_free(&pkey);
        if (sock >= 0) {
            ::close(sock);
        }
    }

    // Non-copyable
    Impl(const Impl&) = delete;
    Impl& operator=(const Impl&) = delete;
};

// =============================================================================
// Custom BIO callbacks for mbedTLS (operate on raw socket fd)
// =============================================================================

static int tls_bio_send(void* ctx, const unsigned char* buf, size_t len) {
    int sock = *static_cast<int*>(ctx);
    auto n = ::send(sock, buf, len, MSG_NOSIGNAL);
    if (n < 0) {
        if (errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_WRITE;
        }
        return MBEDTLS_ERR_NET_SEND_FAILED;
    }
    return static_cast<int>(n);
}

static int tls_bio_recv(void* ctx, unsigned char* buf, size_t len) {
    int sock = *static_cast<int*>(ctx);
    auto n = ::recv(sock, buf, len, 0);
    if (n < 0) {
        if (errno == EINTR) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return MBEDTLS_ERR_SSL_WANT_READ;
        }
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }
    if (n == 0) {
        return MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY;
    }
    return static_cast<int>(n);
}

static int tls_bio_recv_timeout(void* ctx, unsigned char* buf, size_t len, uint32_t timeout) {
    // timeout=0 means "no timeout" in mbedTLS (blocking recv)
    if (timeout == 0) {
        return tls_bio_recv(ctx, buf, len);
    }

    int sock = *static_cast<int*>(ctx);

    struct pollfd pfd{};
    pfd.fd = sock;
    pfd.events = POLLIN;

    int poll_result;
    do {
        poll_result = poll(&pfd, 1, static_cast<int>(timeout));
    } while (poll_result < 0 && errno == EINTR);

    if (poll_result == 0) {
        return MBEDTLS_ERR_SSL_TIMEOUT;
    }
    if (poll_result < 0) {
        return MBEDTLS_ERR_NET_RECV_FAILED;
    }

    return tls_bio_recv(ctx, buf, len);
}

// =============================================================================
// PSA crypto one-time initialization
// =============================================================================

static bool ensure_psa_init() {
    static bool initialized = false;
    if (!initialized) {
        psa_status_t status = psa_crypto_init();
        if (status != PSA_SUCCESS) {
            return false;
        }
        initialized = true;
    }
    return true;
}

// =============================================================================
// TlsTransport Implementation
// =============================================================================

TlsTransport::TlsTransport(int sock, TlsConfig config,
                             const std::string& peer_addr, u16 peer_port)
    : impl_(std::make_unique<Impl>()) {
    if (!ensure_psa_init()) {
        ::close(sock);
        throw SecurityError("TLS: failed to initialize PSA crypto");
    }

    impl_->sock = sock;
    impl_->peer_addr = peer_addr;
    impl_->peer_port = peer_port;
    impl_->is_server = config.is_server();
    impl_->is_cert_mode = (config.mode() == TlsConfig::Mode::certificate);
    impl_->verify_required = (config.verify_mode() == TlsConfig::VerifyMode::required);
    impl_->verify_optional = (config.verify_mode() == TlsConfig::VerifyMode::optional);
    impl_->expected_hostname = config.expected_hostname();

    // Configure TLS defaults
    int endpoint = config.is_server() ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT;
    int ret = mbedtls_ssl_config_defaults(&impl_->conf, endpoint,
                                           MBEDTLS_SSL_TRANSPORT_STREAM,
                                           MBEDTLS_SSL_PRESET_DEFAULT);
    if (ret != 0) {
        throw SecurityError("TLS config_defaults failed: " + tls_error_string(ret));
    }

    // Set verification mode
    int authmode = MBEDTLS_SSL_VERIFY_REQUIRED;
    switch (config.verify_mode()) {
        case TlsConfig::VerifyMode::none:     authmode = MBEDTLS_SSL_VERIFY_NONE; break;
        case TlsConfig::VerifyMode::optional: authmode = MBEDTLS_SSL_VERIFY_OPTIONAL; break;
        case TlsConfig::VerifyMode::required: authmode = MBEDTLS_SSL_VERIFY_REQUIRED; break;
    }
    mbedtls_ssl_conf_authmode(&impl_->conf, authmode);

    // Load credentials based on mode
    if (config.mode() == TlsConfig::Mode::certificate) {
        // Load CA certificate
        if (!config.ca_path().empty()) {
            ret = mbedtls_x509_crt_parse_file(&impl_->cacert, config.ca_path().c_str());
            if (ret != 0) {
                throw SecurityError("TLS: failed to load CA cert '" + config.ca_path() +
                                    "': " + tls_error_string(ret));
            }
            mbedtls_ssl_conf_ca_chain(&impl_->conf, &impl_->cacert, nullptr);
        }

        // Load own certificate and key
        if (!config.cert_path().empty() && !config.key_path().empty()) {
            ret = mbedtls_x509_crt_parse_file(&impl_->cert, config.cert_path().c_str());
            if (ret != 0) {
                throw SecurityError("TLS: failed to load cert '" + config.cert_path() +
                                    "': " + tls_error_string(ret));
            }

            ret = mbedtls_pk_parse_keyfile(&impl_->pkey, config.key_path().c_str(),
                                            nullptr);
            if (ret != 0) {
                throw SecurityError("TLS: failed to load key '" + config.key_path() +
                                    "': " + tls_error_string(ret));
            }

            ret = mbedtls_ssl_conf_own_cert(&impl_->conf, &impl_->cert, &impl_->pkey);
            if (ret != 0) {
                throw SecurityError("TLS: failed to configure own cert: " +
                                    tls_error_string(ret));
            }
        }
    } else {
        // PSK mode
        ret = mbedtls_ssl_conf_psk(&impl_->conf,
            reinterpret_cast<const unsigned char*>(config.psk().data()),
            config.psk().size(),
            reinterpret_cast<const unsigned char*>(config.psk_identity().data()),
            config.psk_identity().size());
        if (ret != 0) {
            throw SecurityError("TLS: failed to configure PSK: " + tls_error_string(ret));
        }
    }

    // Setup SSL context
    ret = mbedtls_ssl_setup(&impl_->ssl, &impl_->conf);
    if (ret != 0) {
        throw SecurityError("TLS ssl_setup failed: " + tls_error_string(ret));
    }

    // Set BIO callbacks using our socket
    mbedtls_ssl_set_bio(&impl_->ssl, &impl_->sock,
                         tls_bio_send, tls_bio_recv, tls_bio_recv_timeout);

    impl_->connected = true;
}

TlsTransport::~TlsTransport() = default;

TlsTransport::TlsTransport(TlsTransport&&) noexcept = default;
TlsTransport& TlsTransport::operator=(TlsTransport&&) noexcept = default;

void TlsTransport::handshake() {
    if (!impl_ || !impl_->connected) {
        throw SecurityError("TLS handshake: not connected");
    }

    // A certificate-mode client must bind the expected server hostname so mbedTLS
    // matches it against the peer certificate's CN/SAN. Without this call mbedTLS
    // validates only the chain to the trusted CA and accepts ANY same-CA cert for
    // ANY host, so an on-path attacker holding a valid same-CA certificate could
    // complete the handshake and MITM the channel. If verification is required but
    // no hostname was provided, fail closed rather than silently proceeding with
    // the name check disabled. (Server endpoints and PSK mode do not do CN/SAN
    // matching, so this applies only to certificate-mode clients.)
    if (!impl_->is_server && impl_->is_cert_mode) {
        if (impl_->expected_hostname.empty()) {
            if (impl_->verify_required) {
                impl_->connected = false;
                throw SecurityError(
                    "TLS client: certificate verification is required but no expected "
                    "hostname was set (call TlsConfig::set_expected_hostname); refusing "
                    "to handshake without hostname verification");
            }
            // verify none/optional: no name to match against; leave hostname unset.
        } else {
            int hret = mbedtls_ssl_set_hostname(&impl_->ssl, impl_->expected_hostname.c_str());
            if (hret != 0) {
                impl_->connected = false;
                throw SecurityError("TLS: failed to set expected hostname '" +
                                    impl_->expected_hostname + "': " + tls_error_string(hret));
            }
        }
    }

    int ret;
    do {
        ret = mbedtls_ssl_handshake(&impl_->ssl);
    } while (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE);

    if (ret != 0) {
        impl_->connected = false;
        throw SecurityError("TLS handshake failed: " + tls_error_string(ret));
    }

    // In OPTIONAL verify mode mbedTLS completes the handshake even when the peer
    // certificate fails validation -- it records the result instead of aborting,
    // and the application must inspect it. Without this check OPTIONAL silently
    // behaves like NONE and accepts expired/self-signed/wrong-CA certs. (REQUIRED
    // already aborts inside mbedtls_ssl_handshake(); NONE intentionally skips
    // verification.)
    if (impl_->verify_optional) {
        uint32_t vr = mbedtls_ssl_get_verify_result(&impl_->ssl);
        if (vr != 0) {
            char info[512];
            mbedtls_x509_crt_verify_info(info, sizeof(info), "  ", vr);
            impl_->connected = false;
            throw SecurityError(std::string("TLS peer certificate verification failed:\n") + info);
        }
    }

    impl_->handshake_done = true;
}

void TlsTransport::send(const Buffer& msg) {
    if (!impl_ || !impl_->connected || !impl_->handshake_done) {
        throw ServiceError("TlsTransport: not connected or handshake incomplete");
    }

    // Stamp the encrypted flag on the outgoing message header (byte offset 4)
    // This is informational -- the actual encryption is handled by TLS below
    Buffer stamped;
    stamped.write(msg.data(), msg.size());
    if (stamped.size() >= 16) {
        auto* flags_byte = reinterpret_cast<u8*>(stamped.data() + 4);
        *flags_byte |= static_cast<u8>(wire::MsgFlags::encrypted);
    }

    size_t offset = 0;
    while (offset < stamped.size()) {
        int ret = mbedtls_ssl_write(&impl_->ssl,
            reinterpret_cast<const unsigned char*>(stamped.data()) + offset,
            stamped.size() - offset);

        if (ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
            continue;
        }
        if (ret < 0) {
            impl_->connected = false;
            throw ServiceError("TlsTransport: write failed: " + tls_error_string(ret));
        }
        offset += static_cast<size_t>(ret);
    }
}

bool TlsTransport::receive(Buffer& msg, int timeout_ms) {
    if (!impl_ || !impl_->connected || !impl_->handshake_done) {
        return false;
    }

    // Set read timeout. Song's Transport contract uses timeout_ms == -1 for a
    // blocking read and timeout_ms == 0 for "return immediately if nothing is
    // ready" (as TcpTransport does with poll(...,0)). mbedTLS, however, overloads
    // a read timeout of 0 to mean "block indefinitely", so a naive 0 -> 0 mapping
    // makes receive(msg, 0) block forever, diverging from every other transport.
    // Map 0 to the smallest positive poll interval (1 ms) to approximate a
    // non-blocking check without blocking.
    if (timeout_ms > 0) {
        mbedtls_ssl_conf_read_timeout(&impl_->conf, static_cast<uint32_t>(timeout_ms));
    } else if (timeout_ms == 0) {
        mbedtls_ssl_conf_read_timeout(&impl_->conf, 1);
    } else {
        mbedtls_ssl_conf_read_timeout(&impl_->conf, 0);  // timeout_ms < 0: blocking
    }

    msg.reset();

    // Read header (16 bytes)
    unsigned char header_buf[16];
    size_t header_offset = 0;
    while (header_offset < 16) {
        int ret = mbedtls_ssl_read(&impl_->ssl,
            header_buf + header_offset, 16 - header_offset);

        if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
            continue;
        }
        if (ret == MBEDTLS_ERR_SSL_TIMEOUT) {
            throw ServiceError("TlsTransport: read timeout");
        }
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY || ret == 0) {
            impl_->connected = false;
            return false;  // Clean disconnect
        }
        if (ret < 0) {
            impl_->connected = false;
            throw ServiceError("TlsTransport: read failed: " + tls_error_string(ret));
        }
        header_offset += static_cast<size_t>(ret);
    }

    msg.write(header_buf, 16);
    msg.reset_read();

    // Decode header to get payload size
    auto hdr = wire::decode_header(msg);

    // Read payload if present
    if (hdr.payload_size > 0) {
        if (hdr.payload_size > wire::kMaxPayloadSize) {
            throw ProtocolError("TlsTransport: payload size exceeds maximum");
        }

        std::vector<unsigned char> payload_buf(hdr.payload_size);
        size_t offset = 0;
        while (offset < hdr.payload_size) {
            int ret = mbedtls_ssl_read(&impl_->ssl,
                payload_buf.data() + offset, hdr.payload_size - offset);

            if (ret == MBEDTLS_ERR_SSL_WANT_READ) {
                continue;
            }
            if (ret < 0) {
                impl_->connected = false;
                throw ServiceError("TlsTransport: payload read failed: " +
                                   tls_error_string(ret));
            }
            if (ret == 0) {
                throw ServiceError("TlsTransport: connection closed during payload read");
            }
            offset += static_cast<size_t>(ret);
        }

        msg.write(payload_buf.data(), hdr.payload_size);
    }

    msg.reset_read();
    return true;
}

void TlsTransport::close() {
    if (impl_) {
        if (impl_->handshake_done) {
            mbedtls_ssl_close_notify(&impl_->ssl);
            impl_->handshake_done = false;
        }
        if (impl_->sock >= 0) {
            ::close(impl_->sock);
            impl_->sock = -1;
        }
        impl_->connected = false;
    }
}

bool TlsTransport::is_connected() const {
    return impl_ && impl_->connected && impl_->handshake_done;
}

const char* TlsTransport::type_name() const {
    return "tls";
}

const std::string& TlsTransport::peer_address() const {
    static const std::string empty;
    return impl_ ? impl_->peer_addr : empty;
}

u16 TlsTransport::peer_port() const {
    return impl_ ? impl_->peer_port : 0;
}

// =============================================================================
// TlsListener Implementation
// =============================================================================

TlsListener::~TlsListener() {
    close();
}

void TlsListener::listen(TlsConfig& config, u16 port, int backlog) {
    config.set_server(true);
    config_ = &config;
    inner_.listen(port, backlog);
}

std::unique_ptr<TlsTransport> TlsListener::accept(int timeout_ms) {
    if (!config_) {
        throw ServiceError("TlsListener: not configured");
    }

    auto tcp = inner_.accept(timeout_ms);
    if (!tcp) {
        return nullptr;  // Timeout
    }

    // Steal the socket fd from TcpTransport
    int sock = tcp->socket_fd();
    std::string peer_addr = tcp->peer_address();
    u16 peer_port = tcp->peer_port();

    // Detach the socket so TcpTransport destructor does not close it
    // We do this by moving the TcpTransport and explicitly releasing
    // NOTE: TcpTransport does not have a release() method, so we
    // create the TlsTransport before the TcpTransport destructor runs.
    // We need to prevent the double-close by duplicating the fd.
    int tls_sock = dup(sock);
    if (tls_sock < 0) {
        throw ServiceError("TlsListener: dup() failed: " + std::string(strerror(errno)));
    }
    tcp->close();  // Close original

    // Disable Nagle on the duped socket
    int nodelay = 1;
    setsockopt(tls_sock, IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay));

#ifdef SO_NOSIGPIPE
    int nosigpipe = 1;
    setsockopt(tls_sock, SOL_SOCKET, SO_NOSIGPIPE, &nosigpipe, sizeof(nosigpipe));
#endif

    // Create a server-mode TlsConfig copy for this connection
    // Since TlsConfig is move-only, we need to construct from the stored config's fields
    TlsConfig conn_config;
    if (config_->mode() == TlsConfig::Mode::certificate) {
        conn_config = TlsConfig(config_->cert_path(), config_->key_path(), config_->ca_path());
    } else {
        conn_config = TlsConfig(config_->psk(), config_->psk_identity(), TlsConfig::Mode::psk);
    }
    conn_config.set_server(true);
    conn_config.set_verify_mode(config_->verify_mode());

    auto tls = std::make_unique<TlsTransport>(tls_sock, std::move(conn_config),
                                               peer_addr, peer_port);
    tls->handshake();
    return tls;
}

void TlsListener::close() {
    inner_.close();
    config_ = nullptr;
}

} // namespace song

#endif // SONG_HAS_TLS
