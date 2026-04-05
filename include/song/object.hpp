// MIT License
// Copyright (c) 2026 dbjwhs

#pragma once

#include "types.hpp"
#include "buffer.hpp"
#include <unordered_map>
#include <unordered_set>
#include <mutex>
#include <functional>
#include <atomic>

namespace song {

class ObjectRegistry;

/// Base class for all remotable objects (DAG-style reference types)
/// Objects have identity (object_id), are reference counted, and support
/// property access and method dispatch via the wire protocol.
/// Callback type for property change notifications
/// Parameters: (type_id, object_id, property_id, value)
using PropertyNotifyCallback = std::function<void(u32, i32, u16, const Buffer&)>;

class Object {
    i32 object_id_ = 0;       // Assigned by registry (negative integers, 0 = null)
    u32 type_id_ = 0;         // Type identifier (set by registry on creation)
    std::atomic<int> ref_count_{1};  // Reference count
    ObjectRegistry* registry_ = nullptr;
    PropertyNotifyCallback notify_cb_;

public:
    virtual ~Object() = default;

    // Non-copyable, non-movable (identity semantics)
    Object() = default;
    Object(const Object&) = delete;
    Object& operator=(const Object&) = delete;
    Object(Object&&) = delete;
    Object& operator=(Object&&) = delete;

    /// Get the object's unique ID (negative integer, 0 = null)
    i32 object_id() const { return object_id_; }

    /// Get the object's type identifier
    u32 type_id() const { return type_id_; }

    /// Check if this is a null reference
    bool is_null() const { return object_id_ == 0; }

    /// Get the registry this object belongs to
    ObjectRegistry* registry() const { return registry_; }

    /// Called by runtime for property get operations
    /// Subclasses implement this to dispatch to the appropriate property getter
    /// @param prop_id Property identifier
    /// @param resp Buffer to write the property value to
    virtual void prop_get(u16 prop_id, Buffer& resp) = 0;

    /// Called by runtime for property set operations
    /// Subclasses implement this to dispatch to the appropriate property setter
    /// @param prop_id Property identifier
    /// @param req Buffer containing the new property value
    /// @param resp Buffer to write the actual stored value to
    virtual void prop_set(u16 prop_id, Buffer& req, Buffer& resp) = 0;

    /// Called by runtime for method dispatch
    /// Subclasses implement this to dispatch to the appropriate method
    /// @param method_id Method identifier
    /// @param req Buffer containing method arguments
    /// @param resp Buffer to write method result to
    virtual void dispatch(u16 method_id, Buffer& req, Buffer& resp) = 0;

    /// Notify that a property value has changed
    /// This triggers push notifications to subscribed clients.
    /// Call this from prop_set() or any internal state change.
    /// @param prop_id Property that changed
    /// @param value Buffer containing the new property value
    void notify_property(u16 prop_id, const Buffer& value);

    /// Set the notification callback (called by ServiceRuntime)
    void set_notify_callback(PropertyNotifyCallback cb) { notify_cb_ = std::move(cb); }

    /// Increment reference count
    void add_ref() {
        ref_count_.fetch_add(1, std::memory_order_relaxed);
    }

    /// Decrement reference count
    /// @return true if reference count reached zero (object should be deleted)
    bool release() {
        return ref_count_.fetch_sub(1, std::memory_order_acq_rel) == 1;
    }

    /// Get current reference count (for debugging/testing)
    int ref_count() const {
        return ref_count_.load(std::memory_order_relaxed);
    }

private:
    friend class ObjectRegistry;

    /// Initialize object with ID, type, and registry (called by ObjectRegistry)
    void init(i32 id, ObjectRegistry* reg, u32 type_id = 0) {
        object_id_ = id;
        type_id_ = type_id;
        registry_ = reg;
    }
};

/// Factory function signature for creating objects
/// @param constructor_id Which constructor to use (0 = default)
/// @param args Constructor arguments (serialized)
/// @return Newly created object (ownership transferred to registry)
using ObjectFactory = std::function<Object*(u16 constructor_id, Buffer& args)>;

/// Server-side object registry
/// Manages the lifecycle of remotable objects, assigning IDs and tracking references.
/// Objects are identified by negative integers (like DAG).
class ObjectRegistry {
    std::unordered_map<i32, Object*> objects_;
    std::unordered_map<u32, ObjectFactory> factories_;  // type_id -> factory
    i32 next_id_ = -1;  // Start at -1, decrement for each new object
    mutable std::mutex mutex_;

public:
    ObjectRegistry() = default;
    ~ObjectRegistry();

    // Non-copyable, non-movable
    ObjectRegistry(const ObjectRegistry&) = delete;
    ObjectRegistry& operator=(const ObjectRegistry&) = delete;
    ObjectRegistry(ObjectRegistry&&) = delete;
    ObjectRegistry& operator=(ObjectRegistry&&) = delete;

    /// Check if any factories are registered
    bool has_factories() const { return !factories_.empty(); }

    /// Register a factory for creating objects of a given type
    /// @param type_id Type identifier for this class
    /// @param factory Function that creates instances of this class
    void register_factory(u32 type_id, ObjectFactory factory);

    /// Create a new object using a registered factory
    /// @param type_id Type identifier
    /// @param constructor_id Constructor to use
    /// @param args Constructor arguments
    /// @return Object ID of the newly created object
    /// @throws ServiceError if type_id has no registered factory
    i32 create_object(u32 type_id, u16 constructor_id, Buffer& args);

    /// Register an existing object with the registry
    /// @param obj Object to register (registry takes ownership)
    /// @return Assigned object ID (negative integer)
    i32 register_object(Object* obj);

    /// Look up an object by ID
    /// @param id Object ID
    /// @return Pointer to object, or nullptr if not found
    Object* get(i32 id) const;

    /// Add a reference to an object
    /// @param id Object ID
    /// @return true if object was found and reference added
    bool add_ref(i32 id);

    /// Release a reference to an object
    /// If the reference count reaches zero, the object is deleted.
    /// @param id Object ID
    void release(i32 id);

    /// Get the number of registered objects
    size_t size() const;

    /// Check if an object exists
    bool contains(i32 id) const;

    /// Clear all objects (for shutdown)
    void clear();
};

} // namespace song
