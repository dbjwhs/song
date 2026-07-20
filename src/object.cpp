// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/object.hpp"
#include "song/subscription.hpp"
#include "song/error.hpp"

namespace song {

void Object::notify_property(u16 prop_id, const Buffer& value) {
    // Fan-out through subscription registry if available (multi-client)
    if (sub_registry_) {
        sub_registry_->notify(type_id_, object_id_, prop_id, value);
        return;
    }
    // Single-client callback fallback
    if (notify_cb_) {
        notify_cb_(type_id_, object_id_, prop_id, value);
    }
}

ObjectRegistry::~ObjectRegistry() {
    clear();
}

void ObjectRegistry::register_factory(u32 type_id, ObjectFactory factory) {
    std::lock_guard lock(mutex_);
    factories_[type_id] = std::move(factory);
}

i32 ObjectRegistry::create_object(u32 type_id, u16 constructor_id, Buffer& args) {
    std::lock_guard lock(mutex_);

    auto it = factories_.find(type_id);
    if (it == factories_.end()) {
        throw ServiceError("Unknown type ID: " + std::to_string(type_id));
    }

    // Create the object using the factory
    Object* obj = it->second(constructor_id, args);
    if (!obj) {
        throw ServiceError("Factory returned null for type ID: " + std::to_string(type_id));
    }

    // Assign ID and register (registry owns the object via shared_ptr)
    i32 id = next_id_--;
    obj->init(id, this, type_id);
    objects_[id] = std::shared_ptr<Object>(obj);

    return id;
}

i32 ObjectRegistry::register_object(Object* obj) {
    if (!obj) {
        throw ServiceError("Cannot register null object");
    }

    std::lock_guard lock(mutex_);
    i32 id = next_id_--;
    obj->init(id, this);
    objects_[id] = std::shared_ptr<Object>(obj);
    return id;
}

Object* ObjectRegistry::get(i32 id) const {
    std::lock_guard lock(mutex_);
    auto it = objects_.find(id);
    return it != objects_.end() ? it->second.get() : nullptr;
}

std::shared_ptr<Object> ObjectRegistry::get_shared(i32 id) const {
    std::lock_guard lock(mutex_);
    auto it = objects_.find(id);
    return it != objects_.end() ? it->second : nullptr;
}

bool ObjectRegistry::add_ref(i32 id) {
    std::lock_guard lock(mutex_);
    auto it = objects_.find(id);
    if (it != objects_.end()) {
        it->second->add_ref();
        return true;
    }
    return false;
}

void ObjectRegistry::release(i32 id) {
    std::lock_guard lock(mutex_);
    auto it = objects_.find(id);
    if (it != objects_.end()) {
        if (it->second->release()) {
            // Wire refcount reached zero: drop the registry's reference. The
            // object's memory is freed once the last get_shared() handle (if any
            // dispatch thread is mid-use) also drops -- never underneath it.
            objects_.erase(it);
        }
    }
}

size_t ObjectRegistry::size() const {
    std::lock_guard lock(mutex_);
    return objects_.size();
}

bool ObjectRegistry::contains(i32 id) const {
    std::lock_guard lock(mutex_);
    return objects_.find(id) != objects_.end();
}

void ObjectRegistry::clear() {
    std::lock_guard lock(mutex_);
    objects_.clear();  // shared_ptrs drop; each object freed when its last ref goes
}

} // namespace song
