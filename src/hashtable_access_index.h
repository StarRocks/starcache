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

#include <shared_mutex>

#include "access_index.h"
#include "utils/parallel_hash_map.h"

namespace starrocks::starcache {

const static uint32_t kHashMapShardBits = 6;

class HashTableAccessIndex : public AccessIndex {
public:
    bool insert(const CacheId& id, const CacheItemPtr& item) override;
    CacheItemPtr find(const CacheId& id) override;
    bool remove(const CacheId& id) override;

private:
    ParallelHashMap<CacheId, CacheItemPtr, kHashMapShardBits> _cache_items;
};

} // namespace starrocks::starcache
