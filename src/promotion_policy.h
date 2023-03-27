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

#include "cache_item.h"

namespace starrocks::starcache {

enum class BlockLocation : uint8_t { NONE, MEM, DISK };

inline std::ostream& operator<<(std::ostream& os, const BlockLocation& loc) {
    switch (loc) {
    case BlockLocation::MEM:
        os << "memory";
        break;
    case BlockLocation::DISK:
        os << "disk";
        break;
    default:
        os << "none";
    }
    return os;
}

class BlockKey;
// The class to decide the block location
class PromotionPolicy {
public:
    virtual ~PromotionPolicy() = default;

    // Check the location to write the given block first time
    virtual BlockLocation check_write(const CacheItemPtr& cache_item, const BlockKey& block_key,
                                      const WriteOptions* options) = 0;

    // Check whether to promote the given block
    virtual bool check_promote(const CacheItemPtr& cache_item, const BlockKey& block_key,
                               const ReadOptions* options) = 0;
};

} // namespace starrocks::starcache
