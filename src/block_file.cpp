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

#include "block_file.h"

#include <glog/logging.h>

#include "aux_funcs.h"
#include "common/macros.h"

namespace starrocks::starcache {

Status BlockFile::open(bool pre_allocate) {
    int oflag = O_RDWR | O_CREAT;
    if (!config::FLAGS_enable_os_page_cache) {
        oflag |= O_DIRECT;
    }
    _fd = ::open(_file_path.c_str(), oflag, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    if (_fd < 0) {
        return _report_io_error("fail to open block file");
    }

    if (pre_allocate) {
        if (::ftruncate(_fd, _quota_bytes) != 0) {
            return _report_io_error("fail to truncate block file");
        }
    }
    LOG(INFO) << "open block file success, fd: " << _fd << ", path: " << _file_path;
    return Status::OK();
}

Status BlockFile::close() {
    if (::close(_fd) != 0) {
        return _report_io_error("fail to close block file");
    }
    return Status::OK();
}

Status BlockFile::write(off_t offset, const IOBuf& buf) {
    ssize_t ret = 0;
    void* aligned_data = nullptr;
    size_t aligned_size = buf.size();

    size_t block_num = buf.backing_block_num();
    if (block_num == 1) {
        auto data = buf.backing_block(0).data();
        if (mem_need_align(data, buf.size())) {
            aligned_size = align_iobuf(buf, &aligned_data);
            ret = ::pwrite(_fd, aligned_data, aligned_size, offset);
            free(aligned_data);
        } else {
            ret = ::pwrite(_fd, data, aligned_size, offset);
        }
    } else if (!config::FLAGS_enable_os_page_cache) {
        aligned_size = align_iobuf(buf, &aligned_data);
        ret = ::pwrite(_fd, aligned_data, aligned_size, offset);
        free(aligned_data);
    } else {
        struct iovec iov[block_num];
        for (size_t i = 0; i < block_num; ++i) {
            iov[i] = {(void*)buf.backing_block(i).data(), buf.backing_block(i).size()};
        }
        ret = ::pwritev(_fd, iov, block_num, offset);
    }
    if (ret < 0) {
        return _report_io_error("fail to write block file");
    }

    STAR_VLOG << "write block file success, fd: " << _fd << ", path: " << _file_path << ", offset: " << offset
              << ", buf size: " << buf.size() << ", aligned_size: " << aligned_size << ", buf block num: " << block_num;
    return Status::OK();
}

Status BlockFile::read(off_t offset, size_t size, IOBuf* buf) {
    void* data = nullptr;
    size_t aligned_size = size;
    if (config::FLAGS_enable_os_page_cache) {
        data = malloc(size);
    } else {
        size_t aligned_unit = config::FLAGS_io_align_unit_size;
        aligned_size = round_up(size, aligned_unit);
        int ret = posix_memalign(&data, aligned_unit, aligned_size);
        if (ret != 0) {
            Status st(ret, "posix posix_memalign erro %s", std::strerror(ret));
            LOG(ERROR) << st.error_str();
            return st;
        }
        DCHECK(data);
    }
    int ret = ::pread(_fd, data, aligned_size, offset);
    if (ret < 0) {
        return _report_io_error("fail to read block file");
    }
    buf->append_user_data(data, aligned_size, /*deleter*/ nullptr);

    STAR_VLOG << "read block file, fd: " << _fd << ", path: " << _file_path << ", offset: " << offset
              << ", size: " << size << ", aligned_size: " << aligned_size;
    return Status::OK();
}

Status BlockFile::writev(off_t offset, const std::vector<IOBuf*>& bufv) {
    // NOTICE: This function has not been used now
    ssize_t ret = 0;
    size_t block_num = 0;
    for (auto& buf : bufv) {
        block_num += buf->backing_block_num();
    }

    struct iovec iov[block_num];
    size_t index = 0;
    for (auto& buf : bufv) {
        for (size_t i = 0; i < buf->backing_block_num(); ++i) {
            iov[index++] = {(void*)buf->backing_block(i).data(), buf->backing_block(i).size()};
        }
    }
    ret = ::pwritev(_fd, iov, block_num, offset);
    if (ret < 0) {
        return _report_io_error("fail to writev block file");
    }
    return Status::OK();
}

Status BlockFile::readv(off_t offset, const std::vector<size_t>& sizev, std::vector<IOBuf*>* bufv) {
    // NOTICE: This function has not been used now
    struct iovec iov[sizev.size()];
    for (size_t i = 0; i < sizev.size(); ++i) {
        void* data = malloc(sizev[i]);
        iov[i] = {data, sizev[i]};
    }
    int ret = ::preadv(_fd, iov, sizev.size(), offset);
    if (ret < 0) {
        return _report_io_error("fail to readv block file");
    }

    for (size_t i = 0; i < sizev.size(); ++i) {
        (*bufv)[i]->append_user_data(iov[i].iov_base, iov[i].iov_len, /*deleter*/ nullptr);
    }
    return Status::OK();
}

Status BlockFile::_report_io_error(const std::string& err_desc) {
    Status st(errno, "%s, reason: %s", err_desc.c_str(), std::strerror(errno));
    LOG(ERROR) << err_desc << ", reason: " << std::strerror(errno);
    return st;
}

} // namespace starrocks::starcache
