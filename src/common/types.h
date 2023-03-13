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
#include <fmt/format.h>

#include <atomic>

namespace starrocks::starcache {

using Status = butil::Status;
using IOBuf = butil::IOBuf;
using CacheId = uint64_t;
using CacheKey = std::string;

struct BlockId {
    uint8_t dir_index;
    uint32_t block_index;

    std::string str() const {
        std::string block_id = fmt::format("{}_{}", dir_index, block_index);
        return block_id;
    }
};

struct BlockKey {
    CacheId cache_id;
    uint32_t block_index;

    std::string str() const {
        std::string block_key = fmt::format("{}_{}", cache_id, block_index);
        return block_key;
    }
};

struct DirSpace {
    std::string path;
    size_t quota_bytes;
};

inline std::ostream& operator<<(std::ostream& os, const BlockKey& block_key) {
    os << block_key.cache_id << "_" << block_key.block_index;
    return os;
}

constexpr size_t KB = 1024;
constexpr size_t MB = KB * 1024;
constexpr size_t GB = MB * 1024;

} // namespace starrocks::starcache
