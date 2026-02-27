#include "config/config.h"
#include "consts.h"
#include "logger/logger.h"
#include "lsm/engine.h"
#include "lsm/transaction.h"
#include <cstdlib>
#include <filesystem>
#include <gtest/gtest.h>
#include <iostream>
#include <iomanip>
#include <memory>
#include <sstream>

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

  // Build a fully-wired LSMEngine + TranManager pair (mirrors LSM::LSM).
  std::pair<std::shared_ptr<LSMEngine>, std::shared_ptr<TranManager>>
  make_engine() {
    auto eng = std::make_shared<LSMEngine>(test_dir);
    auto mgr = std::make_shared<TranManager>(test_dir);
    mgr->set_engine(eng);
    eng->set_tran_manager(mgr);
    mgr->init_new_wal();
    return {eng, mgr};
  }

  std::string test_dir;
};

// Helper: write N filler keys through LSMEngine with auto-incrementing tranc
// ids so each put uses a fresh id (avoids MVCC collisions with the test keys).
static void write_filler_keys(LSMEngine &eng, TranManager &mgr, int count,
                               int start_idx = 0) {
  for (int i = start_idx; i < start_idx + count; ++i) {
    std::ostringstream k, v;
    k << "filler_" << std::setw(6) << std::setfill('0') << i;
    v << "fval_" << i;
    eng.put(k.str(), v.str(), mgr.getNextTransactionId());
  }
}

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

// ──────────────────────────────────────────────────────────────────────────────
// Test: after L0→L1 compaction, all per-key MVCC versions must still be
// readable with the corresponding tranc_id.
//
// Strategy:
//   1. Shrink mem/block limits so every put flushes a new L0 SST.
//   2. Write the same key ("mvcc_key") three times with large tranc_ids
//      (V1=1000, V2=2000, V3=3000), interleaved with filler puts that use
//      auto-assigned ids (1, 2, 3...), avoiding any tranc_id collision.
//   3. Accumulate > lsm_sst_level_ratio L0 SSTs to trigger L0→L1 compaction.
//   4. Verify engine->get("mvcc_key", id) returns the correct version for
//      each snapshot, and that an id just below V1 returns nothing.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(CompactTest, MultiVersionSurvivedAfterL0Compact) {
  auto &cfg = const_cast<TomlConfig &>(TomlConfig::getInstance());
  cfg.modify_lsm_tol_mem_size_limit(1);
  cfg.modify_lsm_block_size(64);

  auto [eng, mgr] = make_engine();

  // Use large tranc_ids for MVCC versions so they don't collide with the
  // auto-incrementing ids used by filler writes (which start from 1).
  const uint64_t V1 = 1000, V2 = 2000, V3 = 3000;

  // Write mvcc_key with three explicit tranc_ids, each separated by a filler
  // put that triggers a flush so the versions land in different SSTs.
  eng->put("mvcc_key", "version_1", V1);
  write_filler_keys(*eng, *mgr, 1, 0); // flush → L0 SST

  eng->put("mvcc_key", "version_2", V2);
  write_filler_keys(*eng, *mgr, 1, 1); // flush → L0 SST

  eng->put("mvcc_key", "version_3", V3);
  write_filler_keys(*eng, *mgr, 1, 2); // flush → L0 SST

  // Push enough additional SSTs so L0 exceeds lsm_sst_level_ratio (default 4)
  // and the next flush triggers L0→L1 compaction.
  write_filler_keys(*eng, *mgr, 3, 10);

  // Flush any remaining memtable data.
  while (eng->memtable.get_total_size() > 0) {
    eng->flush();
  }

  // L1 must be non-empty (compaction happened).
  // L0 may have at most 1 SST remaining (the post-compaction flush).
  ASSERT_LT(eng->level_sst_ids[0].size(), 2u)
      << "Expected at most 1 SST remaining in L0";
  ASSERT_GT(eng->level_sst_ids[1].size(), 0u)
      << "Expected L1 to have SSTs after compaction";

  // tranc_id just below V1 → no version visible for mvcc_key.
  EXPECT_FALSE(eng->get("mvcc_key", V1 - 1).has_value())
      << "tranc_id just below V1 should see no version of mvcc_key";

  // tranc_id=V1 → version_1.
  {
    auto res = eng->get("mvcc_key", V1);
    ASSERT_TRUE(res.has_value()) << "tranc_id=V1 should find mvcc_key";
    EXPECT_EQ(res->first, "version_1")
        << "tranc_id=V1 should return version_1, got: " << res->first;
  }

  // tranc_id=V2 → version_2 (latest visible at id≤V2).
  {
    auto res = eng->get("mvcc_key", V2);
    ASSERT_TRUE(res.has_value()) << "tranc_id=V2 should find mvcc_key";
    EXPECT_EQ(res->first, "version_2")
        << "tranc_id=V2 should return version_2, got: " << res->first;
  }

  // tranc_id=V3 → version_3.
  {
    auto res = eng->get("mvcc_key", V3);
    ASSERT_TRUE(res.has_value()) << "tranc_id=V3 should find mvcc_key";
    EXPECT_EQ(res->first, "version_3")
        << "tranc_id=V3 should return version_3, got: " << res->first;
  }

  // Large tranc_id → version_3 (the latest).
  {
    auto res = eng->get("mvcc_key", V3 + 10000);
    ASSERT_TRUE(res.has_value()) << "large tranc_id should find mvcc_key";
    EXPECT_EQ(res->first, "version_3")
        << "large tranc_id should return version_3, got: " << res->first;
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Test: versions survive a second compaction (L1→L2).
// Uses same setup but writes enough data to push L1 past lsm_sst_level_ratio.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(CompactTest, MultiVersionSurvivedAfterL1Compact) {
  auto &cfg = const_cast<TomlConfig &>(TomlConfig::getInstance());
  cfg.modify_lsm_tol_mem_size_limit(1);
  cfg.modify_lsm_block_size(64);

  auto [eng, mgr] = make_engine();

  const uint64_t V1 = 10000, V2 = 20000;

  // Write two versions of mvcc_key2.
  eng->put("mvcc_key2", "v1", V1);
  write_filler_keys(*eng, *mgr, 1, 100);

  eng->put("mvcc_key2", "v2", V2);
  write_filler_keys(*eng, *mgr, 1, 101);

  // Trigger multiple L0→L1 compactions by writing enough filler.
  // Each batch of (ratio + 1) flushes causes one L0→L1 compaction;
  // repeating it (ratio + 1) times fills L1 and triggers L1→L2.
  int ratio = TomlConfig::getInstance().getLsmSstLevelRatio();
  for (int round = 0; round < ratio + 1; ++round) {
    write_filler_keys(*eng, *mgr, ratio + 1, 200 + round * (ratio + 1));
  }

  while (eng->memtable.get_total_size() > 0) {
    eng->flush();
  }

  // After all compactions there must be data in at least L1 (or L2).
  bool has_deep_level = eng->level_sst_ids[1].size() > 0 ||
                        eng->level_sst_ids[2].size() > 0;
  ASSERT_TRUE(has_deep_level) << "Expected SSTs in L1 or L2 after compaction";

  // tranc_id just below V1 → no version visible.
  EXPECT_FALSE(eng->get("mvcc_key2", V1 - 1).has_value())
      << "tranc_id just below V1 should see no version of mvcc_key2";

  // Version V1 must still be readable.
  {
    auto res = eng->get("mvcc_key2", V1);
    ASSERT_TRUE(res.has_value()) << "tranc_id=V1 should find mvcc_key2";
    EXPECT_EQ(res->first, "v1")
        << "tranc_id=V1 should return v1, got: " << res->first;
  }

  // Version V2 must still be readable.
  {
    auto res = eng->get("mvcc_key2", V2);
    ASSERT_TRUE(res.has_value()) << "tranc_id=V2 should find mvcc_key2";
    EXPECT_EQ(res->first, "v2")
        << "tranc_id=V2 should return v2, got: " << res->first;
  }
}

// ──────────────────────────────────────────────────────────────────────────────
// Test: tombstone survives compaction – a deleted key must not be visible after
// flushing enough data to trigger L0→L1 compaction.
// ──────────────────────────────────────────────────────────────────────────────
TEST_F(CompactTest, TombstoneSurvivedAfterL0Compact) {
  auto &cfg = const_cast<TomlConfig &>(TomlConfig::getInstance());
  cfg.modify_lsm_tol_mem_size_limit(98304);
  cfg.modify_lsm_per_mem_size_limit(4096);
  cfg.modify_lsm_block_size(1024);

  const int N = 30001;
  // Pseudo-shuffle matching random_change_vector from test_lsm.cpp
  std::vector<int> idx1, idx2;
  for (int i = 0; i <= N; ++i) {
    if (i <= N / 2)
      idx1.push_back(i);
    else
      idx2.push_back(i);
  }
  // Apply same shuffle as test_lsm.cpp random_change_vector
  {
    std::vector<int> pri{3, 5, 7, 11, 13};
    int pri_pos = 0;
    int swap_cnt = std::min((int)idx1.size(), 50000);
    for (int i = 0; i < swap_cnt; i++) {
      int x1 = ((i * pri[pri_pos] + i * i + 19) % (idx1.size()));
      int x2 = ((i * pri[(pri_pos + 1) % 5] + i * 3 + 37) % (idx1.size()));
      std::swap(idx1[x1], idx1[x2]);
      pri_pos = (pri_pos + 1) % 5;
    }
  }

  {
    LSM lsm(test_dir);
    for (int i : idx1) {
      lsm.put("key" + std::to_string(i), "value" + std::to_string(i));
    }
    for (int i : idx2) {
      lsm.put("key" + std::to_string(i), "value" + std::to_string(i));
    }
    // Shuffle idx2 the same way before deleting
    {
      std::vector<int> pri{3, 5, 7, 11, 13};
      int pri_pos = 0;
      int swap_cnt = std::min((int)idx2.size(), 50000);
      for (int i = 0; i < swap_cnt; i++) {
        int x1 = ((i * pri[pri_pos] + i * i + 19) % (idx2.size()));
        int x2 = ((i * pri[(pri_pos + 1) % 5] + i * 3 + 37) % (idx2.size()));
        std::swap(idx2[x1], idx2[x2]);
        pri_pos = (pri_pos + 1) % 5;
      }
    }
    for (int i : idx2) {
      lsm.remove("key" + std::to_string(i));
    }
  }

  LSM lsm(test_dir);
  int fail_count = 0;
  for (int i = 0; i <= N; ++i) {
    std::string key = "key" + std::to_string(i);
    if (i <= N / 2) {
      EXPECT_TRUE(lsm.get(key).has_value())
          << "key " << key << " should exist but was not found";
    } else {
      if (lsm.get(key).has_value()) {
        if (fail_count < 5) {
          std::cout << "FAIL: deleted key " << key
                    << " returns value: " << lsm.get(key).value_or("(none)")
                    << "\n";
        }
        fail_count++;
        EXPECT_FALSE(lsm.get(key).has_value())
            << "key " << key << " was deleted but still visible";
      }
    }
  }
  if (fail_count > 0) {
    std::cout << "Total failures: " << fail_count << "\n";
  }
}

// Replicates BigPersistence2 logic with a smaller dataset to debug tombstone
// survival with default config.
TEST_F(CompactTest, TombstoneDefaultConfig) {
  // Reset config to defaults
  auto &cfg = const_cast<TomlConfig &>(TomlConfig::getInstance());
  cfg.modify_lsm_tol_mem_size_limit(67108864);  // 64MB default
  cfg.modify_lsm_per_mem_size_limit(4194304);   // 4MB default
  cfg.modify_lsm_block_size(32768);             // 32KB default

  const int num = 1000000;
  std::vector<int> idx1, idx2;
  for (int i = 0; i <= num; ++i) {
    if (i <= num / 2) idx1.push_back(i);
    else idx2.push_back(i);
  }

  // Shuffle idx1 (exact copy of random_change_vector from test_lsm.cpp)
  {
    std::vector<int> pri{3, 5, 7, 11, 13};
    int pri_pos = 0;
    int swap_cnt = std::min((int)idx1.size(), 50000);
    for (int i = 0; i < swap_cnt; i++) {
      int x1 = ((i * pri[pri_pos] + i * i + 19) % (idx1.size()));
      int x2 = ((i * i * pri[(pri_pos + 1) % 5] * pri[(pri_pos + 1) % 5] + i * i +
                 31) % (idx1.size()));
      std::swap(idx1[x1], idx1[x2]);
      // NOTE: pri_pos is NOT incremented (matches test_lsm.cpp random_change_vector)
    }
  }

  {
    LSM lsm(test_dir);
    for (int i : idx1)
      lsm.put("key" + std::to_string(i), "value" + std::to_string(i));
    for (int i : idx2)
      lsm.put("key" + std::to_string(i), "value" + std::to_string(i));
    // Shuffle idx2 then delete (exact copy of random_change_vector)
    {
      std::vector<int> pri{3, 5, 7, 11, 13};
      int pri_pos = 0;
      int swap_cnt = std::min((int)idx2.size(), 50000);
      for (int i = 0; i < swap_cnt; i++) {
        int x1 = ((i * pri[pri_pos] + i * i + 19) % (idx2.size()));
        int x2 = ((i * i * pri[(pri_pos + 1) % 5] * pri[(pri_pos + 1) % 5] + i * i +
                   31) % (idx2.size()));
        std::swap(idx2[x1], idx2[x2]);
        // NOTE: pri_pos is NOT incremented (matches test_lsm.cpp)
      }
    }
    for (int i : idx2)
      lsm.remove("key" + std::to_string(i));
  }

  // Reload and verify
  {
    LSM lsm(test_dir);
    int fail_count = 0;
    for (int i = 0; i <= num; ++i) {
      std::string key = "key" + std::to_string(i);
      if (i > num / 2) {
        if (lsm.get(key).has_value()) {
          if (fail_count < 5)
            std::cout << "FAIL: deleted key " << key
                      << " = " << lsm.get(key).value_or("?") << "\n";
          fail_count++;
        }
      }
    }
    if (fail_count > 0) std::cout << "Total failures: " << fail_count << "\n";
    EXPECT_EQ(fail_count, 0) << "Some deleted keys are still visible";
  }
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}
