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

#include <butil/time.h>
#include <ctime>
#include <fcntl.h>
#include <fmt/format.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include <sys/uio.h>

#include <chrono>
#include <filesystem>
#include <list>
#include <memory>
#include <numeric>
#include <thread>
#include <shared_mutex>

#include "star_cache.h"
#include "utils/blocking_queue.hpp"

namespace starrocks::starcache {

DEFINE_uint64(read_thread_num, 8, "Number of threads reading object stores");
DEFINE_uint64(max_obj_size, 30 * 1024 * 1024, "Maximum object size");
DEFINE_uint64(obj_count_per_cycle, 3000, "Number of objects to process per cycle");
DEFINE_uint64(remove_interval_s, 5, "The interval of seconds to remove objects");
DEFINE_uint64(update_context_interval_s, 60, "The interval of seconds to update test context");
DEFINE_uint64(execution_time_s, 24 * 60 * 60, "The totol time that the test executes, if 0, never stop automatically");
DEFINE_uint64(mem_cache_quota, 10 * GB, "The quota bytes of memory cache");
DEFINE_string(disk_cache_paths, "disk1:128;disk2:128", "The disk cache path and quota(GB)");
DEFINE_string(object_key_prefix, "starcache_test_object", "Prefix of object keys");
DEFINE_string(test_data_path, "test_data", "The directory path to hold all test data during the test");

// NOTICE: As this program aims to quickly test to find runtime problems and keep the most original scene as much as possible,
// instead of returning an error status, we use a number of `CHECK` macros in verifying.

namespace fs = std::filesystem;

static void check_and_create_dir(const std::string& dir, bool recursive = false) {
    fs::path local_path(dir);
    bool success = true;
    if (!fs::exists(local_path)) {
        if (recursive) {
            success = fs::create_directories(dir);
        } else {
            success = fs::create_directory(dir);
        }
        CHECK(success) << "create directory failed! path: " << dir;
    }
}

static void write_file(const std::string& file, const IOBuf& buf) {
    int fd = open(file.c_str(), O_WRONLY | O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP);
    CHECK_GT(fd, 0) << "fail to open file: " << file << ", err: " << std::strerror(errno);
    CHECK_EQ(ftruncate(fd, 0), 0);
    size_t block_num = buf.backing_block_num();
    int ret = 0;
    if (block_num == 1) {
        auto data = buf.backing_block(0).data();
        ret = write(fd, data, buf.size());
    } else {
        struct iovec iov[block_num];
        for (size_t i = 0; i < block_num; ++i) {
            iov[i] = {(void*)buf.backing_block(i).data(), buf.backing_block(i).size()};
        }
        ret = writev(fd, iov, block_num);
    }
    CHECK_EQ(ret, buf.size()) << "fail to write file:" << file << ", err: " << std::strerror(errno);
    CHECK_EQ(close(fd), 0) << "fail to close file: " << file << ", err: " << std::strerror(errno);
}

class ObjectStore {
public:
    virtual ~ObjectStore() = default;

    virtual Status init(void* options) = 0;

    virtual Status set(const std::string& key, IOBuf& buf) = 0;

    virtual Status read(const std::string& key, off_t offset, size_t size, IOBuf* buf) = 0;

    virtual Status remove(const std::string& key) = 0;

    virtual std::string name() = 0;

    virtual Status destroy() = 0;
};

class LocalStore : public ObjectStore {
public:
    LocalStore() = default;

    Status init(void* options) override {
        check_and_create_dir(fmt::format("{}/{}", FLAGS_test_data_path, name()));
        return Status::OK();
    }

    Status set(const std::string& key, IOBuf& buf) override {
        std::string keyfile = key2file(key);
        fs::path local_path(keyfile);
        CHECK(local_path.has_filename()) << "invalid object key to set to local store. key: " << key;
        if (local_path.has_parent_path()) {
            check_and_create_dir(local_path.parent_path().string(), true);
        }
        write_file(keyfile, buf);
        LOG(INFO) << "write object to local store success, key: " << key << ", buf len: " << buf.size();
        return Status::OK();
    }

    Status read(const std::string& key, off_t offset, size_t size, IOBuf* buf) override {
        std::string keyfile = key2file(key);
        fs::path local_path(keyfile);
        if (!fs::exists(local_path)) {
            return Status(ENOENT, "no such file %s", keyfile.c_str());
        }

        int fd = open(keyfile.c_str(), O_RDONLY);
        CHECK_GT(fd, 0) << "fail to open file: " << keyfile << ", err: " << std::strerror(errno);
        void* data = malloc(size);
        int ret = pread(fd, data, size, offset);
        CHECK_GE(ret, 0) << "fail to read file:" << keyfile << ", err: " << std::strerror(errno);
        buf->append_user_data(data, ret, nullptr);
        CHECK_EQ(close(fd), 0) << "fail to close file: " << keyfile << ", err: " << std::strerror(errno);
        LOG(INFO) << "read object from local store success, key: " << key << ", offset: " << offset
                  << ", size: " << size << ", buf len: " << buf->size();
        return Status::OK();
    }

    Status remove(const std::string& key) override {
        std::string keyfile = key2file(key);
        fs::path local_path(keyfile);
        if (!fs::exists(local_path)) {
            return Status(ENOENT, "no such file %s", keyfile.c_str());
        }
        CHECK(fs::remove(local_path));
        LOG(INFO) << "remove object from local store success, key: " << key;
        return Status::OK();
    }

    std::string name() override { return "localstore"; }

    Status destroy() override { return Status::OK(); }

private:
    std::string key2file(const std::string& key) { return fmt::format("{}/{}/{}", FLAGS_test_data_path, name(), key); }
};

class StarCacheStore : public ObjectStore {
public:
    StarCacheStore() : _cache(new StarCache) {}

    Status init(void* options) override {
        check_and_create_dir(fmt::format("{}/{}", FLAGS_test_data_path, name()));
        auto cache_options = reinterpret_cast<CacheOptions*>(options);
        return _cache->init(*cache_options);
    }

    Status set(const std::string& key, IOBuf& buf) override { return _cache->set(key, buf); }

    Status read(const std::string& key, off_t offset, size_t size, IOBuf* buf) {
        return _cache->read(key, offset, size, buf);
    }

    Status remove(const std::string& key) override { return _cache->remove(key); }

    std::string name() override { return "starcachestore"; }

    Status destroy() override { return Status::OK(); }

private:
    std::shared_ptr<StarCache> _cache = nullptr;
};

class StarCacheTester;

static void read_thread_func(StarCacheTester* tester);
static void write_thread_func(StarCacheTester* tester);
static void remove_thread_func(StarCacheTester* tester);
static void update_ctx_thread_func(StarCacheTester* tester);

const std::string DUMPS_DIR = "dumps";

class StarCacheTester {
public:
    StarCacheTester() : _stopped(true), _obj_mutexes(FLAGS_obj_count_per_cycle) { _cur_dir = current_dir(); }

    std::shared_mutex& _obj_mutex(size_t obj_index) {
        uint64_t shard_count = _obj_mutexes.size();
        return _obj_mutexes[obj_index & (shard_count - 1)];
    }

    static size_t rand_less_than(size_t max) { return rand() % max; }

    static std::string parse_obj_file(const std::string& key) {
        auto slash_pos = key.rfind('/');
        CHECK_NE(slash_pos, std::string::npos) << "invalid object key: " << key;
        return key.substr(slash_pos + 1);
    }

    static size_t parse_obj_index(const std::string& key) {
        std::string obj_file = parse_obj_file(key);
        auto slash_pos = obj_file.rfind('_');
        CHECK_NE(slash_pos, std::string::npos) << "invalid object key: " << key;
        auto index_str = obj_file.substr(slash_pos + 1);
        return std::stoul(index_str);
    }

    static IOBuf gen_random_buf(size_t len) {
        static const char alphanum[] =
                "0123456789"
                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                "abcdefghijklmnopqrstuvwxyz";

        void* data = malloc(len);
        for (size_t i = 0; i < len; ++i) {
            ((char*)data)[i] = alphanum[rand_less_than(sizeof(alphanum))];
        }
        IOBuf tmp_buf;
        tmp_buf.append_user_data(data, len, nullptr);
        return tmp_buf;
    }

    static std::string current_dir() {
        struct tm stime;
        time_t now = time(nullptr);
        localtime_r(&now, &stime);

        char dirname[32] = {0};
        strftime(dirname, sizeof(dirname), "%Y-%m-%d_%H", &stime);
        return std::string(dirname);
    }

    std::string gen_obj_key(size_t obj_index) {
        return fmt::format("{}/{}_{}", _get_cur_dir(), FLAGS_object_key_prefix, obj_index);
    }

    void add_store_instance(ObjectStore* store) { _stores.push_back(store); }

    void write_store() {
        std::string key;
        while (!_stopped) {
            if (_objs_to_write.blocking_get(&key)) {
                std::string cur_dir = _get_cur_dir();
                if (key.substr(0, cur_dir.size()) != cur_dir) {
                    LOG(INFO) << "skip writing expired object " << key;
                    continue;
                }
                size_t obj_index = parse_obj_index(key);
                IOBuf buf = gen_random_buf(rand_less_than(FLAGS_max_obj_size));
                LOG(INFO) << "start write object " << key << ", buf size: " << buf.size();
                std::unique_lock<std::shared_mutex> wlck(_obj_mutex(obj_index));
                Status base_res = _stores[0]->set(key, buf);
                for (size_t i = 1; i < _stores.size(); ++i) {
                    Status res = _stores[i]->set(key, buf);
                    CHECK_EQ(res.error_code(), base_res.error_code())
                            << "unmatched write result, store index: " << i << ", key: " << key
                            << ", res: " << res.error_str() << ", base_res: " << base_res.error_str();
                }
                wlck.unlock();
                LOG(INFO) << "write object " << key << " finished, res: " << base_res.error_str();
            }
        }
        LOG(INFO) << "writing store loop stopped";
    }

    void read_store() {
        while (!_stopped) {
            size_t obj_index = rand_less_than(FLAGS_obj_count_per_cycle);
            std::string key = gen_obj_key(obj_index);
            size_t offset = rand_less_than(FLAGS_max_obj_size);
            size_t size = FLAGS_max_obj_size - offset;
            IOBuf base_buf;

            LOG(INFO) << "start read object " << key << ", offset: " << offset << ", size: " << size;
            std::shared_lock<std::shared_mutex> rlck(_obj_mutex(obj_index));
            Status base_res = _stores[0]->read(key, offset, size, &base_buf);
            for (size_t i = 1; i < _stores.size(); ++i) {
                IOBuf buf;
                Status res = _stores[i]->read(key, offset, size, &buf);
                CHECK_EQ(res.error_code(), base_res.error_code())
                        << "unmatched read result, store index: " << i << ", key: " << key
                        << ", res: " << res.error_str() << ", base_res: " << base_res.error_str();

                if (!res.ok()) {
                    continue;
                }
                if (buf != base_buf) {
                    std::string base_file = fmt::format("{}/{}/{}_{}_{}_{}", FLAGS_test_data_path, DUMPS_DIR,
                                                        _stores[0]->name(), parse_obj_file(key), offset, size);
                    std::string cur_file = fmt::format("{}/{}/{}_{}_{}_{}", FLAGS_test_data_path, DUMPS_DIR,
                                                       _stores[i]->name(), parse_obj_file(key), offset, size);
                    write_file(base_file, base_buf);
                    write_file(cur_file, buf);
                    CHECK(false) << "unmatched read data, store index: " << i << ", key: " << key
                                 << ", buf len: " << buf.length() << ", base buf len: " << base_buf.length();
                }
            }
            rlck.unlock();
            if (base_res.error_code() == ENOENT) {
                LOG(INFO) << "read an non-exist object " << key << ", try to write it to store";
                if (!_objs_to_write.put(key)) {
                    LOG(ERROR) << "blocking queue for objects to write has been shutdown";
                    break;
                }
            } else {
                LOG(INFO) << "read object " << key << " finished, res: " << base_res.error_str();
            }
        }
        LOG(INFO) << "reading store loop stopped";
    }

    void remove_store() {
        while (!_stopped) {
            size_t obj_index = rand_less_than(FLAGS_obj_count_per_cycle);
            std::string key = gen_obj_key(obj_index);
            std::unique_lock<std::shared_mutex> wlck(_obj_mutex(obj_index));
            Status base_res = _stores[0]->remove(key);
            for (size_t i = 1; i < _stores.size(); ++i) {
                LOG(INFO) << "start remove object " << key;
                Status res = _stores[i]->remove(key);
                CHECK_EQ(res.error_code(), base_res.error_code())
                        << "unmatched remove result, store index: " << i << ", key: " << key
                        << ", res: " << res.error_str() << ", base_res: " << base_res.error_str();
            }
            wlck.unlock();
            LOG(INFO) << "remove object " << key << " finished, res: " << base_res.error_str();
            std::this_thread::sleep_for(std::chrono::seconds(FLAGS_remove_interval_s));
        }
        LOG(INFO) << "reading store loop stopped";
    }

    void update_context() {
        int64_t remain_seconds = FLAGS_execution_time_s;
        while (!_stopped) {
            bool update = false;
            std::string cur_dir = _get_cur_dir();
            std::string dir = current_dir();
            if (dir != cur_dir) {
                _set_cur_dir(dir);
                update = true;
                LOG(INFO) << "update current directory from " << cur_dir << " to " << dir;
            }
            std::this_thread::sleep_for(std::chrono::seconds(FLAGS_update_context_interval_s));
            if (update) {
                auto dir_path = fmt::format("{}/{}/{}", FLAGS_test_data_path, _stores[0]->name(), cur_dir);
                fs::remove_all(dir_path);
                LOG(INFO) << "clean old working directory " << dir_path;
            }

            if (FLAGS_execution_time_s > 0) {
                remain_seconds -= FLAGS_update_context_interval_s;
                if (remain_seconds <= 0) {
                    _stopped = true;
                }
            }
        }
    }

    void init() {
        check_and_create_dir(FLAGS_test_data_path);
        check_and_create_dir(fmt::format("{}/{}", FLAGS_test_data_path, DUMPS_DIR));

        ObjectStore* local_store = new LocalStore;
        Status st = local_store->init(nullptr);
        CHECK(st.ok()) << "fail to init loal store, reason: " << st.error_str();
        add_store_instance(local_store);

        ObjectStore* starcache_store = new StarCacheStore;
        CacheOptions options;
        options.mem_quota_bytes = FLAGS_mem_cache_quota;
        std::stringstream ss(FLAGS_disk_cache_paths);
        std::string disk;
        while (std::getline(ss, disk, ';')) {
            std::string disk_path;
            std::string disk_quota;
            std::stringstream dss(disk);
            std::getline(dss, disk_path, ':');
            std::getline(dss, disk_quota, ':');
            fs::path local_path(disk_path);
            if (local_path.is_relative()) {
                disk_path = fmt::format("{}/{}/{}", FLAGS_test_data_path, starcache_store->name(), disk_path);
            }
            options.disk_dir_spaces.push_back({.path = disk_path, .quota_bytes = std::stoul(disk_quota) * GB});
        }
        st = starcache_store->init(&options);
        CHECK(st.ok()) << "fail to init starcache store, reason: " << st.error_str();
        add_store_instance(starcache_store);
    }

    void destroy() {
        for (auto& store : _stores) {
            store->destroy();
            delete store;
        }
        fs::path test_path(FLAGS_test_data_path);
        fs::remove_all(test_path);
    }

    void start() {
        _stopped = false;
        int64_t start_s = butil::monotonic_time_s();
        _write_thread = std::thread(write_thread_func, this);
        LOG(INFO) << "start write thread, tid: " << _write_thread.get_id();

        _remove_thread = std::thread(remove_thread_func, this);
        LOG(INFO) << "start remove thread, tid: " << _remove_thread.get_id();

        _update_ctx_thread = std::thread(update_ctx_thread_func, this);
        LOG(INFO) << "start update context thread, tid: " << _update_ctx_thread.get_id();

        _read_threads.reserve(FLAGS_read_thread_num);
        for (size_t i = 0; i < FLAGS_read_thread_num; ++i) {
            _read_threads.push_back(std::thread(read_thread_func, this));
            LOG(INFO) << "start read thread, index: " << i << ", tid: " << _read_threads[i].get_id();
        }

        _write_thread.join();
        _remove_thread.join();
        _update_ctx_thread.join();
        for (size_t i = 0; i < FLAGS_read_thread_num; ++i) {
            _read_threads[i].join();
            LOG(INFO) << "finish read thread, index: " << i << ", tid: " << _read_threads[i].get_id();
        }
        LOG(INFO) << "finish all tests, execution_time_s: " << butil::monotonic_time_s() - start_s;
    }

private:
    void _set_cur_dir(const std::string& dir) {
        std::unique_lock<std::shared_mutex> wlck;
        _cur_dir = dir;
    }

    std::string& _get_cur_dir() {
        std::shared_lock<std::shared_mutex> rlck;
        return _cur_dir;
    }

    std::vector<ObjectStore*> _stores;
    UnboundedBlockingQueue<std::string> _objs_to_write;
    std::string _cur_dir;
    std::atomic<bool> _stopped;

    std::thread _write_thread;
    std::thread _remove_thread;
    std::thread _update_ctx_thread;
    std::vector<std::thread> _read_threads;
    std::vector<std::shared_mutex> _obj_mutexes;
    std::shared_mutex _mutexe;
};

static void read_thread_func(StarCacheTester* tester) {
    tester->read_store();
}

static void write_thread_func(StarCacheTester* tester) {
    tester->write_store();
}

static void remove_thread_func(StarCacheTester* tester) {
    tester->remove_store();
}

static void update_ctx_thread_func(StarCacheTester* tester) {
    tester->update_context();
}

} // namespace starrocks::starcache

int main(int argc, char** argv) {
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    std::unique_ptr<starrocks::starcache::StarCacheTester> tester(new starrocks::starcache::StarCacheTester);
    tester->init();
    tester->start();
    tester->destroy();

    gflags::ShutDownCommandLineFlags();
    google::ShutdownGoogleLogging();
    return 0;
}
