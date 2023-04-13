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
#include <mutex>
#include <shared_mutex>

namespace starrocks::starcache {

class BlockSegment;

class MemSpaceManager {
public:
    static MemSpaceManager* GetInstance() { return Singleton<MemSpaceManager>::get(); }

    void add_cache_zone(void* base_addr, size_t size);

    bool inc_mem(size_t size, bool urgent);
    void dec_mem(size_t size);

    size_t quota_bytes() const { return _quota_bytes; }
    size_t used_bytes() {
        std::shared_lock<std::shared_mutex> rlck(_mutex);
        return _used_bytes;
    }

    void reset() {
        _quota_bytes = 0;
        _used_bytes = 0;
        _private_quota_bytes = 0;
        _need_bytes = 0;
    }

private:
    MemSpaceManager() = default;
    friend struct DefaultSingletonTraits<MemSpaceManager>;
    DISALLOW_COPY_AND_ASSIGN(MemSpaceManager);

    size_t _quota_bytes = 0;
    size_t _used_bytes = 0;

    size_t _private_quota_bytes = 0;
    int64_t _need_bytes = 0;

    std::shared_mutex _mutex;
};

} // namespace starrocks::starcache
