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

#include "mem_cache.h"
#include "disk_cache.h"

namespace starrocks::starcache {

class AccessIndex;
class AdmissionPolicy;
class PromotionPolicy;

class StarCacheImpl {
public:
    StarCacheImpl();
    ~StarCacheImpl();

    Status init(const CacheOptions& options);

    const CacheOptions* options();

    Status set(const std::string& cache_key, const IOBuf& buf, uint64_t ttl_seconds);

    Status get(const std::string& cache_key, IOBuf* buf);

    Status read(const std::string& cache_key, off_t offset, size_t size, IOBuf* buf);

    Status remove(const std::string& cache_key);

    Status pin(const std::string& cache_key);

    Status unpin(const std::string& cache_key);

    bool has_disk_layer() {
        STATIC_EXCEPT_UT bool has_disk = _options.disk_dir_spaces.size() > 0;
        return has_disk;
    }

private:
    static size_t _continuous_segments_size(const std::vector<BlockSegmentPtr>& segments);

    Status _write_cache_item(const CacheId& cache_id, const CacheKey& cache_key, const IOBuf& buf,
                             uint64_t ttl_seconds);
    Status _read_cache_item(const CacheId& cache_id, CacheItemPtr cache_item, off_t offset, size_t size, IOBuf* buf);
    void _remove_cache_item(const CacheId& cache_id, CacheItemPtr cache_item);
    Status _pin_cache_item(const CacheId& cache_id, CacheItemPtr cache_item);
    Status _unpin_cache_item(const CacheId& cache_id, CacheItemPtr cache_item);

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

    CacheOptions _options;
    std::unique_ptr<MemCache> _mem_cache = nullptr;
    std::unique_ptr<DiskCache> _disk_cache = nullptr;
    AccessIndex* _access_index = nullptr;
    AdmissionPolicy* _admission_policy = nullptr;
    PromotionPolicy* _promotion_policy = nullptr;

    std::atomic<size_t> _concurrent_writes = 0;
};

} // namespace starrocks::starcache
