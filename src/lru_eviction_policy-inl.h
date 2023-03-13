// Copyright 2023-present StarRocks, Inc. All rights reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     https://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include "lru_eviction_policy.h"

namespace starrocks::starcache {

template <typename T>
LruEvictionPolicy<T>::~LruEvictionPolicy() {
    // We do not plan to support the persistence of the eviction policy in this period,
    // so clear the elements directly.
    _lru_container->prune();
}

template <typename T>
char* makeup_key(const T& id) {
    return reinterpret_cast<char*>(const_cast<T*>(&id));
}

inline void free_key_old(char* key) {
    delete[] key;
}

template <typename T>
bool LruEvictionPolicy<T>::add(const T& id, size_t size) {
    auto deleter = [](const LRUKey& key, void* value) {};
    char* key = makeup_key(id);
    auto handle = _lru_container->insert(LRUKey(key, sizeof(T)), size, nullptr, deleter);
    if (handle) {
        _lru_container->release(handle);
        return true;
    }
    return false;
}

template <typename T>
typename EvictionPolicy<T>::HandlePtr LruEvictionPolicy<T>::touch(const T& id) {
    char* key = makeup_key(id);
    auto handle = _lru_container->lookup(LRUKey(key, sizeof(T)));
    if (handle) {
        return std::make_shared<typename EvictionPolicy<T>::Handle>(this, handle);
    }
    return nullptr;
}

template <typename T>
void LruEvictionPolicy<T>::evict(size_t size, std::vector<T>* evicted) {
    std::vector<LRUHandle*> handles;
    _lru_container->evict(size, &handles);
    for (auto h : handles) {
        T* eid = reinterpret_cast<T*>(h->key_data);
        evicted->push_back(*eid);
    }
}

template <typename T>
void LruEvictionPolicy<T>::evict_for(const T& id, size_t size, std::vector<T>* evicted) {
    char* key = makeup_key(id);
    std::vector<LRUHandle*> handles;
    _lru_container->evict_for(LRUKey(key, sizeof(T)), size, &handles);
    for (auto h : handles) {
        T* eid = reinterpret_cast<T*>(h->key_data);
        evicted->push_back(*eid);
        h->free();
    }
}

template <typename T>
void LruEvictionPolicy<T>::release(void* hdl) {
    auto h = reinterpret_cast<LRUHandle*>(hdl);
    _lru_container->release(h);
}

template <typename T>
void LruEvictionPolicy<T>::remove(const T& id) {
    char* key = makeup_key(id);
    _lru_container->erase(LRUKey(key, sizeof(T)));
}

template <typename T>
void LruEvictionPolicy<T>::clear() {
    _lru_container->prune();
}

} // namespace starrocks::starcache
