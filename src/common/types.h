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

constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024;
constexpr size_t GB = MB * 1024;

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
};

struct WriteOptions {
    enum class WriteMode{
        // Write according the starcache promotion policy.
        WRITE_BACK,
        // Write to disk directly.
        WRITE_THROUGH
    };
    // If ttl_seconds=0 (default), no ttl restriction will be set. If an old one exists, remove it. 
    uint64_t ttl_seconds=0;
    // If pinned=true, the starcache guarantees the atomicity of write and pin operations.
    bool pinned = false;
    // If overwrite=true, the cache value will be replaced if it already exists.
    bool overwrite = true;
    WriteMode mode = WriteMode::WRITE_BACK;

    // Output, store the statistics information about this write.
    struct WriteStats {
    } stats;
};

struct ReadOptions {
    enum class ReadMode{
        // Read according the starcache promotion policy.
        READ_BACK,
        // Skip promoting the data read from disk.
        READ_THROUGH
    };
    ReadMode mode = ReadMode::READ_BACK;

    // Output, store the statistics information about this read.
    struct WriteStats {
    } stats;
};

inline std::ostream& operator<<(std::ostream& os, const WriteOptions::WriteMode& mode) {
    switch (mode) {
    case WriteOptions::WriteMode::WRITE_BACK:
       os << "write_back"; 
       break;
    case WriteOptions::WriteMode::WRITE_THROUGH:
       os << "write_through";
       break;
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ReadOptions::ReadMode& mode) {
    switch (mode) {
    case ReadOptions::ReadMode::READ_BACK:
       os << "read_back"; 
       break;
    case ReadOptions::ReadMode::READ_THROUGH:
       os << "read_through";
       break;
    }
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const WriteOptions& options) {
    os << "{ ttl_seconds: " << options.ttl_seconds << ", pinned: " << options.pinned
       << ", overwrite: " << options.overwrite << ", mode: " << options.mode << " }";
    return os;
}

inline std::ostream& operator<<(std::ostream& os, const ReadOptions& options) {
    os << "{ mode: " << options.mode << " }";
    return os;
}

} // namespace starrocks::starcache
