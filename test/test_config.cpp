#include "config/config.h"
#include "logger/logger.h"
#include <gtest/gtest.h>

using namespace ::tiny_lsm;

TEST(ConfigTest, BasicTest) {
  // 编译后的单元测试位置为: build/linux/x86_64/debug/test_config
  // 而配置文件放在项目根目录
  TomlConfig gConfig = TomlConfig::getInstance("../../../../config.toml");

  EXPECT_EQ(gConfig.getLsmBlockSize(), 32768);
}

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}