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

#include "size_based_admission_policy.h"

#include "disk_space_manager.h"

namespace starrocks::starcache {

SizeBasedAdmissionPolicy::SizeBasedAdmissionPolicy(const Config& config)
        : _max_check_size(config.max_check_size),
          _flush_probability(config.flush_probability),
          _delete_probability(config.delete_probability),
          _rand_generator(std::rand()) {}

BlockAdmission SizeBasedAdmissionPolicy::check_admission(const CacheItemPtr& cache_item, const BlockKey& block_key) {
    STATIC_EXCEPT_UT size_t disk_quota = DiskSpaceManager::GetInstance()->quota_bytes();
    if (disk_quota == 0) {
        return BlockAdmission::DELETE;
    }
    if (cache_item->size >= _max_check_size) {
        return BlockAdmission::FLUSH;
    }

    double size_ratio = static_cast<double>(cache_item->size) / _max_check_size;
    double probability = static_cast<double>(_rand_generator(), _rand_generator.max());
    if (probability < size_ratio * _flush_probability) {
        return BlockAdmission::FLUSH;
    }
    if (probability < size_ratio * _delete_probability) {
        return BlockAdmission::DELETE;
    }
    return BlockAdmission::SKIP;
}

} // namespace starrocks::starcache
