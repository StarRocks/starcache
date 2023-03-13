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

#include "disk_cache.h"

#include <butil/fast_rand.h>
#include <glog/logging.h>

#include <filesystem>

#include "aux_funcs.h"
#include "block_item.h"
#include "disk_space_manager.h"
#include "lru_eviction_policy.h"

namespace starrocks::starcache {

Status DiskCache::init(const DiskCacheOptions& options) {
    _space_manager = DiskSpaceManager::GetInstance();
    _space_manager->reset();
    for (auto& dir : options.disk_dir_spaces) {
        std::filesystem::path local_path(dir.path);
        if (!std::filesystem::exists(local_path)) {
            bool ret = std::filesystem::create_directory(dir.path);
            DCHECK(ret) << "create directory failed! path: " << dir.path;
        }
        RETURN_IF_ERROR(_space_manager->add_cache_dir(dir));
    }

    _eviction_policy = new LruEvictionPolicy<CacheId>;
    return Status::OK();
}

Status DiskCache::write_block(const CacheId& cache_id, DiskBlockPtr block, const BlockSegment& segment) const {
    if (_space_manager->quota_bytes() == 0) {
        return Status(ENOENT, "disk cache is not exist");
    }
    if (UNLIKELY(segment.offset % config::FLAGS_slice_size != 0)) {
        Status st(EINVAL, "offset must be aligned by slice size %lu", config::FLAGS_slice_size);
        return st;
    }

    if (config::FLAGS_enable_disk_checksum) {
        _update_block_checksum(block, segment);
    }
    auto st = _space_manager->write_block({block->dir_index, block->block_index}, segment.offset, segment.buf);
    return st;
}

Status DiskCache::read_block(const CacheId& cache_id, DiskBlockPtr block, BlockSegment* segment) const {
    if (_space_manager->quota_bytes() == 0) {
        return Status(ENOENT, "disk cache is not exist");
    }
    // Use a local scope to release the handle early
    {
        auto handle = evict_touch(cache_id, false);
    }

    if (UNLIKELY(segment->offset % config::FLAGS_slice_size != 0)) {
        return Status(EINVAL, "offset must be aligned by slice size %lu", config::FLAGS_slice_size);
    }

    Status st = _space_manager->read_block({block->dir_index, block->block_index}, segment->offset, segment->size,
                                           &(segment->buf));
    RETURN_IF_ERROR(st);

    if (UNLIKELY(config::FLAGS_enable_disk_checksum && !_check_block_checksum(block, *segment))) {
        return Status(E_INTERNAL, "fail to verify checksum for cache: %lu", cache_id);
    }
    return Status::OK();
}

void DiskCache::_update_block_checksum(DiskBlockPtr block, const BlockSegment& segment) const {
    const uint32_t slice_size = config::FLAGS_slice_size;
    for (off_t off = 0; off < segment.size; off += slice_size) {
        int index = off2slice(segment.offset + off);
        size_t size = std::min(slice_size, static_cast<uint32_t>(segment.size - off));
        IOBuf slice_buf;
        segment.buf.append_to(&slice_buf, size, off);
        block->checksums[index] = crc32_iobuf(slice_buf);
        STAR_VLOG_FULL << "update checksum for block: " << block.get() << ", slice index: " << index
                       << ", checksum: " << block->checksums[index] << ", buf size: " << slice_buf.size()
                       << ", head char: " << slice_buf.to_string()[0] << ", file block index: " << block->block_index;
    }
}

bool DiskCache::_check_block_checksum(DiskBlockPtr block, const BlockSegment& segment) const {
    const uint32_t slice_size = config::FLAGS_slice_size;
    for (off_t off = 0; off < segment.size; off += slice_size) {
        int index = off2slice(segment.offset + off);
        size_t size = std::min(slice_size, static_cast<uint32_t>(segment.size - off));
        IOBuf slice_buf;
        segment.buf.append_to(&slice_buf, size, off);
        if (block->checksums[index] != crc32_iobuf(slice_buf)) {
            LOG(ERROR) << "fail to check checksum for block: " << block.get() << ", slice index: " << index
                       << ", expected: " << block->checksums[index] << ", real: " << crc32_iobuf(slice_buf)
                       << ", buf size: " << slice_buf.size() << ", head char: " << slice_buf.to_string()[0]
                       << ", file block index: " << block->block_index;
            return false;
        }
    }
    return true;
}

DiskBlockPtr DiskCache::new_block_item(const CacheId& cache_id) const {
    BlockId block_id;
    Status st = _space_manager->alloc_block(&block_id);
    if (!st.ok()) {
        STAR_VLOG << "allocate disk block failed, cache_id" << cache_id << ", reason: " << st.error_str();
        return nullptr;
    }
    DiskBlockPtr block_item(new DiskBlockItem(block_id.dir_index, block_id.block_index), [this](DiskBlockItem* block) {
        BlockId block_id = {.dir_index = block->dir_index, .block_index = block->block_index};
        Status st = _space_manager->free_block(block_id);
        LOG_IF(ERROR, !st.ok()) << "free block failed" << st.error_str();
        delete block;
    });
    return block_item;
}

void DiskCache::evict_track(const CacheId& id, size_t size) const {
    _eviction_policy->add(id, size);
}

void DiskCache::evict_untrack(const CacheId& id) const {
    _eviction_policy->remove(id);
}

EvictionPolicy<CacheId>::HandlePtr DiskCache::evict_touch(const CacheId& id, bool force) const {
    if (force || butil::fast_rand_less_than(100) < config::FLAGS_evict_touch_disk_probalility) {
        return _eviction_policy->touch(id);
    }
    return nullptr;
}

void DiskCache::evict(size_t size, std::vector<CacheId>* evicted) const {
    _eviction_policy->evict(size, evicted);
}

void DiskCache::evict_for(const CacheId& id, size_t size, std::vector<CacheId>* evicted) const {
    _eviction_policy->evict_for(id, size, evicted);
}

size_t DiskCache::quota_bytes() const {
    return _space_manager->quota_bytes();
}

size_t DiskCache::used_bytes() const {
    return _space_manager->used_bytes();
}

} // namespace starrocks::starcache
