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

#include "promotion_policy.h"

namespace starrocks::starcache {

// The class to decide the block location based on the memory capacity
class CapacityBasedPromotionPolicy : public PromotionPolicy {
public:
    struct Config {
        size_t mem_cap_threshold;
    };
    explicit CapacityBasedPromotionPolicy(const Config& config);
    ~CapacityBasedPromotionPolicy() override = default;

    BlockLocation check_write(const CacheItemPtr& cache_item, const BlockKey& block_key) override;

    bool check_promote(const CacheItemPtr& cache_item, const BlockKey& block_key) override;

private:
    // TODO: Monitor disk ioutil or io latency to reject some write requets when disks overload.
    bool _is_disk_overload() const { return false; }

    // TODO: Monitor memory allocation/free state to reject some promotion when massive memory operation
    // are pending.
    bool _is_mem_overload() const { return false; }

    size_t _mem_cap_threshold;
};

} // namespace starrocks::starcache
