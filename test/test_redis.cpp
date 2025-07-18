#include "../include/logger/logger.h"
#include "../include/redis_wrapper/redis_wrapper.h"
#include <gtest/gtest.h>
#include <memory>
#include <string>
#include <vector>

using namespace ::tiny_lsm;

class RedisCommandsTest : public ::testing::Test {
protected:
  void SetUp() override {
    // 初始化 LSM 引擎
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

TEST_F(RedisCommandsTest, SetAndGet) {
  RedisWrapper lsm(test_dir);

  std::vector<std::string> set_args = {"SET", "mykey", "myvalue"};
  std::vector<std::string> get_args = {"GET", "mykey"};

  EXPECT_EQ(lsm.set(set_args), "+OK\r\n");
  EXPECT_EQ(lsm.get(get_args), "$7\r\nmyvalue\r\n");
}

TEST_F(RedisCommandsTest, IncrAndDecr) {
  RedisWrapper lsm(test_dir);

  std::vector<std::string> incr_args = {"INCR", "counter"};
  std::vector<std::string> decr_args = {"DECR", "counter"};

  EXPECT_EQ(lsm.incr(incr_args), "1");
  EXPECT_EQ(lsm.incr(incr_args), "2");
  EXPECT_EQ(lsm.decr(decr_args), "1");
  EXPECT_EQ(lsm.decr(decr_args), "0");
  EXPECT_EQ(lsm.decr(decr_args), "-1");
}

TEST_F(RedisCommandsTest, Expire) {
  RedisWrapper lsm(test_dir);

  std::vector<std::string> set_args = {"SET", "mykey", "myvalue"};
  std::vector<std::string> expire_args = {"EXPIRE", "mykey", "1"};
  std::vector<std::string> get_args = {"GET", "mykey"};

  EXPECT_EQ(lsm.set(set_args), "+OK\r\n");

  // GET key
  EXPECT_EQ(lsm.get(get_args), "$7\r\nmyvalue\r\n");

  // 设置过期时间
  EXPECT_EQ(lsm.expire(expire_args), ":1\r\n");

  // 马上 GET key
  EXPECT_EQ(lsm.get(get_args), "$7\r\nmyvalue\r\n");

  // 等待 2 秒，确保键过期
  std::this_thread::sleep_for(std::chrono::seconds(2));
  EXPECT_EQ(lsm.get(get_args), "$-1\r\n");
}

TEST_F(RedisCommandsTest, HSetAndHGet) {
  RedisWrapper lsm(test_dir);

  std::string key = "myhash";
  std::string field1 = "field1";
  std::string value1 = "value1";
  std::string field2 = "field2";
  std::string value2 = "value2";

  // HSET
  std::vector<std::string> hset_args1 = {"HSET", key, field1, value1};
  EXPECT_EQ(lsm.hset(hset_args1), ":1\r\n");
  std::vector<std::string> hset_args2 = {"HSET", key, field2, value2};
  EXPECT_EQ(lsm.hset(hset_args2), ":1\r\n");

  // HGET
  std::vector<std::string> hget_args1 = {"HGET", key, field1};
  EXPECT_EQ(lsm.hget(hget_args1),
            "$" + std::to_string(value1.size()) + "\r\n" + value1 + "\r\n");
  std::vector<std::string> hget_args2 = {"HGET", key, field2};
  EXPECT_EQ(lsm.hget(hget_args2),
            "$" + std::to_string(value2.size()) + "\r\n" + value2 + "\r\n");
}

TEST_F(RedisCommandsTest, HDel) {
  RedisWrapper lsm(test_dir);

  std::string key = "myhash";
  std::string field1 = "field1";
  std::string value1 = "value1";
  std::string field2 = "field2";
  std::string value2 = "value2";

  // HSET
  std::vector<std::string> hset_args1 = {"HSET", key, field1, value1};
  EXPECT_EQ(lsm.hset(hset_args1), ":1\r\n");
  std::vector<std::string> hset_args2 = {"HSET", key, field2, value2};
  EXPECT_EQ(lsm.hset(hset_args2), ":1\r\n");

  // HDEL
  std::vector<std::string> hdel_args = {"HDEL", key, field1};
  EXPECT_EQ(lsm.hdel(hdel_args), ":1\r\n");
  std::vector<std::string> hget_args1 = {"HGET", key, field1};
  EXPECT_EQ(lsm.hget(hget_args1), "$-1\r\n"); // Field1 should be deleted
  std::vector<std::string> hget_args2 = {"HGET", key, field2};
  EXPECT_EQ(lsm.hget(hget_args2), "$" + std::to_string(value2.size()) + "\r\n" +
                                      value2 +
                                      "\r\n"); // Field2 should still exist
}

TEST_F(RedisCommandsTest, HKeys) {
  RedisWrapper lsm(test_dir);

  std::string key = "myhash";
  std::string field1 = "field1";
  std::string value1 = "value1";
  std::string field2 = "field2";
  std::string value2 = "value2";

  // HSET
  std::vector<std::string> hset_args1 = {"HSET", key, field1, value1};
  EXPECT_EQ(lsm.hset(hset_args1), ":1\r\n");
  std::vector<std::string> hset_args2 = {"HSET", key, field2, value2};
  EXPECT_EQ(lsm.hset(hset_args2), ":1\r\n");

  // HKEYS
  std::string expected_keys =
      "*2\r\n$" + std::to_string(field1.size()) + "\r\n" + field1 + "\r\n$" +
      std::to_string(field2.size()) + "\r\n" + field2 + "\r\n";
  std::vector<std::string> hkeys_args = {"HKEYS", key};
  EXPECT_EQ(lsm.hkeys(hkeys_args), expected_keys);
}

TEST_F(RedisCommandsTest, HGetWithTTL) {
  RedisWrapper lsm(test_dir);

  std::string key = "myhash";
  std::string field = "field1";
  std::string value = "value1";

  // HSET
  std::vector<std::string> hset_args = {"HSET", key, field, value};
  EXPECT_EQ(lsm.hset(hset_args), ":1\r\n");

  // Set TTL
  std::vector<std::string> args{"EXPIRE", key, "1"};
  EXPECT_EQ(lsm.expire(args), ":1\r\n");

  // Wait for TTL to expire
  std::this_thread::sleep_for(std::chrono::seconds(2) +
                              std::chrono::milliseconds(100)); // 1.1 s

  // HGET after TTL expired
  std::vector<std::string> hget_args = {"HGET", key, field};
  // EXPECT_EQ(lsm.hget(hget_args), "$-1\r\n");
}

TEST_F(RedisCommandsTest, HExpire) {
  RedisWrapper lsm(test_dir);

  std::string key = "myhash";
  std::string field = "field1";
  std::string value = "value1";
  std::string value2 = "value2";

  // HSET
  std::vector<std::string> hset_args = {"HSET", key, field, value};
  EXPECT_EQ(lsm.hset(hset_args), ":1\r\n");

  // Set TTL
  std::vector<std::string> args{"EXPIRE", key, "1"};
  EXPECT_EQ(lsm.expire(args), ":1\r\n");

  // Wait for TTL to expire
  std::this_thread::sleep_for(std::chrono::seconds(1) +
                              std::chrono::milliseconds(500)); // 1.5 s

  // HGET after TTL expired
  std::vector<std::string> hset_args2 = {"HSET", key, field, value2};
  auto res = lsm.hset(hset_args2);
  EXPECT_EQ(res, ":1\r\n");

  // HGET after setting new value
  std::vector<std::string> hget_args = {"HGET", key, field};
  EXPECT_EQ(lsm.hget(hget_args), "$6\r\n" + value2 + "\r\n");
}

TEST_F(RedisCommandsTest, ListOperations) {
  RedisWrapper lsm(test_dir);

  std::string key = "mylist";
  std::string value1 = "value1";
  std::string value2 = "value2";
  std::string value3 = "value3";

  // LPUSH
  std::vector<std::string> lpush_args1 = {"LPUSH", key, value1};
  EXPECT_EQ(lsm.lpush(lpush_args1), ":1\r\n");
  std::vector<std::string> lpush_args2 = {"LPUSH", key, value2};
  EXPECT_EQ(lsm.lpush(lpush_args2), ":2\r\n");

  // RPUSH
  std::vector<std::string> rpush_args1 = {"RPUSH", key, value3};
  EXPECT_EQ(lsm.rpush(rpush_args1), ":3\r\n");

  // LLEN
  std::vector<std::string> llen_args = {"LLEN", key};
  EXPECT_EQ(lsm.llen(llen_args), ":3\r\n");

  // LRANGE
  std::vector<std::string> lrange_args1 = {"LRANGE", key, "0", "-1"};
  std::string expected_lrange1 =
      "*3\r\n$6\r\nvalue2\r\n$6\r\nvalue1\r\n$6\r\nvalue3\r\n";
  EXPECT_EQ(lsm.lrange(lrange_args1), expected_lrange1);

  // LPOP
  std::vector<std::string> lpop_args = {"LPOP", key};
  EXPECT_EQ(lsm.lpop(lpop_args), "$6\r\nvalue2\r\n");

  // RPOP
  std::vector<std::string> rpop_args = {"RPOP", key};
  EXPECT_EQ(lsm.rpop(rpop_args), "$6\r\nvalue3\r\n");

  // LLEN after LPOP and RPOP
  EXPECT_EQ(lsm.llen(llen_args), ":1\r\n");

  // LRANGE after LPOP and RPOP
  std::vector<std::string> lrange_args2 = {"LRANGE", key, "0", "-1"};
  std::string expected_lrange2 = "*1\r\n$6\r\nvalue1\r\n";
  EXPECT_EQ(lsm.lrange(lrange_args2), expected_lrange2);
}
TEST_F(RedisCommandsTest, ZSetOperations) {
  RedisWrapper lsm(test_dir);

  // 1. 使用 ZADD 添加多个成员到有序集合
  std::vector<std::string> zadd_args1 = {"ZADD", "myzset", "1", "one",
                                         "2",    "two",    "3", "three"};
  auto res = lsm.zadd(zadd_args1);
  EXPECT_EQ(res, ":3\r\n");

  // 2. 使用 ZRANGE 查询有序集合中的成员
  std::vector<std::string> zrange_args1 = {"ZRANGE", "myzset", "0", "-1"};
  res = lsm.zrange(zrange_args1);
  EXPECT_EQ(res, "*3\r\n$3\r\none\r\n$3\r\ntwo\r\n$5\r\nthree\r\n");

  // 3. 使用 ZCARD 获取有序集合的成员数量
  std::vector<std::string> zcard_args1 = {"ZCARD", "myzset"};
  res = lsm.zcard(zcard_args1);
  EXPECT_EQ(res, ":3\r\n");

  // 4. 使用 ZSCORE 获取特定成员的分数
  std::vector<std::string> zscore_args1 = {"ZSCORE", "myzset", "two"};
  res = lsm.zscore(zscore_args1);
  EXPECT_EQ(res, "$1\r\n2\r\n");

  // 5. 使用 ZINCRBY 增加特定成员的分数
  std::vector<std::string> zincrby_args1 = {"ZINCRBY", "myzset", "2", "two"};
  res = lsm.zincrby(zincrby_args1);
  EXPECT_EQ(res, ":4\r\n"); // TODO 需要与实际的 redis-server 返回响应对比

  // 6. 再次使用 ZRANGE 查询有序集合中的成员，验证分数是否更新
  res = lsm.zrange(zrange_args1);
  EXPECT_EQ(res, "*3\r\n$3\r\none\r\n$5\r\nthree\r\n$3\r\ntwo\r\n");

  // 7. 使用 ZREM 删除特定成员
  std::vector<std::string> zrem_args1 = {"ZREM", "myzset", "one"};
  res = lsm.zrem(zrem_args1);
  EXPECT_EQ(res, ":1\r\n");

  // 8. 再次使用 ZCARD 获取有序集合的成员数量，验证成员是否被删除
  res = lsm.zcard(zcard_args1);
  EXPECT_EQ(res, ":2\r\n");
}

TEST_F(RedisCommandsTest, SetOperations) {
  RedisWrapper lsm(test_dir);

  // 测试 sadd 命令
  std::vector<std::string> sadd_args1 = {"SADD", "myset", "member1", "member2",
                                         "member3"};
  EXPECT_EQ(lsm.sadd(sadd_args1), ":3\r\n");

  // 测试 scard 命令
  std::vector<std::string> scard_args = {"SCARD", "myset"};
  EXPECT_EQ(lsm.scard(scard_args), ":3\r\n");

  // 测试 sismember 命令
  std::vector<std::string> sismember_args1 = {"SISMEMBER", "myset", "member1"};
  EXPECT_EQ(lsm.sismember(sismember_args1), ":1\r\n");
  std::vector<std::string> sismember_args2 = {"SISMEMBER", "myset", "member4"};
  EXPECT_EQ(lsm.sismember(sismember_args2), ":0\r\n");

  // 测试 smembers 命令
  std::vector<std::string> smembers_args = {"SMEMBERS", "myset"};
  std::string expected_smembers =
      "*3\r\n$7\r\nmember1\r\n$7\r\nmember2\r\n$7\r\nmember3\r\n";
  EXPECT_EQ(lsm.smembers(smembers_args), expected_smembers);

  // 测试 srem 命令
  std::vector<std::string> srem_args1 = {"SREM", "myset", "member1", "member3"};
  EXPECT_EQ(lsm.srem(srem_args1), ":2\r\n");

  // 再次测试 scard 命令
  EXPECT_EQ(lsm.scard(scard_args), ":1\r\n");

  // 再次测试 smembers 命令
  std::string expected_smembers_after_rem = "*1\r\n$7\r\nmember2\r\n";
  EXPECT_EQ(lsm.smembers(smembers_args), expected_smembers_after_rem);

  // 再次测试 sismember 命令
  std::vector<std::string> sismember_args3 = {"SISMEMBER", "myset", "member1"};
  EXPECT_EQ(lsm.sismember(sismember_args3), ":0\r\n");
  std::vector<std::string> sismember_args4 = {"SISMEMBER", "myset", "member2"};
  EXPECT_EQ(lsm.sismember(sismember_args4), ":1\r\n");
  std::vector<std::string> sismember_args5 = {"SISMEMBER", "myset", "member3"};
  EXPECT_EQ(lsm.sismember(sismember_args5), ":0\r\n");
}
int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  init_spdlog_file();
  return RUN_ALL_TESTS();
}