#!/bin/bash

PARALLEL=$[$(nproc)/4+1]

curdir=`dirname "$0"`
curdir=`cd "$curdir"; pwd`

export STARCACHE_HOME=$curdir/..
export TP_DIR=$curdir

# thirdparties will be downloaded and unpacked here
export TP_SOURCE_DIR=$TP_DIR/src

# thirdparties will be installed to here
#
# If $INSTALL_DIR_PREFIX is provided, install to the location,
# otherwise install to "installed/" subdir in current location
export TP_INSTALL_DIR=${INSTALL_DIR_PREFIX:-$TP_DIR/installed}

# patches for all thirdparties
export TP_PATCH_DIR=$TP_DIR/patches

# header files of all thirdparties will be intalled to here
export TP_INCLUDE_DIR=$TP_INSTALL_DIR/include

# libraries of all thirdparties will be intalled to here
export TP_LIB_DIR=$TP_INSTALL_DIR/lib

mkdir -p $TP_INSTALL_DIR
mkdir -p $TP_INSTALL_DIR/include
mkdir -p $TP_INSTALL_DIR/lib

BUILD_SYSTEM=${BUILD_SYSTEM:-make}
CMAKE_CMD=cmake
CMAKE_GENERATOR="Unix Makefiles"

export CXXFLAGS="-O3 -fno-omit-frame-pointer -Wno-class-memaccess -fPIC -g -I${TP_INCLUDE_DIR}"
export CPPFLAGS=$CXXFLAGS
# https://stackoverflow.com/questions/42597685/storage-size-of-timespec-isnt-known
export CFLAGS="-O3 -fno-omit-frame-pointer -std=c99 -fPIC -g -D_POSIX_C_SOURCE=199309L -I${TP_INCLUDE_DIR}"
export LDFLAGS="-L${TP_LIB_DIR}"

# force use gcc/g++ to compile
export CC=$GCC_INSTALL_DIR/bin/gcc
export CXX=$GCC_INSTALL_DIR/bin/g++
export LD_LIBRARY_PATH="$GCC_INSTALL_DIR/lib:$GCC_INSTALL_DIR/lib64:$LD_LIBRARY_PATH"

# check md5sum exists
MD5_CMD=md5sum
if ! which $MD5_CMD >/dev/null 2>&1; then
    echo "Warn: $MD5_CMD is not installed"
    exit 1
fi

# check_checksum <filepath> <expected_checksum>
check_checksum()
{
    local filepath=$1
    local checksum=$2

    md5=`$MD5_CMD "$filepath" | awk '{print $1}'`
    if [ "$md5" != "$checksum" ] ; then
        echo "$filepath checksum check failed!"
        echo "expect-md5: $checksum"
        echo "actual-md5: $md5"
        return 1
    fi
    return 0
}

# check_if_done <name> <install_prefix>
check_if_done()
{
    local name=$1
    local installprefix=$2
    done_file="${name}_DONE"
    test -f "$installprefix/${!done_file}"
}

# check_todo_third_party <install_prefix> <archive1> <archive2> ... <archiveN>
# print to stdout: <todo1> <todo2> <todoN>
check_todo_third_party()
{
    local installprefix=$1
    shift

    local todo=""
    for tp in "$@"
    do
        if ! check_if_done $tp $installprefix >/dev/null ; then
            echo "Thirdparty $tp doesn't have its artifacts in $installprefix ..." >&2
            todo="$todo $tp"
        fi
    done
    echo "$todo"
    return 0
}


# download_archive <file_basename> <download_url> <dest_dir> <checksum>
download_archive()
{
    local filename=$1
    local download_url=$2
    local dest_dir=$3
    local checksum=$4
    local target_fullpath=$dest_dir/$filename

    if [ -z "$filename" ]; then
        echo "Error: No file name specified to download"
        exit 1
    fi
    if [ -z "$download_url" ]; then
        echo "Error: No download url specified for $filename"
        exit 1
    fi
    if [ -z "$dest_dir" ]; then
        echo "Error: No dest dir specified for $filename"
        exit 1
    fi

    if [ -f $target_fullpath ] ; then
        # file already exists, check its checksum
        if check_checksum $target_fullpath $checksum ; then
            return 0
        else
            echo "Exist $target_fullpath checksum mismatch, delete and re-download ..."
            rm -f $target_fullpath
        fi
    fi

    echo "Downloading $filename from $download_url to $dest_dir"
    if wget --tries=3 --read-timeout=60 --connect-timeout=15 --no-check-certificate $download_url -O $dest_dir/$filename ; then
        check_checksum $dest_dir/$filename $checksum
        return $?
    else
        echo "[ERROR] Failed to download $filename from $download_url!"
        return $ret
    fi
}

# download_all <source_dir> <archive1> <archive2> ... <archive-N>
download_all()
{
    local srcdir=$1
    shift
    local archives="$@"

    echo "===== Downloading thirdparty archives..."
    for tpname in $archives
    do
        name="${tpname}_NAME"
        checksum="${tpname}_MD5SUM"
        url="${tpname}_DOWNLOAD"
        if ! download_archive ${!name} ${!url} $srcdir ${!checksum} ; then
            # break the download process
            return 1
        fi
    done
    echo "===== Downloading thirdparty archives...done"
}


# unpack_download_archives <src_dir> <archive1> <archive2> ... <archive-N>
unpack_download_archives()
{
    local srcdir=$1
    shift
    local archives="$@"

    # unpacking thirdpart archives
    echo "===== Unpacking all thirdparty archives..."
    TAR_CMD="tar"
    UNZIP_CMD="unzip"
    SUFFIX_TGZ="\.(tar\.gz|tgz)$"
    SUFFIX_XZ="\.tar\.xz$"
    SUFFIX_ZIP="\.zip$"

    # temporary directory for unpacking
    # package is unpacked in tmp_dir and then renamed.
    mkdir -p $srcdir/tmp_dir
    for tp in $archives
    do
        name="${tp}_NAME"
        src="${tp}_SOURCE"

        if [ -z ${!src} ] ; then
            continue
        fi

        if [ ! -d $srcdir/${!src} ]; then
            echo "processing ${!name} ..."
            if [[ "${!name}" =~ $SUFFIX_TGZ  ]] ; then
                if ! $TAR_CMD xzf "$srcdir/${!name}" -C $srcdir/tmp_dir &>/dev/null ; then
                    echo "Failed to untar ${!name}"
                    exit 1
                fi
            elif [[ "${!name}" =~ $SUFFIX_XZ ]] ; then
                if ! $TAR_CMD xJf "$srcdir/${!name}" -C $srcdir/tmp_dir &>/dev/null ; then
                    echo "Failed to untar ${!name}"
                    exit 1
                fi
            elif [[ "${!name}" =~ $SUFFIX_ZIP ]] ; then
                if ! $UNZIP_CMD "$srcdir/${!name}" -d $srcdir/tmp_dir &>/dev/null ; then
                    echo "Failed to unzip ${!name}"
                    exit 1
                fi
            else
                echo "Nothing to do with ${!name} ..."
                continue
            fi
            mv $srcdir/tmp_dir/* $srcdir/${!src}
        else
            echo "${!src} already unpacked."
        fi
    done
    rm -r $srcdir/tmp_dir
    echo "===== Unpacking all thirdparty archives...done"
}

# build_zlib <srcdir> <install_prefix>
build_zlib()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    ./configure --prefix=$installprefix --static
    make -j$PARALLEL
    make install
    popd
}

# build_openssl <srcdir> <install_prefix>
build_openssl()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    # save build flags
    OLD_CXXFLAGS=$CXXFLAGS
    OLD_CPPFLAGS=$CPPFLAGS
    OLD_CFLAGS=$CFLAGS
    unset CXXFLAGS
    unset CPPFLAGS
    export CFLAGS="-O3 -fno-omit-frame-pointer -fPIC"
    OPENSSL_PLATFORM="linux-x86_64"
    if [[ "${MACHINE_TYPE}" == "aarch64" ]]; then
        OPENSSL_PLATFORM="linux-aarch64"
    fi

    LIBDIR="lib" ./Configure --prefix=$installprefix -zlib -no-shared ${OPENSSL_PLATFORM}
    make -j$PARALLEL
    make install_sw

    # recover build flags
    export CXXFLAGS=$OLD_CXXFLAGS
    export CPPFLAGS=$OLD_CPPFLAGS
    export CFLAGS=$OLD_CFLAGS

    popd
}

# build_curl <srcdir> <install_prefix>
build_curl()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    LIBS="-lcrypto -lssl -ldl" ./configure --prefix=$installprefix --disable-shared --enable-static \
            --without-librtmp --with-ssl=${installprefix} --without-libidn2 --without-libgsasl --disable-ldap --enable-ipv6 --without-zstd
    make -j$PARALLEL
    make install
    popd
}

# cmake_build <location_of_CMakeLists.txt> <cmake_options> ...
cmake_build()
{
    local srcdir=$1
    shift

    common_opts="
        -DCMAKE_BUILD_TYPE=RelWithDebInfo
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON
        -DBUILD_SHARED_LIBS=OFF
        -DBUILD_TESTING=OFF
        -DCMAKE_CXX_STANDARD=17
        -DCMAKE_INSTALL_LIBDIR=lib"

    local build_dir=build
    if [ -f $build_dir ] ; then
        # detect name confliction
        build_dir=tmp-build
    fi
    rm -rf $build_dir/ CMakeCache.tx
    #mkdir -p $build_dir
    echo $CMAKE_CMD $srcdir -B $build_dir -G "${CMAKE_GENERATOR}" $common_opts "$@"
    $CMAKE_CMD $srcdir -B $build_dir -G "${CMAKE_GENERATOR}" $common_opts "$@"

    pushd $build_dir &>/dev/null
    ${BUILD_SYSTEM} -j$PARALLEL
    ${BUILD_SYSTEM} install
    popd # build
}

# build_gflags <srcdir> <install_prefix>
build_gflags()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cmake_build . -DCMAKE_INSTALL_PREFIX=${installprefix}
    popd
}

# build_glog <srcdir> <install_prefix>
build_glog()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    local srcname=$(basename $(pwd))
    cmake_build . -DCMAKE_INSTALL_PREFIX=$installprefix -Dgflags_DIR=${installprefix}/lib/cmake/gflags/
    popd
}

# build_gtest <srcdir> <install_prefix>
build_gtest()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cmake_build . -DCMAKE_INSTALL_PREFIX=$installprefix
    popd
}

# build_protobuf <srcdir> <install_prefix>
build_protobuf()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cmake_build cmake/ -DCMAKE_INSTALL_PREFIX=${installprefix}  \
      		-Dprotobuf_BUILD_TESTS=OFF                           \
    		-DZLIB_LIBRARY_RELEASE=${installprefix}/lib/libz.a
    popd
}

# build_absl <srcdir> <install_prefix>
build_absl()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cmake_build . -DCMAKE_INSTALL_PREFIX=${installprefix}
    popd
}

build_bs_thread_pool()
{
    local srcdir=$1
    local installprefix=$2
    local installfiles="BS_thread_pool.hpp"
    for f in $installfiles
    do
        install -m 644 $srcdir/$f $installprefix/include/$f
    done
    return
}

build_leveldb()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    local srcname=$(basename $(pwd))
    if [[ $srcname == "leveldb-1.20" ]] ; then
        patch -p1 -f -i $TP_PATCH_DIR/leveldb-1.20-remove-snappy-dependency.patch || true
    fi

    LDFLAGS="-L ${installprefix}/lib -static-libstdc++ -static-libgcc" \
    make -j4
    cp out-static/libleveldb.a $installprefix/lib/libleveldb.a
    cp -r include/leveldb $installprefix/include/

    popd
}

build_brpc()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    local srcname=$(basename $(pwd))
    if [[ $srcname == "brpc-1.3.0" ]] ; then
        patch -p1 -f -i $TP_PATCH_DIR/brpc-1.3.0-cmake-patch.patch || true
    fi

    cmake_build . -DCMAKE_INSTALL_PREFIX=${installprefix}               \
            -DDOWNLOAD_GTEST=OFF                                        \
            -DBUILD_SHARED_LIBS=OFF                                     \
            -DBUILD_BRPC_TOOLS=OFF                                      \
            -DOPENSSL_ROOT_DIR=${installprefix}                         \
            -DOPENSSL_USE_STATIC_LIBS=TRUE                              \
            -DProtobuf_USE_STATIC_LIBS=ON                               \
            -DPROTOBUF_INCLUDE_DIR=${installprefix}/include             \
            -DPROTOBUF_LIBRARY=${installprefix}/lib/libprotobuf.a       \
            -DPROTOBUF_PROTOC_EXECUTABLE=${installprefix}/bin/protoc    \
            -DPROTOC_LIB=${installprefix}/lib/libprotoc.a               \
            -DGFLAGS_INCLUDE_PATH=${installprefix}/include              \
            -DGFLAGS_LIBRARY=${installprefix}/lib/libgflags.a           \
            -DGLOG_LIB=${installprefix}/lib/libglog.a                   \
            -DCMAKE_CXX_FLAGS=""                                        \
            -DWITH_GLOG=ON

    popd
}

build_fmt()
{
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cmake_build . -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX=${installprefix}  \
            -DFMT_TEST=OFF
    popd
}

# boost
build_boost() {
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    # It is difficult to generate static linked b2, so we use LD_LIBRARY_PATH instead
    ./bootstrap.sh --prefix=${installprefix}
    LD_LIBRARY_PATH=${STARCACHE_GCC_HOME}/lib:${STARCACHE_GCC_HOME}/lib64:${LD_LIBRARY_PATH} \
    ./b2 link=static runtime-link=static -j $PARALLEL --without-mpi --without-graph --without-graph_parallel --without-python cxxflags="-std=c++11 -g -fPIC" install
    popd
}

# rapidjson
build_rapidjson() {
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cp -r ${srcdir}/include/rapidjson ${installprefix}/include/
    popd
}

# phmap
build_phmap() {
    local srcdir=$1
    local installprefix=$2

    pushd $srcdir &>/dev/null
    cp -r ${srcdir}/parallel_hashmap ${installprefix}/include/
    popd
}

# build_and_install_archives <src_root_dir> <archive1> <archive2> ... <archive-N>
# * install to $TP_INSTALL_DIR
build_and_install_archives()
{
    local srcdir=$1
    local installprefix=$2
    shift 2
    local archives="$@"

    echo "===== build all thirdparty archives..."
    for tp in $archives
    do
        name="${tp}_NAME"
        src="${tp}_SOURCE"
        local lc_tp=`echo $tp | tr 'A-Z' 'a-z'`
        echo "Start build ${tp} ..."
        eval build_${lc_tp} $srcdir/${!src} $installprefix
        ret=$?
        if [[ "$ret" -ne 0 ]] ; then
            echo "[ERROR] build $tp failed!"
            exit 1
        fi
    done
    echo "===== build all thirdparty archives done"
}

main()
{
    # can use global variables in main
    local todo_list=`check_todo_third_party $TP_INSTALL_DIR $TP_ARCHIVES`
    if [[ "x$todo_list" = "x" ]] ; then
        echo "All Thirdparty build DONE!"
        return 0;
    else
        echo "Need to build following thirdparty modules: $todo_list"
    fi

    mkdir -p $TP_SOURCE_DIR
    download_all $TP_SOURCE_DIR $todo_list
    unpack_download_archives $TP_SOURCE_DIR $todo_list
    build_and_install_archives $TP_SOURCE_DIR $TP_INSTALL_DIR $todo_list
}

# break on any execution failure
set -e

. $TP_DIR/vars.sh

main
