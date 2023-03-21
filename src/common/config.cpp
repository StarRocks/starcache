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

#include "common/config.h"

namespace starrocks::starcache::config {

DEFINE_uint64(block_size, 1048576, "The size of block, which is the basic unit of cache management"); // 1MB
DEFINE_uint64(slice_size, 65536, "The size of slice, which is the minimum read unit");                // 64KB
DEFINE_uint64(block_file_size, 10737418240,
              "The size of disk block file, which cache a batch of blocks in a disk file"); // 10GB

DEFINE_bool(pre_allocate_block_file, false, "Whether to pre-allocate the the block file to the target size");
DEFINE_bool(enable_disk_checksum, true, "Whether to do checksum to check the data correctness read from disk");

DEFINE_uint64(max_concurrent_writes, 1500000,
              "The maximum concurrent write count, to avoid too many writes that affect write latency");

DEFINE_uint32(mem_evict_times, 2, "The times of target size that need to be evicted in an memery eviction");
DEFINE_uint32(disk_evict_times, 2, "The times of target size that need to be evicted in a disk eviction");

DEFINE_uint32(max_retry_when_allocate, 10, "The maximum retry count when allocate segment or block failed");

DEFINE_uint32(admission_max_check_size, 65536,
              "The max object size that need to be checked by size based admission policy"); // 64KB
DEFINE_double(admission_flush_probability, 1.0, "The probability to flush the small block to disk when evict");
DEFINE_double(admission_delete_probability, 1.0, "The probability to delete the small block directly when evict");

DEFINE_uint64(alloc_mem_threshold, 90,
              "The threshold under which the memory allocation is allowed, expressed as a percentage");
DEFINE_uint64(promotion_mem_threshold, 80,
              "The threshold under which the promotion is allowed, expressed as a percentage");
DEFINE_uint32(promotion_probalility, 30, "The probability to promote the disk cache data");
DEFINE_uint32(evict_touch_mem_probalility, 30, "The probability to touch memory block item");
DEFINE_uint32(evict_touch_disk_probalility, 30, "The probability to touch disk cache item");

DEFINE_bool(enable_os_page_cache, false,
            "Whether to enable os page cache, if not, the disk data will be processed in direct io mode");
// It's best to set this parameter to the size of the cache disk sector.
// The sector size can be get by `sudo blockdev --getss {device}`, such as `sudo blockdev --getss /dev/vda1`.
// Although we also can get it by `ioctl`, but it always need root permisson.
// The default value (4096) can always work normally, but sometimes, especially if the sector size small than 4096, the
// performance is not the best.
DEFINE_uint32(io_align_unit_size, 4096, "The unit size for direct io alignment");

DEFINE_uint32(access_index_shard_bits, 5, "The shard bits of access index hashmap");  // 32 shards
DEFINE_uint32(sharded_lock_shard_bits, 12, "The shard bits of sharded lock manager"); // 4096 shards
DEFINE_uint32(lru_container_shard_bits, 5, "The shard bits of lru container");        // 32 shards

} // namespace starrocks::starcache::config
