#include "consts.h"
#include "logger/logger.h"
#include "lsm/engine.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>

using namespace ::tiny_lsm;

class CompactTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a temporary test directory
    test_dir = "dir_test_compact_data";
    if (std::filesystem::exists(test_dir)) {
      std::filesystem::remove_all(test_dir);
    }
    std::filesystem::create_directory(test_dir);
  }

  void TearDown() override {
    // Clean up test directory
    if (std::filesystem::exists(test_dir)) {
      std::filesystem::remove_all(test_dir);
    }
  }

  std::string test_dir;
};

TEST_F(CompactTest, Persistence) {
  std::unordered_map<std::string, std::string> kvs;
  int num = 100000;
  {
    LSM lsm(test_dir);
    for (int i = 0; i < num; ++i) {
      std::ostringstream oss_key, oss_value;
      oss_key << "key" << std::setw(6) << std::setfill('0') << i;
      oss_value << "value" << std::setw(6) << std::setfill('0') << i;
      std::string key = oss_key.str();
      std::string value = oss_value.str();
      lsm.put(key, value);
      kvs[key] = value;

      // 删除之前被10整除的key
      if (i % 10 == 0 && i != 0) {
        std::ostringstream oss_del_key;
        oss_del_key << "key" << std::setw(6) << std::setfill('0') << (i - 10);
        std::string del_key = oss_del_key.str();
        lsm.remove(del_key);
        kvs.erase(del_key);
      }
    }
  } // LSM destructor called here

  std::cout << "LSM destructor called here" << std::endl;

  // Create new LSM instance
  LSM lsm(test_dir);
  for (int i = 0; i < num; ++i) {
    std::ostringstream oss_key;
    oss_key << "key" << std::setw(6) << std::setfill('0') << i;
    std::string key = oss_key.str();
    if (kvs.find(key) != kvs.end()) {
      auto res = lsm.get(key);
      EXPECT_EQ(res.has_value(), true);
      if (!res.has_value()) {
        std::cout << "key: " << key << " not found" << std::endl;
        exit(-1);
      }
    } else {
      EXPECT_FALSE(lsm.get(key).has_value());
    }
  }

  // Query a not exist key
  EXPECT_FALSE(lsm.get("nonexistent").has_value());
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}