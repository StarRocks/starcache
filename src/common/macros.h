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

#include <butil/compiler_specific.h>
#include <glog/logging.h>

namespace starrocks::starcache {

#define STAR_VLOG VLOG(88)
#define STAR_VLOG_IF(cond) VLOG_IF(88, (cond))

#define STAR_VLOG_FULL VLOG(89)
#define STAR_VLOG_FULL_IF(cond) VLOG_IF(89, (cond))

#ifndef UNLIKELY
#define UNLIKELY BAIDU_UNLIKELY
#endif

#ifndef LIKELY
#define LIKELY BAIDU_LIKELY
#endif

#ifndef LOG_IF_ERROR
#define LOG_IF_ERROR(stmt)                      \
    do {                                        \
        auto&& status__ = (stmt);               \
        if (UNLIKELY(!status__.ok())) {         \
            LOG(ERROR) << status__.error_str(); \
        }                                       \
    } while (false);
#endif

#ifndef RETURN_IF_ERROR
#define RETURN_IF_ERROR(stmt)           \
    do {                                \
        auto&& status__ = (stmt);       \
        if (UNLIKELY(!status__.ok())) { \
            return status__;            \
        }                               \
    } while (false);
#endif

// lock
#define LOCK_IF(lck, condition) \
    if (condition) {            \
        (lck)->lock();          \
    }

#define UNLOCK_IF(lck, condition) \
    if (condition) {              \
        (lck)->unlock();          \
    }

// The static local variables in functions can avoid repeated computation and assignment at runtime.
// But in unittest, the variables cannot be updated by different configuration in multiple test cases,
// and will cause lots of unexpeted exceptions.
#ifndef UNIT_TEST
#define STATIC_EXCEPT_UT static
#else
#define STATIC_EXCEPT_UT
#endif

// custom error code
#define E_INTERNAL 100001

} // namespace starrocks::starcache
