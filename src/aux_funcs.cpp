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

#include "aux_funcs.h"

#include <butil/crc32c.h>

namespace starrocks::starcache {

uint64_t cachekey2id(const std::string& key) {
    return hash64(key.data(), key.size(), 0);
}

size_t align_iobuf(const butil::IOBuf& buf, void** aligned_data) {
    size_t aligned_unit = config::FLAGS_io_align_unit_size;
    size_t aligned_size = round_up(buf.size(), aligned_unit);
    if (posix_memalign(aligned_data, aligned_unit, aligned_size) != 0) {
        return 0;
    }

    butil::IOBuf tmp_buf(buf);
    // IOBufCutter is a specialized utility to cut from IOBuf faster than using corresponding
    // methods in IOBuf.
    butil::IOBufCutter cutter(&tmp_buf);
    cutter.cutn(*aligned_data, buf.size());
    return aligned_size;
}

uint32_t crc32_iobuf(const butil::IOBuf& buf) {
    uint32_t hash = 0;
    const size_t block_num = buf.backing_block_num();
    for (size_t i = 0; i < block_num; ++i) {
        auto sp = buf.backing_block(i);
        if (!sp.empty()) {
            hash = butil::crc32c::Extend(hash, sp.data(), sp.size());
        }
    }
    return hash;
}

} // namespace starrocks::starcache
