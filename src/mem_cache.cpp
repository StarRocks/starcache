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

#include "mem_cache.h"

#include <butil/fast_rand.h>
#include <glog/logging.h>

#include "aux_funcs.h"
#include "cache_item.h"
#include "lru_eviction_policy.h"
#include "mem_space_manager.h"

namespace starrocks::starcache {

Status MemCache::init(const MemCacheOptions& options) {
    _space_manager = MemSpaceManager::GetInstance();
    _space_manager->reset();
    _space_manager->add_cache_zone(nullptr, options.mem_quota_bytes);
    _eviction_policy = new LruEvictionPolicy<BlockKey>();
    return Status::OK();
}

Status MemCache::write_block(const BlockKey& key, MemBlockPtr block,
                             const std::vector<BlockSegmentPtr>& segments) const {
    for (auto& seg : segments) {
        if (seg->buf.size() == 0) {
            continue;
        }
        int start_slice_index = off2slice(seg->offset);
        int end_slice_index = off2slice(seg->offset + seg->size - 1);
        STAR_VLOG << "set block segment, block_key: " << key << ", segments count: " << segments.size()
                  << ", seg_off: " << seg->offset << ", seg_size: " << seg->size
                  << ", start_slice_index: " << start_slice_index << ", end_slice_index: " << end_slice_index
                  << ", mem_block: " << block;
        set_block_segment(block, start_slice_index, end_slice_index, seg);
    }
    return Status::OK();
}

Status MemCache::read_block(const BlockKey& key, MemBlockPtr block, off_t offset, size_t size,
                            std::vector<BlockSegment>* segments) const {
    if (!block || size == 0) {
        return Status::OK();
    }
    // Use a local scope to release the handle early
    {
        auto handle = evict_touch(key, false);
    }

    int start_slice_index = off2slice(offset);
    int end_slice_index = off2slice(offset + size - 1);

    auto cur_segment = block->slices[start_slice_index];
    off_t segment_off = offset;
    for (int index = start_slice_index + 1; index <= end_slice_index; ++index) {
        if (block->slices[index] == cur_segment) {
            continue;
        }

        size_t length = slice_lower(index) - segment_off;
        if (cur_segment) {
            IOBuf buf;
            uint32_t pos = segment_off - cur_segment->offset;
            cur_segment->buf.append_to(&buf, length, pos);
            BlockSegment seg(segment_off, buf.size(), buf);
            segments->emplace_back(std::move(seg));
        }
        size -= length;
        cur_segment = block->slices[index];
        segment_off = slice_lower(index);
    }

    if (cur_segment) {
        IOBuf buf;
        uint32_t pos = segment_off - cur_segment->offset;
        cur_segment->buf.append_to(&buf, size, pos);
        BlockSegment seg(segment_off, buf.size(), buf);
        segments->emplace_back(std::move(seg));
    }
    return Status::OK();
}

CacheItemPtr MemCache::new_cache_item(const CacheKey& cache_key, size_t size, uint64_t expire_time, bool urgent) const {
    // We don't plus the disk block item and mem block item size here, as they may change during
    // the flushing and promotion.
    size_t block_count = size > 0 ? (size - 1) / config::FLAGS_block_size + 1 : 0;
    size_t item_size = sizeof(CacheItem) + cache_key.size() + sizeof(BlockItem) * block_count;
    if (!_space_manager->inc_mem(item_size, urgent)) {
        return nullptr;
    }

    CacheItemPtr cache_item(new CacheItem(cache_key, size, expire_time), [item_size, this](CacheItem* cache_item) {
        delete cache_item;
        _space_manager->dec_mem(item_size);
    });
    return cache_item;
}

MemBlockPtr MemCache::new_block_item(const BlockKey& key, BlockState state, bool urgent) const {
    // TODO: Allocate the block item strcture by `MemSpaceManager`
    MemBlockPtr block_item(new MemBlockItem(state));
    return block_item;
}

BlockSegmentPtr MemCache::new_block_segment(off_t offset, uint32_t size, const IOBuf& buf, bool urgent) const {
    size_t segment_size = sizeof(BlockSegment) + buf.size();
    if (!_space_manager->inc_mem(segment_size, urgent)) {
        return nullptr;
    }

    BlockSegmentPtr segment(new BlockSegment(offset, size, buf), [segment_size, this](BlockSegment* segment) {
        delete segment;
        _space_manager->dec_mem(segment_size);
    });
    return segment;
}

void MemCache::set_block_segment(MemBlockPtr block, int start_slice_index, int end_slice_index,
                                 BlockSegmentPtr segment) const {
    for (int index = start_slice_index; index <= end_slice_index; ++index) {
        block->slices[index] = segment;
    }
}

void MemCache::evict_track(const BlockKey& key, size_t size) const {
    _eviction_policy->add(key, size);
}

void MemCache::evict_untrack(const BlockKey& key) const {
    _eviction_policy->remove(key);
}

EvictionPolicy<BlockKey>::HandlePtr MemCache::evict_touch(const BlockKey& key, bool force) const {
    if (force || butil::fast_rand_less_than(100) < config::FLAGS_evict_touch_mem_probalility) {
        return _eviction_policy->touch(key);
    }
    return nullptr;
}

void MemCache::evict(size_t size, std::vector<BlockKey>* evicted) const {
    _eviction_policy->evict(size, evicted);
}

void MemCache::evict_for(const BlockKey& key, size_t size, std::vector<BlockKey>* evicted) const {
    _eviction_policy->evict_for(key, size, evicted);
}

size_t MemCache::quota_bytes() const {
    return _space_manager->quota_bytes();
}

size_t MemCache::used_bytes() const {
    return _space_manager->used_bytes();
}

} // namespace starrocks::starcache
