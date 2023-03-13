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

#include "disk_cache.h"
#include "mem_cache.h"

namespace starrocks::starcache {

struct CacheOptions {
    // Cache Space (Required)
    uint64_t mem_quota_bytes;
    std::vector<DirSpace> disk_dir_spaces;

    // Policy (Optional)
    /*
    EvictPolicy mem_evict_policy;
    EvictPolicy disk_evict_policy;
    AdmissionCtrlPolicy admission_ctrl_policy;
    PromotionPolicy promotion_policy;
    */

    // Other (Optional)
    bool checksum = config::FLAGS_enable_disk_checksum;
    size_t block_size = config::FLAGS_block_size;
};

class AccessIndex;
class AdmissionPolicy;
class PromotionPolicy;

class StarCache {
public:
    StarCache();
    ~StarCache();

    // Init the starcache instance with given options
    Status init(const CacheOptions& options);

    // Set the whole cache object.
    // If the `cache_key` exists, replace the cache data.
    // If the `ttl_seconds` is 0 (default), no ttl restriction will be set.
    Status set(const std::string& cache_key, const IOBuf& buf, uint64_t ttl_seconds = 0);

    // Get the whole cache object.
    // If no such object, return ENOENT error
    Status get(const std::string& cache_key, IOBuf* buf);

    // Read the partial cache object with given range.
    // If the range exceeds the object size, only read the range within the object size.
    // Only if all the data in the valid range exists in the cache will it return success, otherwise will return ENOENT.
    Status read(const std::string& cache_key, off_t offset, size_t size, IOBuf* buf);

    // Remove the cache object.
    // If no such object, return ENOENT error
    Status remove(const std::string& cache_key);

    // Set the cache object ttl.
    // If the `ttl_seconds` is 0, the origin ttl restriction will be removed.
    Status set_ttl(const std::string& cache_key, uint64_t ttl_seconds) { return Status::OK(); }

    // Pin the cache object in cache.  The cache object will not be evicted by eviction policy.
    // The object still can be removed in the cases:
    // 1. calling remove api
    // 2. the ttl is timeout
    Status pin(const std::string& cache_key) { return Status::OK(); }

    // UnPin the cache object in cache.
    // If the object is not exist or has been removed, return ENOENT error.
    Status unpin(const std::string& cache_key) { return Status::OK(); }

private:
    static size_t _continuous_segments_size(const std::vector<BlockSegmentPtr>& segments);

    Status _read_cache_item(const CacheId& cache_id, CacheItemPtr cache_item, off_t offset, size_t size, IOBuf* buf);
    void _remove_cache_item(const CacheId& cache_id, CacheItemPtr cache_item);

    Status _write_block(CacheItemPtr cache_item, const BlockKey& block_key, const IOBuf& buf);
    Status _read_block(CacheItemPtr cache_item, const BlockKey& block_key, off_t offset, size_t size, IOBuf* buf);

    Status _flush_block(CacheItemPtr cache_item, const BlockKey& block_key);
    Status _flush_block_segments(CacheItemPtr cache_item, const BlockKey& block_key,
                                 const std::vector<BlockSegmentPtr>& segments);
    void _promote_block_segments(CacheItemPtr cache_item, const BlockKey& block_key,
                                 const std::vector<BlockSegment>& segments);

    void _evict_mem_block(size_t size);
    void _evict_for_mem_block(const BlockKey& block_key, size_t size);
    void _evict_for_disk_block(const CacheId& cache_id, size_t size);
    void _process_evicted_mem_blocks(const std::vector<BlockKey>& evicted);
    void _process_evicted_disk_items(const std::vector<CacheId>& evicted);

    CacheItemPtr _alloc_cache_item(const std::string& cache_key, size_t size, uint64_t expire_time);
    BlockSegmentPtr _alloc_block_segment(const BlockKey& block_key, off_t offset, uint32_t size, const IOBuf& buf);
    DiskBlockPtr _alloc_disk_block(const BlockKey& block_key);

    std::unique_ptr<MemCache> _mem_cache = nullptr;
    std::unique_ptr<DiskCache> _disk_cache = nullptr;
    AccessIndex* _access_index = nullptr;
    AdmissionPolicy* _admission_policy = nullptr;
    PromotionPolicy* _promotion_policy = nullptr;
};

} // namespace starrocks::starcache
