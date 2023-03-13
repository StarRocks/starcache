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

#include "cache_item.h"
#include "eviction_policy.h"

namespace starrocks::starcache {

struct MemCacheOptions {
    // Cache Space (Required)
    uint64_t mem_quota_bytes;

    // Policy (Optional)
    /*
    EvictPolicy evict_policy;
    */
};

class MemSpaceManager;

class MemCache {
public:
    MemCache() = default;
    ~MemCache() { delete _eviction_policy; }

    Status init(const MemCacheOptions& options);

    // Write the segments to memory block.
    // If old segments in the same position exist, replace them.
    Status write_block(const BlockKey& key, MemBlockPtr block, const std::vector<BlockSegmentPtr>& segments) const;

    // Read the segments of memory block with given range.
    // If old segments in the same position exist, replace them.
    Status read_block(const BlockKey& key, MemBlockPtr block, off_t offset, size_t size,
                      std::vector<BlockSegment>* segments) const;

    // Create a new cache item.
    // If memory space is not enough, return null.
    // The `urgent` indicates whether try to allocate memory space from private area.
    CacheItemPtr new_cache_item(const CacheKey& cache_key, size_t size, uint64_t expire_time, bool urgent) const;

    // Create a new memory block item.
    // The `urgent` indicates whether try to allocate memory space from private area.
    MemBlockPtr new_block_item(const BlockKey& key, BlockState state, bool urgent) const;

    // Create a block segment.
    // If memory space is not enough, return null.
    // The `urgent` indicates whether try to allocate memory space from private area.
    BlockSegmentPtr new_block_segment(off_t offset, uint32_t size, const IOBuf& buf, bool urgent) const;

    // Set the range of block slices to point to the target segment.
    void set_block_segment(MemBlockPtr block, int start_slice_index, int end_slice_index,
                           BlockSegmentPtr segment) const;

    // Track the block in the eviction component.
    void evict_track(const BlockKey& key, size_t size) const;

    // Remove the block from the eviction component.
    void evict_untrack(const BlockKey& key) const;

    // Touch the block in the eviction component.
    // If `force` is false, the touch will be executed according a probability mechanism.
    EvictionPolicy<BlockKey>::HandlePtr evict_touch(const BlockKey& key, bool force) const;

    // Evict blocks from eviction component.
    // The `size` is the expected space size to evict.
    // The `evicted` is used to hold the result block keys that be evicted.
    void evict(size_t size, std::vector<BlockKey>* evicted) const;

    // Evict blocks from eviction component to make room for given `key`, if the underlying component
    // is sharded, it will evict the entries in the same shard with given `key`.
    // The `size` is the expected space size to evict.
    // The `evicted` is used to hold the result block keys that be evicted.
    void evict_for(const BlockKey& key, size_t size, std::vector<BlockKey>* evicted) const;

    // Get the memory cache quota (in bytes)
    size_t quota_bytes() const;

    // Get the memory cache usage (in bytes)
    size_t used_bytes() const;

private:
    MemSpaceManager* _space_manager = nullptr;
    EvictionPolicy<BlockKey>* _eviction_policy = nullptr;
};

} // namespace starrocks::starcache
