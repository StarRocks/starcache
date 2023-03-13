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

#include <fcntl.h>
#include <sys/uio.h>

#include "common/types.h"

namespace starrocks::starcache {

class BlockFile {
public:
    BlockFile(const std::string& path, size_t quota_bytes) : _file_path(path), _quota_bytes(quota_bytes) {}
    ~BlockFile() {
        if (_fd > 0) {
            close();
        }
    }

    Status open(bool pre_allocate);
    Status close();

    Status write(off_t offset, const IOBuf& buf);
    Status read(off_t offset, size_t size, IOBuf* buf);
    Status writev(off_t offset, const std::vector<IOBuf*>& bufv);
    Status readv(off_t offset, const std::vector<size_t>& sizev, std::vector<IOBuf*>* bufv);

private:
    Status _report_io_error(const std::string& err_desc);

    std::string _file_path;
    size_t _quota_bytes;
    int _fd = 0;
};

} // namespace starrocks::starcache
