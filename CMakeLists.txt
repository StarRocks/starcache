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

option(WITH_TESTS "Build the starcache test" ON)
option(WITH_TOOLS "Build the starcache tools" ON)
option(WITH_COVERAGE "Enable code coverage build" OFF)

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

# For CMAKE_BUILD_TYPE=Asan
SET(CXX_FLAGS_ASAN "-ggdb3 -O0 -gdwarf-4 -fsanitize=address -DADDRESS_SANITIZER")

# Set compile flags based on the build type.
if ("${CMAKE_BUILD_TYPE}" STREQUAL "DEBUG")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_DEBUG}")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "RELEASE")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_RELEASE}")
elseif ("${CMAKE_BUILD_TYPE}" STREQUAL "ASAN")
    SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} ${CXX_FLAGS_ASAN}")
else()
    message(FATAL_ERROR "Unknown build type: ${CMAKE_BUILD_TYPE}")
endif()

# Add flags that are common across build types
SET(CMAKE_CXX_FLAGS "${CXX_COMMON_FLAGS} ${CMAKE_CXX_FLAGS}")

message(STATUS "Compiler Flags: ${CMAKE_CXX_FLAGS}")

#### External Dependencies ####

FUNCTION(SEARCH_LIBRARY RESULT LIB_NAME)
    find_library(${RESULT} ${LIB_NAME} ${thirdparty_DIR}/lib)
    if(NOT ${RESULT})
        find_library(${RESULT} ${LIB_NAME} ${thirdparty_DIR}/lib64)
    endif()
ENDFUNCTION()

## PROTOBUF
SEARCH_LIBRARY(PROTOBUF_LIBPROTOBUF protobuf)

## GFLAGS
SEARCH_LIBRARY(GFLAGS_LIB gflags)

## GLOG
SEARCH_LIBRARY(GLOG_LIB glog)

## BRPC
SEARCH_LIBRARY(BRPC_LIB brpc)

## SSL
SEARCH_LIBRARY(SSL_LIB ssl)
SEARCH_LIBRARY(CRYPTO_LIB crypto)

## FMT
SEARCH_LIBRARY(FMT_LIB fmt)

## BOOST
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
include_directories(${thirdparty_DIR/include})

#### STARCACHE PROJECT ####
## GLOBAL VARAIBLES
set(STARCACHE_SRC_DIR ${CMAKE_CURRENT_LIST_DIR}/src)
set(STARCACHE_TEST_DIR ${CMAKE_CURRENT_LIST_DIR}/test)

include_directories(${STARCACHE_SRC_DIR})

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
# starcache_bench
#add_executable(cache_bench ${STARCACHE_SRC_DIR}/starcache_bench.cc)
#target_link_libraries(cache_bench starcache)

# tools
if(WITH_TOOLS)
    set(STARCACHE_TOOLS_SRCS
        ${STARCACHE_SRC_DIR}/tools/starcache_tester.cpp
    )
    add_executable(starcache_tester ${STARCACHE_TOOLS_SRCS})
    target_link_libraries(starcache_tester starcache)

    #add_executable(cache_bench ${STARCACHE_SRC_DIR}/starcache_bench.cc)
endif()

### TEST SECTION

if(WITH_TESTS)
    if(WITH_COVERAGE)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -fprofile-arcs -ftest-coverage")
    endif()
    unset(CXX_INCLUDES)
    find_package(GTest CONFIG REQUIRED)
    get_target_property(GTest_INCLUDE_DIR GTest::gtest_main INTERFACE_INCLUDE_DIRECTORIES)
    message(STATUS "Using GTest version: ${GTest_VERSION}")
    message(STATUS "  include: ${GTest_INCLUDE_DIR}")
    include_directories(${GTest_INCLUDE_DIR})

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
        GTest::gtest_main
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
