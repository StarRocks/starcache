#!/usr/bin/env bash

# usually starcache
curdir=$(cd `dirname $0`; pwd)

if [[ -z ${STARCACHE_HOME}  ]]; then
    export STARCACHE_HOME=`cd "$curdir/.."; pwd`
fi

if [ -f ${STARCACHE_HOME}/env.sh ]; then
    . ${STARCACHE_HOME}/env.sh
fi

# Check args
usage() {
  echo "
Usage: $0 <options>
  Optional options:
     --with-tests       build starcache with unit tests
     --with-tools       build starcache with tools
     --clean            clean and build target

  Eg.
    $0                                           only build starcache library
    $0 --clean                                   clean and build starcache library
    $0 --with-tests                              build starcache library and tests
    $0 --with-tests --with-tools                 build starcache library, tests and tools
    BUILD_TYPE=build_type ./build.sh             build_type could be Release, Debug, or Asan. Default value is Release. To build starcache in Debug mode, you can execute: BUILD_TYPE=Debug ./build.sh)
  "
  exit 1
}

OPTS=$(getopt \
  -n $0 \
  -o '' \
  -o 'h' \
  -l 'with-tests' \
  -l 'with-tools' \
  -l 'clean' \
  -l 'help' \
  -- "$@")

if [ $? != 0 ] ; then
    usage
fi

eval set -- "$OPTS"

HELP=0
CLEAN=0
WITH_TESTS=OFF
WITH_TOOLS=OFF

if [ $# == 1 ] ; then
    # default
    CLEAN=0
else
    while true; do
        case "$1" in
            --clean) CLEAN=1 ; shift ;;
            --with-tests) WITH_TESTS=ON; shift ;;
            --with-tools) WITH_TOOLS=ON; shift ;;
            -h) HELP=1; shift ;;
            --help) HELP=1; shift ;;
            --) shift ;  break ;;
            *) echo "Internal error" ; exit 1 ;;
        esac
    done
fi

if [[ ${HELP} -eq 1 ]]; then
    usage
    exit
fi

CMAKE_BUILD_TYPE=${BUILD_TYPE:-Release}

echo "Get params:
    CMAKE_BUILD_TYPE    -- $CMAKE_BUILD_TYPE
    CLEAN               -- $CLEAN
    WITH_TESTS          -- $WITH_TESTS
    WITH_TOOLS          -- $WITH_TOOLS
"

# GCC is needed anyway even with clang
if [ -z "${STARCACHE_GCC_HOME}" ] ; then
    export STARCACHE_GCC_HOME=$(dirname `which gcc`)/..
fi

if [[ -z "$CC" || -z "$CXX" ]] ; then
    gcc_path=`which gcc 2>/dev/null`
    if [[ -n "${STARCACHE_GCC_HOME}" && "$gcc_path" != "${STARCACHE_GCC_HOME}/bin/gcc"  ]] ; then
        # ensure get the right gcc/g++
        export PATH=${STARCACHE_GCC_HOME}/bin:$PATH
        export LD_LIBRARY_PATH="${STARCACHE_GCC_HOME}/lib:$STARCACHE_GCC_HOME/lib64:$LD_LIBRARY_PATH"
    fi
    # force cmake use gcc/g++ instead of default cc/c++
    export CC=gcc
    export CXX=g++
fi

if [ -n "${STARCACHE_CMAKE_HOME}" ] ; then
    export PATH=${STARCACHE_CMAKE_HOME}/bin:$PATH
    echo "cmake version: $(cmake --version)"
    echo "cmake path: $(which cmake)"
fi

if [ -z "${INSTALL_DIR_PREFIX}" ]; then
    INSTALL_DIR_PREFIX=${STARCACHE_HOME}/third_party/installed
fi

# default installed directory structure
#
#  $INSTALL_DIR_PREFIX/
#      ├── starcache_installed  # STARCACHE_INSTALLED
#      └── third_party          # $STARCACHE_THIRDPARTY
#          ├── bin
#          ├── include
#          ├── lib
#          └── lib64

if [ -z "${STARCACHE_THIRDPARTY}" ] ; then
    echo
    echo "NOTE: \$STARCACHE_THIRDPARTY is not set. If you have prebuilt libraries, "
    echo "  set STARCACHE_THIRDPARTY env variable to the path with the following correct layout,"
    echo "  * include_dir: \$STARCACHE_THIRDPARTY/include/"
    echo "  * lib_dir:     \$STARCACHE_THIRDPARTY/lib/"
    echo
    THIRD_PARTY_INSTALL_PREFIX=$INSTALL_DIR_PREFIX/third_party/
else
    THIRD_PARTY_INSTALL_PREFIX=${STARCACHE_THIRDPARTY}
fi

PARALLEL=${PARALLEL:-$[$(nproc)/4+1]}

# external depdendencies should be added to third-party/build-thirdparty.sh
pushd third_party &>/dev/null
# temporary overwrite INSTALL_DIR_PREFIX variable
OLD_INSTALL_DIR_PREFIX=$INSTALL_DIR_PREFIX
INSTALL_DIR_PREFIX=$THIRD_PARTY_INSTALL_PREFIX ./build-thirdparty.sh || exit 1
INSTALL_DIR_PREFIX=$OLD_INSTALL_DIR_PREFIX
popd

CMAKE_BUILD_DIR=${STARCACHE_HOME}/build/build_${CMAKE_BUILD_TYPE}

if [ -z "${STARCACHE_INSTALL_DIR}" ] ; then
    STARCACHE_INSTALL_DIR=$INSTALL_DIR_PREFIX/starcache_installed
fi

if [ ${CLEAN} -eq 1  ]; then
    rm -rf ${CMAKE_BUILD_DIR}
fi

mkdir -p ${CMAKE_BUILD_DIR}
mkdir -p ${STARCACHE_INSTALL_DIR}

cmake -B ${CMAKE_BUILD_DIR} -DCMAKE_CXX_COMPILER_LAUNCHER=ccache                                \
	  -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE} 													\
	  -DWITH_TESTS=${WITH_TESTS} 																\
	  -DWITH_TOOLS=${WITH_TOOLS} 																\
	  -DWITH_COVERAGE=OFF																		\
      -DOPENSSL_ROOT_DIR=${THIRD_PARTY_INSTALL_PREFIX}                                          \
      -DOPENSSL_USE_STATIC_LIBS=TRUE                                                            \
      -DGTest_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib/cmake/GTest                                 \
      -DGFLAGS_LIB_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib                                        \
      -DGLOG_LIB_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib                                          \
      -DPROTOBUF_LIB_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib                                      \
      -DBRPC_LIB_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib                                          \
      -Dfmt_DIR=${THIRD_PARTY_INSTALL_PREFIX}/lib/cmake/fmt/                                    \
      -DBOOST_ROOT=${THIRD_PARTY_INSTALL_PREFIX}                                                \
      -Dthirdparty_DIR=${THIRD_PARTY_INSTALL_PREFIX}/                                           \
      ${STARCACHE_TEST_COVERAGE:+"-Dstarcache_BUILD_COVERAGE=$STARCACHE_TEST_COVERAGE"}         \
      -DCMAKE_INSTALL_PREFIX=${STARCACHE_INSTALL_DIR}                                           \
      .

      #-DBoost_LIBRARYDIR=${THIRD_PARTY_INSTALL_PREFIX}/lib                                     \

cd ${CMAKE_BUILD_DIR}
echo make -j $PARALLEL
make -j $PARALLEL
make install -j $PARALLEL
