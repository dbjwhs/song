// MIT License
// Copyright (c) 2026 dbjwhs

#include "song/object.hpp"
#include "song/error.hpp"

namespace song {

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

    // Assign ID and register
    i32 id = next_id_--;
    obj->init(id, this);
    objects_[id] = obj;

    return id;
}

i32 ObjectRegistry::register_object(Object* obj) {
    if (!obj) {
        throw ServiceError("Cannot register null object");
    }

    std::lock_guard lock(mutex_);
    i32 id = next_id_--;
    obj->init(id, this);
    objects_[id] = obj;
    return id;
}

Object* ObjectRegistry::get(i32 id) const {
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
            // Reference count reached zero - delete object
            delete it->second;
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
    for (auto& [id, obj] : objects_) {
        delete obj;
    }
    objects_.clear();
}

} // namespace song
