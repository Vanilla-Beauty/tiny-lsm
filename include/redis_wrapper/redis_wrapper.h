#pragma once
#include "lsm/engine.h"
#include <memory>

namespace tiny_lsm {
std::vector<std::string>
get_fileds_from_hash_value(const std::optional<std::string> &field_list_opt);

std::string get_hash_value_from_fields(const std::vector<std::string> &fields);

inline std::string get_hash_filed_key(const std::string &key,
                                      const std::string &field);

inline bool is_value_hash(const std::string &key);

inline std::string get_explire_key(const std::string &key);

class RedisWrapper {
private:
  std::unique_ptr<LSM> lsm;
  std::shared_mutex redis_mtx;

private:
  // 检查 hash 的 key 是否过期并清理, 过期返回 true
  bool expire_hash_clean(const std::string &key,
                         std::shared_lock<std::shared_mutex> &rlock);

  bool expire_list_clean(const std::string &key,
                         std::shared_lock<std::shared_mutex> &rlock);
  bool expire_zset_clean(const std::string &key,
                         std::shared_lock<std::shared_mutex> &rlock);
  bool expire_set_clean(const std::string &key,
                        std::shared_lock<std::shared_mutex> &rlock);

public:
  RedisWrapper(const std::string &db_path);
  void clear();
  void flushall();

  // ************************* Redis Command Parser *************************
  // 基础操作
  std::string set(std::vector<std::string> &args);
  std::string get(std::vector<std::string> &args);
  std::string incr(std::vector<std::string> &args);
  std::string decr(std::vector<std::string> &args);
  std::string expire(std::vector<std::string> &args);
  std::string del(std::vector<std::string> &args);
  std::string ttl(std::vector<std::string> &args);
  // 哈希操作
  std::string hset(std::vector<std::string> &args);
  std::string hget(std::vector<std::string> &args);
  std::string hdel(std::vector<std::string> &args);
  std::string hkeys(std::vector<std::string> &args);
  // 链表操作
  std::string lpush(std::vector<std::string> &args);
  std::string rpush(std::vector<std::string> &args);
  std::string lpop(std::vector<std::string> &args);
  std::string rpop(std::vector<std::string> &args);
  std::string llen(std::vector<std::string> &args);
  std::string lrange(std::vector<std::string> &args);
  // 有序集合操作
  std::string zadd(std::vector<std::string> &args);
  std::string zrem(std::vector<std::string> &args);
  std::string zrange(std::vector<std::string> &args);
  std::string zcard(std::vector<std::string> &args);
  std::string zscore(std::vector<std::string> &args);
  std::string zincrby(std::vector<std::string> &args);
  std::string zrank(std::vector<std::string> &args);
  // 无序集合操作
  std::string sadd(std::vector<std::string> &args);
  std::string srem(std::vector<std::string> &args);
  std::string sismember(std::vector<std::string> &args);
  std::string scard(std::vector<std::string> &args);
  std::string smembers(std::vector<std::string> &args);

private:
  // ************************* Redis Command Handler *************************
  // 基础操作
  std::string redis_incr(const std::string &key);
  std::string redis_decr(const std::string &key);
  std::string redis_expire(const std::string &key, std::string seconds_count);
  std::string redis_set(std::string &key, std::string &value);
  std::string redis_get(std::string &key);
  std::string redis_del(std::vector<std::string> &args);
  std::string redis_ttl(std::string &key);

  // 哈希操作
  std::string redis_hset(const std::string &key, const std::string &field,
                         const std::string &value);
  std::string redis_hset_batch(
      const std::string &key,
      std::vector<std::pair<std::string, std::string>> &field_value_pairs);
  std::string redis_hget(const std::string &key, const std::string &field);
  std::string redis_hdel(const std::string &key, const std::string &field);
  std::string redis_hkeys(const std::string &key);
  // 链表操作
  std::string redis_lpush(const std::string &key, const std::string &value);
  std::string redis_rpush(const std::string &key, const std::string &value);
  std::string redis_lpop(const std::string &key);
  std::string redis_rpop(const std::string &key);
  std::string redis_llen(const std::string &key);
  std::string redis_lrange(const std::string &key, int start, int stop);
  // 有序集合操作
  std::string redis_zadd(std::vector<std::string> &args);
  std::string redis_zrem(std::vector<std::string> &args);
  std::string redis_zrange(std::vector<std::string> &args);
  std::string redis_zcard(const std::string &key);
  std::string redis_zscore(const std::string &key, const std::string &elem);
  std::string redis_zincrby(const std::string &key,
                            const std::string &increment,
                            const std::string &elem);
  std::string redis_zrank(const std::string &key, const std::string &elem);
  // 无序集合操作
  std::string redis_sadd(std::vector<std::string> &args);
  std::string redis_srem(std::vector<std::string> &args);
  std::string redis_sismember(const std::string &key,
                              const std::string &member);
  std::string redis_scard(const std::string &key);
  std::string redis_smembers(const std::string &key);
};
} // namespace tiny_lsm
