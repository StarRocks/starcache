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

#include "cache_item.h"

namespace starrocks::starcache {

enum class BlockAdmission : uint8_t { FLUSH, SKIP, DELETE };

inline std::ostream& operator<<(std::ostream& os, const BlockAdmission& admisson) {
    switch (admisson) {
    case BlockAdmission::FLUSH:
        os << "flush";
        break;
    case BlockAdmission::SKIP:
        os << "skip";
        break;
    case BlockAdmission::DELETE:
        os << "delete";
    }
    return os;
}

class BlockKey;
// The class to handle admission control logic
class AdmissionPolicy {
public:
    virtual ~AdmissionPolicy() = default;

    // Check the admisstion control for the given block
    virtual BlockAdmission check_admission(const CacheItemPtr& cache_item, const BlockKey& block_key) = 0;
};

} // namespace starrocks::starcache
