# Set the minimum version of CMake that can be used
#
# To find the cmake version run
# $ cmake --version
# In order to get aws-sdk-cpp 1.9.179 build successful, need to set cmake version to 3.22
# cmake_minimum_required(VERSION 3.22)
cmake_minimum_required(VERSION 3.5.1)

# Project settings
project(starcache)
set(PACKAGE_VERSION "0.0.1")
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
# Turn on -fPIC by default for both static and shared library
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if (CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    if (DEFINED ENV{GCC_INSTALL_DIR})
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --gcc-toolchain=$ENV{GCC_INSTALL_DIR}")
    else()
        message(WARNING "GCC_INSTALL_DIR evnironment variable is not set!")
    endif()
endif()

option(WITH_TESTS "Build the starcache test" OFF)
option(WITH_TOOLS "Build the starcache tools" OFF)
option(WITH_COVERAGE "Enable code coverage build" OFF)
option(FIND_DEFAULT_PATH "Whether to find the library in default system library path" OFF)

# set CMAKE_BUILD_TYPE
if (NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RELEASE)
endif()

string(TOUPPER ${CMAKE_BUILD_TYPE} CMAKE_BUILD_TYPE)
message(STATUS "Build type is ${CMAKE_BUILD_TYPE}")

# support to pass cxx flags from environment.
set(CXX_COMMON_FLAGS "${CXX_COMMON_FLAGS} $ENV{STARCACHE_CXX_COMMON_FLAGS}")

# For CMAKE_BUILD_TYPE=Debug
#   -ggdb: Enable gdb debugging
# Debug information is stored as dwarf2 to be as compatible as possible
#   -Werror: compile warnings should be errors when using the toolchain compiler.
# Only enable for debug builds because this is what we test in pre-commit tests.
set(CXX_FLAGS_DEBUG "-ggdb -O0 -gdwarf-4 -DDEBUG")

# For CMAKE_BUILD_TYPE=Release
#   -O3: Enable all compiler optimizations
#   -DNDEBUG: Turn off dchecks/asserts/debug only code.
#   -gdwarf-4: Debug information is stored as dwarf2 to be as compatible as possible
set(CXX_FLAGS_RELEASE "-O3 -gdwarf-4 -DNDEBUG")

# For CMAKE_BUILD_TYPE=RelWithDebInfo
set(CXX_FLAGS_RELWITHDEBINFO "-O2 -ggdb -gdwarf-4 -DNDEBUG")

# For CMAKE_BUILD_TYPE=Asan
SET(CXX_FLAGS_ASAN "-ggdb3 -O0 -gdwarf-4 -fsanitize=address -DADDRESS_SANITIZER")

# Set compile flags based on the build type.
if ("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_DEBUG}")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_RELEASE}")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "ASAN")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_ASAN}")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RELWITHDEBINFO")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_RELWITHDEBINFO}")
else()
    message(FATAL_ERROR "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()

# Add flags that are common across build types
SET(CMAKE_CXX_FLAGS "${CXX_COMMON_FLAGS} ${CMAKE_CXX_FLAGS}")

message(STATUS "Compiler Flags: ${CMAKE_CXX_FLAGS}")

if ("${STARCACHE_INSTALL_DIR}" STREQUAL "")
    set(STARCACHE_INSTALL_DIR ${CMAKE_CURRENT_LIST_DIR}/installed)
endif()
set(CMAKE_INSTALL_PREFIX ${STARCACHE_INSTALL_DIR})
set(CMAKE_INSTALL_LIBDIR ${CMAKE_INSTALL_PREFIX}/lib)
set(CMAKE_INSTALL_INCLUDEDIR ${CMAKE_INSTALL_PREFIX}/include)

#### External Dependencies ####

FUNCTION(FIND_LIBRARY_IN_PATH RESULT LIB_FILE LIB_NAME LIB_PATH)
    if(FIND_DEFAULT_PATH)
        find_library(${RESULT} NAMES ${LIB_FILE} ${LIB_NAME} PATHS ${LIB_PATH}/lib)
    else()
        find_library(${RESULT} NAMES ${LIB_FILE} ${LIB_NAME} PATHS ${LIB_PATH}/lib NO_DEFAULT_PATH)
    endif()
    if (NOT ${RESULT})
        if(FIND_DEFAULT_PATH)
            find_library(${RESULT} NAMES ${LIB_FILE} ${LIB_NAME} PATHS ${LIB_PATH}/lib64)
        else()
            find_library(${RESULT} NAMES ${LIB_FILE} ${LIB_NAME} PATHS ${LIB_PATH}/lib64 NO_DEFAULT_PATH)
        endif()
    endif()
ENDFUNCTION()

FUNCTION(SEARCH_LIBRARY RESULT LIB_FILE LIB_NAME LIB_ROOT)
    if ("${LIB_ROOT}" STREQUAL "")
        set(SEARCH_PATH ${STARCACHE_THIRDPARTY_DIR})
    else()
        set(SEARCH_PATH ${LIB_ROOT})
    endif()

    FIND_LIBRARY_IN_PATH(${RESULT} ${LIB_FILE} ${LIB_NAME} ${SEARCH_PATH})
    if (${RESULT})
        message(STATUS "find library ${LIB_NAME}")
        if ((NOT "${LIB_ROOT}" STREQUAL "") AND (EXISTS "${LIB_ROOT}/include"))
            include_directories(${LIB_ROOT}/include)
        endif()
    else()
        message(ERROR "cannot find library ${LIB_NAME}")
    endif()
ENDFUNCTION()

## PROTOBUF
SEARCH_LIBRARY(PROTOBUF_LIBPROTOBUF libprotobuf.a protobuf "${PROTOBUF_ROOT}")

## GFLAGS
SEARCH_LIBRARY(GFLAGS_LIB libgflags.a gflags "${GFLAGS_ROOT}")

## GLOG
SEARCH_LIBRARY(GLOG_LIB libglog.a glog "${GLOG_ROOT}")

## BRPC
SEARCH_LIBRARY(BRPC_LIB libbrpc.a brpc "${BRPC_ROOT}")

## SSL
SEARCH_LIBRARY(SSL_LIB libssl.a ssl "${SSL_ROOT}")
SEARCH_LIBRARY(CRYPTO_LIB libcrypto.a crypto "${SSL_ROOT}")

## FMT
SEARCH_LIBRARY(FMT_LIB libfmt.a fmt "${FMT_ROOT}")

## BOOST
if ("${BOOST_ROOT}" STREQUAL "")
    set(BOOST_ROOT ${STARCACHE_THIRDPARTY_DIR})
endif()

set(Boost_USE_STATIC_LIBS ON)
set(Boost_USE_STATIC_RUNTIME ON)
set(Boost_DEBUG FALSE)
set(Boost_USE_MULTITHREADED ON)
set(Boost_NO_BOOST_CMAKE ON)
# boost suppress warning is supported on cmake 3.20
# https://cmake.org/cmake/help/latest/module/FindBoost.html
set(Boost_NO_WARN_NEW_VERSIONS ON)
find_package(Boost 1.75.0 REQUIRED COMPONENTS thread regex program_options filesystem context)
message(STATUS "  include: ${Boost_INCLUDE_DIRS}")
message(STATUS "  libs: ${Boost_LIBRARIES}")
include_directories(${Boost_INCLUDE_DIRS})
set(BOOST_LIB ${Boost_LIBRARIES})

## include third_party/include
message(STATUS "STARCACHE_THIRDPARTY_DIR: ${STARCACHE_THIRDPARTY_DIR}")
include_directories(${STARCACHE_THIRDPARTY_DIR}/include)

#### STARCACHE PROJECT ####
## GLOBAL VARAIBLES
set(STARCACHE_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(STARCACHE_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/test)

include_directories(BEFORE ${STARCACHE_SRC_DIR})

## Common
set(STARCACHE_COMMON_SRCS
    ${STARCACHE_SRC_DIR}/common/config.cpp
)
add_library(starcache_common_objs OBJECT ${STARCACHE_COMMON_SRCS})

## Utils
set(STARCACHE_UTILS_SRCS
    ${STARCACHE_SRC_DIR}/utils/murmur_hash3.cpp
    ${STARCACHE_SRC_DIR}/utils/lru_container.cpp
)
add_library(starcache_utils_objs OBJECT ${STARCACHE_UTILS_SRCS})

## StarCache core source 
set(STARCACHE_CORE_SRCS
    ${STARCACHE_SRC_DIR}/aux_funcs.cpp
    ${STARCACHE_SRC_DIR}/block_file.cpp
    ${STARCACHE_SRC_DIR}/disk_cache.cpp
    ${STARCACHE_SRC_DIR}/disk_space_manager.cpp
    ${STARCACHE_SRC_DIR}/hashtable_access_index.cpp
    ${STARCACHE_SRC_DIR}/mem_cache.cpp
    ${STARCACHE_SRC_DIR}/mem_space_manager.cpp
    ${STARCACHE_SRC_DIR}/size_based_admission_policy.cpp
    ${STARCACHE_SRC_DIR}/capacity_based_promotion_policy.cpp
    ${STARCACHE_SRC_DIR}/star_cache.cpp
    ${STARCACHE_SRC_DIR}/star_cache_impl.cpp
)
add_library(starcache_core_objs OBJECT ${STARCACHE_CORE_SRCS})

### Build target libraries

## libstarcache.a
add_library(starcache STATIC
    $<TARGET_OBJECTS:starcache_common_objs>
    $<TARGET_OBJECTS:starcache_utils_objs>
    $<TARGET_OBJECTS:starcache_core_objs>
)

set(STARCACHE_BASIC_LIBS
    -lbfd -liberty -lc -lm -ldl -rdynamic -pthread -static-libstdc++ -static-libgcc
)

if ("${CMAKE_BUILD_TYPE}" STREQUAL "ASAN")
    set(STARCACHE_BASIC_LIBS "${STARCACHE_BASIC_LIBS} -static-libasan")
endif()

target_link_libraries(starcache
    -Wl,--start-group
    ${STARCACHE_BASIC_LIBS}
    ${PROTOBUF_LIBPROTOBUF}
    ${SSL_LIB}
    ${CRYPTO_LIB}
    ${BOOST_LIB}
    ${GLOG_LIB}
    ${GFLAGS_LIB}
    ${BRPC_LIB}
    ${FMT_LIB}
    -Wl,--end-group
)

### BINARY EXECUTABLE

# tools
if(WITH_TOOLS)
    set(STARCACHE_TOOLS_SRCS
        ${STARCACHE_SRC_DIR}/tools/starcache_tester.cpp
    )
    add_executable(starcache_tester ${STARCACHE_TOOLS_SRCS})
    target_link_libraries(starcache_tester starcache)
endif()

### TEST SECTION

if(WITH_TESTS)
    if(WITH_COVERAGE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fprofile-arcs -ftest-coverage")
    endif()
    unset(CXX_INCLUDES)

    SEARCH_LIBRARY(GTEST_LIB libgtest.a gtest "${GTEST_ROOT}")

    # TEST LIBRARY
    set(STARCACHE_TEST_SRCS
        ${STARCACHE_TEST_DIR}/star_cache_test.cpp
    )

    add_definitions(-DUNIT_TEST)
    add_executable(starcache_test ${STARCACHE_TEST_SRCS})
    # Allow includes from source file location for testing
    target_include_directories(starcache_test PRIVATE ${STARCACHE_SRC_DIR})

    target_link_libraries(starcache_test
        starcache
        ${GTEST_LIB}
    )
endif()

### INSTALLATION SECTION

# include CMAKE_INSTALL_INCLUDEDIR definitions
include(GNUInstallDirs)

## install include files
install(
    DIRECTORY ${STARCACHE_SRC_DIR}/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/starcache
    FILES_MATCHING PATTERN "*.h*"
)

# install libraries
install(
    TARGETS ${PROJECT_NAME}
        starcache
    DESTINATION ${CMAKE_INSTALL_LIBDIR}
    EXPORT ${PROJECT_NAME}
)

include(CMakePackageConfigHelpers)
write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfigVersion.cmake
    VERSION 0.0.1
    COMPATIBILITY SameMajorVersion
)

set(STARCACHE_CMAKE_DIR ${CMAKE_INSTALL_LIBDIR}/cmake/starcache)

configure_package_config_file(
    ${PROJECT_SOURCE_DIR}/cmake/starcacheConfig.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfig.cmake
    INSTALL_DESTINATION ${STARCACHE_CMAKE_DIR}
    PATH_VARS CMAKE_INSTALL_INCLUDEDIR CMAKE_INSTALL_LIBDIR
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfig.cmake
              ${CMAKE_CURRENT_BINARY_DIR}/starcacheConfigVersion.cmake
              DESTINATION ${STARCACHE_CMAKE_DIR}
)
