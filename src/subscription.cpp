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

    // Send with the lock held for the whole fan-out. In the multi-client runtime
    // (run_tcp_multi) each subscriber transport is owned by a different client
    // thread, which frees the transport immediately after its client_loop() calls
    // unsubscribe_all() on disconnect. If we released the lock before sending, a
    // peer thread could free a transport between our snapshot and our send() -- a
    // use-after-free that the try/catch below cannot catch (it is undefined
    // behavior, not an exception). Because unsubscribe_all() also acquires mutex_,
    // holding it across the sends forces a disconnecting subscriber to wait for any
    // in-flight notify to it, so its transport cannot be destroyed mid-send.
    //
    // mutex_ is recursive: a subscriber's send() is allowed to call back into the
    // registry on this same thread (e.g. subscribe/unsubscribe/subscriber_count)
    // without self-deadlocking. We snapshot the target list first so such a
    // reentrant mutation cannot invalidate the iteration in progress. (Serializing
    // a notify against a concurrent write the owning thread makes on the same
    // transport is a transport-level concern; see the single-writer note in
    // transport.hpp.)
    std::lock_guard lock(mutex_);

    std::vector<Transport*> targets;
    {
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
