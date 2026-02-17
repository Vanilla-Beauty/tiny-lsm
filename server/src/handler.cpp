#include "../include/handler.h"

// 将字符串转换为小写
std::string toLower(const std::string &str) {
  std::string lowerStr = str;
  std::transform(lowerStr.begin(), lowerStr.end(), lowerStr.begin(),
                 [](unsigned char c) { return std::tolower(c); });
  return lowerStr;
}

const std::unordered_map<std::string, OPS> &getOpsMap() {
  static const std::unordered_map<std::string, OPS> opsMap = {
      {"ping", OPS::PING},       {"flushall", OPS::FLUSHALL},
      {"save", OPS::SAVE},       {"get", OPS::GET},
      {"set", OPS::SET},         {"del", OPS::DEL},
      {"incr", OPS::INCR},       {"decr", OPS::DECR},
      {"expire", OPS::EXPIRE},   {"ttl", OPS::TTL},
      {"hset", OPS::HSET},       {"hget", OPS::HGET},
      {"hdel", OPS::HDEL},       {"hkeys", OPS::HKEYS},
      {"rpush", OPS::RPUSH},     {"lpush", OPS::LPUSH},
      {"rpop", OPS::RPOP},       {"lpop", OPS::LPOP},
      {"llen", OPS::LLEN},       {"lrange", OPS::LRANGE},
      {"zadd", OPS::ZADD},       {"zcard", OPS::ZCARD},
      {"zincrby", OPS::ZINCRBY}, {"zrange", OPS::ZRANGE},
      {"zrank", OPS::ZRANK},     {"zrem", OPS::ZREM},
      {"zscore", OPS::ZSCORE},   {"sadd", OPS::SADD},
      {"scard", OPS::SCARD},     {"smembers", OPS::SMEMBERS},
      {"srem", OPS::SREM},       {"sismember", OPS::SISMEMBER}};
  return opsMap;
}

OPS string2Ops(const std::string &opStr) {
  std::string lowerOpStr = toLower(opStr);
  const auto &opsMap = getOpsMap();
  auto it = opsMap.find(lowerOpStr);
  return (it != opsMap.end()) ? it->second : OPS::UNKNOWN;
}

std::string flushall_handler(RedisWrapper &engine) {
  engine.clear();
  return "+OK\r\n";
}

std::string save_handler(RedisWrapper &engine) {
  // 这里数据库中的flush是指刷盘的意思, 和redis中的flush含义不同
  engine.flushall();
  return "+OK\r\n";
}

// **************************** 基础操作 ****************************
std::string set_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 3) {
    return "-ERR wrong number of arguments for 'SET' command\r\n";
  }

  return engine.set(args);
}

std::string get_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'GET' command\r\n";

  return engine.get(args);
}

std::string del_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 2)
    return "-ERR wrong number of arguments for 'DEL' command\r\n";

  return engine.del(args);
}

std::string incr_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'INCR' command\r\n";

  auto res = engine.incr(args);

  return ":" + res + "\r\n";
}

std::string decr_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'DECR' command\r\n";

  auto res = engine.decr(args);

  return ":" + res + "\r\n";
}

std::string expire_handler(std::vector<std::string> &args,
                           RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'EXPIRE' command\r\n";

  return engine.expire(args);
}

std::string ttl_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'TTL' command\r\n";

  return engine.ttl(args);
}
// **************************** 哈希操作 ****************************
std::string hset_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 4)
    return "-ERR wrong number of arguments for 'HSET' command\r\n";

  return engine.hset(args);
}

std::string hget_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'HGET' command\r\n";

  return engine.hget(args);
}

std::string hdel_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'HDEL' command\r\n";

  return engine.hdel(args);
}

std::string hkeys_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'HKEYS' command\r\n";

  return engine.hkeys(args);
}

// ***************************** 链表操作 ****************************
std::string lpush_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'LPUSH' command\r\n";

  return engine.lpush(args);
}

std::string rpush_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'RPUSH' command\r\n";

  return engine.rpush(args);
}

std::string lpop_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'LPOP' command\r\n";

  return engine.lpop(args);
}

std::string rpop_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 2)
    return "-ERR wrong number of arguments for 'RPOP' command\r\n";

  return engine.rpop(args);
}

std::string llen_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() != 3)
    return "-ERR wrong number of arguments for 'LLEN' command\r\n";

  return engine.llen(args);
}

std::string lrange_handler(std::vector<std::string> &args,
                           RedisWrapper &engine) {
  if (args.size() != 4)
    return "-ERR wrong number of arguments for 'LRANGE' command\r\n";

  return engine.lrange(args);
}

// ****************************** 有序集合操作 ******************************
std::string zadd_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 4 || (args.size() - 2) % 2 != 0) {
    return "-ERR wrong number of arguments for 'zadd' command\r\n";
  }
  return engine.zadd(args);
}
std::string zrem_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 3) {
    return "-ERR wrong number of arguments for 'zrem' command\r\n";
  }
  return engine.zrem(args);
}
std::string zrange_handler(std::vector<std::string> &args,
                           RedisWrapper &engine) {
  if (args.size() < 4) {
    return "-ERR wrong number of arguments for 'zrange' command\r\n";
  }
  return engine.zrange(args);
}
std::string zcard_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 2) {
    return "-ERR wrong number of arguments for 'zcard' command\r\n";
  }
  return engine.zcard(args);
}
std::string zscore_handler(std::vector<std::string> &args,
                           RedisWrapper &engine) {
  if (args.size() != 3) {
    return "-ERR wrong number of arguments for 'zscore' command\r\n";
  }
  return engine.zscore(args);
}
std::string zincrby_handler(std::vector<std::string> &args,
                            RedisWrapper &engine) {
  if (args.size() != 4) {
    return "-ERR wrong number of arguments for 'zincrby' command\r\n";
  }
  return engine.zincrby(args);
}

std::string zrank_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 3) {
    return "-ERR wrong number of arguments for 'zrank' command\r\n";
  }
  return engine.zrank(args);
}

// ******************************* 无序集合操作 ******************************
std::string sadd_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 3) {
    return "-ERR wrong number of arguments for 'sadd' command\r\n";
  }
  return engine.sadd(args);
}

std::string srem_handler(std::vector<std::string> &args, RedisWrapper &engine) {
  if (args.size() < 3) {
    return "-ERR wrong number of arguments for 'srem' command\r\n";
  }
  return engine.srem(args);
}

std::string sismember_handler(std::vector<std::string> &args,
                              RedisWrapper &engine) {
  if (args.size() != 3) {
    return "-ERR wrong number of arguments for 'sismember' command\r\n";
  }
  return engine.sismember(args);
}

std::string scard_handler(std::vector<std::string> &args,
                          RedisWrapper &engine) {
  if (args.size() != 2) {
    return "-ERR wrong number of arguments for 'scard' command\r\n";
  }
  return engine.scard(args);
}

std::string smembers_handler(std::vector<std::string> &args,
                             RedisWrapper &engine) {
  if (args.size() != 2) {
    return "-ERR wrong number of arguments for 'smembers' command\r\n";
  }
  return engine.smembers(args);
}