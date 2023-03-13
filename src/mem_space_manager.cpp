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

#include "mem_space_manager.h"

#include <fmt/format.h>
#include <glog/logging.h>

#include "block_item.h"

namespace starrocks::starcache {

void MemSpaceManager::add_cache_zone(void* base_addr, size_t size) {
    // TODO: Add availble shared memory areas, and then we will allocate segment
    // from them.
    _quota_bytes += size;
}

bool MemSpaceManager::inc_mem(size_t size, bool urgent) {
    std::unique_lock<std::shared_mutex> wlck(_mutex);
    if (urgent && _private_quota_bytes >= size) {
        _private_quota_bytes -= size;
        _used_bytes += size;
        return true;
    }

    size_t public_quota_bytes = _quota_bytes - _private_quota_bytes;
    size_t upper_threshold = public_quota_bytes * config::FLAGS_alloc_mem_threshold / 100;
    if (_used_bytes + size < upper_threshold) {
        _used_bytes += size;
        return true;
    }

    _need_bytes += size;
    return false;
}

void MemSpaceManager::dec_mem(size_t size) {
    // TODO: free the segment from shared memory area
    std::unique_lock<std::shared_mutex> wlck(_mutex);
    if (_need_bytes >= size) {
        _private_quota_bytes += size;
        _need_bytes -= size;
    } else {
        _private_quota_bytes += _need_bytes;
        _need_bytes = 0;
    }
    _used_bytes -= size;
}

} // namespace starrocks::starcache
