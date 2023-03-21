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

#include <gflags/gflags.h>

namespace starrocks::starcache::config {

DECLARE_uint64(block_size);
DECLARE_uint64(slice_size);
DECLARE_uint64(block_file_size);

DECLARE_bool(pre_allocate_block_file);
DECLARE_bool(enable_disk_checksum);

DECLARE_uint64(max_concurrent_writes);

DECLARE_uint32(mem_evict_times);
DECLARE_uint32(disk_evict_times);

DECLARE_uint32(max_retry_when_allocate);

DECLARE_uint32(admission_max_check_size);
DECLARE_double(admission_flush_probability);
DECLARE_double(admission_delete_probability);
DECLARE_uint32(promotion_probalility);
DECLARE_uint32(evict_touch_mem_probalility);
DECLARE_uint32(evict_touch_disk_probalility);

DECLARE_uint64(alloc_mem_threshold);
DECLARE_uint64(promotion_mem_threshold);
DECLARE_bool(enable_os_page_cache);
DECLARE_uint32(io_align_unit_size);

DECLARE_uint32(access_index_shard_bits);
DECLARE_uint32(sharded_lock_shard_bits);
DECLARE_uint32(lru_container_shard_bits);

} // namespace starrocks::starcache::config
