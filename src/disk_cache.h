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

#include <butil/iobuf.h>

#include <atomic>

#include "cache_item.h"
#include "eviction_policy.h"

namespace starrocks::starcache {

struct DiskCacheOptions {
    // Cache Space (Required)
    std::vector<DirSpace> disk_dir_spaces;

    // Policy (Optional)
    /*
    EvictPolicy evict_policy;
    */
};

class DiskSpaceManager;

class DiskCache {
public:
    DiskCache() = default;
    ~DiskCache() { delete _eviction_policy; }

    Status init(const DiskCacheOptions& options);

    // Write data to the disk block.
    // The segment contains the block range, and the data to write.
    Status write_block(const CacheId& cache_id, DiskBlockPtr block, const BlockSegment& segment) const;

    // Read data from the disk block.
    // The segment contains target block range, and will hold the data read from disk.
    Status read_block(const CacheId& cache_id, DiskBlockPtr block, BlockSegment* segment) const;

    // Create a new disk block item.
    // If disk space is not enough, return null.
    DiskBlockPtr new_block_item(const CacheId& cache_id) const;

    // Track the cache item in the eviction component.
    void evict_track(const CacheId& id, size_t size) const;

    // Remove the cache item from the eviction component.
    void evict_untrack(const CacheId& id) const;

    // Touch the cache item in the eviction component.
    // If `force` is false, the touch will be executed according a probability mechanism.
    EvictionPolicy<CacheId>::HandlePtr evict_touch(const CacheId& id, bool force) const;

    // Evict cache items from eviction component.
    // The `size` is the expected space size to evict.
    // The `evicted` is used to hold the result cache id that be evicted.
    void evict(size_t size, std::vector<CacheId>* evicted) const;

    // Evict cache items from eviction component to make room for given `id`, if the underlying component
    // is sharded, it will evict the entries in the same shard with given `id`.
    // The `size` is the expected space size to evict.
    // The `evicted` is used to hold the result cache id that be evicted.
    void evict_for(const CacheId& id, size_t count, std::vector<CacheId>* evicted) const;

    // Get the disk cache quota (in bytes)
    size_t quota_bytes() const;

    // Get the disk cache usage (in bytes)
    size_t used_bytes() const;

private:
    void _update_block_checksum(DiskBlockPtr block, const BlockSegment& segment) const;

    bool _check_block_checksum(DiskBlockPtr block, const BlockSegment& segment) const;

    DiskSpaceManager* _space_manager = nullptr;
    EvictionPolicy<CacheId>* _eviction_policy = nullptr;
};

} // namespace starrocks::starcache
