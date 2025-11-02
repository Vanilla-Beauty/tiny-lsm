# Lab 6.2 哈希表
# 1 Redis 实现思路
`Redis`的哈希结构就是一个`key`中管理了一个哈希表, 这里的实现有以下方案
**方案1-序列化整个哈希数据结构到value**
你可以采用自己编码或者引入第三方库的形式, 将整个哈希数据结构序列化成字符串, 然后存储到`value`中, 这样查询的时候直接反序列化即可。
- **优点**: 查询方便, 不需要关心序列化功能的具体实现
- **缺点**: 序列化/反序列化耗时, 占用内存, 不利于扩展。比如，你对哈希结构中的一个`field`进行修改, 需要重新序列化整个哈希数据结构

**方案2-field分离存储**
这里的方案类似我们之前设置超时时间的一种解决方案, 你可以将每个`field`作为额外的键值对进行存储, 然后代表整个哈希结构的`key`的`value`中可以存储这些`field`的元信息, 下面给出一种具体的实现思路供你参考:

1. 整个`hash`数据结构的`key`存储的`value`是其所有`field`的集合拼接的字符串
2. 每个`field`的`value`单独用另一个键值对存储, 但`field`不能直接作为一个`key`, 而是需要加上指定的前缀以标识这是一个哈希结构的`field`

那么查询的逻辑就是, 将`key`和`field`拼接起来形成实际存储的`key`, 然后查询对应的`value`即可

# 2 TTL 设计
同样, 你的哈希结构也需要支持`TTL`和`Expire`命令, 不过这2个命令针对的对象是整个哈希结构, 你不需要考虑对哈希中的一个`field`设计超时时间。

需要注意的是，你的实现方案同样需要考虑到旧数据的清理流程。

最后， 如果你的哈希结构采用的是分离存储`field`的方式（这个方式其实更推荐），你需要对内部函数的并发控制进行处理，因为这里你操作的对象可能涉及底层`LSM Tree`中的多个`Key`, 因此你需要灵活地利用批量化的操作接口, 以及自身组件的上锁与解锁逻辑控制。

> 你在不同操作时, 建议利用好读写锁相比独占锁的优势, 以及在必要时进行锁升级

# 3 代码实现
你需要修改的代码文件包括:
- `src/redis_wrapper/redis_wrapper.cpp`
- `include/redis_wrapper/redis_wrapper.h` (Optional)

> 下面的接口中, 你仍然需要进行`TTL`超时时间的判断, 同时你可能需要更新之前的`redis_ttl`和`redis_expire`以兼容`Hash`的`TTL`机制。

## 3.1 hset
```cpp

std::string RedisWrapper::redis_hset_batch(
    const std::string &key,
    std::vector<std::pair<std::string, std::string>> &field_value_pairs) {
  std::shared_lock<std::shared_mutex> rlock(redis_mtx);
  // TODO: Lab 6.2 批量设置一个哈希类型的`key`的多个字段值
  // ? 返回值的格式, 你需要查询 RESP 官方文档或者问 LLM
  int added_count = 0;
  return ":" + std::to_string(added_count) + "\r\n";
}
std::string RedisWrapper::redis_hset(const std::string &key,
                                     const std::string &field,
                                     const std::string &value) {
  std::shared_lock<std::shared_mutex> rlock(redis_mtx); // 读锁线判断是否过期
  // TODO: Lab 6.2 设置一个哈希类型的`key`的某个字段值
  // ? 返回值的格式, 你需要查询 RESP 官方文档或者问 LLM
  return "+OK\r\n";
}
```

`hset`允许单次的`field`设置, 也可以批量设置多个`field`。

## 3.2 hget
```cpp
std::string RedisWrapper::redis_hget(const std::string &key,
                                     const std::string &field) {
  // TODO: Lab 6.2 获取一个哈希类型的`key`的某个字段值
  // ? 返回值的格式, 你需要查询 RESP 官方文档或者问 LLM
  return "$-1\r\n"; // 表示键不存在
}
```

类似之前的简单字符串的`get`操作, 你可能需要再此时判断一下`key`是否已经过期, 如果已经过期则删除该`key`及其`field`(如果是分离存储的实现方案)。

## 3.3 hdel
```cpp
std::string RedisWrapper::redis_hdel(const std::string &key,
                                     const std::string &field) {
  // TODO: Lab 6.2 删除一个哈希类型的`key`的某个字段
  // ? 返回值的格式, 你需要查询 RESP 官方文档或者问 LLM
  return ":1\r\n";
}
```
你需要删除单个哈希结构的`field`。

## 3.4 hkeys
```cpp
std::string RedisWrapper::redis_hkeys(const std::string &key) {
  // TODO: Lab 6.2 获取一个哈希类型的`key`的所有字段
  // ? 返回值的格式, 你需要查询 RESP 官方文档或者问 LLM
  return "*0\r\n";
}
```
`_hkeys`就是返回哈希结构中所有的`field`。

同时,这也是为什么在之前的理论介绍中 (在分离存储`field`的实现方案中), 为什么建议你在代表整个哈希结构的大`key`的`value`中存储所有`field`的元信息。长这样你在实现 `hkeys`的时候就会方便很多, 只需要查询单个`key`就可以了。否则你需要调用前缀查询来获取所有的`field`的键值对。

> `Redis`中涉及哈希的命令还有很多, 这里并没有完全实现, 毕竟本`Lab`的主题是介绍实现`Redis`命令的设计方法, 有了上述基础命令, 其他的命令实现应该非常简单了, 都是简单重复的操作了, 有兴趣你可以自己补充其余命令

# 4 测试
在完成上面的功能后, 你应该能通过下面的测试:
```bash
✗ xmake
✗ xmake run test_redis
[==========] Running 11 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 11 tests from RedisCommandsTest
[ RUN      ] RedisCommandsTest.SetAndGet
[       OK ] RedisCommandsTest.SetAndGet (14 ms)
[ RUN      ] RedisCommandsTest.IncrAndDecr
[       OK ] RedisCommandsTest.IncrAndDecr (9 ms)
[ RUN      ] RedisCommandsTest.Expire
[       OK ] RedisCommandsTest.Expire (2009 ms)
[ RUN      ] RedisCommandsTest.HSetAndHGet
[       OK ] RedisCommandsTest.HSetAndHGet (17 ms)
[ RUN      ] RedisCommandsTest.HDel
[       OK ] RedisCommandsTest.HDel (9 ms)
[ RUN      ] RedisCommandsTest.HKeys
[       OK ] RedisCommandsTest.HKeys (9 ms)
[ RUN      ] RedisCommandsTest.HGetWithTTL
[       OK ] RedisCommandsTest.HGetWithTTL (2113 ms)
[ RUN      ] RedisCommandsTest.HExpire
[       OK ] RedisCommandsTest.HExpire (1111 ms)
[ RUN      ] RedisCommandsTest.ListOperations
```
