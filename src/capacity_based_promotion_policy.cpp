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

#include "capacity_based_promotion_policy.h"

#include <butil/fast_rand.h>

#include "cache_item.h"
#include "disk_space_manager.h"
#include "mem_space_manager.h"

namespace starrocks::starcache {

CapacityBasedPromotionPolicy::CapacityBasedPromotionPolicy(const Config& config)
        : _mem_cap_threshold(config.mem_cap_threshold) {}

BlockLocation CapacityBasedPromotionPolicy::check_write(const CacheItemPtr& cache_item, const BlockKey& block_key) {
    STATIC_EXCEPT_UT size_t disk_quota = DiskSpaceManager::GetInstance()->quota_bytes();
    if (disk_quota == 0) {
        return BlockLocation::MEM;
    }
    auto mem_space_mgr = MemSpaceManager::GetInstance();
    size_t used_rate = mem_space_mgr->used_bytes() * 100 / mem_space_mgr->quota_bytes();
    if (used_rate < _mem_cap_threshold && !_is_mem_overload()) {
        return BlockLocation::MEM;
    }
    if (!_is_disk_overload()) {
        return BlockLocation::DISK;
    }
    return BlockLocation::NONE;
}

bool CapacityBasedPromotionPolicy::check_promote(const CacheItemPtr& cache_item, const BlockKey& block_key) {
    if (UNLIKELY(_is_mem_overload())) {
        return false;
    }

    // Return true accoding probalility temperally until we add access count infomaition
    if (butil::fast_rand_less_than(100) < config::FLAGS_promotion_probalility) {
        return true;
    }
    return false;
}

} // namespace starrocks::starcache
