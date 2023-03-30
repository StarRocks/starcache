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
#include <boost/align/is_aligned.hpp>

#include "common/config.h"
#include "common/macros.h"
#include "common/types.h"
#include "utils/murmur_hash3.h"

namespace starrocks::starcache {

uint64_t cachekey2id(const CacheKey& key);

inline int off2block(off_t offset) {
    return offset / config::FLAGS_block_size;
}

inline uint32_t block_lower(int block_index) {
    return block_index * config::FLAGS_block_size;
}

inline uint32_t block_upper(int block_index) {
    return (block_index + 1) * config::FLAGS_block_size - 1;
}

inline int off2slice(off_t offset) {
    return offset / config::FLAGS_slice_size;
}

inline uint32_t slice_lower(int slice_index) {
    return slice_index * config::FLAGS_slice_size;
}

inline uint32_t slice_upper(int slice_index) {
    return (slice_index + 1) * config::FLAGS_slice_size - 1;
}

inline uint64_t block_shard(const BlockKey& key) {
    return key.cache_id + key.block_index;
}

inline int64_t round_up(int64_t value, int64_t factor) {
    return (value + (factor - 1)) / factor * factor;
}

inline int64_t round_down(int64_t value, int64_t factor) {
    return (value + (factor - 1)) / factor * factor;
    return (value / factor) * factor;
}

inline uint32_t block_slice_count() {
    STATIC_EXCEPT_UT uint32_t slice_count = config::FLAGS_block_size / config::FLAGS_slice_size;
    return slice_count;
}

inline uint32_t file_block_count() {
    STATIC_EXCEPT_UT uint32_t block_count = config::FLAGS_block_file_size / config::FLAGS_block_size;
    return block_count;
}

inline bool mem_need_align(const void* data, size_t size) {
    if (config::FLAGS_enable_os_page_cache) {
        return false;
    }
    const size_t aligned_unit = config::FLAGS_io_align_unit_size;
    if (boost::alignment::is_aligned(data, aligned_unit) && (size % aligned_unit == 0)) {
        return false;
    }
    return true;
}

size_t align_iobuf(const butil::IOBuf& buf, void** aligned_buf);
uint32_t crc32_iobuf(const butil::IOBuf& buf);

inline Status to_status(const std::string& context, int err_number) {
    return Status(err_number, "%s: %s", context.c_str(), std::strerror(err_number));
}

// TODO: Check cpuinfo like `CpuInfo::is_supported(CpuInfo::SSE4_2)` to accelerate,
// refer to starrocks::HashUtil.
inline uint64_t hash64(const void* data, int32_t bytes, uint64_t seed) {
    uint64_t hash = 0;
    murmur_hash3_x64_64(data, bytes, seed, &hash);
    return hash;
}

} // namespace starrocks::starcache
