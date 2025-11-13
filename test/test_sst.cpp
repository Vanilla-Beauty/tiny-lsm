#include "../include/config/config.h"
#include "../include/consts.h"
#include "../include/logger/logger.h"
#include "../include/sst/sst.h"
#include "../include/sst/sst_iterator.h"
#include <filesystem>
#include <gtest/gtest.h>

using namespace ::tiny_lsm;

class SSTTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 创建测试目录
    if (!std::filesystem::exists("test_data")) {
      std::filesystem::create_directory("test_data");
    }
  }

  void TearDown() override {
    // 清理测试文件
    std::filesystem::remove_all("test_data");
  }

  // 辅助函数：创建一个包含有序数据的SST
  std::shared_ptr<SST> create_test_sst(size_t block_size, size_t num_entries) {
    SSTBuilder builder(block_size, true);

    for (size_t i = 0; i < num_entries; i++) {
      std::string key = "key" + std::to_string(i);
      std::string value = "value" + std::to_string(i);
      builder.add(key, value, 0);
    }

    auto block_cache = std::make_shared<BlockCache>(
        TomlConfig::getInstance().getLsmBlockCacheCapacity(),
        TomlConfig::getInstance().getLsmBlockCacheK());

    return builder.build(1, "test_data/test.sst", block_cache);
  }
};

// 测试基本的写入和读取
TEST_F(SSTTest, BasicWriteAndRead) {
  SSTBuilder builder(1024, true); // 1KB block size
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 添加一些数据
  builder.add("key1", "value1", 0);
  builder.add("key2", "value2", 0);
  builder.add("key3", "value3", 0);

  // 构建SST
  auto sst = builder.build(1, "test_data/basic.sst", block_cache);

  // 验证基本属性
  EXPECT_EQ(sst->get_first_key(), "key1");
  EXPECT_EQ(sst->get_last_key(), "key3");
  EXPECT_EQ(sst->get_sst_id(), 1);
  EXPECT_GT(sst->sst_size(), 0);

  // 读取并验证数据
  auto block = sst->read_block(0);
  EXPECT_TRUE(block != nullptr);
  auto value = block->get_value_binary("key2", 0);
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "value2");
}

// 测试block分裂
TEST_F(SSTTest, BlockSplitting) {
  // 使用小的block size强制分裂
  SSTBuilder builder(64, true); // 很小的block size
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 添加足够多的数据以触发分裂
  for (int i = 0; i < 10; i++) {
    std::string key = "key" + std::to_string(i);
    std::string value = std::string(20, 'v') + std::to_string(i); // 较大的value
    builder.add(key, value, 0);
  }

  auto sst = builder.build(1, "test_data/split.sst", block_cache);

  // 验证有多个block
  EXPECT_GT(sst->num_blocks(), 1);

  // 验证每个block都可以正确读取
  for (size_t i = 0; i < sst->num_blocks(); i++) {
    auto block = sst->read_block(i);
    EXPECT_TRUE(block != nullptr);
  }
}

// 测试key查找
TEST_F(SSTTest, KeySearch) {
  auto sst = create_test_sst(256, 100); // 创建包含100个entry的SST

  // 测试find_block_idx
  int64_t idx = sst->find_block_idx("key50");
  auto block = sst->read_block(idx);
  auto value = block->get_value_binary("key50", 0);
  EXPECT_TRUE(value.has_value());
  EXPECT_EQ(*value, "value50");

  // 测试边界情况
  EXPECT_EQ(sst->find_block_idx("key999"), -1);
}

// 测试元数据
TEST_F(SSTTest, Metadata) {
  auto sst = create_test_sst(512, 10);

  // 验证block数量
  EXPECT_GT(sst->num_blocks(), 0);

  // 验证首尾key
  EXPECT_EQ(sst->get_first_key(), "key0");
  EXPECT_EQ(sst->get_last_key(), "key9");
}

// 测试空SST构建
TEST_F(SSTTest, EmptySST) {
  SSTBuilder builder(1024, true);
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());
  EXPECT_THROW(builder.build(1, "test_data/empty.sst", block_cache),
               std::runtime_error);
}

// 测试SST重新打开
TEST_F(SSTTest, ReopenSST) {
  // 首先创建一个SST
  auto sst = create_test_sst(256, 10);
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 重新打开SST
  FileObj file = FileObj::open("test_data/test.sst", false);
  auto reopened_sst = SST::open(1, std::move(file), block_cache);

  // 验证数据一致性
  EXPECT_EQ(sst->get_first_key(), reopened_sst->get_first_key());
  EXPECT_EQ(sst->get_last_key(), reopened_sst->get_last_key());
  EXPECT_EQ(sst->num_blocks(), reopened_sst->num_blocks());
}

// 测试大文件
TEST_F(SSTTest, LargeSST) {
  SSTBuilder builder(4096, true); // 4KB blocks
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 添加大量数据
  for (int i = 0; i < 1000; i++) {
    // key格式：key000, key001, ..., key999
    std::string key = "key" + std::string(3 - std::to_string(i).length(), '0') +
                      std::to_string(i);

    // value格式：val000, val001, ..., val999
    std::string value = "val" +
                        std::string(3 - std::to_string(i).length(), '0') +
                        std::to_string(i);

    builder.add(key, value, 0);
  }

  auto sst = builder.build(1, "test_data/large.sst", block_cache);

  // 验证数据完整性
  EXPECT_GT(sst->num_blocks(), 1);
  EXPECT_EQ(sst->get_first_key(), "key000");
  EXPECT_EQ(sst->get_last_key(), "key999");

  // 随机访问一些key
  std::vector<int> test_indices = {0, 100, 500, 999};
  for (int i : test_indices) {
    std::string key = "key" + std::string(3 - std::to_string(i).length(), '0') +
                      std::to_string(i);
    int64_t idx = sst->find_block_idx(key);
    auto block = sst->read_block(idx);
    auto value = block->get_value_binary(key, 0);
    EXPECT_TRUE(value.has_value());

    // 构造期望的value
    std::string expected_value =
        "val" + std::string(3 - std::to_string(i).length(), '0') +
        std::to_string(i);
    EXPECT_EQ(*value, expected_value);
  }
}

TEST_F(SSTTest, LargeSSTPredicate) {
  SSTBuilder builder(4096, true); // 4KB blocks
  auto block_cache = std::make_shared<BlockCache>(
      TomlConfig::getInstance().getLsmBlockCacheCapacity(),
      TomlConfig::getInstance().getLsmBlockCacheK());

  // 添加大量数据
  for (int i = 0; i < 1000; i++) {
    // key格式：key000, key001, ..., key999
    std::string key = "key" + std::string(3 - std::to_string(i).length(), '0') +
                      std::to_string(i);

    // value格式：val000, val001, ..., val999
    std::string value = "val" +
                        std::string(3 - std::to_string(i).length(), '0') +
                        std::to_string(i);

    builder.add(key, value, 0);
  }

  auto sst = builder.build(1, "test_data/large.sst", block_cache);

  auto result =
      sst_iters_monotony_predicate(sst, 0, [](const std::string &key) {
        if (key < "key300") {
          return 1;
          ;
        }
        if (key > "key500") {
          return -1;
          ;
        }
        return 0;
        // return key >= "key300" && key <= "key500";
      });
  EXPECT_TRUE(result.has_value());
  auto [iter_begin, iter_end] = result.value();
  EXPECT_EQ(iter_begin.key(), "key300");
  for (int i = 0; i < 100; i++) {
    ++iter_begin;
  }
  EXPECT_EQ(iter_begin.key(), "key400");
  EXPECT_EQ(iter_end.key(), "key501");
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}