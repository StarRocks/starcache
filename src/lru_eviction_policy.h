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

#pragma once

#include <atomic>

#include "common/config.h"
#include "eviction_policy.h"
#include "utils/lru_container.h"

namespace starrocks::starcache {

template <typename T>
class LruEvictionPolicy : public EvictionPolicy<T> {
public:
    using HandlePtr = typename EvictionPolicy<T>::HandlePtr;

    LruEvictionPolicy() : _lru_container(new ShardedLRUContainer(1lu << config::FLAGS_lru_container_shard_bits)) {}

    ~LruEvictionPolicy() override;

    bool add(const T& id, size_t size) override;

    HandlePtr touch(const T& id) override;

    void evict(size_t count, std::vector<T>* evicted) override;

    void evict_for(const T& id, size_t count, std::vector<T>* evicted) override;

    void release(void* hdl) override;

    void remove(const T& id) override;

    void clear() override;

private:
    std::unique_ptr<ShardedLRUContainer> _lru_container = nullptr;
};

} // namespace starrocks::starcache

#include "lru_eviction_policy-inl.h"
