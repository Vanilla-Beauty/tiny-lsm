#include "../include/logger/logger.h"
#include "../include/lsm/engine.h"
#include "../include/lsm/level_iterator.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <unordered_map>

using namespace ::tiny_lsm;

class LSMTest : public ::testing::Test {
protected:
  void SetUp() override {
    // Create a temporary test directory
    test_dir = "test_lsm_data";
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

// Test basic operations: put, get, remove
TEST_F(LSMTest, BasicOperations) {
  LSM lsm(test_dir);

  // Test put and get
  lsm.put("key1", "value1");
  EXPECT_EQ(lsm.get("key1").value(), "value1");

  // Test update
  lsm.put("key1", "new_value");
  EXPECT_EQ(lsm.get("key1").value(), "new_value");

  // Test remove
  lsm.remove("key1");
  EXPECT_FALSE(lsm.get("key1").has_value());

  // Test non-existent key
  EXPECT_FALSE(lsm.get("nonexistent").has_value());
}

// Test persistence across restarts
TEST_F(LSMTest, Persistence) {
  std::unordered_map<std::string, std::string> kvs;
  int num = 100000;
  {
    LSM lsm(test_dir);
    for (int i = 0; i < num; ++i) {
      std::string key = "key" + std::to_string(i);
      std::string value = "value" + std::to_string(i);
      lsm.put(key, value);
      kvs[key] = value;

      // 删除之前被10整除的key
      if (i % 10 == 0 && i != 0) {
        std::string del_key = "key" + std::to_string(i - 10);
        lsm.remove(del_key);
        kvs.erase(del_key);
      }
    }
  } // LSM destructor called here

  // Create new LSM instance
  LSM lsm(test_dir);
  for (int i = 0; i < num; ++i) {
    std::string key = "key" + std::to_string(i);
    if (kvs.find(key) != kvs.end()) {
      EXPECT_EQ(lsm.get(key).value(), kvs[key]);
    } else {
      if (key == "key4410") {
        // debug
        auto res = lsm.get("key4410");
      }
      if (lsm.get(key).has_value()) {
        std::cout << "key" << i << " not exist but found" << std::endl;
        exit(-1);
      }
      // EXPECT_FALSE(lsm.get(key).has_value());
    }
  }

  // Query a not exist key
  EXPECT_FALSE(lsm.get("nonexistent").has_value());
}

// Test large scale operations
TEST_F(LSMTest, LargeScaleOperations) {
  LSM lsm(test_dir);
  std::vector<std::pair<std::string, std::string>> data;

  // Insert enough data to trigger multiple flushes
  for (int i = 0; i < 1000; i++) {
    std::string key = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    lsm.put(key, value);
    data.emplace_back(key, value);
  }

  // Verify all data
  for (const auto &[key, value] : data) {
    EXPECT_EQ(lsm.get(key).value(), value);
  }
}

// Test iterator functionality
TEST_F(LSMTest, IteratorOperations) {
  LSM lsm(test_dir);
  std::map<std::string, std::string> reference;

  // Insert data
  for (int i = 0; i < 100; i++) {
    std::string key = "key" + std::to_string(i);
    std::string value = "value" + std::to_string(i);
    lsm.put(key, value);
    reference[key] = value;
  }

  // Test iterator
  auto it = lsm.begin(0);
  auto ref_it = reference.begin();

  while (it != lsm.end() && ref_it != reference.end()) {
    EXPECT_EQ(it->first, ref_it->first);
    EXPECT_EQ(it->second, ref_it->second);
    ++it;
    ++ref_it;
  }

  EXPECT_EQ(it == lsm.end(), ref_it == reference.end());
}

// Test mixed operations
TEST_F(LSMTest, MixedOperations) {
  LSM lsm(test_dir);
  std::map<std::string, std::string> reference;

  // Perform mixed operations
  lsm.put("key1", "value1");
  reference["key1"] = "value1";

  lsm.put("key2", "value2");
  reference["key2"] = "value2";

  lsm.remove("key1");
  reference.erase("key1");

  lsm.put("key3", "value3");
  reference["key3"] = "value3";

  // Verify final state
  for (const auto &[key, value] : reference) {
    EXPECT_EQ(lsm.get(key).value(), value);
  }
  EXPECT_FALSE(lsm.get("key1").has_value());
}

TEST_F(LSMTest, MonotonyPredicate) {
  LSM lsm(test_dir);

  // Insert data
  for (int i = 0; i < 100; i++) {
    std::ostringstream oss_key;
    std::ostringstream oss_value;
    oss_key << "key" << std::setw(2) << std::setfill('0') << i;
    oss_value << "value" << std::setw(2) << std::setfill('0') << i;
    std::string key = oss_key.str();
    std::string value = oss_value.str();
    lsm.put(key, value);
    if (i == 50) {
      // 主动刷一次盘
      lsm.flush();
    }
  }

  // Define a predicate function
  auto predicate = [](const std::string &key) -> int {
    // Extract the number from the key
    int key_num = std::stoi(key.substr(3));
    if (key_num < 20) {
      return 1;
    }
    if (key_num > 60) {
      return -1;
    }
    return 0;
  };

  // Call the method under test
  auto result = lsm.lsm_iters_monotony_predicate(0, predicate);

  // Check if the result is not empty
  ASSERT_TRUE(result.has_value());

  // Extract the iterators from the result
  auto [start, end] = result.value();

  // Verify the range of keys returned by the iterators
  std::set<std::string> expected_keys;
  for (int i = 20; i <= 60; i++) {
    std::ostringstream oss;
    oss << "key" << std::setw(2) << std::setfill('0') << i;
    expected_keys.insert(oss.str());
  }

  std::set<std::string> actual_keys;
  for (auto it = start; it != end; ++it) {
    actual_keys.insert(it->first);
  }

  EXPECT_EQ(actual_keys, expected_keys);
}

TEST_F(LSMTest, TrancIdTest) {
  // 注意是 LSMEngine 而不是 LSM
  // 因为 LSMEngine 才能手动控制事务id
  LSMEngine lsm(test_dir);

  // key00-key20 先插入, 此时事务id为1
  for (int i = 0; i < 20; i++) {
    std::ostringstream oss_key;
    oss_key << "key" << std::setw(2) << std::setfill('0') << i;
    std::string key = oss_key.str();
    lsm.put(key, "tranc1", 1);
  }
  lsm.flush();

  // key10-key10 再插入, 此时事务id为2
  for (int i = 0; i < 10; i++) {
    std::ostringstream oss_key;
    oss_key << "key" << std::setw(2) << std::setfill('0') << i;
    std::string key = oss_key.str();
    lsm.put(key, "tranc2", 2);
  }

  // 在事务id为1时进行遍历, 事务id为2的记录是不可见的
  for (int i = 0; i < 20; i++) {
    std::ostringstream oss_key;
    oss_key << "key" << std::setw(2) << std::setfill('0') << i;
    std::string key = oss_key.str();

    auto res = lsm.get(key, 1);

    EXPECT_EQ(res.value().first, "tranc1");
  }

  // 在事务id为2时进行遍历, 事务id为2的记录现在是可见的了
  for (int i = 0; i < 20; i++) {
    std::ostringstream oss_key;
    oss_key << "key" << std::setw(2) << std::setfill('0') << i;
    std::string key = oss_key.str();

    auto res = lsm.get(key, 2);
    if (i < 10) {
      EXPECT_EQ(res.value().first, "tranc2");
    } else {
      EXPECT_EQ(res.value().first, "tranc1");
    }
  }
}

TEST_F(LSMTest, TranContextTest) {
  LSM lsm(test_dir);
  auto tran_ctx = lsm.begin_tran(IsolationLevel::REPEATABLE_READ);

  tran_ctx->put("key1", "value1");
  tran_ctx->put("key2", "value2");

  auto query = lsm.get("key1");
  // 事务还没有提交, 应该查不到数据
  EXPECT_FALSE(query.has_value());

  auto commit_res = tran_ctx->commit();
  EXPECT_TRUE(commit_res);

  // 事务已经提交, 应该可以查到数据
  query = lsm.get("key1");
  EXPECT_EQ(query.value(), "value1");
  query = lsm.get("key2");
  EXPECT_EQ(query.value(), "value2");

  auto tran_ctx2 = lsm.begin_tran(IsolationLevel::REPEATABLE_READ);
  tran_ctx2->put("key1", "value1");
  tran_ctx2->put("key2", "value2");

  lsm.put("key2", "value22");

  commit_res = tran_ctx2->commit();
  EXPECT_FALSE(commit_res);
}

TEST_F(LSMTest, Recover) {
  {
    LSM lsm(test_dir);

    lsm.put("xxx  ", "yyy");
    auto tran_ctx = lsm.begin_tran(IsolationLevel::REPEATABLE_READ);

    for (int i = 0; i < 100; i++) {
      std::ostringstream oss_key;
      std::ostringstream oss_value;
      oss_key << "key" << std::setw(2) << std::setfill('0') << i;
      oss_value << "value" << std::setw(2) << std::setfill('0') << i;
      std::string key = oss_key.str();
      std::string value = oss_value.str();

      tran_ctx->put(key, value);
    }

    // 提交事务时true表示不会真正写入
    tran_ctx->commit(true);
  }
  {
    LSM lsm(test_dir);

    for (int i = 0; i < 100; i++) {
      std::ostringstream oss_key;
      std::ostringstream oss_value;
      oss_key << "key" << std::setw(2) << std::setfill('0') << i;
      oss_value << "value" << std::setw(2) << std::setfill('0') << i;
      std::string key = oss_key.str();
      std::string value = oss_value.str();

      EXPECT_EQ(lsm.get(key).value(), value);
    }
  }
}

TEST_F(LSMTest, BigPersistence) {
  std::unordered_map<std::string, std::string> kvs;
  int num = 2000000;
  {
    LSM lsm(test_dir);
    for (int i = 0; i <= num; ++i) {
      std::string key = "key" + std::to_string(i);
      std::string value = "value" + std::to_string(i);
      lsm.put(key, value);
      kvs[key] = value;
      if (i % 400000 == 0 && i != 0) {
        std::cout << "lsm put key at i = " << i << "......" << std::endl;
      }
      // 删除之前被10整除的key
      if (i % 10 == 0 && i != 0) {
        std::string del_key = "key" + std::to_string(i - 10);
        lsm.remove(del_key);
        kvs.erase(del_key);
      }
    }
  } // LSM destructor called here

  // Create new LSM instance
  LSM lsm(test_dir);
  for (int i = 0; i <= num; ++i) {
    std::string key = "key" + std::to_string(i);
    if (i % 400000 == 0 && i != 0) {
      std::cout << "lsm get key at i = " << i << "......" << std::endl;
    }
    if (kvs.find(key) != kvs.end()) {
      EXPECT_EQ(lsm.get(key).value(), kvs[key]);
    } else {
      EXPECT_EQ(lsm.get(key).has_value(), false);
    }
  }
}

TEST_F(LSMTest, BigPersistence2) {
  //固定num的后一半会被删除
  int num = 1000000;
  std::vector<int> idx1, idx2;
  idx1.reserve(num / 2 + 1), idx2.reserve(num / 2 + 1);

  for (int i = 0; i <= num; ++i) {
    if (i <= num / 2)
      idx1.push_back(i);
    else
      idx2.push_back(i);
  }

  //伪随机，打乱vector
  auto random_change_vector = [](std::vector<int> &a) {
    std::vector<int> pri{3, 5, 7, 11, 13};
    int pri_pos = 0;
    int swap_cnt = std::min((int)a.size(), 50000);
    for (int i = 0; i < swap_cnt; i++) {
      int x1 = ((i * pri[pri_pos] + i * i + 19) % (a.size()));
      int x2 = ((i * i * pri[(pri_pos + 1) % 5] * pri[(pri_pos + 1) % 5] +
                 i * i + 31) %
                (a.size()));
      std::swap(a[x1], a[x2]);
    }
  };
  random_change_vector(idx1);
  {
    LSM lsm(test_dir);
    for (int i : idx1) {
      std::string key = "key" + std::to_string(i);
      std::string value = "value" + std::to_string(i);
      lsm.put(key, value);
    }
    for (int i : idx2) {
      std::string key = "key" + std::to_string(i);
      std::string value = "value" + std::to_string(i);
      lsm.put(key, value);
    }
    random_change_vector(idx2);
    for (int i : idx2) {
      std::string key = "key" + std::to_string(i);
      lsm.remove(key);
    }
  } // LSM destructor called here

  // Create new LSM instance
  LSM lsm(test_dir);
  for (int i = 0; i <= num; ++i) {
    std::string key = "key" + std::to_string(i);
    if (i <= num / 2) {
      ASSERT_EQ(lsm.get(key).has_value(), true);
      ASSERT_EQ(lsm.get(key).value(),
                (std::string) "value" + std::to_string(i));
    } else {
      ASSERT_EQ(lsm.get(key).has_value(), false);
    }
  }
}
int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}