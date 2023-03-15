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
#include <butil/status.h>

#include <atomic>

namespace starrocks::starcache {

using Status = butil::Status;
using IOBuf = butil::IOBuf;
using CacheId = uint64_t;
using CacheKey = std::string;

struct BlockId {
    uint8_t dir_index;
    uint32_t block_index;
};

struct BlockKey {
    CacheId cache_id;
    uint32_t block_index;
};

struct DirSpace {
    std::string path;
    size_t quota_bytes;
};

inline std::ostream& operator<<(std::ostream& os, const BlockKey& block_key) {
    os << block_key.cache_id << "_" << block_key.block_index;
    return os;
}

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
    bool checksum = false;
    size_t block_size = 0;
};

constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024;
constexpr size_t GB = MB * 1024;

} // namespace starrocks::starcache
