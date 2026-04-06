// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/subscription.hpp"
#include "song/transport.hpp"
#include <algorithm>

namespace song {

void SubscriptionRegistry::subscribe(SubscriberId subscriber_id, i32 object_id,
                                      u16 property_id, Transport* transport) {
    std::lock_guard lock(mutex_);
    SubscriptionKey key{object_id, property_id};
    auto& subscribers = subs_[key];

    // Check for duplicate
    for (const auto& sub : subscribers) {
        if (sub.id == subscriber_id) { // Already subscribed
            return;
        }
    }

    subscribers.push_back({subscriber_id, transport});
}

void SubscriptionRegistry::unsubscribe(SubscriberId subscriber_id, i32 object_id,
                                        u16 property_id) {
    std::lock_guard lock(mutex_);
    SubscriptionKey key{object_id, property_id};
    auto it = subs_.find(key);
    if (it == subs_.end()) {
        return;
    }

    auto& subscribers = it->second;
    subscribers.erase(
        std::remove_if(subscribers.begin(), subscribers.end(),
            [subscriber_id](const Subscriber& s) { return s.id == subscriber_id; }),
        subscribers.end());

    if (subscribers.empty()) {
        subs_.erase(it);
    }
}

void SubscriptionRegistry::unsubscribe_all(SubscriberId subscriber_id) {
    std::lock_guard lock(mutex_);
    for (auto it = subs_.begin(); it != subs_.end(); ) {
        auto& subscribers = it->second;
        subscribers.erase(
            std::remove_if(subscribers.begin(), subscribers.end(),
                [subscriber_id](const Subscriber& s) { return s.id == subscriber_id; }),
            subscribers.end());

        if (subscribers.empty()) {
            it = subs_.erase(it);
        } else {
            ++it;
        }
    }
}

void SubscriptionRegistry::notify(u32 type_id, i32 object_id, u16 property_id,
                                   const Buffer& value) {
    // Build the notification message once
    Buffer notify_msg = wire::create_property_notify_message(type_id, object_id, property_id, value);

    // Copy subscriber list under lock, then send outside lock
    std::vector<Transport*> targets;
    {
        std::lock_guard lock(mutex_);
        SubscriptionKey key{object_id, property_id};
        auto it = subs_.find(key);
        if (it == subs_.end()) {
            return;
        }

        targets.reserve(it->second.size());
        for (const auto& sub : it->second) {
            targets.push_back(sub.transport);
        }
    }

    // Send to all subscribers (outside lock to avoid deadlocks)
    for (Transport* tp : targets) {
        try {
            tp->send(notify_msg);
        } catch (...) {
            // Client may have disconnected -- will be cleaned up on next unsubscribe_all
        }
    }
}

size_t SubscriptionRegistry::subscriber_count(i32 object_id, u16 property_id) const {
    std::lock_guard lock(mutex_);
    SubscriptionKey key{object_id, property_id};
    auto it = subs_.find(key);
    if (it == subs_.end()) {
        return 0;
    }
    return it->second.size();
}

size_t SubscriptionRegistry::total_subscriptions() const {
    std::lock_guard lock(mutex_);
    size_t total = 0;
    for (const auto& [key, subscribers] : subs_) {
        total += subscribers.size();
    }
    return total;
}

} // namespace song
