#include "block/block.h"
#include "block/block_iterator.h"
#include "config/config.h"
#include "logger/logger.h"
#include <gtest/gtest.h>
#include <iomanip>
#include <memory>
#include <vector>

using namespace ::tiny_lsm;

class BlockTest : public ::testing::Test {
protected:
  // 预定义的编码数据
  std::vector<uint8_t> getEncodedBlock() {
    /*
    Block layout (3 entries):
    Entry1: key="apple", value="red"
    Entry2: key="banana", value="yellow"
    Entry3: key="orange", value="orange"
    */
    std::vector<uint8_t> encoded = {
        // Data Section
        // Entry 1: "apple" -> "red"
        5, 0,                    // key_len = 5
        'a', 'p', 'p', 'l', 'e', // key
        3, 0,                    // value_len = 3
        'r', 'e', 'd',           // value
        1, 0, 0, 0, 0, 0, 0, 0,  // tranc_id = 1

        // Entry 2: "banana" -> "yellow"
        6, 0,                         // key_len = 6
        'b', 'a', 'n', 'a', 'n', 'a', // key
        6, 0,                         // value_len = 6
        'y', 'e', 'l', 'l', 'o', 'w', // value
        2, 0, 0, 0, 0, 0, 0, 0,       // tranc_id = 2

        // Entry 3: "orange" -> "orange3"
        6, 0,                              // key_len = 6
        'o', 'r', 'a', 'n', 'g', 'e',      // key
        7, 0,                              // value_len = 6
        'o', 'r', 'a', 'n', 'g', 'e', '3', // value
        3, 0, 0, 0, 0, 0, 0, 0,            // tranc_id = 3

        // Entry 4: "orange" -> "orange2"
        6, 0,                              // key_len = 6
        'o', 'r', 'a', 'n', 'g', 'e',      // key
        7, 0,                              // value_len = 6
        'o', 'r', 'a', 'n', 'g', 'e', '2', // value
        2, 0, 0, 0, 0, 0, 0, 0,            // tranc_id = 2

        // Entry 5: "orange" -> "orange1"
        6, 0,                              // key_len = 6
        'o', 'r', 'a', 'n', 'g', 'e',      // key
        7, 0,                              // value_len = 6
        'o', 'r', 'a', 'n', 'g', 'e', '1', // value
        1, 0, 0, 0, 0, 0, 0, 0,            // tranc_id = 1

        // Offset Section (每个entry的起始位置)
        0, 0,  // offset[0] = 0
        20, 0, // offset[1] = 12 (第二个entry的起始位置)
        44, 0, // offset[2] = 24 (第三个entry的起始位置)
        69, 0, // offset[3] = 36 (第四个entry的起始位置)
        94, 0, // offset[4] = 48 (第五个entry的起始位置)

        // Num of elements
        5, 0 // num_elements = 5
    };
    return encoded;
  }
};

// 测试解码
TEST_F(BlockTest, DecodeTest) {
  auto encoded = getEncodedBlock();
  auto block = Block::decode(encoded,false);

  // 验证第一个key
  EXPECT_EQ(block->get_first_key(), "apple");

  // 验证所有key-value对
  EXPECT_EQ(block->get_value_binary("apple", 0).value(), "red");
  EXPECT_EQ(block->get_value_binary("banana", 0).value(), "yellow");
  EXPECT_EQ(block->get_value_binary("orange", 0).value(), "orange3");

  // 指定事务id查询
  EXPECT_EQ(block->get_value_binary("orange", 1).value(), "orange1");
  EXPECT_EQ(block->get_value_binary("orange", 2).value(), "orange2");
  EXPECT_EQ(block->get_value_binary("orange", 3).value(), "orange3");
}

// 测试编码
TEST_F(BlockTest, EncodeTest) {
  Block block(1024);
  block.add_entry("apple", "red", 1, false);
  block.add_entry("banana", "yellow", 2, false);
  block.add_entry("orange", "orange3", 3, false);
  block.add_entry("orange", "orange2", 2, false);
  block.add_entry("orange", "orange1", 1, false);

  auto encoded = block.encode();

  // 解码并验证
  auto decoded = Block::decode(encoded);
  EXPECT_EQ(decoded->get_value_binary("apple", 1).value(), "red");
  EXPECT_EQ(decoded->get_value_binary("banana", 2).value(), "yellow");
  EXPECT_EQ(decoded->get_value_binary("orange", 0).value(), "orange3");

  // 指定事务id查询
  EXPECT_EQ(decoded->get_value_binary("orange", 1).value(), "orange1");
  EXPECT_EQ(decoded->get_value_binary("orange", 2).value(), "orange2");
  EXPECT_EQ(decoded->get_value_binary("orange", 3).value(), "orange3");
}

// 测试二分查找
TEST_F(BlockTest, BinarySearchTest) {
  Block block(1024);
  block.add_entry("apple", "red", 0, false);
  block.add_entry("banana", "yellow", 0, false);
  block.add_entry("orange", "orange", 0, false);

  // 测试存在的key
  EXPECT_EQ(block.get_value_binary("apple", 0).value(), "red");
  EXPECT_EQ(block.get_value_binary("banana", 0).value(), "yellow");
  EXPECT_EQ(block.get_value_binary("orange", 0).value(), "orange");

  // 测试不存在的key
  EXPECT_FALSE(block.get_value_binary("grape", 0).has_value());
  EXPECT_FALSE(block.get_value_binary("", 0).has_value());
}

// 测试边界情况
TEST_F(BlockTest, EdgeCasesTest) {
  Block block(1024);

  // 空block
  EXPECT_EQ(block.get_first_key(), "");
  EXPECT_FALSE(block.get_value_binary("any", 0).has_value());

  // 添加空key和value
  block.add_entry("", "", 0, false);
  EXPECT_EQ(block.get_first_key(), "");
  EXPECT_EQ(block.get_value_binary("", 0).value(), "");

  // 添加包含特殊字符的key和value
  block.add_entry("key\0with\tnull", "value\rwith\nnull", 0, false);
  std::string special_key("key\0with\tnull");
  std::string special_value("value\rwith\nnull");
  EXPECT_EQ(block.get_value_binary(special_key, 0).value(), special_value);
}

// 测试大数据量
TEST_F(BlockTest, LargeDataTest) {
  Block block(1024 * 32);
  const int n = 1000;

  // 添加大量数据
  for (int i = 0; i < n; i++) {
    // 使用 std::format 或 sprintf 进行补零
    char key_buf[16];
    snprintf(key_buf, sizeof(key_buf), "key%03d", i); // 补零到3位
    std::string key = key_buf;

    char value_buf[16];
    snprintf(value_buf, sizeof(value_buf), "value%03d", i);
    std::string value = value_buf;

    block.add_entry(key, value, 0, false);
  }

  // 验证所有数据
  for (int i = 0; i < n; i++) {
    char key_buf[16];
    snprintf(key_buf, sizeof(key_buf), "key%03d", i);
    std::string key = key_buf;

    char value_buf[16];
    snprintf(value_buf, sizeof(value_buf), "value%03d", i);
    std::string expected_value = value_buf;

    EXPECT_EQ(block.get_value_binary(key, 0).value(), expected_value);
  }
}

// 测试错误处理
TEST_F(BlockTest, ErrorHandlingTest) {
  // 测试解码无效数据
  std::vector<uint8_t> invalid_data = {1}; // 太短
  EXPECT_THROW(Block::decode(invalid_data), std::runtime_error);

  // 测试空vector
  std::vector<uint8_t> empty_data;
  EXPECT_THROW(Block::decode(empty_data), std::runtime_error);
}

// 测试迭代器
TEST_F(BlockTest, IteratorTest) {
  // 使用 make_shared 创建 Block
  auto block = std::make_shared<Block>(4096);

  // 1. 测试空block的迭代器
  EXPECT_EQ(block->begin(), block->end());

  // 2. 添加有序数据
  const int n = 100;
  std::vector<std::pair<std::string, std::string>> test_data;

  for (int i = 0; i < n; i++) {
    char key_buf[16], value_buf[16];
    snprintf(key_buf, sizeof(key_buf), "key%03d", i);
    snprintf(value_buf, sizeof(value_buf), "value%03d", i);

    block->add_entry(key_buf, value_buf, 0, false);
    test_data.emplace_back(key_buf, value_buf);
  }

  // 3. 测试正向遍历和数据正确性
  size_t count = 0;
  for (const auto &[key, value] : *block) { // 注意这里使用 *block
    EXPECT_EQ(key, test_data[count].first);
    EXPECT_EQ(value, test_data[count].second);
    count++;
  }
  EXPECT_EQ(count, test_data.size());

  // 4. 测试迭代器的比较和移动
  auto it = block->begin();
  EXPECT_EQ(it->first, "key000");
  ++it;
  EXPECT_EQ(it->first, "key001");
  ++it;
  EXPECT_EQ(it->first, "key002");

  // 5. 测试编码后的迭代
  auto encoded = block->encode();
  auto decoded_block = Block::decode(encoded);
  count = 0;
  for (auto it = decoded_block->begin(); it != decoded_block->end(); ++it) {
    EXPECT_EQ(it->first, test_data[count].first);
    EXPECT_EQ(it->second, test_data[count].second);
    count++;
  }
}

// 包含多个事务操作的key的迭代器
TEST_F(BlockTest, TrancIteratorTest) {
  auto block = std::make_shared<Block>(4096);

  // 添加多个事务操作的key
  block->add_entry("key1", "value1", 1, false);

  block->add_entry("key2", "value222", 3, false);
  block->add_entry("key2", "value22", 2, false);
  block->add_entry("key2", "value2", 1, false);

  block->add_entry("key3", "value3", 1, false);
  block->add_entry("key4", "value4", 2, false);
  block->add_entry("key5", "value5", 3, false);

  std::vector<std::pair<std::string, std::string>> expected_data = {
      {"key1", "value1"},
      {"key2", "value222"},
      {"key3", "value3"},
      {"key4", "value4"},
      {"key5", "value5"}};

  std::vector<std::pair<std::string, std::string>> results;

  for (auto it = block->begin(); it != block->end(); ++it) {
    results.emplace_back(it->first, it->second);
  }

  EXPECT_EQ(results, expected_data);
}

TEST_F(BlockTest, PredicateTest) {
  std::vector<uint8_t> encoded_p;
  {
    std::shared_ptr<Block> block1 =
        std::make_shared<Block>(TomlConfig::getInstance().getLsmBlockSize());
    int num = 50;

    for (int i = 0; i < num; ++i) {
      std::ostringstream oss_key;
      std::ostringstream oss_value;

      // 设置数字为4位长度，不足的部分用前导零填充
      oss_key << "key" << std::setw(4) << std::setfill('0') << i;
      oss_value << "value" << std::setw(4) << std::setfill('0') << i;

      std::string key = oss_key.str();
      std::string value = oss_value.str();

      block1->add_entry(key, value, 0, false);
    }

    auto result =
        block1->get_monotony_predicate_iters(0, [](const std::string &key) {
          if (key < "key0020") {
            return 1;
          }
          if (key >= "key0030") {
            return -1;
          }
          return 0;
        });
    EXPECT_TRUE(result.has_value());
    auto [it_begin, it_end] = result.value();
    EXPECT_EQ((*it_begin)->first, "key0020");
    EXPECT_EQ((*it_end)->first, "key0030");
    for (int i = 0; i < 5; i++) {
      ++(*it_begin);
    }
    EXPECT_EQ((*it_begin)->first, "key0025");

    encoded_p = block1->encode();
  }
  std::shared_ptr<Block> block2 = Block::decode(encoded_p);

  auto result =
      block2->get_monotony_predicate_iters(0, [](const std::string &key) {
        if (key < "key0020") {
          return 1;
        }
        if (key >= "key0030") {
          return -1;
        }
        return 0;
      });
  EXPECT_TRUE(result.has_value());
  auto [it_begin, it_end] = result.value();
  EXPECT_EQ((*it_begin)->first, "key0020");
  EXPECT_EQ((*it_end)->first, "key0030");
  for (int i = 0; i < 5; i++) {
    ++(*it_begin);
  }
  EXPECT_EQ((*it_begin)->first, "key0025");
}

// 包含了事务的谓词迭代器
TEST_F(BlockTest, TrancPredicateTest) {
  std::vector<uint8_t> encoded_p;

  {
    std::shared_ptr<Block> block1 =
        std::make_shared<Block>(TomlConfig::getInstance().getLsmBlockSize());
    int num = 50;

    block1->add_entry("key0", "value0", 0, false);
    block1->add_entry("key1", "value1", 1, false);
    block1->add_entry("key2", "value22", 10, false);
    block1->add_entry("key2", "value2", 2, false);
    block1->add_entry("key3", "value3", 3, false);
    block1->add_entry("key4", "value4444", 9, false);
    block1->add_entry("key4", "value444", 8, false);
    block1->add_entry("key4", "value44", 7, false);
    block1->add_entry("key4", "value4", 4, false);
    block1->add_entry("key5", "value5555", 8, false);
    block1->add_entry("key5", "value555", 7, false);
    block1->add_entry("key5", "value55", 6, false);
    block1->add_entry("key5", "value5", 5, false);
    block1->add_entry("key6", "value6", 6, false);

    encoded_p = block1->encode();
  }

  std::shared_ptr<Block> block2 = Block::decode(encoded_p);

  auto result =
      block2->get_monotony_predicate_iters(7, [](const std::string &key) {
        if (key < "key2") {
          return 1;
        }
        if (key >= "key6") {
          return -1;
        }
        return 0;
      });
  EXPECT_TRUE(result.has_value());
  auto [it_begin, it_end] = result.value();

  EXPECT_EQ((*it_end)->first, "key6");

  EXPECT_EQ((*it_begin)->first, "key2");
  EXPECT_EQ((*it_begin)->second, "value2");

  ++(*it_begin);
  EXPECT_EQ((*it_begin)->first, "key3");

  ++(*it_begin);
  EXPECT_EQ((*it_begin)->first, "key4");
  EXPECT_EQ((*it_begin)->second, "value44");

  // 遍历打印
  result = block2->get_monotony_predicate_iters(6, [](const std::string &key) {
    if (key < "key2") {
      return 1;
    }
    if (key >= "key7") {
      return -1;
    }
    return 0;
  });
  auto [it_begin2, it_end2] = result.value();

  std::vector<std::string> results;
  for (auto it = it_begin2; (*it) != (*it_end2); ++(*it)) {
    results.push_back((*it)->second);
  }

  std::vector<std::string> expected = {"value2", "value3", "value4", "value55",
                                       "value6"};
  EXPECT_EQ(results, expected);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}