# Lab 2.1 简单 CRUD
# 1 准备工作
本小节需要你修改的代码：
-`src/memtable/memtable.cpp`
- `include/memtable/memtable.h` (Optional)

同样的，我们先看看代码的头文件东一，从而了解我们的`MemTable`的整体实现思路:
```cpp
class MemTable {
    // ...
private:
  std::shared_ptr<SkipList> current_table;
  std::list<std::shared_ptr<SkipList>> frozen_tables;
  size_t frozen_bytes;
  std::shared_mutex frozen_mtx; // 冻结表的锁
  std::shared_mutex cur_mtx;    // 活跃表的锁
};
```
这里我们根据之前的原理介绍, 对之前的`SkipList`简单包装, 使用`list`保证一系列冻结的`Skiplist`。这里建议的规定是：
**最新的`Skiplist`放在`list`的`head`位置，最旧的`Skiplist`放在`list`的`tail`位置。**

此外, 你在头文件中除了基础的`CRUD`函数外, 还会看到这个函数:
```cpp
  std::shared_ptr<SST> flush_last(SSTBuilder &builder, std::string &sst_path,
                                  size_t sst_id,
                                  std::vector<uint64_t> &flushed_tranc_ids,
                                  std::shared_ptr<BlockCache> block_cache);
```

这个函数不是本`Lab`要求实现的函数, 但可以先进行简单的介绍便于认知整体架构。当`MemTable`中的数据量达到阈值时, 会调用这个函数将最古老的一个`SST`进行持久化, 形成一个`Level 0`的`SST`, 因此你可以理解为, `Skiplist`是和`Level 0 SST`的数据来源。

其中新增的`flushed_tranc_ids`是一个输出参数, 刷盘过程中会将已经完成的事务`id`收集到这个数组中, 供上层组件通知事务管理器哪些事务已经安全持久化。

# 2 put 的实现
你首先要实现的是`put`系列的函数:
```cpp
void MemTable::put_(const std::string &key, const std::string &value,
                    uint64_t tranc_id) {
  // TODO: Lab2.1 无锁版本的 put
}

void MemTable::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put
}

void MemTable::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  // TODO: Lab2.1 有锁版本的 put_batch
}
```

这里的`put`有多个版本, 分别是无锁版本和有锁版本的单次`put`以及有锁版本的批量`put`, 你必须按照语义实现这些函数, 因为后续上层组件调用的函数默认携带`_`后缀的函数版本是无锁操作版本。

同时简单讲述以下如此设计的原因，某些并发控制只需要当前的`MemTable`组件控制即可, 但有些并发控制场景设计多个组件, 需要再上层进行, 例如后续事务提交时的冲突检测就是一个典型的案例。

> Hint: 你不仅仅需要做简单的`API`调用, 还需要判断什么时候`Skiplist`的容量超出阈值需要进行冻结

# 3 get 的实现
接下来实现`get`的一系列函数, 同样包括无锁版本与有锁版本, 并且你还需要实现不同部分的分阶段查询:
```cpp
SkipListIterator MemTable::cur_get_(const std::string &key, uint64_t tranc_id) {
  // 检查当前活跃的memtable
  // TODO: Lab2.1 从活跃跳表中查询
  return SkipListIterator{};
}

SkipListIterator MemTable::frozen_get_(const std::string &key,
                                       uint64_t tranc_id) {
  // TODO: Lab2.1 从冻结跳表中查询
  // ? 你需要尤其注意跳表的遍历顺序
  // ? tranc_id 参数可暂时忽略, 直接插入即可
  return SkipListIterator{};
  ;
}

SkipListIterator MemTable::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 建议复用 cur_get_ 和 frozen_get_
  // ? 注意并发控制

  return SkipListIterator{};
}

SkipListIterator MemTable::get_(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab2.1 查询, 无锁版本
}
```

# 4 remove 实现
最后, 插入`value`为空的键值对表示对数据的删除标记, 同样有不同的版本:
```cpp
void MemTable::remove_(const std::string &key, uint64_t tranc_id) {
  // TODO Lab2.1 无锁版本的remove
}

void MemTable::remove(const std::string &key, uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove
}

void MemTable::remove_batch(const std::vector<std::string> &keys,
                            uint64_t tranc_id) {
  // TODO Lab2.1 有锁版本的remove_batch
}
```

# 5 冻结活跃表
`Skiplist`的容量超出阈值需要进行冻结时需要调用下述函数。

至于这个函数的调用实际，作者建议是在每次`put`后检查容量是否超出阈值, 然后同步地调用该函数, 当然你也可以启用一个后台线程进行周期性检查。

```cpp
void MemTable::frozen_cur_table_() {
  // TODO: 冻结活跃表
}

void MemTable::frozen_cur_table() {
  // TODO: 冻结活跃表, 有锁版本
}
```

> **`Hint`**
> 1. `src/config/config.cpp`中定义了一个配置文件的单例, 其会解析项目目录中的`config.toml`配置文件, 其中包含了各种阈值的推荐值
> 2. 你可以使用类似`TomlConfig::getInstance().getLsmPerMemSizeLimit()`的方法获取配置文件中定义的常量
> 3. 你可以修改`config.toml`配置文件的值, 但尽量不要新增配置项, 否则你需要自行修改`src/config/config.cpp`中的解析函数

# 6 测试
当你完成上述所有功能后, 你可以通过如下测试:
```bash
✗ xmake
✗ xmake run test_memtable
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from MemTableTest
[ RUN      ] MemTableTest.BasicOperations
[       OK ] MemTableTest.BasicOperations (2 ms)
[ RUN      ] MemTableTest.RemoveOperations
[       OK ] MemTableTest.RemoveOperations (0 ms)
[ RUN      ] MemTableTest.FrozenTableOperations
[       OK ] MemTableTest.FrozenTableOperations (0 ms)
[ RUN      ] MemTableTest.LargeScaleOperations
[       OK ] MemTableTest.LargeScaleOperations (0 ms)
[ RUN      ] MemTableTest.MemorySizeTracking
[       OK ] MemTableTest.MemorySizeTracking (0 ms)
[ RUN      ] MemTableTest.MultipleFrozenTables
[       OK ] MemTableTest.MultipleFrozenTables (0 ms)
[ RUN      ] MemTableTest.ConcurrentOperations
^C
```
`ConcurrentOperations`需要你实现后续的迭代器功能。

接下来你可以开启下一小节的[Lab](./lab2.2-iterator.md)
