#include "../include/logger/logger.h"
#include "../include/wal/record.h"
#include "../include/lsm/engine.h"
#include "../include/wal/wal.h"
#include <filesystem>
#include <gmock/gmock.h>
#include <gtest/gtest.h>

using namespace testing;
using namespace ::tiny_lsm;

class MockWAL : public WAL {
public:
  MockWAL(const std::string &log_path, size_t buffer_size,
          uint64_t checkpoint_tranc_id)
      : WAL(log_path, buffer_size, checkpoint_tranc_id, 1, 100) {}

  MOCK_METHOD(void, cleaner, ());
  MOCK_METHOD(void, cleanWALFile, ());
  MOCK_METHOD(void, reset_file, ());

  FileObj *get_log_file() { return &log_file_; }

  std::vector<Record> &get_log_buffer() { return log_buffer_; }

  size_t get_file_size_limit() { return file_size_limit_; }

  void set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id) {
    checkpoint_tranc_id_ = checkpoint_tranc_id;
  }

  uint64_t get_checkpoint_tranc_id() { return checkpoint_tranc_id_; }
};

class WALTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 创建测试目录
    if (!std::filesystem::exists(test_dir)) {
      std::filesystem::create_directory(test_dir);
    } else {
      // 清空测试目录
      for (const auto &entry : std::filesystem::directory_iterator(test_dir)) {
        std::filesystem::remove_all(entry.path());
      }
    }
  }

  void TearDown() override {
    // 清理测试文件
    if (std::filesystem::exists(test_dir)) {
      std::filesystem::remove_all(test_dir);
    }
  }

  std::string test_dir = "test_wal_dir";
};

TEST_F(WALTest, LogAndFlush) {
  MockWAL wal(test_dir, 10, 100);

  auto mock_file = wal.get_log_file();

  std::vector<Record> records1;
  std::vector<Record> records2;
  int record1_len = 8;
  int record2_len = 12;

  uint64_t tranc_id = 0;
  for (int i = 0; i < record1_len; i++) {
    auto tranc_str = std::to_string(tranc_id);
    auto key = tranc_str + "-key-" + std::to_string(i);
    if (i == 0) {
      auto rec = Record::createRecord(tranc_id);
      records1.push_back(rec);
    } else if (i == record1_len - 1) {
      auto rec = Record::commitRecord(tranc_id++);
      records1.push_back(rec);
    } else {
      auto value = tranc_str + "-value-" + std::to_string(i);
      auto rec = Record::putRecord(tranc_id, key, value);
      records1.push_back(rec);
    }
  }

  for (int i = 0; i < record2_len; i++) {
    auto tranc_str = std::to_string(tranc_id);
    auto key = tranc_str + "-key-" + std::to_string(i);
    if (i == 0) {
      auto rec = Record::createRecord(tranc_id);
      records2.push_back(rec);
    } else if (i == record2_len - 1) {
      auto rec = Record::commitRecord(tranc_id++);
      records2.push_back(rec);
    } else {
      auto value = tranc_str + "-value-" + std::to_string(i);
      auto rec = Record::putRecord(tranc_id, key, value);
      records2.push_back(rec);
    }
  }

  wal.log(records1, false);
  EXPECT_EQ(wal.get_log_buffer().size(), 8);

  wal.log(records2, false);
  EXPECT_EQ(wal.get_log_buffer().size(), 0);

  wal.flush();
  EXPECT_EQ(wal.get_log_buffer().size(), 0);
}

TEST_F(WALTest, RecoverTest) {
  std::map<uint64_t, std::vector<Record>> expected;

  {
    MockWAL wal(test_dir, 10, 100);

    std::vector<Record> records;
    int record1_len = 50;

    uint64_t tranc_id = 0;
    for (int i = 0; i < record1_len; i++) {
      auto tranc_str = std::to_string(tranc_id);
      auto key = tranc_str + "-key-" + std::to_string(i);
      if (i == 0) {
        auto rec = Record::createRecord(tranc_id);
        records.push_back(rec);
        expected[tranc_id].push_back(rec);
      } else if (i % 10 == 0 || i == record1_len - 1) {
        // 每10条记录或者最后一条记录提交一次
        auto rec = Record::commitRecord(tranc_id);
        records.push_back(rec);
        expected[tranc_id].push_back(rec);
        tranc_id++;
      } else {
        auto value = tranc_str + "-value-" + std::to_string(i);
        auto rec = Record::putRecord(tranc_id, key, value);
        records.push_back(rec);
        expected[tranc_id].push_back(rec);
      }
      if (i % 20 == 0) {
        // 每20条记录写入一次
        wal.log(records);
        records.clear();
      }
    }
    if (!records.empty()) {
      wal.log(records);
    }
  }

  // 恢复
  auto tranc_records = WAL::recover(test_dir, 100);
  // 校验 Record
  uint64_t tranc_id = 0;
  for (auto &[tranc_id, records] : tranc_records) {
    EXPECT_EQ(records, expected[tranc_id]);
  }
}

TEST_F(WALTest, PartialFlushRecoveryTest) {
  {
    LSM lsm(test_dir);
    auto tran_ctx1 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    auto tran_ctx2 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    auto tran_ctx3 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    auto tran_ctx4 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);

    tran_ctx4->put("key0", "value0");

    tran_ctx2->put("key1", "value1");
    tran_ctx2->put("key2", "value2");
    // 提交并刷新wal
    tran_ctx2->commit();
    tran_ctx4->commit();
    tran_ctx1->put("key3", "value3");

    tran_ctx3->put("key4", "value4");
    // 模拟写入到WAL后系统崩溃
    tran_ctx1->commit(true);
  }
  {
    LSM lsm(test_dir);
    EXPECT_EQ(lsm.get("key0").value(), "value0");
    EXPECT_EQ(lsm.get("key1").value(), "value1");
    EXPECT_EQ(lsm.get("key2").value(), "value2");
    EXPECT_EQ(lsm.get("key3").value(), "value3");
    EXPECT_FALSE(lsm.get("key4").has_value());
  }
}

TEST_F(WALTest, ConcurrentTransactionsTest) {
  {
    LSM lsm(test_dir);
    std::vector<std::thread> threads;
    auto global_ctx = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    for (int j = 0; j < 10; ++j) {
      global_ctx->put("key" + std::to_string(-1) + "-" + std::to_string(j),
                "value" + std::to_string(j));
    }
    for (int i = 0; i < 40; ++i) {
      threads.emplace_back([&, i]() {
        auto ctx = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
        for (int j = 0; j < 1000; ++j) {
          ctx->put("key" + std::to_string(i) + "-" + std::to_string(j),
                   "value" + std::to_string(j));
        }
        ctx->commit();
      });
    }
    for (auto& t : threads) t.join();
    lsm.flush_all();
    for (int j = 10; j < 20; ++j) {
      global_ctx->put("key" + std::to_string(-1) + "-" + std::to_string(j),
                "value" + std::to_string(j));
    }
    global_ctx->commit(true);
  }

  // 崩溃恢复后验证
  
  {
    LSM lsm(test_dir);
    for (int i = 0; i < 40; ++i) {
      for (int j = 0; j < 100; ++j) {
        auto val = lsm.get("key" + std::to_string(i) + "-" + std::to_string(j));
        EXPECT_TRUE(val.has_value());
        EXPECT_EQ(val.value(), "value" + std::to_string(j));
      }
    }
    for (int j = 0; j < 20; ++j) {
      auto val = lsm.get("key" + std::to_string(-1) + "-" + std::to_string(j));
      EXPECT_TRUE(val.has_value());
      EXPECT_EQ(val.value(), "value" + std::to_string(j));
    }
  }
}

TEST_F(WALTest, HighConcurrencyWithAborts) {
  // 测试高并发下混合提交和abort
  const int THREAD_COUNT = 20;
  const int OPS_PER_THREAD = 100;
  std::atomic<int> commit_count{0};
  std::atomic<int> abort_count{0};
  
  {
    LSM lsm(test_dir);
    std::vector<std::thread> threads;
    
    for (int i = 0; i < THREAD_COUNT; i++) {
      threads.emplace_back([&, i] {
        auto ctx = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
        bool do_abort = (i % 5 == 0);  // 20%的事务abort
        
        for (int j = 0; j < OPS_PER_THREAD; j++) {
          std::string key = "k" + std::to_string(i) + "_" + std::to_string(j);
          ctx->put(key, "v" + std::to_string(j));
          
          // 随机插入abort
          if (j == OPS_PER_THREAD / 2 && do_abort) {
            ctx->abort();
            abort_count++;
            return;
          }
        }
        
        if (do_abort) {
          ctx->abort();
          abort_count++;
        } else {
          ctx->commit();
          commit_count++;
        }
      });
    }
    
    for (auto& t : threads) t.join();
  }

  // 恢复验证
  LSM recovered_lsm(test_dir);
  int recovered_count = 0;
  
  // 验证提交的事务存在，abort的事务不存在
  for (int i = 0; i < THREAD_COUNT; i++) {
    bool should_exist = (i % 5 != 0);  // 非abort线程
    
    for (int j = 0; j < OPS_PER_THREAD; j++) {
      std::string key = "k" + std::to_string(i) + "_" + std::to_string(j);
      auto result = recovered_lsm.get(key);
      
      if (should_exist) {
        EXPECT_TRUE(result.has_value()) << "Missing key: " << key;
        EXPECT_EQ(result.value(), "v" + std::to_string(j));
        if (result.has_value()) recovered_count++;
      } else {
        EXPECT_FALSE(result.has_value()) << "Aborted key present: " << key;
      }
    }
  }
  
  EXPECT_EQ(recovered_count, commit_count * OPS_PER_THREAD);
}

TEST_F(WALTest, MixedTransactionCommitAbortAndCrashRecovery) {
  {
    LSM lsm(test_dir);

    auto t1 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t1->put("k1", "v1");
    t1->commit();  // 提交

    auto t2 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t2->put("k2", "v2");
    t2->abort();  // 中止

    auto t3 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    // 未提交，也未中止
    t3->put("k3", "v3");

    auto t5 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t5->put("k5", "v5");
    t5->commit();


    auto t6 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t6->put("k6", "v6");
    t6->commit();

    auto t4 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t4->put("k4", "v4");
    t4->commit();

    auto t7 = lsm.begin_tran(IsolationLevel::READ_OP_COMMITTED);
    t7->put("k7", "v7");
    t7->commit();

    // 崩溃，t3未提交，t1、t4~t7已提交，t2已中止
  }

  // 模拟系统重启后的恢复
  LSM lsm(test_dir);

  // t1、t4~t7 的数据应恢复
  EXPECT_EQ(lsm.get("k1").value(), "v1");
  EXPECT_EQ(lsm.get("k4").value(), "v4");
  EXPECT_EQ(lsm.get("k5").value(), "v5");
  EXPECT_EQ(lsm.get("k6").value(), "v6");
  EXPECT_EQ(lsm.get("k7").value(), "v7");

  // t2中止，t3未提交，应该被丢弃
  EXPECT_FALSE(lsm.get("k2").has_value());
  EXPECT_FALSE(lsm.get("k3").has_value());
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}