// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include "wire.hpp"
#include <mutex>
#include <vector>
#include <unordered_map>
#include <functional>

namespace song {

class Transport;

/// Thread-safe subscription registry for property notifications.
/// Maps (object_id, property_id) -> set of subscriber transports.
/// When a property changes, fans out MSG_PROP_NOTIFY to all subscribers.
class SubscriptionRegistry {
public:
    /// Subscriber identifier (opaque, used for unsubscribe-all-on-disconnect)
    using SubscriberId = uintptr_t;

    SubscriptionRegistry() = default;

    // Non-copyable, non-movable
    SubscriptionRegistry(const SubscriptionRegistry&) = delete;
    SubscriptionRegistry& operator=(const SubscriptionRegistry&) = delete;

    /// Subscribe a transport to property changes
    /// @param subscriber_id Unique ID for this subscriber (e.g., Transport pointer)
    /// @param object_id Object to watch
    /// @param property_id Property to watch
    /// @param transport Transport to send notifications on (must outlive subscription)
    void subscribe(SubscriberId subscriber_id, i32 object_id, u16 property_id,
                   Transport* transport);

    /// Unsubscribe from a specific property
    void unsubscribe(SubscriberId subscriber_id, i32 object_id, u16 property_id);

    /// Remove all subscriptions for a subscriber (call on client disconnect)
    void unsubscribe_all(SubscriberId subscriber_id);

    /// Notify all subscribers of a property change
    /// Sends MSG_PROP_NOTIFY to each subscribed transport.
    /// @param type_id Object type
    /// @param object_id Object instance
    /// @param property_id Property that changed
    /// @param value New property value
    void notify(u32 type_id, i32 object_id, u16 property_id, const Buffer& value);

    /// Get number of subscribers for a specific property
    size_t subscriber_count(i32 object_id, u16 property_id) const;

    /// Get total number of subscriptions across all properties
    size_t total_subscriptions() const;

private:
    struct SubscriptionKey {
        i32 object_id;
        u16 property_id;

        bool operator==(const SubscriptionKey& other) const {
            return object_id == other.object_id && property_id == other.property_id;
        }
    };

    struct SubscriptionKeyHash {
        size_t operator()(const SubscriptionKey& k) const {
            return std::hash<i32>()(k.object_id) ^ (std::hash<u16>()(k.property_id) << 16);
        }
    };

    struct Subscriber {
        SubscriberId id;
        Transport* transport;
    };

    mutable std::mutex mutex_;
    std::unordered_map<SubscriptionKey, std::vector<Subscriber>, SubscriptionKeyHash> subs_;
};

} // namespace song
