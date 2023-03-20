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

#include <atomic>
#include <shared_mutex>

#include "utils/phmap/phmap.h"

namespace starrocks::starcache {

// K: key
// V: value
// N: shard bits of the hash map
template <typename K, typename V, uint32_t N = 5>
class ParallelHashMap {
public:
    bool insert(const K& key, const V& value) {
        return _kv.try_emplace_l(
                key, [](V&) {}, value);
    }

    void update(const K& key, const V& value) {
        _kv.try_emplace_l(
                key, [&value](V& val) { val = value;  }, value);
    }

    bool find(const K& key, V* value) {
        return _kv.if_contains(key, [value](const V& val) { *value = val;  });
    }

    bool remove(const K& key) {
        return _kv.erase_if(key, [](V& v) { return true;  });
    }

private:
    using PMap = phmap::parallel_flat_hash_map<K, V, std::hash<K>, phmap::priv::hash_default_eq<K>,
                                               std::allocator<std::pair<const K, V>>, N, std::shared_mutex>;

    PMap _kv;
};

} // namespace starrocks::starcache
