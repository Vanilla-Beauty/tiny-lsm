#include "block/block.h"
#include "block/block_cache.h"
#include "logger/logger.h"
#include <gtest/gtest.h>
#include <memory>
#include <vector>

using namespace ::tiny_lsm;

class BlockCacheTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 初始化缓存池，容量为3，K值为2
    cache = std::make_unique<BlockCache>(3, 2);
  }

  std::unique_ptr<BlockCache> cache;
};

TEST_F(BlockCacheTest, PutAndGet) {
  auto block1 = std::make_shared<Block>();
  auto block2 = std::make_shared<Block>();
  auto block3 = std::make_shared<Block>();

  cache->put(1, 1, block1);
  cache->put(1, 2, block2);
  cache->put(1, 3, block3);

  EXPECT_EQ(cache->get(1, 1), block1);
  EXPECT_EQ(cache->get(1, 2), block2);
  EXPECT_EQ(cache->get(1, 3), block3);
}

TEST_F(BlockCacheTest, CacheEviction1) {
  auto block1 = std::make_shared<Block>();
  auto block2 = std::make_shared<Block>();
  auto block3 = std::make_shared<Block>();
  auto block4 = std::make_shared<Block>();

  cache->put(1, 1, block1);
  cache->put(1, 2, block2);
  cache->put(1, 3, block3);

  // 访问 block1 和 block2
  cache->get(1, 1);
  cache->get(1, 2);

  // 插入 block4，应该驱逐 block3
  cache->put(1, 4, block4);

  EXPECT_EQ(cache->get(1, 1), block1);
  EXPECT_EQ(cache->get(1, 2), block2);
  EXPECT_EQ(cache->get(1, 3), nullptr); // block3 被驱逐
  EXPECT_EQ(cache->get(1, 4), block4);
}

TEST_F(BlockCacheTest, CacheEviction2) {
  auto block1 = std::make_shared<Block>();
  auto block2 = std::make_shared<Block>();
  auto block3 = std::make_shared<Block>();
  auto block4 = std::make_shared<Block>();

  cache->put(1, 1, block1);
  cache->put(1, 2, block2);
  cache->put(1, 3, block3);

  // 访问 block1 和 block2
  cache->get(1, 1);
  cache->get(1, 2);
  cache->get(1, 3);

  // 插入 block4，应该驱逐 block3
  cache->put(1, 4, block4);

  EXPECT_EQ(cache->get(1, 1), nullptr); // block1 被驱逐
  EXPECT_EQ(cache->get(1, 2), block2);
  EXPECT_EQ(cache->get(1, 3), block3);
  EXPECT_EQ(cache->get(1, 4), block4);
}

TEST_F(BlockCacheTest, HitRate) {
  auto block1 = std::make_shared<Block>();
  auto block2 = std::make_shared<Block>();

  cache->put(1, 1, block1);
  cache->put(1, 2, block2);

  // 访问 block1 和 block2
  cache->get(1, 1);
  cache->get(1, 2);

  // 访问不存在的 block3
  cache->get(1, 3);

  EXPECT_EQ(cache->hit_rate(), 2.0 / 3.0);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}
