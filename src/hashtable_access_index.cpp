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

#include "hashtable_access_index.h"

#include <glog/logging.h>

#include "common/types.h"

namespace starrocks::starcache {

bool HashTableAccessIndex::insert(const CacheId& id, const CacheItemPtr& item) {
    _cache_items.update(id, item);
    return true;
}

CacheItemPtr HashTableAccessIndex::find(const CacheId& id) {
    CacheItemPtr cache_item(nullptr);
    _cache_items.find(id, &cache_item);
    return cache_item;
}

bool HashTableAccessIndex::remove(const CacheId& id) {
    return _cache_items.remove(id);
}

} // namespace starrocks::starcache
