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

#include <random>

#include "admission_policy.h"

namespace starrocks::starcache {

// The class to handle admission control logic based on object size
class SizeBasedAdmissionPolicy : public AdmissionPolicy {
public:
    struct Config {
        // Only the object that smaller than this size will be checked by this policy
        size_t max_check_size;
        // The probability to flush the small block
        double flush_probability;
        // The probability to delete the small block
        double delete_probability;
    };
    explicit SizeBasedAdmissionPolicy(const Config& config);
    ~SizeBasedAdmissionPolicy() override = default;

    BlockAdmission check_admission(const CacheItemPtr& cache_item, const BlockKey& block_key) override;

private:
    size_t _max_check_size;
    double _flush_probability;
    double _delete_probability;
    std::minstd_rand _rand_generator;
};

} // namespace starrocks::starcache
