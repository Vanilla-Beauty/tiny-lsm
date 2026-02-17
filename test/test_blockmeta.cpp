#include "block/blockmeta.h"
#include "logger/logger.h"
#include <gtest/gtest.h>

using namespace ::tiny_lsm;

namespace tiny_lsm {
class BlockMetaTest : public ::testing::Test {
  friend class BlockMeta;

protected:
  // 创建一组测试用的 BlockMeta，确保 key 有序
  std::vector<BlockMeta> createTestMetas() {
    std::vector<BlockMeta> metas;

    // Block 1: offset=0, "a100" -> "a199"
    BlockMeta meta1;
    meta1.offset = 0;
    meta1.first_key = "a100";
    meta1.last_key = "a199";
    metas.push_back(meta1);

    // Block 2: offset=100, "a200" -> "a299"
    BlockMeta meta2;
    meta2.offset = 100;
    meta2.first_key = "a200";
    meta2.last_key = "a299";
    metas.push_back(meta2);

    // Block 3: offset=200, "a300" -> "a399"
    BlockMeta meta3;
    meta3.offset = 200;
    meta3.first_key = "a300";
    meta3.last_key = "a399";
    metas.push_back(meta3);

    return metas;
  }
};
} // namespace tiny_lsm

// 测试基本的编码和解码功能
TEST_F(BlockMetaTest, BasicEncodeDecodeTest) {
  // 1. 创建测试数据
  auto original_metas = createTestMetas();
  std::vector<uint8_t> encoded_data;

  // 2. 编码
  BlockMeta::encode_meta_to_slice(original_metas, encoded_data);
  EXPECT_FALSE(encoded_data.empty());

  // 3. 解码
  auto decoded_metas = BlockMeta::decode_meta_from_slice(encoded_data);

  // 4. 验证
  ASSERT_EQ(original_metas.size(), decoded_metas.size());
  for (size_t i = 0; i < original_metas.size(); i++) {
    EXPECT_EQ(original_metas[i].offset, decoded_metas[i].offset);
    EXPECT_EQ(original_metas[i].first_key, decoded_metas[i].first_key);
    EXPECT_EQ(original_metas[i].last_key, decoded_metas[i].last_key);
  }
}

// 测试空数据的处理
TEST_F(BlockMetaTest, EmptyMetaTest) {
  std::vector<BlockMeta> empty_metas;
  std::vector<uint8_t> encoded_data;

  // 编码空数据
  BlockMeta::encode_meta_to_slice(empty_metas, encoded_data);
  EXPECT_FALSE(encoded_data.empty()); // 至少应该包含元素个数和哈希值

  // 解码空数据
  auto decoded_metas = BlockMeta::decode_meta_from_slice(encoded_data);
  EXPECT_TRUE(decoded_metas.empty());
}

// 测试特殊字符的处理
TEST_F(BlockMetaTest, SpecialCharTest) {
  std::vector<BlockMeta> metas;
  BlockMeta meta;
  meta.offset = 0;
  meta.first_key = std::string("key\0with\0null", 12);
  meta.last_key = std::string("value\0with\0null", 14);
  metas.push_back(meta);

  std::vector<uint8_t> encoded_data;
  BlockMeta::encode_meta_to_slice(metas, encoded_data);

  auto decoded_metas = BlockMeta::decode_meta_from_slice(encoded_data);
  ASSERT_EQ(decoded_metas.size(), 1);
  EXPECT_EQ(decoded_metas[0].first_key, std::string("key\0with\0null", 12));
  EXPECT_EQ(decoded_metas[0].last_key, std::string("value\0with\0null", 14));
}

// 测试错误处理
TEST_F(BlockMetaTest, ErrorHandlingTest) {
  // 测试解码无效数据
  std::vector<uint8_t> invalid_data = {0x00, 0x01, 0x02};
  EXPECT_THROW(BlockMeta::decode_meta_from_slice(invalid_data),
               std::runtime_error);
}

// 修改大数据量测试，确保 key 有序
TEST_F(BlockMetaTest, LargeDataTest) {
  std::vector<BlockMeta> large_metas;
  const int n = 1000;

  // 创建大量有序数据
  for (int i = 0; i < n; i++) {
    BlockMeta meta;
    meta.offset = i * 100;

    // 使用格式化确保 key 有序
    char first_key[16], last_key[16];
    snprintf(first_key, sizeof(first_key), "key%03d00", i); // 例如: key00000
    snprintf(last_key, sizeof(last_key), "key%03d99", i);   // 例如: key00099

    meta.first_key = first_key;
    meta.last_key = last_key;
    large_metas.push_back(meta);
  }

  // 编码和解码
  std::vector<uint8_t> encoded_data;
  BlockMeta::encode_meta_to_slice(large_metas, encoded_data);
  auto decoded_metas = BlockMeta::decode_meta_from_slice(encoded_data);

  // 验证
  ASSERT_EQ(large_metas.size(), decoded_metas.size());
  for (int i = 0; i < n; i++) {
    EXPECT_EQ(large_metas[i].offset, decoded_metas[i].offset);
    EXPECT_EQ(large_metas[i].first_key, decoded_metas[i].first_key);
    EXPECT_EQ(large_metas[i].last_key, decoded_metas[i].last_key);

    // 验证相邻 block 之间的顺序
    if (i > 0) {
      EXPECT_LT(decoded_metas[i - 1].last_key, decoded_metas[i].first_key);
    }
  }
}

// 添加顺序性测试
TEST_F(BlockMetaTest, OrderTest) {
  auto metas = createTestMetas();

  // 验证每个 block 内的顺序
  for (const auto &meta : metas) {
    EXPECT_LT(meta.first_key, meta.last_key);
  }

  // 验证相邻 block 之间的顺序
  for (size_t i = 1; i < metas.size(); i++) {
    EXPECT_LT(metas[i - 1].last_key, metas[i].first_key);
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}
