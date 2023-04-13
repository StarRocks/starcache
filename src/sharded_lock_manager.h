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

#include <butil/memory/singleton.h>

#include <mutex>
#include <shared_mutex>

#include "aux_funcs.h"
#include "common/config.h"
#include "common/macros.h"

namespace starrocks::starcache {

class ShardedLockManager {
public:
    static ShardedLockManager* GetInstance() { return Singleton<ShardedLockManager>::get(); }

    std::unique_lock<std::shared_mutex> unique_lock(uint64_t hash) {
        std::unique_lock<std::shared_mutex> lck(_mutex(hash), std::defer_lock);
        return lck;
    }

    std::shared_lock<std::shared_mutex> shared_lock(uint64_t hash) {
        std::shared_lock<std::shared_mutex> lck(_mutex(hash), std::defer_lock);
        return lck;
    }

private:
    ShardedLockManager() : _mutexes(1lu << config::FLAGS_sharded_lock_shard_bits) {}

    std::shared_mutex& _mutex(uint64_t hash) {
        STATIC_EXCEPT_UT uint64_t shard_count = _mutexes.size();
        return _mutexes[hash & (shard_count - 1)];
    }

    friend struct DefaultSingletonTraits<ShardedLockManager>;
    DISALLOW_COPY_AND_ASSIGN(ShardedLockManager);

    std::vector<std::shared_mutex> _mutexes;
};

inline std::unique_lock<std::shared_mutex> block_unique_lock(const BlockKey& key) {
    return ShardedLockManager::GetInstance()->unique_lock(block_shard(key));
}

inline std::shared_lock<std::shared_mutex> block_shared_lock(const BlockKey& key) {
    return ShardedLockManager::GetInstance()->shared_lock(block_shard(key));
}

} // namespace starrocks::starcache
