#include "../include/logger/logger.h"
#include "../include/utils/bloom_filter.h"
#include "../include/utils/cursor.h"
#include "../include/utils/files.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <random>

using namespace ::tiny_lsm;

class FileTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 确保测试目录存在
    if (!std::filesystem::exists("test_data")) {
      std::filesystem::create_directory("test_data");
    }
  }

  void TearDown() override {
    // 清理测试文件
    std::filesystem::remove_all("test_data");
  }

  // 生成随机数据
  std::vector<uint8_t> generate_random_data(size_t size) {
    std::vector<uint8_t> data(size);
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, 255);

    for (size_t i = 0; i < size; ++i) {
      data[i] = static_cast<uint8_t>(dis(gen));
    }
    return data;
  }
};

// 测试基本的写入和读取
TEST_F(FileTest, BasicWriteAndRead) {
  const std::string path = "test_data/basic.dat";
  std::vector<uint8_t> data = {1, 2, 3, 4, 5};

  // 写入文件
  auto file = FileObj::create_and_write(path, data);
  EXPECT_EQ(file.size(), data.size());

  // 重新打开并读取
  auto opened_file = FileObj::open(path, false);
  EXPECT_EQ(opened_file.size(), data.size());

  auto read_data = opened_file.read_to_slice(0, data.size());
  EXPECT_EQ(read_data, data);
}

// 测试大文件
TEST_F(FileTest, LargeFile) {
  const std::string path = "test_data/large.dat";
  const size_t size = 1024 * 1024; // 1MB
  auto data = generate_random_data(size);

  // 写入大文件
  auto file = FileObj::create_and_write(path, data);
  EXPECT_EQ(file.size(), size);

  // 分块读取并验证
  auto opened_file = FileObj::open(path, false);
  const size_t chunk_size = 1024;

  for (size_t offset = 0; offset < size; offset += chunk_size) {
    size_t current_chunk = std::min(chunk_size, size - offset);
    auto chunk = opened_file.read_to_slice(offset, current_chunk);

    // 验证每个块的数据
    for (size_t i = 0; i < current_chunk; ++i) {
      EXPECT_EQ(chunk[i], data[offset + i]);
    }
  }
}

// 测试部分读取
TEST_F(FileTest, PartialRead) {
  const std::string path = "test_data/partial.dat";
  std::vector<uint8_t> data = {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};

  auto file = FileObj::create_and_write(path, data);
  auto opened_file = FileObj::open(path, false);

  // 读取中间部分
  auto middle = opened_file.read_to_slice(2, 3);
  EXPECT_EQ(middle.size(), 3);
  EXPECT_EQ(middle[0], 3);
  EXPECT_EQ(middle[1], 4);
  EXPECT_EQ(middle[2], 5);

  // 读取开头
  auto start = opened_file.read_to_slice(0, 2);
  EXPECT_EQ(start[0], 1);
  EXPECT_EQ(start[1], 2);

  // 读取结尾
  auto end = opened_file.read_to_slice(8, 2);
  EXPECT_EQ(end[0], 9);
  EXPECT_EQ(end[1], 10);
}

// 测试错误情况
TEST_F(FileTest, ErrorCases) {
  const std::string path = "test_data/error.dat";
  std::vector<uint8_t> data = {1, 2, 3};

  auto file = FileObj::create_and_write(path, data);
  auto opened_file = FileObj::open(path, false);

  // 测试越界读取
  EXPECT_THROW(opened_file.read_to_slice(2, 2), std::out_of_range);
  EXPECT_THROW(opened_file.read_to_slice(3, 1), std::out_of_range);
  EXPECT_THROW(opened_file.read_to_slice(0, 4), std::out_of_range);

  // 测试打开不存在的文件
  EXPECT_THROW(FileObj::open("nonexistent.dat", false), std::runtime_error);
}

// 测试移动语义
TEST_F(FileTest, MoveSemantics) {
  const std::string path = "test_data/move.dat";
  std::vector<uint8_t> data = {1, 2, 3};

  // 测试移动构造
  auto file1 = FileObj::create_and_write(path, data);
  FileObj file2 = std::move(file1);

  // 验证移动后的对象可以正常工作
  auto read_data = file2.read_to_slice(0, data.size());
  EXPECT_EQ(read_data, data);
}

TEST_F(FileTest, TruncateFile) {
  const std::string path = "test_data/truncate.dat";
  std::vector<uint8_t> data = {10, 20, 30, 40, 50, 60, 70, 80};

  // 创建并写入文件
  auto file = FileObj::create_and_write(path, data);
  EXPECT_EQ(file.size(), data.size());

  // 截断到更小
  size_t new_size = 4;
  EXPECT_TRUE(file.truncate(new_size));
  EXPECT_EQ(file.size(), new_size);

  // 重新打开并验证内容
  auto opened_file = FileObj::open(path, false);
  EXPECT_EQ(opened_file.size(), new_size);
  auto truncated_data = opened_file.read_to_slice(0, new_size);
  EXPECT_EQ(truncated_data.size(), new_size);
  for (size_t i = 0; i < new_size; ++i) {
    EXPECT_EQ(truncated_data[i], data[i]);
  }

  // 截断到0
  EXPECT_TRUE(file.truncate(0));
  EXPECT_EQ(file.size(), 0);
  auto reopened_file = FileObj::open(path, false);
  EXPECT_EQ(reopened_file.size(), 0);
}

TEST_F(FileTest, MixedWriteAndAppendIntegers) {
  const std::string path = "test_data/mixed_rw.dat";

  uint8_t u8 = 0x12;
  uint16_t u16 = 0x3456;
  uint32_t u32 = 0x789ABCDE;
  uint64_t u64 = 0x1122334455667788ULL;
  int i32 = -654321;

  // 先用指定偏移写入
  {
    auto file = FileObj::create_and_write(path, {});
    EXPECT_TRUE(file.write_uint8(0, u8));
    EXPECT_TRUE(file.write_uint16(1, u16));
    EXPECT_TRUE(file.write_uint32(3, u32));
    file.sync();

    // 再追加写入
    EXPECT_TRUE(file.append_uint64(u64));
    EXPECT_TRUE(file.append_int(i32));
    file.sync();
  }

  // 读取并校验
  {
    auto file = FileObj::open(path, false);
    EXPECT_EQ(file.read_uint8(0), u8);
    EXPECT_EQ(file.read_uint16(1), u16);
    EXPECT_EQ(file.read_uint32(3), u32);

    // 追加部分的偏移
    size_t offset_u64 = 7; // 0+1+2+4=7
    size_t offset_i32 = offset_u64 + sizeof(uint64_t);

    EXPECT_EQ(file.read_uint64(offset_u64), u64);

    std::vector<uint8_t> buf = file.read_to_slice(offset_i32, sizeof(int));
    int read_i32;
    ::memcpy(&read_i32, buf.data(), sizeof(int));
    EXPECT_EQ(read_i32, i32);
  }
}

TEST_F(FileTest, CursorReadWrite) {
  const std::string path = "test_data/cursor_rw.dat";
  std::vector<uint8_t> data = {10, 20, 30, 40, 50};

  // 创建文件并写入数据
  {
    auto file = FileObj::create_and_write(path, data);

    // 获取Cursor
    Cursor cursor = file.get_cursor(file);

    // 读取前3个字节
    std::vector<uint8_t> buf = cursor.read(3);
    EXPECT_EQ(buf[0], 10);
    EXPECT_EQ(buf[1], 20);
    EXPECT_EQ(buf[2], 30);

    // 写入新数据到当前位置（应为offset=3）
    uint8_t new_val = 99;
    cursor.write_uint8(new_val);
  }

  // 重新读取文件验证写入
  auto reopened = FileObj::open(path, false);
  auto read_back = reopened.read_to_slice(3, 2);
  EXPECT_EQ(read_back[0], 99);
  EXPECT_EQ(read_back[1], 50);
}

// 综合测试布隆过滤器的功能
TEST(BloomFilterTest, ComprehensiveTest) {
  // 创建布隆过滤器，预期插入1000个元素，假阳性率为0.01
  BloomFilter bf(1000, 0.1);

  // 添加一些键
  for (int i = 0; i < 1000; ++i) {
    bf.add("key" + std::to_string(i));
  }

  // 检查存在的键
  for (int i = 0; i < 1000; ++i) {
    EXPECT_TRUE(bf.possibly_contains("key" + std::to_string(i)))
        << "Key key" << i << " should be found in the Bloom Filter";
  }

  // 检查不存在的键
  int false_positives = 0;
  for (int i = 1000; i < 2000; ++i) {
    if (bf.possibly_contains("key" + std::to_string(i))) {
      ++false_positives;
    }
  }

  // 计算假阳性率
  double false_positive_rate = static_cast<double>(false_positives) / 1000;

  // 预期的假阳性率是0.1，允许一些误差（例如0.2）
  EXPECT_LE(false_positive_rate, 0.2)
      << "False positive rate should be less than or equal to 0.2, but got "
      << false_positive_rate;

// 输出假阳性率

}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}