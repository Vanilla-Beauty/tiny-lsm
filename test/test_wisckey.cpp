#include "lsm/engine.h"
#include "config/config.h"
#include "sst/sst.h"
#include "sst/sst_iterator.h"
#include "vlog/vlog.h"
#include "block/block_cache.h"
#include "utils/files.h"
#include <filesystem>
#include <gtest/gtest.h>
#include <string>
#include <vector>

using namespace tiny_lsm;

// ---- helpers -------------------------------------------------------

static std::string make_value(size_t sz, char fill = 'x') {
    return std::string(sz, fill);
}

static std::string tmp_dir(const std::string &name) {
    std::string p = "/tmp/test_wisckey_" + name;
    std::filesystem::remove_all(p);
    std::filesystem::create_directories(p);
    return p;
}

static std::shared_ptr<BlockCache> make_cache() {
    return std::make_shared<BlockCache>(256, 2);
}

// ---- VLog unit tests -----------------------------------------------

TEST(VLogAppendRead, Single) {
    std::string path = "/tmp/test_vlog_single.data";
    std::filesystem::remove(path);

    auto vlog = VLog::open(path);
    uint64_t off = vlog->append("hello", "world");
    EXPECT_EQ(off, 0u);
    EXPECT_EQ(vlog->read_value(off, 5), "world");

    std::filesystem::remove(path);
}

TEST(VLogMultiAppend, HundredRecords) {
    std::string path = "/tmp/test_vlog_multi.data";
    std::filesystem::remove(path);

    auto vlog = VLog::open(path);
    std::vector<uint64_t> offsets;
    std::vector<std::string> values;

    for (int i = 0; i < 100; ++i) {
        std::string key = "key" + std::to_string(i);
        std::string val = make_value(i + 1, 'a' + (i % 26));
        offsets.push_back(vlog->append(key, val));
        values.push_back(val);
    }

    for (int i = 0; i < 100; ++i) {
        auto got = vlog->read_value(offsets[i], static_cast<uint32_t>(values[i].size()));
        EXPECT_EQ(got, values[i]) << "mismatch at i=" << i;
    }

    std::filesystem::remove(path);
}

TEST(VLogReopen, PersistAndContinue) {
    std::string path = "/tmp/test_vlog_reopen.data";
    std::filesystem::remove(path);

    uint64_t off0, off1;
    {
        auto vlog = VLog::open(path);
        off0 = vlog->append("k0", "value0");
        vlog->sync();
    }
    {
        auto vlog = VLog::open(path);
        EXPECT_EQ(vlog->read_value(off0, 6), "value0");
        off1 = vlog->append("k1", "value1");
        EXPECT_GT(off1, off0);
        EXPECT_EQ(vlog->read_value(off1, 6), "value1");
    }

    std::filesystem::remove(path);
}

// ---- SST WiscKey tests -----------------------------------------------

TEST(SSTWiscKeyBuildRead, ThresholdSeparation) {
    std::string dir = tmp_dir("sst_wisckey");
    std::string vlog_path = dir + "/vlog.data";
    std::string sst_path = dir + "/sst_00000.0";

    auto vlog = VLog::open(vlog_path);
    auto cache = make_cache();

    // threshold = 64 bytes: short values inline, long values -> vlog
    SSTBuilder builder(4096, true, vlog, 64);
    builder.add("aaa", "short",                 1); // inline
    builder.add("bbb", make_value(128, 'B'),    1); // vlog
    builder.add("ccc", make_value(200, 'C'),    1); // vlog
    builder.add("ddd", "also-short",            1); // inline

    auto sst = builder.build(0, sst_path, cache);
    EXPECT_TRUE(sst->is_wisckey());

    // Verify footer magic: last byte of file must be 0x4B
    FileObj f = FileObj::open(sst_path, false);
    EXPECT_EQ(f.read_uint8(f.size() - 1), 0x4Bu);
    EXPECT_EQ(f.read_uint8(f.size() - 2), 1u); // storage_mode == 1

    // Read back through iterator
    auto it = sst->begin(1);
    EXPECT_FALSE(it.is_end());
    EXPECT_EQ((*it).second, "short");
    ++it;
    EXPECT_EQ((*it).second, make_value(128, 'B'));
    ++it;
    EXPECT_EQ((*it).second, make_value(200, 'C'));
    ++it;
    EXPECT_EQ((*it).second, "also-short");
    ++it;
    EXPECT_TRUE(it.is_end());

    std::filesystem::remove_all(dir);
}

TEST(SSTBackwardCompat, OldFormat) {
    std::string dir = tmp_dir("sst_compat");
    std::string sst_path = dir + "/sst_00001.0";
    auto cache = make_cache();

    // Old 2-argument builder (inline only)
    SSTBuilder builder(4096, true);
    builder.add("foo", "bar", 1);
    builder.add("zoo", "baz", 1);
    auto sst = builder.build(1, sst_path, cache);

    EXPECT_FALSE(sst->is_wisckey());

    // Reopen without vlog
    auto sst2 = SST::open(1, FileObj::open(sst_path, false), cache);
    EXPECT_FALSE(sst2->is_wisckey());

    auto it = sst2->begin(1);
    EXPECT_EQ((*it).second, "bar");

    std::filesystem::remove_all(dir);
}

TEST(SSTTombstoneInline, EmptyValueAlwaysInline) {
    std::string dir = tmp_dir("sst_tombstone");
    std::string vlog_path = dir + "/vlog.data";
    std::string sst_path = dir + "/sst_00002.0";
    auto vlog = VLog::open(vlog_path);
    auto cache = make_cache();

    // threshold = 1: even 1-byte values would go to vlog, but empty stays inline
    SSTBuilder builder(4096, true, vlog, 1);
    builder.add("tomb", "", 1); // tombstone — empty value must stay inline

    auto sst = builder.build(2, sst_path, cache);
    auto it = sst->begin(1);
    EXPECT_FALSE(it.is_end());
    EXPECT_EQ((*it).second, ""); // tombstone survives as empty

    std::filesystem::remove_all(dir);
}

// ---- LSM integration tests ------------------------------------------

TEST(WiscKeyDisabled, AllInlineFull) {
    std::string dir = tmp_dir("lsm_disabled");
    {
        const_cast<TomlConfig &>(TomlConfig::getInstance())
            .modify_lsm_tol_mem_size_limit(64 * 1024 * 1024);

        LSM lsm(dir);

        for (int i = 0; i < 50; ++i) {
            lsm.put("key" + std::to_string(i), make_value(100, 'v'));
        }
        lsm.flush();

        // All values must be readable
        for (int i = 0; i < 50; ++i) {
            auto res = lsm.get("key" + std::to_string(i));
            ASSERT_TRUE(res.has_value()) << "missing key" << i;
            EXPECT_EQ(res.value(), make_value(100, 'v'));
        }
    }
    std::filesystem::remove_all(dir);
}

TEST(WiscKeyPutGet, LargeValueViaVLog) {
    std::string dir = tmp_dir("lsm_putget");
    std::string vlog_path = dir + "/vlog.data";

    {
        auto vlog = VLog::open(vlog_path);
        auto cache = make_cache();

        // Build a WiscKey SST directly
        std::string sst_path = dir + "/sst_wk.0";
        SSTBuilder builder(4096, true, vlog, 64);
        std::string large = make_value(200, 'Z');
        builder.add("mykey", large, 1);
        auto sst = builder.build(0, sst_path, cache);

        EXPECT_TRUE(sst->is_wisckey());
        auto it = sst->get("mykey", 1);
        EXPECT_FALSE(it.is_end());
        EXPECT_EQ((*it).second, large);
    }
    std::filesystem::remove_all(dir);
}

TEST(WiscKeyPutRemove, TombstoneCorrect) {
    std::string dir = tmp_dir("lsm_remove");
    std::string vlog_path = dir + "/vlog.data";

    {
        auto vlog = VLog::open(vlog_path);
        auto cache = make_cache();

        std::string sst_path = dir + "/sst_wk2.0";
        SSTBuilder builder(4096, true, vlog, 64);
        std::string large = make_value(200, 'R');
        builder.add("delkey", large,  1);
        builder.add("delkey", "",     2); // tombstone at newer tranc_id
        auto sst = builder.build(0, sst_path, cache);

        // Iterate with keep_all_versions=true to see all entries
        auto it = sst->begin(2, true);
        EXPECT_FALSE(it.is_end());
        // First entry seen is tranc_id=1 (insertion order), resolved via vlog
        EXPECT_EQ((*it).second, large);
        ++it;
        // Second entry is the tombstone at tranc_id=2
        EXPECT_EQ((*it).second, "");
    }
    std::filesystem::remove_all(dir);
}

TEST(WiscKeyPersistence, FlushAndReopen) {
    std::string dir = tmp_dir("lsm_persist");

    const int N = 200;
    const size_t SMALL = 10;
    const size_t LARGE = 128;

    // Phase 1: write
    {
        const_cast<TomlConfig &>(TomlConfig::getInstance())
            .modify_lsm_tol_mem_size_limit(1); // force flush every put
        LSM lsm(dir);

        for (int i = 0; i < N; ++i) {
            std::string key = "key" + std::to_string(i);
            size_t sz = (i % 2 == 0) ? SMALL : LARGE;
            lsm.put(key, make_value(sz, 'a' + (i % 26)));
        }
        lsm.flush_all();
    }

    // Phase 2: reopen and verify
    {
        const_cast<TomlConfig &>(TomlConfig::getInstance())
            .modify_lsm_tol_mem_size_limit(64 * 1024 * 1024);
        LSM lsm(dir);
        for (int i = 0; i < N; ++i) {
            std::string key = "key" + std::to_string(i);
            size_t sz = (i % 2 == 0) ? SMALL : LARGE;
            auto res = lsm.get(key);
            ASSERT_TRUE(res.has_value()) << "missing " << key;
            EXPECT_EQ(res.value(), make_value(sz, 'a' + (i % 26))) << key;
        }
    }

    std::filesystem::remove_all(dir);
}

TEST(WiscKeyCompaction, L0L1Compact) {
    std::string dir = tmp_dir("lsm_compact");

    {
        // Small mem limit forces many flushes -> compaction
        const_cast<TomlConfig &>(TomlConfig::getInstance())
            .modify_lsm_tol_mem_size_limit(1);
        LSM lsm(dir);

        for (int i = 0; i < 100; ++i) {
            std::string key = "ckey" + std::to_string(i);
            lsm.put(key, make_value(80, 'c'));
        }
        lsm.flush_all();

        // All values still accessible after compaction
        for (int i = 0; i < 100; ++i) {
            std::string key = "ckey" + std::to_string(i);
            auto res = lsm.get(key);
            ASSERT_TRUE(res.has_value()) << "missing " << key;
            EXPECT_EQ(res.value(), make_value(80, 'c'));
        }
    }

    std::filesystem::remove_all(dir);
}

int main(int argc, char **argv) {
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
