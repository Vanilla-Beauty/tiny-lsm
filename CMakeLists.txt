cmake_minimum_required(VERSION 3.16)

# 项目定义
project(tiny-lsm VERSION 0.0.1 LANGUAGES CXX)

# 设置 C++ 标准
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 设置构建类型
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# 添加编译选项
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Coverage 模式支持
option(ENABLE_COVERAGE "Enable code coverage" OFF)
if(ENABLE_COVERAGE)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} --coverage")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} --coverage")
endif()

# Debug 模式下添加宏定义
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_definitions(-DLSM_DEBUG)
endif()

# 包含目录
include_directories(include)

# 查找依赖包
find_package(PkgConfig REQUIRED)
find_package(GTest REQUIRED)
find_package(GMock QUIET)
if(NOT GMock_FOUND)
    # 如果找不到 GMock，尝试使用 GTest 的 GMock
    if(TARGET GTest::gmock)
        set(GMOCK_LIBRARIES GTest::gmock GTest::gmock_main)
    else()
        message(WARNING "GMock not found, some tests may not build")
        set(GMOCK_LIBRARIES "")
    endif()
else()
    set(GMOCK_LIBRARIES GMock::gmock GMock::gmock_main)
endif()

# Python 绑定是可选的
option(BUILD_PYTHON_BINDINGS "Build Python bindings" OFF)
if(BUILD_PYTHON_BINDINGS)
    find_package(pybind11 REQUIRED)
endif()

# 查找 spdlog
find_package(spdlog REQUIRED)

# 查找 asio (通常作为 header-only 库)
find_path(ASIO_INCLUDE_DIR asio.hpp PATHS /usr/include /usr/local/include)
if(NOT ASIO_INCLUDE_DIR)
    message(FATAL_ERROR "asio not found")
endif()

# toml11 是 header-only 库，尝试多种查找方式
find_path(TOML11_INCLUDE_DIR 
    NAMES toml.hpp toml11/toml.hpp
    PATHS 
        /usr/include 
        /usr/local/include 
        /opt/homebrew/include
        ${CMAKE_CURRENT_SOURCE_DIR}/third_party/toml11/include
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/toml11/include
)

if(NOT TOML11_INCLUDE_DIR)
    message(STATUS "toml11 not found in system paths, trying to find via pkg-config")
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
        pkg_check_modules(TOML11 QUIET toml11)
        if(TOML11_FOUND)
            set(TOML11_INCLUDE_DIR ${TOML11_INCLUDE_DIRS})
        endif()
    endif()
endif()

if(NOT TOML11_INCLUDE_DIR)
    message(WARNING "toml11 not found. You may need to install it or set TOML11_INCLUDE_DIR manually.")
    message(STATUS "On macOS with Homebrew: brew install toml11")
    message(STATUS "Or manually set: -DTOML11_INCLUDE_DIR=/path/to/toml11/include")
    message(FATAL_ERROR "toml11 is required but not found")
endif()

message(STATUS "Found toml11 at: ${TOML11_INCLUDE_DIR}")

# 添加包含目录
include_directories(${ASIO_INCLUDE_DIR})
include_directories(${TOML11_INCLUDE_DIR})

# 静态库目标

# logger 静态库
add_library(logger STATIC
    src/logger/logger.cpp
)
target_link_libraries(logger spdlog::spdlog)
target_include_directories(logger PUBLIC include)

# config 静态库
add_library(config STATIC
    src/config/config.cpp
)
target_link_libraries(config spdlog::spdlog)
target_include_directories(config PUBLIC include)

# utils 静态库
add_library(utils STATIC
    src/utils/bloom_filter.cpp
    src/utils/std_file.cpp
    src/utils/files.cpp
    src/utils/mmap_file.cpp
    src/utils/cursor.cpp
)
target_link_libraries(utils spdlog::spdlog)
target_include_directories(utils PUBLIC include)

# iterator 静态库
add_library(iterator STATIC
    src/iterator/iterator.cpp
)
target_link_libraries(iterator spdlog::spdlog)
target_include_directories(iterator PUBLIC include)

# skiplist 静态库
add_library(skiplist STATIC
    src/skiplist/skipList.cpp
)
target_link_libraries(skiplist spdlog::spdlog)
target_include_directories(skiplist PUBLIC include)

# block 静态库
add_library(block STATIC
    src/block/block.cpp
    src/block/block_cache.cpp
    src/block/block_iterator.cpp
    src/block/blockmeta.cpp
)
target_link_libraries(block config spdlog::spdlog)
target_include_directories(block PUBLIC include)

# sst 静态库
add_library(sst STATIC
    src/sst/sst.cpp
    src/sst/sst_iterator.cpp
    src/sst/concact_iterator.cpp
)
target_link_libraries(sst block utils iterator spdlog::spdlog)
target_include_directories(sst PUBLIC include)

# memtable 静态库
add_library(memtable STATIC
    src/memtable/memtable.cpp
)
target_link_libraries(memtable skiplist iterator config sst spdlog::spdlog)
target_include_directories(memtable PUBLIC include)

# wal 静态库
add_library(wal STATIC
    src/wal/wal.cpp
    src/wal/record.cpp
)
target_link_libraries(wal sst memtable spdlog::spdlog)
target_include_directories(wal PUBLIC include)

# lsm 静态库
add_library(lsm STATIC
    src/lsm/engine.cpp
    src/lsm/level_iterator.cpp
    src/lsm/transation.cpp
    src/lsm/two_merge_iterator.cpp
)
target_link_libraries(lsm sst memtable wal logger spdlog::spdlog)
target_include_directories(lsm PUBLIC include)

# redis 静态库
add_library(redis STATIC
    src/redis_wrapper/redis_wrapper.cpp
)
target_link_libraries(redis lsm spdlog::spdlog)
target_include_directories(redis PUBLIC include)

# 共享库目标
add_library(lsm_shared SHARED
    src/block/block.cpp
    src/block/block_cache.cpp
    src/block/block_iterator.cpp
    src/block/blockmeta.cpp
    src/config/config.cpp
    src/iterator/iterator.cpp
    src/logger/logger.cpp
    src/lsm/engine.cpp
    src/lsm/level_iterator.cpp
    src/lsm/transation.cpp
    src/lsm/two_merge_iterator.cpp
    src/memtable/memtable.cpp
    src/redis_wrapper/redis_wrapper.cpp
    src/skiplist/skipList.cpp
    src/sst/sst.cpp
    src/sst/sst_iterator.cpp
    src/sst/concact_iterator.cpp
    src/utils/bloom_filter.cpp
    src/utils/std_file.cpp
    src/utils/files.cpp
    src/utils/mmap_file.cpp
    src/utils/cursor.cpp
    src/wal/wal.cpp
    src/wal/record.cpp
)
target_link_libraries(lsm_shared spdlog::spdlog)
target_include_directories(lsm_shared PUBLIC include)
set_target_properties(lsm_shared PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# 测试目标
enable_testing()

# 测试宏
macro(add_test_target target_name source_file)
    add_executable(${target_name} ${source_file})
    target_link_libraries(${target_name} ${ARGN} GTest::gtest GTest::gtest_main spdlog::spdlog)
    target_include_directories(${target_name} PRIVATE include)
    add_test(NAME ${target_name} COMMAND ${target_name})
endmacro()

# 各种测试
add_test_target(test_config test/test_config.cpp logger config)
add_test_target(test_skiplist test/test_skiplist.cpp logger skiplist)
add_test_target(test_memtable test/test_memtable.cpp logger memtable)
add_test_target(test_block test/test_block.cpp logger block)
add_test_target(test_blockmeta test/test_blockmeta.cpp logger block)
add_test_target(test_utils test/test_utils.cpp logger utils)
add_test_target(test_sst test/test_sst.cpp logger sst)
add_test_target(test_lsm test/test_lsm.cpp logger lsm memtable iterator)
add_test_target(test_block_cache test/test_block_cache.cpp logger block)
add_test_target(test_compact test/test_compact.cpp logger lsm memtable iterator)
add_test_target(test_redis test/test_redis.cpp logger redis memtable iterator)
# test_wal 需要 GMock
if(GMOCK_LIBRARIES)
    add_executable(test_wal test/test_wal.cpp)
    target_link_libraries(test_wal logger wal lsm GTest::gtest GTest::gtest_main ${GMOCK_LIBRARIES} spdlog::spdlog)
    target_include_directories(test_wal PRIVATE include)
    add_test(NAME test_wal COMMAND test_wal)
else()
    message(WARNING "Skipping test_wal because GMock is not available")
endif()

# 示例程序
add_executable(example example/main.cpp)
target_link_libraries(example lsm_shared)
target_include_directories(example PUBLIC include)
set_target_properties(example PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

add_executable(debug example/debug.cpp)
target_link_libraries(debug lsm_shared)
target_include_directories(debug PUBLIC include)
set_target_properties(debug PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# 服务器程序
add_executable(server
    server/src/server.cpp
    server/src/handler.cpp
)
target_link_libraries(server redis)
target_include_directories(server PUBLIC include)
target_include_directories(server PRIVATE ${ASIO_INCLUDE_DIR})
set_target_properties(server PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Python 绑定（可选）
if(BUILD_PYTHON_BINDINGS)
    add_library(lsm_pybind SHARED
        sdk/lsm_pybind.cpp
    )
    target_link_libraries(lsm_pybind PRIVATE pybind11::module lsm_shared)
    target_include_directories(lsm_pybind PUBLIC include)
    target_compile_definitions(lsm_pybind PRIVATE 
        TINYLSM_EXPORT=__attribute__\(\(visibility\(\"default\"\)\)\)
    )
    target_compile_options(lsm_pybind PRIVATE -fvisibility=hidden)
    set_target_properties(lsm_pybind PROPERTIES
        PREFIX ""
        SUFFIX ".so"
        LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        LINK_FLAGS "-Wl,-rpath,$ORIGIN"
    )
    pybind11_extension(lsm_pybind)
endif()

# 安装规则
install(TARGETS lsm_shared
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
)

install(DIRECTORY include/ 
    DESTINATION include/tiny-lsm
    FILES_MATCHING PATTERN "*.h" PATTERN "*.hpp"
)

# 自定义目标：运行所有测试
set(ALL_TEST_TARGETS 
    test_config test_skiplist test_memtable test_block test_blockmeta 
    test_utils test_sst test_lsm test_block_cache test_compact 
    test_redis
)

# 如果 test_wal 构建了，添加到依赖列表
if(GMOCK_LIBRARIES)
    list(APPEND ALL_TEST_TARGETS test_wal)
endif()

add_custom_target(run-all-tests
    COMMENT "Running all tests"
    COMMAND ${CMAKE_CTEST_COMMAND} --verbose
    DEPENDS ${ALL_TEST_TARGETS}
)
