#!/bin/bash

# zlib
ZLIB_DOWNLOAD="https://github.com/madler/zlib/archive/refs/tags/v1.2.11.tar.gz"
ZLIB_NAME=zlib-1.2.11.tar.gz
ZLIB_SOURCE=zlib-1.2.11
ZLIB_MD5SUM="0095d2d2d1f3442ce1318336637b695f"
ZLIB_DONE="lib/libz.a;lib64/libz.a"

# openssl
OPENSSL_DOWNLOAD="https://github.com/openssl/openssl/archive/OpenSSL_1_1_1m.tar.gz"
OPENSSL_NAME=openssl-OpenSSL_1_1_1m.tar.gz
OPENSSL_SOURCE=openssl-OpenSSL_1_1_1m
OPENSSL_MD5SUM="710c2368d28f1a25ab92e25b5b9b11ec"
OPENSSL_DONE="lib/libssl.a;lib64/libssl.a"

# curl
CURL_DOWNLOAD="https://curl.se/download/curl-7.79.0.tar.gz"
CURL_NAME=curl-7.79.0.tar.gz
CURL_SOURCE=curl-7.79.0
CURL_MD5SUM="b40e4dc4bbc9e109c330556cd58c8ec8"
CURL_DONE="lib/libcurl.a;lib64/libcurl.a"

# gflags
GFLAGS_DOWNLOAD="https://github.com/gflags/gflags/archive/v2.2.2.tar.gz"
GFLAGS_NAME=gflags-2.2.2.tar.gz
GFLAGS_SOURCE=gflags-2.2.2
GFLAGS_MD5SUM="1a865b93bacfa963201af3f75b7bd64c"
GFLAGS_DONE="lib/libgflags.a;lib64/libgflags.a"

# glog
GLOG_DOWNLOAD="https://github.com/google/glog/archive/v0.4.0.tar.gz"
GLOG_NAME=glog-0.4.0.tar.gz
GLOG_SOURCE=glog-0.4.0
GLOG_MD5SUM="0daea8785e6df922d7887755c3d100d0"
GLOG_DONE="lib/libglog.a;lib64/libglog.a"

# gtest
GTEST_DOWNLOAD="https://github.com/google/googletest/archive/release-1.10.0.tar.gz"
GTEST_NAME=googletest-release-1.10.0.tar.gz
GTEST_SOURCE=googletest-release-1.10.0
GTEST_MD5SUM="ecd1fa65e7de707cd5c00bdac56022cd"
GTEST_DONE="lib/libgtest.a;lib64/libgtest.a"

# protobuf
PROTOBUF_DOWNLOAD="https://github.com/google/protobuf/archive/v3.14.0.tar.gz"
PROTOBUF_NAME=protobuf-3.14.0.tar.gz
PROTOBUF_SOURCE=protobuf-3.14.0
PROTOBUF_MD5SUM="0c9d2a96f3656ba7ef3b23b533fb6170"
PROTOBUF_DONE="lib/libprotobuf.a;lib64/libprotobuf.a"

# leveldb
LEVELDB_DOWNLOAD="https://github.com/google/leveldb/archive/v1.20.tar.gz"
LEVELDB_NAME=leveldb-1.20.tar.gz
LEVELDB_SOURCE=leveldb-1.20
LEVELDB_MD5SUM="298b5bddf12c675d6345784261302252"
LEVELDB_DONE="lib/libleveldb.a;lib64/libleveldb.a"

# brpc
BRPC_DOWNLOAD="https://github.com/apache/brpc/archive/refs/tags/1.3.0.tar.gz"
BRPC_NAME=brpc-1.3.0.tar.gz
BRPC_SOURCE=brpc-1.3.0
BRPC_MD5SUM="9470f1a77ec153e82cd8a25dc2148e47"
BRPC_DONE="lib/libbrpc.a;lib64/libbrpc.a"

# fmt
FMT_DOWNLOAD="https://github.com/fmtlib/fmt/releases/download/8.1.1/fmt-8.1.1.zip"
FMT_NAME="fmt-8.1.1.zip"
FMT_SOURCE="fmt-8.1.1"
FMT_MD5SUM="16dcd48ecc166f10162450bb28aabc87"
FMT_DONE="lib/libfmt.a;lib64/libfmt.a"

# boost
BOOST_DOWNLOAD="https://boostorg.jfrog.io/artifactory/main/release/1.80.0/source/boost_1_80_0.tar.gz"
BOOST_NAME=boost_1_80_0.tar.gz
BOOST_SOURCE=boost_1_80_0
BOOST_MD5SUM="077f074743ea7b0cb49c6ed43953ae95"
BOOST_DONE="lib/libboost_filesystem.a;lib64/libboost_filesystem.a"

# rapidjson
RAPIDJSON_DOWNLOAD="https://github.com/miloyip/rapidjson/archive/v1.1.0.tar.gz"
RAPIDJSON_NAME=rapidjson-1.1.0.tar.gz
RAPIDJSON_SOURCE=rapidjson-1.1.0
RAPIDJSON_MD5SUM="badd12c511e081fec6c89c43a7027bce"
RAPIDJSON_DONE="include/rapidjson/rapidjson.h"

TP_ARCHIVES="ZLIB OPENSSL CURL GFLAGS GLOG GTEST PROTOBUF LEVELDB BRPC FMT BOOST RAPIDJSON"
