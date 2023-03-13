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

#include <shared_mutex>

#include "aux_funcs.h"
#include "common/macros.h"

namespace starrocks::starcache {

enum class BlockState : uint8_t { CLEAN, DIRTY, EVICTED, REMOVED };

struct BlockSegment {
    uint32_t offset;
    uint32_t size;
    IOBuf buf;

    BlockSegment() : offset(0), size(0) {}
    BlockSegment(uint32_t offset_, uint32_t size_) : offset(offset_), size(size_) {}
    BlockSegment(uint32_t offset_, uint32_t size_, const IOBuf& buf_) : offset(offset_), size(size_), buf(buf_) {}
};

using BlockSegmentPtr = std::shared_ptr<BlockSegment>;

struct MemBlockItem {
    BlockSegmentPtr* slices;
    BlockState state;

    explicit MemBlockItem(BlockState state_) : state(state_) {
        const size_t slice_count = block_slice_count();
        slices = new BlockSegmentPtr[slice_count];
    }
    ~MemBlockItem() { delete[] slices; }

    void list_segments(std::vector<BlockSegmentPtr>* segments) { list_segments(0, block_slice_count() - 1, segments); }

    void list_segments(size_t start_slice, size_t end_slice, std::vector<BlockSegmentPtr>* segments) {
        for (size_t i = start_slice; i <= end_slice; ++i) {
            if (slices[i] && (segments->empty() || slices[i] != segments->back())) {
                segments->push_back(slices[i]);
            }
        }
    }
};

struct DiskBlockItem {
    uint8_t dir_index;
    uint32_t block_index;
    uint32_t* checksums;

    DiskBlockItem(uint8_t dir_index_, uint32_t block_index_) : dir_index(dir_index_), block_index(block_index_) {
        checksums = new uint32_t[block_slice_count()];
    }
    ~DiskBlockItem() { delete[] checksums; }
};

using MemBlockPtr = std::shared_ptr<MemBlockItem>;
using DiskBlockPtr = std::shared_ptr<DiskBlockItem>;

struct BlockItem {
    MemBlockPtr mem_block_item = nullptr;
    DiskBlockPtr disk_block_item = nullptr;

    void set_mem_block(MemBlockPtr mem_block, std::unique_lock<std::shared_mutex>* lck = nullptr) {
        LOCK_IF(lck, lck != nullptr);
        mem_block_item = mem_block;
        UNLOCK_IF(lck, lck != nullptr);
    }

    void set_disk_block(DiskBlockPtr disk_block, std::unique_lock<std::shared_mutex>* lck = nullptr) {
        LOCK_IF(lck, lck != nullptr);
        disk_block_item = disk_block;
        UNLOCK_IF(lck, lck != nullptr);
    }

    void clear_block(std::unique_lock<std::shared_mutex>* lck = nullptr) {
        LOCK_IF(lck, lck != nullptr);
        mem_block_item = nullptr;
        disk_block_item = nullptr;
        UNLOCK_IF(lck, lck != nullptr);
    }

    MemBlockPtr mem_block(std::shared_lock<std::shared_mutex>* lck = nullptr) {
        LOCK_IF(lck, lck != nullptr);
        auto mem_block = mem_block_item;
        UNLOCK_IF(lck, lck != nullptr);
        return mem_block;
    }

    DiskBlockPtr disk_block(std::shared_lock<std::shared_mutex>* lck = nullptr) {
        LOCK_IF(lck, lck != nullptr);
        auto disk_block = disk_block_item;
        UNLOCK_IF(lck, lck != nullptr);
        return disk_block;
    }
};

} // namespace starrocks::starcache
