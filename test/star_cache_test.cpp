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

#include "star_cache.h"

#include <gtest/gtest.h>
#include <cstring>
#include <butil/fast_rand.h>
#include <glog/logging.h>
#include <filesystem>
//#include "fs/fs_util.h"
#include "common/types.h"
#include "common/config.h"
#include "sharded_lock_manager.h"
#include "mem_space_manager.h"
#include "disk_space_manager.h"

namespace starrocks::starcache {

IOBuf gen_iobuf(size_t size, char ch) {
    IOBuf buf;
    buf.resize(size, ch);
    return buf;
}

class StarCacheTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::create_directories("./ut_dir/star_disk_cache");
        config::FLAGS_evict_touch_mem_probalility = 100;
        config::FLAGS_evict_touch_disk_probalility = 100;
        config::FLAGS_promotion_probalility = 100;
    }
    void TearDown() override {
        MemSpaceManager::GetInstance()->reset();
        DiskSpaceManager::GetInstance()->reset();
        std::filesystem::remove_all("./ut_dir");
    }

    std::shared_ptr<StarCache> create_simple_cache(size_t mem_quota, size_t disk_quota=0) {
        std::shared_ptr<StarCache> cache(new StarCache);
        CacheOptions options;
        options.mem_quota_bytes = mem_quota;
        if (disk_quota > 0) {
            options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache", .quota_bytes = disk_quota});
        }
        Status status = cache->init(options);
        EXPECT_TRUE(status.ok());
        return cache;
    }
};

template <typename T>
class ConfigUpdater {
public:
    ConfigUpdater(T& param, T value) : _param(param), _saved_old_value(param) {
        param = value;
    }
    ~ConfigUpdater() {
        _param = _saved_old_value;
    }
private:
    T& _param;
    T _saved_old_value; 
};

int add(int a, int b) {
    return a + b;
}

#define CONCAT(x, y) x##y
#define CONCAT2(x, y) CONCAT(x, y)
#define CONFIG_UPDATE(type, param, value) \
    std::shared_ptr<ConfigUpdater<type>> CONCAT2(_config_updater_, __LINE__)(new ConfigUpdater<type>(param, value))

TEST_F(StarCacheTest, basic_ops) {
    auto cache = create_simple_cache(20 * MB, 500 * MB);

    const size_t obj_size = 4 * config::FLAGS_block_size + 123;
    const size_t rounds = 10;
    const std::string cache_key = "test_file";
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf buf = gen_iobuf(obj_size, ch);
        st = cache->set(cache_key + std::to_string(i), buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    // get cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(obj_size, ch);
        IOBuf buf;
        st = cache->get(cache_key + std::to_string(i), &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }

    // read cache
    size_t batch_size = 2 * config::FLAGS_block_size;
    off_t offset = config::FLAGS_block_size;
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(batch_size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), offset, batch_size, &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }
    for (size_t i = 0; i < rounds; ++i) {
        off_t off = butil::fast_rand_less_than(obj_size);
        size_t size = butil::fast_rand_less_than(obj_size - off);
        LOG(INFO) << "random read, offset: " << off << ", size: " << size;
        if (size == 0) {
            continue;
        }
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), off, size, &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }

    // remove cache
    std::string key_to_remove = cache_key + std::to_string(0);
    st = cache->remove(key_to_remove);
    ASSERT_TRUE(st.ok()) << st.error_str();

    IOBuf buf;
    st = cache->get(key_to_remove, &buf);
    ASSERT_EQ(st.error_code(), ENOENT);
}

TEST_F(StarCacheTest, custom_block_size) {
    CONFIG_UPDATE(uint64_t, config::FLAGS_block_size, 256 * KB);
    auto cache = create_simple_cache(20 * MB, 500 * MB);

    const size_t obj_size = 4 * config::FLAGS_block_size + 123;
    const size_t rounds = 10;
    const std::string cache_key = "test_file";
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf buf = gen_iobuf(obj_size, ch);
        st = cache->set(cache_key + std::to_string(i), buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    // get cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(obj_size, ch);
        IOBuf buf;
        st = cache->get(cache_key + std::to_string(i), &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }

    // read cache
    size_t batch_size = 2 * config::FLAGS_block_size;
    off_t offset = config::FLAGS_block_size;
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(batch_size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), offset, batch_size, &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }
    for (size_t i = 0; i < rounds; ++i) {
        off_t off = butil::fast_rand_less_than(obj_size);
        size_t size = butil::fast_rand_less_than(obj_size - off);
        LOG(INFO) << "random read, offset: " << off << ", size: " << size;
        if (size == 0) {
            continue;
        }
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), off, size, &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }
}

TEST_F(StarCacheTest, full_mem_cache) {
    CONFIG_UPDATE(uint32_t, config::FLAGS_lru_container_shard_bits, 0);
    auto cache = create_simple_cache(20 * MB);

    const size_t obj_size = 4 * config::FLAGS_block_size + 123;
    const size_t rounds = 5;
    const std::string cache_key = "test_file";
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf buf = gen_iobuf(obj_size, ch);
        st = cache->set(cache_key + std::to_string(i), buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    // get cache
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(obj_size, ch);
        IOBuf buf;
        st = cache->get(cache_key + std::to_string(i), &buf);
        if (i != 0) {
            ASSERT_TRUE(st.ok()) << st.error_str();
            ASSERT_EQ(buf, expect_buf);
        } else {
            ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();
        }
    }

    // read cache
    for (size_t i = 0; i < rounds; ++i) {
        off_t off = butil::fast_rand_less_than(obj_size);
        size_t size = butil::fast_rand_less_than(obj_size - off);
        LOG(INFO) << "random read, offset: " << off << ", size: " << size;
        if (size == 0) {
            continue;
        }
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), off, size, &buf);
        if (i != 0) {
            ASSERT_TRUE(st.ok()) << st.error_str();
            ASSERT_EQ(buf, expect_buf);
        } else {
            ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();
        }
    }
}

TEST_F(StarCacheTest, disk_cache_eviction) {
    return;
    // TODO: fix it later
    CONFIG_UPDATE(uint32_t, config::FLAGS_lru_container_shard_bits, 0);
    auto cache = create_simple_cache(10 * MB, 10 * MB);

    const size_t obj_size = 3 * config::FLAGS_block_size;
    const size_t rounds = 10;
    const std::string cache_key = "test_file";
    Status st;

    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf buf = gen_iobuf(obj_size, ch);
        st = cache->set(cache_key + std::to_string(i), buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }
    // Now, the block layout: [0 0 0 1 1 1 2 2] [7 7 7 8 8 8 9 9 9 null]

    // Only [0 1 6 9] can be get successfully, 2 is partial block, 8 will be evicted from disk when promoting 7
    std::set<size_t> exist_index({0, 1, 7, 9});
    for (size_t i = 0; i < rounds; ++i) {
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(obj_size, ch);
        IOBuf buf;
        st = cache->get(cache_key + std::to_string(i), &buf);
        if (exist_index.find(i) != exist_index.end()) {
            ASSERT_TRUE(st.ok()) << st.error_str();
            ASSERT_EQ(buf, expect_buf);
        } else {
            ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();
        }
    }
    // Now, the block layout: [9 9 9 2 2 7 7 7] [9 9 9 0 0 0 1 1 1 null]

    // read cache
    std::vector<size_t> read_order = { 9, 2, 7, 0, 1 };
    // Only read 2MB to make sure the object 2 also hits cache
    size_t read_size = 2 * config::FLAGS_block_size;
    for (auto i : read_order) {
        off_t off = butil::fast_rand_less_than(read_size);
        size_t size = butil::fast_rand_less_than(read_size - off);
        LOG(INFO) << "random read, offset: " << off << ", size: " << size;
        if (size == 0) {
            continue;
        }
        char ch = 'a' + i % 26;
        IOBuf expect_buf = gen_iobuf(size, ch);
        IOBuf buf;
        st = cache->read(cache_key + std::to_string(i), off, size, &buf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(buf, expect_buf);
    }
}

TEST_F(StarCacheTest, partial_block_flush) {
    CONFIG_UPDATE(uint32_t, config::FLAGS_lru_container_shard_bits, 0);
    CONFIG_UPDATE(uint64_t, config::FLAGS_promotion_mem_threshold, 50);
    auto cache = create_simple_cache(1 * MB, 4 * MB);

    char ch = 'a';
    // Write to memory, lead the memory exceed threshold
    IOBuf wbuf = gen_iobuf(MB / 2, ch);
    Status st = cache->set("test_file1", wbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();

    // Directly write to disk
    wbuf = gen_iobuf(4 * MB, ch);
    st = cache->set("test_file2", wbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();

    // Read some segment to memory
    IOBuf rbuf;
    for (size_t i = 0; i < block_slice_count(); i+=5) {
        size_t read_size = 100;
        IOBuf expect_buf = gen_iobuf(read_size, ch);
        st = cache->read("test_file2", i * config::FLAGS_slice_size, read_size, &rbuf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(rbuf, expect_buf);
    }

    // Write a new object to disk, evict the old one (file2) from disk
    st = cache->set("test_file3", wbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();

    // Promote file3 to memory, trigger the old promoted file2 segment try to flush to disk.
    // As the partial segments are not allowed to be flushed to disk, so they will be deleted
    st = cache->read("test_file3", 0, MB / 2, &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();

    st = cache->read("test_file2", 0, 100, &rbuf);
    ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();
}

TEST_F(StarCacheTest, remove_cache) {
    auto cache = create_simple_cache(10 * MB, 20 * MB);

    const size_t obj_size = 4 * MB + 123;
    const size_t rounds = 5;
    const std::string cache_key = "test_file";
    char ch = 'a';
    IOBuf wbuf = gen_iobuf(obj_size, ch);
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        st = cache->set(cache_key + std::to_string(i), wbuf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    IOBuf rbuf;
    st = cache->get("test_file0", &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();

    // remove memory cache item
    st = cache->remove("test_file0");
    ASSERT_TRUE(st.ok()) << st.error_str();

    st = cache->get("test_file0", &rbuf);
    ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();

    // remove disk cache item
    st = cache->remove("test_file3");
    ASSERT_TRUE(st.ok()) << st.error_str();

    st = cache->get("test_file3", &rbuf);
    ASSERT_EQ(st.error_code(), ENOENT) << st.error_str();

    st = cache->get("test_file4", &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();
}

TEST_F(StarCacheTest, update_cache) {
    auto cache = create_simple_cache(10 * MB, 20 * MB);

    const size_t obj_size = 4 * MB + 123;
    const size_t rounds = 5;
    const std::string cache_key = "test_file";
    char ch = 'a';
    IOBuf wbuf = gen_iobuf(obj_size, ch);
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        st = cache->set(cache_key + std::to_string(i), wbuf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    IOBuf rbuf;
    IOBuf expect_buf = gen_iobuf(obj_size, ch);
    st = cache->get("test_file0", &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();
    ASSERT_EQ(rbuf, expect_buf);

    // update memory cache item
    char ch2 = 'b';
    IOBuf wbuf2 = gen_iobuf(obj_size, ch2);
    st = cache->set("test_file0", wbuf2);
    ASSERT_TRUE(st.ok()) << st.error_str();

    IOBuf expect_buf2 = gen_iobuf(obj_size, ch2);
    st = cache->get("test_file0", &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();
    ASSERT_EQ(rbuf, expect_buf2);

    // update disk cache item
    st = cache->set("test_file3", wbuf2);
    ASSERT_TRUE(st.ok()) << st.error_str();

    st = cache->get("test_file3", &rbuf);
    ASSERT_TRUE(st.ok()) << st.error_str();
    ASSERT_EQ(rbuf, expect_buf2);
}

TEST_F(StarCacheTest, cache_with_multi_disk) {
    CONFIG_UPDATE(uint32_t, config::FLAGS_lru_container_shard_bits, 0);
    // To make sure all flush process will be skipped
    CONFIG_UPDATE(uint32_t, config::FLAGS_admission_max_check_size, 1 * GB);
    CONFIG_UPDATE(double, config::FLAGS_admission_flush_probability, 0.0);
    CONFIG_UPDATE(double, config::FLAGS_admission_delete_probability, 0.0);

    std::filesystem::create_directories("./ut_dir/star_disk_cache2");
    std::filesystem::create_directories("./ut_dir/star_disk_cache3");

    std::shared_ptr<StarCache> cache(new StarCache);
    CacheOptions options;
    options.mem_quota_bytes = 10 * MB;
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache", .quota_bytes = 4 * MB});
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache2", .quota_bytes = 8 * MB});
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache3", .quota_bytes = 12 * MB});
    Status status = cache->init(options);
    ASSERT_TRUE(status.ok());

    const size_t obj_size = 4 * MB;
    const size_t rounds = 8;
    const std::string cache_key = "test_file";
    char ch = 'a';
    IOBuf wbuf = gen_iobuf(obj_size, ch);
    Status st;

    // write cache
    for (size_t i = 0; i < rounds; ++i) {
        st = cache->set(cache_key + std::to_string(i), wbuf);
        ASSERT_TRUE(st.ok()) << st.error_str();
    }

    IOBuf rbuf;
    IOBuf expect_buf = gen_iobuf(obj_size, ch);
    for (size_t i = 0; i < rounds; ++i) {
        st = cache->get(cache_key + std::to_string(i), &rbuf);
        ASSERT_TRUE(st.ok()) << st.error_str();
        ASSERT_EQ(rbuf, expect_buf);
    }
}

TEST_F(StarCacheTest, replace_cache_content) {
    CONFIG_UPDATE(uint32_t, config::FLAGS_lru_container_shard_bits, 0);

    std::filesystem::create_directories("./ut_dir/star_disk_cache2");
    std::filesystem::create_directories("./ut_dir/star_disk_cache3");

    std::shared_ptr<StarCache> cache(new StarCache);
    CacheOptions options;
    options.mem_quota_bytes = 10 * MB;
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache", .quota_bytes = 8 * MB});
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache2", .quota_bytes = 16 * MB});
    options.disk_dir_spaces.push_back({.path = "./ut_dir/star_disk_cache3", .quota_bytes = 24 * MB});
    Status status = cache->init(options);
    ASSERT_TRUE(status.ok());

    const size_t obj_size = 4 * MB;
    const size_t rounds = 10;
    const size_t batch_count = 6;
    const std::string cache_key = "test_file";
    Status st;

    for (size_t i = 0; i < rounds; ++i) {
        for (size_t j = 0; j < batch_count; ++j) {
            size_t index = i * batch_count + j;
            char ch = 'a' + index;
            IOBuf wbuf = gen_iobuf(obj_size, ch);
            st = cache->set(cache_key + std::to_string(index), wbuf);
            ASSERT_TRUE(st.ok()) << st.error_str();
        }
        for (size_t j = 0; j < batch_count; ++j) {
            size_t index = i * batch_count + j;
            char ch = 'a' + index;
            IOBuf rbuf;
            IOBuf expect_buf = gen_iobuf(obj_size, ch);
            st = cache->get(cache_key + std::to_string(index), &rbuf);
            ASSERT_TRUE(st.ok()) << st.error_str();
            ASSERT_EQ(rbuf, expect_buf);
        }
    }
}

TEST_F(StarCacheTest, max_concurrent_writes) {
    CONFIG_UPDATE(uint64_t, config::FLAGS_max_concurrent_writes, 0);
    auto cache = create_simple_cache(10 * MB, 20 * MB);

    const size_t obj_size = MB;
    const std::string cache_key = "test_file";
    char ch = 'a';
    IOBuf wbuf = gen_iobuf(obj_size, ch);
    Status st;

    st = cache->set(cache_key + std::to_string(0), wbuf);
    ASSERT_EQ(st.error_code(), EBUSY) << st.error_str();
}

} // namespace starrocks::starcache

int main(int argc, char** argv) {
    ::testing::InitGoogleTest(&argc, argv);    
	::google::ParseCommandLineFlags(&argc, &argv, true);
  	::google::InitGoogleLogging(argv[0]);
    FLAGS_log_dir="logs";   
    std::filesystem::create_directories(FLAGS_log_dir);
    int r = RUN_ALL_TESTS();
	google::ShutdownGoogleLogging();
    return r;
}
