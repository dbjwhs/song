// MIT License
// Copyright (c) 2026 dbjwhs

// Backup Agent Service - Server Implementation
// Filesystem-backed backup agent with TCP support.
// Exercises: sync RPC, streaming, TLS, multi-client, property notifications, mDNS

#include <song/song.hpp>
#include <song/logging.hpp>
#include <song/security.hpp>
#include "backup.hpp"
#include "backup_constants.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <mutex>
#include <iomanip>
#include <sstream>
#include <cstdlib>
#include <cstring>

using namespace song;
using namespace song::backup;

namespace fs = std::filesystem;

// Stream chunk size: 64 KB
constexpr size_t kStreamChunkSize = 65536;

// Fixed key for deterministic checksum (not for security, just fingerprinting)
static const std::string kChecksumKey = "song-backup-checksum-key-32byte!";

// Convert filesystem time to unix milliseconds
static i64 to_unix_ms(fs::file_time_type ftime) {
    // file_clock -> system_clock conversion
    auto sys_time = decltype(ftime)::clock::to_sys(ftime);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        sys_time.time_since_epoch());
    return ms.count();
}

// Validate and resolve a path safely within the root.
// Returns the canonical resolved path, or nullopt if the path escapes root.
static std::optional<fs::path> safe_resolve(const fs::path& root, const std::string& rel) {
    // Reject absolute paths
    if (rel.empty() || rel[0] == '/') {
        return std::nullopt;
    }
    // Reject any path component that is ".." (component-based, not substring)
    fs::path rel_path(rel);
    for (const auto& component : rel_path) {
        if (component == "..") {
            return std::nullopt;
        }
    }
    // Resolve and canonicalize
    auto canonical_root = fs::weakly_canonical(root);
    auto canonical_path = fs::weakly_canonical(root / rel);
    // Verify canonical_path starts with canonical_root and is not the root itself
    auto root_str = canonical_root.string();
    auto path_str = canonical_path.string();
    if (path_str.size() <= root_str.size() ||
        path_str.compare(0, root_str.size(), root_str) != 0) {
        return std::nullopt;
    }
    // Return canonical path (not the unresolved one) to prevent symlink TOCTOU
    return canonical_path;
}

// Convert HmacTag to hex string
static std::string hmac_to_hex(const HmacTag& tag) {
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (auto b : tag) {
        oss << std::setw(2) << static_cast<unsigned>(b);
    }
    return oss.str();
}

class BackupAgentImpl : public IBackupAgent {
    fs::path root_;
    Progress progress_{};
    mutable std::mutex progress_mutex_;
    SubscriptionRegistry* sub_registry_ = nullptr;

public:
    explicit BackupAgentImpl(const fs::path& root) : root_(root) {
        // Ensure root exists
        if (!fs::exists(root_)) {
            fs::create_directories(root_);
        }
        progress_.status = static_cast<i32>(BackupStatus::idle);
    }

    void set_subscription_registry(SubscriptionRegistry* reg) {
        sub_registry_ = reg;
    }

    std::vector<FileEntry> list_files() override {
        std::vector<FileEntry> entries;
        if (!fs::exists(root_)) {
            return entries;
        }
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            if (entry.is_regular_file()) {
                FileEntry fe;
                fe.path = fs::relative(entry.path(), root_).string();
                fe.size = static_cast<i64>(entry.file_size());
                fe.modified_at = to_unix_ms(entry.last_write_time());
                entries.push_back(std::move(fe));
            }
        }
        // Sort for deterministic output
        std::sort(entries.begin(), entries.end(),
                  [](const FileEntry& a, const FileEntry& b) { return a.path < b.path; });
        return entries;
    }

    FileEntry get_file_info(const std::string& path) override {
        FileEntry fe;
        fe.path = path;

        auto resolved = safe_resolve(root_, path);
        if (!resolved || !fs::exists(*resolved) || !fs::is_regular_file(*resolved)) {
            fe.size = 0;
            fe.modified_at = 0;
            return fe;
        }

        fe.size = static_cast<i64>(fs::file_size(*resolved));
        fe.modified_at = to_unix_ms(fs::last_write_time(*resolved));
        return fe;
    }

    bool file_exists(const std::string& path) override {
        auto resolved = safe_resolve(root_, path);
        return resolved && fs::exists(*resolved) && fs::is_regular_file(*resolved);
    }

    std::vector<FileEntry> changes_since(i64 timestamp_ms) override {
        auto all = list_files();
        std::vector<FileEntry> changed;
        for (auto& fe : all) {
            if (fe.modified_at >= timestamp_ms) {
                changed.push_back(std::move(fe));
            }
        }
        return changed;
    }

    ChunkResult receive_chunk(const FileChunk& chunk) override {
        ChunkResult result;
        result.success = false;
        result.bytes_written = 0;
        result.total_received = 0;

        // Reject negative offsets
        if (chunk.offset < 0) {
            return result;
        }

        auto resolved = safe_resolve(root_, chunk.path);
        if (!resolved) {
            return result;
        }

        // Create parent directories if needed
        auto parent = resolved->parent_path();
        if (!fs::exists(parent)) {
            fs::create_directories(parent);
        }

        // For offset-0 final chunks, truncate any existing file to avoid stale tail bytes
        bool is_fresh_write = (chunk.offset == 0 && chunk.is_final);

        if (fs::exists(*resolved)) {
            // File exists: open for read+write (or truncate for fresh overwrite)
            auto mode = std::ios::binary | std::ios::in | std::ios::out;
            if (is_fresh_write) {
                mode = std::ios::binary | std::ios::out | std::ios::trunc;
            }
            std::fstream file(*resolved, mode);
            if (!file.is_open()) {
                return result;
            }
            file.seekp(chunk.offset);
            file.write(reinterpret_cast<const char*>(chunk.data.data()),
                        static_cast<std::streamsize>(chunk.data.size()));
            if (!file.good()) {
                return result;
            }
            file.close();
        } else {
            // New file: create and write
            std::ofstream file(*resolved, std::ios::binary);
            if (!file.is_open()) {
                return result;
            }
            if (chunk.offset > 0) {
                // Seek past beginning for non-zero offset (creates sparse file)
                file.seekp(chunk.offset);
            }
            file.write(reinterpret_cast<const char*>(chunk.data.data()),
                        static_cast<std::streamsize>(chunk.data.size()));
            if (!file.good()) {
                return result;
            }
            file.close();
        }

        result.success = true;
        result.bytes_written = static_cast<i64>(chunk.data.size());
        result.total_received = static_cast<i64>(fs::file_size(*resolved));

        // Update progress and notify subscribers
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            progress_.current_file = chunk.path;
            progress_.bytes_done += result.bytes_written;
            if (chunk.is_final) {
                progress_.files_done++;
            }
        }

        notify_progress();

        return result;
    }

    bool delete_file(const std::string& path) override {
        auto resolved = safe_resolve(root_, path);
        if (!resolved || !fs::exists(*resolved) || !fs::is_regular_file(*resolved)) {
            return false;
        }
        return fs::remove(*resolved);
    }

    Progress get_progress() override {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        return progress_;
    }

    i64 get_storage_used() override {
        i64 total = 0;
        if (!fs::exists(root_)) {
            return total;
        }
        for (const auto& entry : fs::recursive_directory_iterator(root_)) {
            if (entry.is_regular_file()) {
                total += static_cast<i64>(entry.file_size());
            }
        }
        return total;
    }

    std::string compute_checksum(const std::string& path) override {
        auto resolved = safe_resolve(root_, path);
        if (!resolved || !fs::exists(*resolved)) {
            return "";
        }

        // Read file contents
        std::ifstream file(*resolved, std::ios::binary);
        if (!file.is_open()) {
            return "";
        }
        std::vector<char> content((std::istreambuf_iterator<char>(file)),
                                   std::istreambuf_iterator<char>());

        // Use HMAC-SHA256 with fixed key as a deterministic checksum
        auto tag = compute_hmac(kChecksumKey.data(), kChecksumKey.size(),
                                content.data(), content.size());
        return hmac_to_hex(tag);
    }

    // Stream a file in chunks (called by streaming dispatcher)
    void stream_file(const std::string& path, StreamWriter& writer) {
        auto resolved = safe_resolve(root_, path);
        if (!resolved || !fs::exists(*resolved)) {
            // Send empty stream (just stream_end via destructor)
            return;
        }

        std::ifstream file(*resolved, std::ios::binary);
        if (!file.is_open()) {
            return;
        }

        std::vector<char> buf(kStreamChunkSize);
        while (file.read(buf.data(), static_cast<std::streamsize>(buf.size())) || file.gcount() > 0) {
            auto bytes_read = file.gcount();
            Buffer chunk;
            encode_bytes(chunk, std::vector<std::byte>(
                reinterpret_cast<std::byte*>(buf.data()),
                reinterpret_cast<std::byte*>(buf.data()) + bytes_read));
            writer.write(chunk);
        }
        // StreamWriter destructor sends stream_end
    }

    // Send progress notification to all subscribed clients
    void notify_progress() {
        if (!sub_registry_) {
            return;
        }
        Progress p;
        {
            std::lock_guard<std::mutex> lock(progress_mutex_);
            p = progress_;
        }
        Buffer val;
        encode_Progress(val, p);
        sub_registry_->notify(kType_BackupAgent, kObject_BackupAgent,
                              kProp_progress, val);
    }

    // Reset progress (useful between operations)
    void reset_progress(i32 files_total = 0, i64 bytes_total = 0) {
        std::lock_guard<std::mutex> lock(progress_mutex_);
        progress_ = Progress{};
        progress_.files_total = files_total;
        progress_.bytes_total = bytes_total;
        progress_.status = static_cast<i32>(BackupStatus::transferring);
    }
};

static BackupAgentImpl* g_agent = nullptr;

void backup_dispatcher(u16 method_id, Buffer& request, Buffer& response) {
    dispatch_BackupAgent(*g_agent, method_id, request, response);
}

void backup_stream_dispatcher(u16 method_id, Buffer& request, StreamWriter& writer) {
    if (method_id == kMethod_BackupAgent_stream_file) {
        std::string path = decode_string(request);
        g_agent->stream_file(path, writer);
    } else {
        throw std::runtime_error("Unknown streaming method: " + std::to_string(method_id));
    }
}

// Custom secure multi-client loop
// Replicates ServiceRuntime::client_loop using public APIs, wrapping each
// accepted TCP connection in SecureTransport for HMAC authentication.
[[noreturn]] static void run_secure_multi(ServiceRuntime& runtime,
                                          TcpListener& listener,
                                          const std::string& key) {
    for (;;) {
        auto tcp_client = listener.accept(-1);
        if (!tcp_client) {
            continue;
        }

        // Wrap in SecureTransport
        auto secure_client = std::make_shared<SecureTransport>(
            std::move(tcp_client), SecurityConfig(key));

        // Detach threads -- they self-clean via subscriber_id unsubscribe.
        // In production, use a thread pool. Detach is safe here because the
        // [[noreturn]] loop outlives all client threads.
        std::thread([&runtime, secure_client]() {
            auto subscriber_id = reinterpret_cast<
                SubscriptionRegistry::SubscriberId>(secure_client.get());
            try {
                // Init handshake
                runtime.send_init_confirmation_transport(*secure_client);

                // Track objects and subscriptions for this connection
                std::unordered_set<i32> tracked_objects;
                ServiceRuntime::SubscriptionSet subscriptions;

                // Message loop
                for (;;) {
                    Buffer msg;
                    if (!secure_client->receive(msg, -1)) {
                        runtime.subscriptions().unsubscribe_all(subscriber_id);
                        return;
                    }

                    wire::Header hdr = wire::decode_header(msg);
                    if (hdr.magic != wire::kMagic) {
                        runtime.subscriptions().unsubscribe_all(subscriber_id);
                        return;
                    }
                    if (hdr.type == wire::MsgType::shutdown) {
                        runtime.subscriptions().unsubscribe_all(subscriber_id);
                        return;
                    }
                    if (hdr.type == wire::MsgType::init_ack) {
                        continue;
                    }

                    runtime.handle_message(hdr, msg, *secure_client,
                                           tracked_objects, subscriptions);
                }
            } catch (const std::exception& e) {
                Log::warn("Secure client error: " + std::string(e.what()));
                runtime.subscriptions().unsubscribe_all(subscriber_id);
            } catch (...) {
                runtime.subscriptions().unsubscribe_all(subscriber_id);
            }
        }).detach();
    }
}

int main(int argc, char* argv[]) {
    // Parse arguments
    u16 port = 19000;
    std::string root = "/tmp/song-backup";
    std::string key;
    bool discover = false;

    for (int ndx = 1; ndx < argc; ++ndx) {
        if (std::strcmp(argv[ndx], "--port") == 0 && ndx + 1 < argc) {
            port = static_cast<u16>(std::atoi(argv[++ndx]));
        } else if (std::strcmp(argv[ndx], "--root") == 0 && ndx + 1 < argc) {
            root = argv[++ndx];
        } else if (std::strcmp(argv[ndx], "--key") == 0 && ndx + 1 < argc) {
            key = argv[++ndx];
        } else if (std::strcmp(argv[ndx], "--discover") == 0) {
            discover = true;
        } else if (argv[ndx][0] != '-') {
            // Positional: port then root
            if (ndx == 1) {
                port = static_cast<u16>(std::atoi(argv[ndx]));
            } else if (ndx == 2) {
                root = argv[ndx];
            }
        }
    }

    Log::info("Backup agent starting on port " + std::to_string(port) +
              " root=" + root +
              (key.empty() ? "" : " [HMAC auth]") +
              (discover ? " [discoverable]" : ""));

    BackupAgentImpl agent(root);
    g_agent = &agent;

    ServiceRuntime runtime;

    // Wire up progress notifications
    agent.set_subscription_registry(&runtime.subscriptions());
    runtime.set_capability(wire::Capability::properties);

    // Register sync RPC dispatcher (service ID 1)
    runtime.register_dispatcher(kService_BackupAgent, backup_dispatcher);

    // Register streaming dispatcher (service ID 2)
    runtime.register_stream_dispatcher(kService_BackupAgent_Stream,
                                       backup_stream_dispatcher);

    // Register all sync methods for capability exchange
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_list_files);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_get_file_info);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_file_exists);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_changes_since);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_receive_chunk);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_delete_file);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_get_progress);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_get_storage_used);
    runtime.register_method(kService_BackupAgent, kMethod_BackupAgent_compute_checksum);

    // Register streaming method
    runtime.register_method(kService_BackupAgent_Stream, kMethod_BackupAgent_stream_file,
                           wire::MethodFlags::streaming);

    // Advertise capabilities
    runtime.set_capability(wire::Capability::streaming);

    Log::debug("Backup agent ready: " +
               std::to_string(runtime.service_count()) + " service(s), " +
               std::to_string(runtime.method_count()) + " method(s)");

    if (!key.empty()) {
        // Secure multi-client mode: custom accept loop with HMAC auth
        TcpListener listener;
        listener.listen(port);

        // Optional mDNS registration
        std::unique_ptr<Discovery> disc;
        std::unique_ptr<ServiceRegistration> reg;
        if (discover) {
            disc = create_discovery();
            if (disc) {
                auto svc_type = Discovery::make_service_type("backup");
                reg = std::make_unique<ServiceRegistration>(
                    *disc, "BackupAgent", svc_type,
                    listener.bound_port());
            }
        }

        run_secure_multi(runtime, listener, key);
    } else if (discover) {
        // Discoverable multi-client mode (no auth)
        runtime.run_tcp_discoverable(port, "BackupAgent", "backup");
    } else {
        // Plain multi-client mode
        runtime.run_tcp_multi(port);
    }

    return 0;
}
