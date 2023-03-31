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
#include <butil/memory/singleton.h>

#include <atomic>
#include <boost/dynamic_bitset.hpp>
#include <mutex>
#include <shared_mutex>

#include "aux_funcs.h"
#include "block_file.h"

namespace starrocks::starcache {

class CacheDir {
public:
    static const std::string BLOCK_FILE_PREFIX;
    static const size_t BLOCK_COUNT_IN_SPACE;

    using BlockFilePtr = std::shared_ptr<BlockFile>;
    struct BlockSpace {
        uint64_t start_block_index;
        boost::dynamic_bitset<> free_bits;
        uint32_t free_count = 0;
    };

    CacheDir(uint8_t index, size_t quota_bytes, const std::string& path)
            : _index(index),
              _quota_bytes(quota_bytes),
              _path(path),
              _total_block_count(quota_bytes / config::FLAGS_block_size) {}
    ~CacheDir();

    Status init();

    int64_t alloc_block();
    void free_block(uint64_t block_index);

    BlockFilePtr get_block_file(uint32_t block_index) const {
        size_t file_index = block_index / file_block_count();
        return _block_files[file_index];
    }

    off_t get_block_file_offset(uint32_t block_index, off_t offset_in_block) const {
        uint32_t block_index_in_file = block_index % file_block_count();
        off_t offset = block_index_in_file * config::FLAGS_block_size + offset_in_block;
        return offset;
    }

    Status write_block(uint32_t block_index, off_t offset_in_block, const IOBuf& buf) const;
    Status read_block(uint32_t block_index, off_t offset_in_block, size_t size, IOBuf* buf) const;
    Status writev_block(uint32_t block_index, off_t offset_in_block, const std::vector<IOBuf*>& bufv) const;
    Status readv_block(uint32_t block_index, off_t offset_in_block, const std::vector<size_t> sizev,
                       std::vector<IOBuf*>* bufv) const;

    uint8_t index() const { return _index; }

    size_t quota_bytes() const { return _quota_bytes; }

    std::string path() const { return _path; }

private:
    Status init_free_space_list();
    Status init_block_files();

    uint8_t _index;
    size_t _quota_bytes;
    std::string _path;

    uint32_t _total_block_count;
    uint32_t _used_block_count = 0;

    BlockSpace* _block_spaces;
    // store free space index
    std::list<uint32_t> _free_space_list;
    std::vector<BlockFilePtr> _block_files;

    std::mutex _space_mutex;
};

using CacheDirPtr = std::shared_ptr<CacheDir>;

class CacheDirRouter {
public:
    struct DirWeight {
        uint8_t index;
        int64_t weight;
        int64_t cur_weight;
        bool enable;
    };

    void add_dir(CacheDirPtr dir);
    void disable_dir(uint8_t index);
    void add_dir_weight(uint8_t index, int64_t weight);
    int next_dir_index();
    void reset() { _dir_weights.clear(); }

private:
    std::vector<DirWeight> _dir_weights;
    std::mutex _mutex;
};

class DiskSpaceManager {
public:
    static DiskSpaceManager* GetInstance() { return Singleton<DiskSpaceManager>::get(); }

    Status add_cache_dir(const DirSpace& dir);

    Status alloc_block(BlockId* block_id);
    Status free_block(BlockId block_id);
    Status write_block(BlockId block_id, off_t offset_in_block, const IOBuf& buf) const;
    Status read_block(BlockId block_id, off_t offset_in_block, size_t size, IOBuf* buf) const;
    Status writev_block(BlockId block_id, off_t offset_in_block, const std::vector<IOBuf*>& bufv) const;
    Status readv_block(BlockId block_id, off_t offset_in_block, const std::vector<size_t> sizev,
                       std::vector<IOBuf*>* bufv) const;

    size_t quota_bytes() const { return _quota_bytes; }

    size_t used_bytes() const { return _used_bytes; }

    std::vector<CacheDirPtr>& cache_dirs() { return _cache_dirs; }

    void reset() {
        _quota_bytes = 0;
        _used_bytes = 0;
        _cache_dirs.clear();
        _cache_dir_router.reset();
    }

private:
    DiskSpaceManager() = default;
    friend struct DefaultSingletonTraits<DiskSpaceManager>;

    DISALLOW_COPY_AND_ASSIGN(DiskSpaceManager);

    size_t _quota_bytes = 0;
    std::atomic<size_t> _used_bytes = 0;
    std::vector<CacheDirPtr> _cache_dirs;
    CacheDirRouter _cache_dir_router;
};

} // namespace starrocks::starcache
