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

#include "star_cache_impl.h"

#include <butil/time.h>
#include <glog/logging.h>

#include <filesystem>

#include "aux_funcs.h"
#include "capacity_based_promotion_policy.h"
#include "hashtable_access_index.h"
#include "mem_space_manager.h"
#include "sharded_lock_manager.h"
#include "size_based_admission_policy.h"

namespace starrocks::starcache {

StarCacheImpl::StarCacheImpl() {
    _mem_cache = std::make_unique<MemCache>();
    _disk_cache = std::make_unique<DiskCache>();
}

StarCacheImpl::~StarCacheImpl() {
    delete _access_index;
    delete _admission_policy;
    delete _promotion_policy;
}

Status StarCacheImpl::init(const CacheOptions& options) {
    _options = options;
    MemCacheOptions mc_options;
    mc_options.mem_quota_bytes = options.mem_quota_bytes;
    RETURN_IF_ERROR(_mem_cache->init(mc_options));

    DiskCacheOptions dc_options;
    dc_options.disk_dir_spaces = options.disk_dir_spaces;
    RETURN_IF_ERROR(_disk_cache->init(dc_options));

    _access_index = new HashTableAccessIndex;

    SizeBasedAdmissionPolicy::Config ac_config;
    ac_config.max_check_size = config::FLAGS_admission_max_check_size;
    ac_config.flush_probability = config::FLAGS_admission_flush_probability;
    ac_config.delete_probability = config::FLAGS_admission_delete_probability;
    _admission_policy = new SizeBasedAdmissionPolicy(ac_config);

    CapacityBasedPromotionPolicy::Config pm_config;
    pm_config.mem_cap_threshold = config::FLAGS_promotion_mem_threshold;
    _promotion_policy = new CapacityBasedPromotionPolicy(pm_config);

    LOG(INFO) << "init starcache success. block_size: " << config::FLAGS_block_size
              << ", disk checksum: " << config::FLAGS_enable_disk_checksum
              << ", mem_quota: " << _mem_cache->quota_bytes() << ", disk_quota: " << _disk_cache->quota_bytes();
    return Status::OK();
}

const CacheOptions* StarCacheImpl::options() {
    return &_options;
}

Status StarCacheImpl::set(const CacheKey& cache_key, const IOBuf& buf, WriteOptions* options) {
    STAR_VLOG << "set cache, cache_key: " << cache_key << ", buf size: " << buf.size();
    if (options) {
        STAR_VLOG << " options: " << *options;
    }
    if (buf.empty()) {
        return Status(E_INTERNAL, "cache value should not be empty");
    }

    if (UNLIKELY(_concurrent_writes + 1 > config::FLAGS_max_concurrent_writes)) {
        LOG(WARNING) << "the concurrent write size exceeds the maximum threshold, reject it";
        return Status(EBUSY, "the cache system is busy now");
    }
    ++_concurrent_writes;

    CacheId cache_id = cachekey2id(cache_key);
    CacheItemPtr cache_item = _access_index->find(cache_id);
    Status st;
    if (cache_item) {
        if (!options || options->overrite) {
            // TODO: Replace the target data directly.
            STAR_VLOG << "remove old cache for update, cache_key: " << cache_key;
            _remove_cache_item(cache_id, cache_item);
        } else {
            STAR_VLOG << "the cache object already exists, skipped, cache_key: " << cache_key;
            st.set_error(EEXIST, "the cache object already exists") ;
        }
    }
    if (st.ok()) {
        st = _write_cache_item(cache_id, cache_key, buf, options);
    }
    --_concurrent_writes;
    STAR_VLOG << "set cache finish, cache_key: " << cache_key << ", status: " << st.error_str();
    return st;
}

Status StarCacheImpl::_write_cache_item(const CacheId& cache_id, const CacheKey& cache_key, const IOBuf& buf,
                                        WriteOptions* options) {
    size_t block_size = config::FLAGS_block_size;
    size_t block_count = (buf.size() - 1) / block_size + 1;
    uint64_t expire_time = butil::monotonic_time_s() + (options ? options->ttl_seconds : 0);
    CacheItemPtr cache_item = _alloc_cache_item(cache_key, buf.size(), expire_time);
    if (options && options->pinned) {
        cache_item->set_pinned(true);
    }

    uint32_t start_block_index = 0;
    uint32_t end_block_index = (buf.size() - 1) / block_size;
    for (uint32_t index = start_block_index; index <= end_block_index; ++index) {
        BlockKey block_key = {.cache_id = cache_id, .block_index = index};
        off_t offset = index * block_size;
        IOBuf seg_buf;
        if (block_count > 1) {
            buf.append_to(&seg_buf, block_size, offset);
        } else {
            seg_buf = buf;
        }
        RETURN_IF_ERROR(_write_block(cache_item, block_key, seg_buf, options));
    }

    _access_index->insert(cache_id, cache_item);
    return Status::OK();
}

Status StarCacheImpl::_write_block(CacheItemPtr cache_item, const BlockKey& block_key, const IOBuf& buf,
                                   WriteOptions* options) {
    BlockItem& block = cache_item->blocks[block_key.block_index];
    auto wlck = block_unique_lock(block_key);
    auto location = _promotion_policy->check_write(cache_item, block_key, options);

    STAR_VLOG << "write block to " << location << ", cache_key: " << cache_item->cache_key
              << ", block_key: " << block_key << ", buf size: " << buf.size();
    if (location == BlockLocation::MEM) {
        // Copy the buffer content to avoid the buffer to be released by users
        void* data = nullptr;
        size_t size = buf.size();
        if (!config::FLAGS_enable_os_page_cache) {
            // We allocate an aligned buffer here to avoid repeatedlly copying data to a new aligned buffer
            // when flush to disk file in `O_DIRECT` mode.
            size = align_iobuf(buf, &data);
            if (size == 0) {
                LOG(ERROR) << "align io buffer failed when write block";
                return Status(ENOMEM, "align io buffer failed");
            }
        } else {
            data = malloc(buf.size());
            buf.copy_to(data);
        }
        IOBuf sbuf;
        sbuf.append_user_data(data, size, nullptr);

        // allocate segment and evict
        auto segment = _alloc_block_segment(block_key, 0, buf.size(), sbuf);
        if (!segment) {
            return Status(E_INTERNAL, "allocate segment failed");
        }
        auto mem_block = _mem_cache->new_block_item(block_key, BlockState::DIRTY, true);
        RETURN_IF_ERROR(_mem_cache->write_block(block_key, mem_block, {segment}));
        block.set_mem_block(mem_block, &wlck);
        if (!options || !options->pinned || has_disk_layer()) {
            _mem_cache->evict_track(block_key, cache_item->block_size(block_key.block_index));
        }
    } else if (location == BlockLocation::DISK) {
        // allocate block and evict
        auto disk_block = _alloc_disk_block(block_key);
        if (!disk_block) {
            return Status(E_INTERNAL, "allocate disk block failed");
        }
        BlockSegment segment(0, buf.size(), buf);
        RETURN_IF_ERROR(_disk_cache->write_block(block_key.cache_id, disk_block, segment));

        block.set_disk_block(disk_block, &wlck);
        if (!options || !options->pinned) {
            _disk_cache->evict_track(block_key.cache_id, cache_item->size);
        }

    } else {
        LOG(ERROR) << "write block is rejected for overload, cache_key: " << cache_item->cache_key
                   << ", block_key: " << block_key;
        return Status(E_INTERNAL, "write block is rejected for overload");
    }

    return Status::OK();
}

Status StarCacheImpl::get(const CacheKey& cache_key, IOBuf* buf, ReadOptions* options) {
    STAR_VLOG << "get cache, cache_key: " << cache_key;
    if (options) {
        STAR_VLOG << " options: " << *options;
    }
    auto cache_id = cachekey2id(cache_key);
    auto cache_item = _access_index->find(cache_id);
    if (!cache_item || cache_item->cache_key != cache_key || cache_item->is_released()) {
        return Status(ENOENT, "The target not found");
    }
    Status st = _read_cache_item(cache_id, cache_item, 0, cache_item->size, buf, options);
    STAR_VLOG << "get cache finish, cache_key: " << cache_key << ", buf_size: " << buf->size()
              << ", status: " << st.error_str();
    return st;
}

Status StarCacheImpl::read(const CacheKey& cache_key, off_t offset, size_t size, IOBuf* buf,
                           ReadOptions* options) {
    STAR_VLOG << "read cache, cache_key: " << cache_key << ", offset: " << offset << ", size: " << size;
    if (options) {
        STAR_VLOG << " options: " << *options;
    }
    auto cache_id = cachekey2id(cache_key);
    auto cache_item = _access_index->find(cache_id);
    if (!cache_item || cache_item->cache_key != cache_key || cache_item->is_released()) {
        return Status(ENOENT, "The target not found");
    }
    Status st =  _read_cache_item(cache_id, cache_item, offset, size, buf, options);
    STAR_VLOG << "read cache finish, cache_key: " << cache_key << ", offset:" << offset << ", size: " << size
              << ", buf_size: " << buf->size() << ", status: " << st.error_str();
    return st;
}

Status StarCacheImpl::_read_cache_item(const CacheId& cache_id, CacheItemPtr cache_item, off_t offset, size_t size,
                                       IOBuf* buf, ReadOptions* options) {
    buf->clear();
    if (offset + size > cache_item->size) {
        size = offset >= cache_item->size ? 0 : cache_item->size - offset;
    }
    if (size == 0) {
        return Status::OK();
    }

    // Align read range by slices
    // TODO: We have no need to align read range if for segments from mem cache,
    // but as the operation is zero copy, so it seems doesn't matter.
    off_t lower = slice_lower(off2slice(offset));
    off_t upper = slice_upper(off2slice(offset + size - 1));
    if (upper >= cache_item->size) {
        upper = cache_item->size - 1;
    }
    size_t aligned_size = upper - lower + 1;
    int start_block_index = off2block(lower);
    int end_block_index = off2block(upper);

    for (uint32_t i = start_block_index; i <= end_block_index; ++i) {
        IOBuf block_buf;
        off_t off_in_block = i == start_block_index ? lower - block_lower(i) : 0;
        size_t to_read = i == end_block_index ? upper - (block_lower(i) + off_in_block) + 1
                                              : config::FLAGS_block_size - off_in_block;
        RETURN_IF_ERROR(_read_block(cache_item, {cache_id, i}, off_in_block, to_read, &block_buf, options));
        aligned_size -= to_read;
        buf->append(block_buf);
    }

    buf->pop_front(offset - lower);
    if (buf->size() > size) {
        buf->pop_back(buf->size() - size);
    }
    return Status::OK();
}

Status StarCacheImpl::_read_block(CacheItemPtr cache_item, const BlockKey& block_key, off_t offset, size_t size,
                                  IOBuf* buf, ReadOptions* options) {
    BlockItem& block = cache_item->blocks[block_key.block_index];
    std::vector<BlockSegment> segments;
    std::vector<BlockSegment> disk_segments;
    auto rlck = block_shared_lock(block_key);
    rlck.lock();
    auto mem_block = block.mem_block();
    auto disk_block = block.disk_block();
    _mem_cache->read_block(block_key, mem_block, offset, size, &segments);
    off_t cursor = offset;

    for (auto& seg : segments) {
        if (seg.offset > cursor) {
            BlockSegment bs(cursor, seg.offset - cursor);
            RETURN_IF_ERROR(_disk_cache->read_block(block_key.cache_id, disk_block, &bs));
            bs.buf.append_to(buf, bs.size, 0);
            disk_segments.emplace_back(std::move(bs));
        }
        buf->append(seg.buf);
        cursor = seg.offset + seg.buf.size();
    }
    if (disk_block && buf->size() < size) {
        IOBuf block_buf;
        BlockSegment bs(cursor, size - buf->size());
        RETURN_IF_ERROR(_disk_cache->read_block(block_key.cache_id, disk_block, &bs));
        bs.buf.append_to(buf, bs.size, 0);
        disk_segments.emplace_back(std::move(bs));
    }
    rlck.unlock();

    if (buf->size() < size) {
        STAR_VLOG << "can not read full block from cache, cache_key: " << cache_item->cache_key
                  << ", expect size: " << size << ", real size: " << buf->size();
        return Status(ENOENT, "can not read full block from cache");
    }

    // TODO: The follow procedure can be done asynchronously
    if (!disk_segments.empty() && _promotion_policy->check_promote(cache_item, block_key, options)) {
        _promote_block_segments(cache_item, block_key, disk_segments);
    }
    return Status::OK();
}

Status StarCacheImpl::remove(const CacheKey& cache_key) {
    STAR_VLOG << "remove cache, cache_key: " << cache_key;
    auto cache_id = cachekey2id(cache_key);
    auto cache_item = _access_index->find(cache_id);
    if (!cache_item || cache_item->cache_key != cache_key) {
        return Status(ENOENT, "The target not found");
    }
    if (!cache_item->set_released(true)) {
        // The cache item has been released, return ok.
        return Status::OK();
    }
    _remove_cache_item(cache_id, cache_item);
    STAR_VLOG << "remove cache success, cache_key: " << cache_key;
    return Status::OK();
}

void StarCacheImpl::_remove_cache_item(const CacheId& cache_id, CacheItemPtr cache_item) {
    _access_index->remove(cache_id);
    for (uint32_t i = 0; i < cache_item->block_count(); ++i) {
        BlockKey block_key = {cache_id, i};
        auto& block = cache_item->blocks[i];
        auto wlck = block_unique_lock(block_key);
        wlck.lock();
        block.clear_block();
        // If the lru handle is hold by other threads, this untrack operation will not
        // remove it from lru list, and it will be put back to lru list once the handle is released
        // by the owners.
        // So, we still check the block key validity (especially the block index) during later eviction.
        _mem_cache->evict_untrack(block_key);
        wlck.unlock();
    }
    _disk_cache->evict_untrack(cache_id);
}

Status StarCacheImpl::pin(const CacheKey& cache_key) {
    STAR_VLOG << "pin cache, cache_key: " << cache_key;
    auto cache_id = cachekey2id(cache_key);
    auto cache_item = _access_index->find(cache_id);
    if (!cache_item || cache_item->cache_key != cache_key || cache_item->is_released()) {
        return Status(ENOENT, "The target not found");
    }
    Status st = _pin_cache_item(cache_id, cache_item);
    STAR_VLOG << "pin cache finish, cache_key: " << cache_key << ", status: " << st.error_str();
    return st;
}

Status StarCacheImpl::_pin_cache_item(const CacheId& cache_id, CacheItemPtr cache_item) {
    // The object has been pinned before
    if (!cache_item->set_pinned(true)) {
        return Status::OK();
    }

    if (has_disk_layer()) {
        _disk_cache->evict_untrack(cache_id);
        return Status::OK();
    }

    for (uint32_t i = 0; i < cache_item->block_count(); ++i) {
        BlockKey block_key = {cache_id, i};
        _mem_cache->evict_untrack(block_key);
    }
    return Status::OK();
}

Status StarCacheImpl::unpin(const CacheKey& cache_key) {
    STAR_VLOG << "unpin cache, cache_key: " << cache_key;
    auto cache_id = cachekey2id(cache_key);
    auto cache_item = _access_index->find(cache_id);
    if (!cache_item || cache_item->cache_key != cache_key || cache_item->is_released()) {
        return Status(ENOENT, "The target not found");
    }
    Status st = _unpin_cache_item(cache_id, cache_item);
    STAR_VLOG << "unpin cache finish, cache_key: " << cache_key << ", status: " << st.error_str();
    return st;
}

Status StarCacheImpl::_unpin_cache_item(const CacheId& cache_id, CacheItemPtr cache_item) {
    // The object is not in pinned state
    if (!cache_item->set_pinned(false)) {
        return Status::OK();
    }

    if (has_disk_layer()) {
        for (uint32_t i = 0; i < cache_item->block_count(); ++i) {
            BlockKey block_key = {cache_id, i};
            auto rlck = block_shared_lock(block_key);
            if (cache_item->blocks[i].disk_block(&rlck)) {
                _disk_cache->evict_track(cache_id, cache_item->size);
                break;
            }
        }
    } else {
        for (uint32_t i = 0; i < cache_item->block_count(); ++i) {
            BlockKey block_key = {cache_id, i};
            auto rlck = block_shared_lock(block_key);
            if (cache_item->blocks[i].mem_block(&rlck)) {
                _mem_cache->evict_track(block_key, cache_item->block_size(block_key.block_index));
            }
        }
    }
    return Status::OK();
}

Status StarCacheImpl::_flush_block(CacheItemPtr cache_item, const BlockKey& block_key) {
    if (cache_item->is_released()) {
        LOG(INFO) << "the block to flush is released, key: " << block_key;
        return Status::OK();
    }
    // Hold the handle, in case that the disk cache item be evicted during the flush process
    auto handle = _disk_cache->evict_touch(block_key.cache_id, true);

    auto& block = cache_item->blocks[block_key.block_index];
    auto mem_block = block.mem_block_item;
    if (!mem_block) {
        return Status::OK();
    }

    BlockAdmission admission = BlockAdmission::FLUSH;
    bool pinned = cache_item->is_pinned();
    if (pinned) {
        admission = has_disk_layer() ? BlockAdmission::FLUSH : BlockAdmission::SKIP;
    } else if (!has_disk_layer()) {
        admission = BlockAdmission::DELETE;
    } else {
        admission = _admission_policy->check_admission(cache_item, block_key);
    }

    STAR_VLOG << "try to flush block, cache_key: " << cache_item->cache_key << ", block_key" << block_key
              << ", admisson policy: " << admission;
    do {
        if (admission == BlockAdmission::FLUSH) {
            std::vector<BlockSegmentPtr> segments;
            mem_block->list_segments(&segments);
            if (!block.disk_block_item &&
                _continuous_segments_size(segments) != cache_item->block_size(block_key.block_index)) {
                break;
            }
            Status st = _flush_block_segments(cache_item, block_key, segments);
            if (st.ok() && !handle && !pinned) {
                _disk_cache->evict_track(block_key.cache_id, cache_item->size);
            }
            return st;
        } else if (admission == BlockAdmission::SKIP) {
            if (!pinned) {
                _mem_cache->evict_track(block_key, cache_item->block_size(block_key.block_index));
            }
            return Status::OK();
        }
    } while (false);

    // The following cases:
    // 1. admission == BlockAdmission::DELETE
    // 2. The partial segments are now allowed to be flushed to a new disk block, delete it directly
    auto wlck = block_unique_lock(block_key);
    block.clear_block(&wlck);
    if (cache_item->release_if_empty()) {
        _remove_cache_item(block_key.cache_id, cache_item);
    }
    return Status::OK();
}

size_t StarCacheImpl::_continuous_segments_size(const std::vector<BlockSegmentPtr>& segments) {
    size_t size = 0;
    for (auto& seg : segments) {
        if (seg->offset != size) {
            break;
        }
        size += seg->size;
    }
    return size;
}

Status StarCacheImpl::_flush_block_segments(CacheItemPtr cache_item, const BlockKey& block_key,
                                        const std::vector<BlockSegmentPtr>& segments) {
    auto cache_id = block_key.cache_id;
    auto& block = cache_item->blocks[block_key.block_index];
    auto rlck = block_shared_lock(block_key);
    auto wlck = block_unique_lock(block_key);
    Status st;

    bool new_block = false;
    do {
        auto disk_block = block.disk_block(&rlck);
        if (!disk_block) {
            DCHECK_EQ(_continuous_segments_size(segments), cache_item->block_size(block_key.block_index))
                    << "unmatched segment size. cache_key: " << cache_item->cache_key << ", block_key: " << block_key
                    << ", segment_size: " << _continuous_segments_size(segments)
                    << ", block_size: " << cache_item->block_size(block_key.block_index);
            disk_block = _alloc_disk_block(block_key);
            if (!disk_block) {
                st.set_error(E_INTERNAL, "allocate disk block failed");
                break;
            }
            new_block = true;
        }

        // For new disk block instance, we have no need to write block under lock
        // because the new disk block is invisible for other threads before `set_disk_block`.
        LOCK_IF(&wlck, !new_block);
        for (auto seg : segments) {
            st = _disk_cache->write_block(cache_id, disk_block, *seg);
            if (!st.ok()) {
                LOG(WARNING) << "flush block failed: " << st.error_str();
                break;
            }
        }
        if (!st.ok()) {
            UNLOCK_IF(&wlck, !new_block);
            break;
        }
        if (new_block) {
            block.set_disk_block(disk_block, &wlck);
        } else {
            block.set_disk_block(disk_block);
            wlck.unlock();
        }
    } while (false);

    block.set_mem_block(nullptr, &wlck);
    return st;
}

void StarCacheImpl::_promote_block_segments(CacheItemPtr cache_item, const BlockKey& block_key,
                                        const std::vector<BlockSegment>& segments) {
    STAR_VLOG << "promote block segment, cache_key: " << cache_item->cache_key << ", block_key: " << block_key
              << ", segments count: " << segments.size() << ", seg.off: " << segments[0].offset
              << ", seg.size: " << segments[0].size;
    auto handle = _mem_cache->evict_touch(block_key, true);
    auto& block = cache_item->blocks[block_key.block_index];
    std::vector<BlockSegmentPtr> mem_segments;
    mem_segments.reserve(segments.size());
    for (const auto& seg : segments) {
        auto segment = _alloc_block_segment(block_key, seg.offset, seg.size, seg.buf);
        if (segment) {
            mem_segments.push_back(segment);
        }
    }
    if (mem_segments.empty()) {
        LOG(WARNING) << "skip promoting segments as allocate failed, block: " << block_key;
        return;
    }

    auto wlck = block_unique_lock(block_key);
    wlck.lock();
    auto mem_block = block.mem_block();
    // For new mem block instance, we have no need to write block under lock
    // because the new mem block is invisible for other threads before `set_mem_block`.
    Status st;
    if (!mem_block) {
        wlck.unlock();
        mem_block = _mem_cache->new_block_item(block_key, BlockState::CLEAN, false);
        DCHECK(mem_block);
        st = _mem_cache->write_block(block_key, mem_block, mem_segments);
        if (st.ok()) {
            block.set_mem_block(mem_block, &wlck);
        }
    } else {
        st = _mem_cache->write_block(block_key, mem_block, mem_segments);
        wlck.unlock();
    }
    if (!st.ok()) {
        LOG(ERROR) << "fail to write memery block, cache_key: " << cache_item->cache_key << ", block_key: " << block_key
                   << ", segments count: " << segments.size();
        return;
    }
    if (!handle) {
        _mem_cache->evict_track(block_key, cache_item->block_size(block_key.block_index));
    }
}

void StarCacheImpl::_evict_mem_block(size_t size) {
    std::vector<BlockKey> evicted;
    _mem_cache->evict(size * config::FLAGS_mem_evict_times, &evicted);
    if (evicted.empty()) {
        LOG(WARNING) << "no evicted for memory space"
                     << ", quota: " << _mem_cache->quota_bytes() << ", used: " << _mem_cache->used_bytes();
    }
    _process_evicted_mem_blocks(evicted);
}

void StarCacheImpl::_evict_for_mem_block(const BlockKey& block_key, size_t size) {
    std::vector<BlockKey> evicted;
    _mem_cache->evict_for(block_key, size * config::FLAGS_mem_evict_times, &evicted);
    if (evicted.empty()) {
        LOG(WARNING) << "no evicted for memory block: " << block_key << ", quota: " << _mem_cache->quota_bytes()
                     << ", used: " << _mem_cache->used_bytes();
        return;
    }
    _process_evicted_mem_blocks(evicted);
}

void StarCacheImpl::_process_evicted_mem_blocks(const std::vector<BlockKey>& evicted) {
    for (const auto& key : evicted) {
        // TODO: Is it needed to store the cache item to eviction component, to avoid
        // the following `find`?
        auto cache_item = _access_index->find(key.cache_id);
        if (!cache_item) {
            LOG(WARNING) << "block does not exist when evict " << key;
            continue;
        }
        STAR_VLOG << "evict mem block, cache_key: " << cache_item->cache_key << ", block_key: " << key;
        auto wlck = block_unique_lock(key);
        wlck.lock();
        // If the block key is expired and point to an old cache item that has been replaced
        if (key.block_index >= cache_item->block_count()) {
            wlck.unlock();
            continue;
        }
        auto& block = cache_item->blocks[key.block_index];
        auto mem_block = block.mem_block();
        if (!mem_block) {
            wlck.unlock();
            continue;
        }
        if (mem_block->state == BlockState::DIRTY || !block.disk_block()) {
            wlck.unlock();
            _flush_block(cache_item, key);
        } else {
            block.set_mem_block(nullptr);
            wlck.unlock();
        }
    }
}

void StarCacheImpl::_evict_for_disk_block(const CacheId& cache_id, size_t size) {
    std::vector<CacheId> evicted;
    _disk_cache->evict_for(cache_id, size * config::FLAGS_disk_evict_times, &evicted);
    if (evicted.empty()) {
        LOG(WARNING) << "no evicted for disk cache: " << cache_id << ", quota: " << _disk_cache->quota_bytes()
                     << ", used: " << _disk_cache->used_bytes();
        return;
    }
    _process_evicted_disk_items(evicted);
}

void StarCacheImpl::_process_evicted_disk_items(const std::vector<CacheId>& evicted) {
    for (auto& cache_id : evicted) {
        // TODO: Is it needed to store the cache item to eviction component, to avoid
        // the following `find`?
        CacheItemPtr cache_item = _access_index->find(cache_id);
        if (!cache_item) {
            return;
        }
        STAR_VLOG << "evict disk cache, cache_key: " << cache_item->cache_key;
        // We only clear disk blocks here, because the memory blocks may be still hot.
        // As a disk block always holds the whold block data, so we should check
        // the target segments when flushing a memory block to disk, to avoid flushing
        // partial block data to disk.
        cache_item->clear_disk_blocks();
        if (cache_item->release_if_empty()) {
            _remove_cache_item(cache_id, cache_item);
        }
    }
}

CacheItemPtr StarCacheImpl::_alloc_cache_item(const CacheKey& cache_key, size_t size, uint64_t expire_time) {
    // allocate cache item and evict
    CacheItemPtr cache_item(nullptr);
    for (size_t i = 0; i < config::FLAGS_max_retry_when_allocate; ++i) {
        cache_item = _mem_cache->new_cache_item(cache_key, size, expire_time, i != 0);
        if (cache_item) {
            break;
        }
        _evict_mem_block(size);
    }
    LOG_IF(ERROR, !cache_item) << "allocate cache item failed too many times for cache: " << cache_key;
    return cache_item;
}

BlockSegmentPtr StarCacheImpl::_alloc_block_segment(const BlockKey& block_key, off_t offset, uint32_t size,
                                                const IOBuf& buf) {
    // allocate segment and evict
    BlockSegmentPtr segment = nullptr;
    for (size_t i = 0; i < config::FLAGS_max_retry_when_allocate; ++i) {
        segment = _mem_cache->new_block_segment(offset, size, buf, i != 0);
        if (segment) {
            break;
        }
        _evict_for_mem_block(block_key, size);
    }
    LOG_IF(ERROR, !segment) << "allocate segment failed too many times for block: " << block_key;
    return segment;
}

DiskBlockPtr StarCacheImpl::_alloc_disk_block(const BlockKey& block_key) {
    // allocate block and evict
    const CacheId& cache_id = block_key.cache_id;
    DiskBlockPtr disk_block(nullptr);
    for (size_t i = 0; i < config::FLAGS_max_retry_when_allocate; ++i) {
        disk_block = _disk_cache->new_block_item(cache_id);
        if (disk_block) {
            break;
        }
        _evict_for_disk_block(cache_id, config::FLAGS_block_size);
    }
    LOG_IF(ERROR, !disk_block) << "allocate disk block failed too many times for block: " << block_key;
    return disk_block;
}

} // namespace starrocks::starcache
