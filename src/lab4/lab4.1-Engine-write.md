# Lab 4.1 Engine 的写入
# 1 概述
与之前的模块不同, `LSMEngine`部分我们不打算按照`CRUD`迭代器的顺序进行实验, 因为其`Put`操作包含了`SST`的构建流程, 而`Get`操作是对已经构建的`SST`进行查询, 因此, 本章的`Lab`以`SST`的生命周期为线索, 逐步实现`Lab`, 这样的设计也有助于你对上层组件运行调度机制的理解。

话不多说，我们先来看看`Engine`的头文件定义, 然后结合理论知识, 介绍`put`流程和`sst`的构建流程
```cpp
class Level_Iterator;

class LSMEngine : public std::enable_shared_from_this<LSMEngine> {
public:
  std::string data_dir;
  MemTable memtable;
  std::map<size_t, std::deque<size_t>> level_sst_ids;
  std::unordered_map<size_t, std::shared_ptr<SST>> ssts;
  std::shared_mutex ssts_mtx;
  std::shared_ptr<BlockCache> block_cache;
  std::shared_ptr<VLog> vlog_;  // WiscKey 键值分离（后续Lab内容）
  size_t next_sst_id = 0; // 下一个要分配的 sst id
  size_t cur_max_level = 0; // 当前最大的 level
};

class LSM {
private:
  std::shared_ptr<LSMEngine> engine;
  std::shared_ptr<TranManager> tran_manager_; // 本Lab不需要关注
};
```

这里, 我们使用了`LSM`包裹了`LSMEngine`, `LSMEngine`是你要补全函数实现的类, 其中定义了`memtable`, `level_sst_ids`, `ssts`, `block_cache`, `next_sst_id`, `cur_max_level`等成员变量。这里比较重要的包括:
- `level_sst_ids`: 从`level`到这一层的`sst_id`数组, 每一个`SST`由一个`sst_id`唯一表示
- `ssts`: `sst_id`到`SST`的映射
- `next_sst_id`: `SST`的`id`分配器, `LSMEngine`在`flush`形成行的`SST`时, 会分配一个`sst_id`给`SST`, 然后将`sst_id`和`SST`映射关系存入`ssts`中, `next_sst_id`就是`sst_id`的分配器, 每次分配`sst_id`时, `next_sst_id`都会自增1
- `cur_max_level`: 顾名思义, 就是当前`SST`的最大的`level`
- `data_dir`: `LSMEngine`的`data_dir`, 即数据文件的存储位置, 这个参数我们在单元测试中会进行指定, `SST`文件需要存放在这个目录下
- `MemTable`: 即整个`LSM Tree`引擎的内存表部分
   
剩下的成员变量:
- `ssts_mtx`: 全局的`sst文件`的访问锁, 这里是一个读写锁, 当然这个变量不是必须的, 你可以按照自己的理解实现并发控制策略(不过建议使用这个变量)
- `block_cache`: 全局的缓存池指针, 你实现缓存池之前, 默认其为`nullptr`即可

> 这里的`l0_sst_ids`记录了所有`sst`的`id`, 其排序是从大到小, 因为`sst`的`id`越大表示这个`sst`越新, 需要优先查询。
>
> 可以使用`l0_sst_ids`获取的`id`从哈希表`ssts`中查询`SST`的描述类(类似于文件描述符)。

# 2 写入/删除流程
结合刚刚对类的成员变量定义的简单介绍, 我们再次回顾一下`LSM Tree`的读写流程:

1. 写入`MemTable`:
   1. 如果写入的`KV`的`value`为空, 表示一个删除标记
   2. 直接调用成员变量`memtable`的接口即可
   3. 同样有批量接口和单次操作的接口
2. 若当前活跃的`MemTable`大小达到阈值, 则将其冻结
   1. 这一部分已经在`MemTable`中实现, 你无需再实现
3. 若冻结的`MemTable`容量达到阈值, 则将最早冻结的`MemTable`转为`SST`
   1. 判断`MemTable`容量并决定是否刷盘是你需要在本小节`Lab`进行实现的内容之一
   2. `SST`文件的设计是每一层的`SST文件`数量不能超过指定阈值, 因此你刷盘的`Level 0`的`SST`文件可能会哦触发`Level 0`和`Level 1`的`SST`文件的`compact`, 不过这一任务没有放在`Lab 4.1`, `Lab4.1`中你当做就只有`Level 0`这一个层级即可

因此, 本小节`Lab`的核心就是整合之前创建的`MemTable`, `SST`, `Block`, `Iterator`, 并调用接口实现对外服务的功能

# 3 代码实现
本小节你需要更改的代码文件为:
- `src/lsm/engine.cpp`
- `include/lsm/engine.h`

## 3.1 Put && Remove
你首先需要实现`put`函数, `put`函数肯定是操纵`memtable`成员变量, 另外你也需要根据其容量接口函数判断什么时候需要进行`flush`操作:
```cpp
uint64_t LSMEngine::put(const std::string &key, const std::string &value,
                        uint64_t tranc_id) {
  // TODO: Lab 4.1 插入
  // ? 由于 put 操作可能触发 flush
  // ? 如果触发了 flush 则返回新刷盘的 sst 的 id
  // ? 在没有实现  flush 的情况下，你返回 0即可
  return 0;
}
uint64_t LSMEngine::remove(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.1 删除
  // ? 在 LSM 中，删除实际上是插入一个空值
  // ? 由于 put 操作可能触发 flush
  // ? 如果触发了 flush 则返回新刷盘的 sst 的 id
  // ? 在没有实现  flush 的情况下，你返回 0即可
  return 0;
}
```

这个函数的最终版本需要调用`flush`进行刷盘, 因此建议你将次函数和后面的`flush`函数一起实现。

此时你仍然可以忽略`tranc_id`, 将其传递到接口的参数即可。至于返回值`uint64_t`, 你现阶段返回0即可。

> 额外说明, 这里说明一下为什么返回值是`uint64_t`，而不是`void`, 这主要是为后续的事务准备的, 刷盘意味着事务操作的持久化完成, 因此需要更新已经成功持久化的最大事务`id`, 也就是这里的返回值。如果你现在看不懂也没关系, 到实现事务的`Lab`就明白了。


根据之前部分描述, 此时你可以简化刷盘部分的逻辑, 即默认现在只有`Level 0`的`SST文件`, `SST文件`不会进行`Compact`形成新的`Level`



## 3.2 put_batch && remove_batch
和`put/remove`函数的逻辑几乎一样, 只是写入时是批量数据:
```cpp
uint64_t LSMEngine::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  // TODO: Lab 4.1 批量插入
  // ? 由于 put 操作可能触发 flush
  // ? 如果触发了 flush 则返回新刷盘的 sst 的 id
  // ? 在没有实现  flush 的情况下，你返回 0即可
  return 0;
}

uint64_t LSMEngine::remove_batch(const std::vector<std::string> &keys,
                                 uint64_t tranc_id) {
  // TODO: Lab 4.1 批量删除
  // ? 在 LSM 中，删除实际上是插入一个空值
  // ? 由于 put 操作可能触发 flush
  // ? 如果触发了 flush 则返回新刷盘的 sst 的 id
  // ? 在没有实现  flush 的情况下，你返回 0即可
  return 0;
}
```

## 3.3 Flush
`flush`函数会将`MemTable`中`frozen_tables`中最旧的一个跳表的数据刷盘，并返回刷盘过程中提取的统计信息`tranc_id`, 现阶段你只需要返回0即可。

> **Hint**:
> `flush()`的返回值是和`put()`等接口的返回值一致的

```cpp
uint64_t LSMEngine::flush() {
  // TODO: Lab 4.1 刷盘形成sst文件
  return 0;
}
```
`flush`函数应该是这一小节的关键函数了, 这里的逻辑就是从`memtable`的接口将最旧的跳表刷盘成`SST`文件, 这里涉及到文件`IO`的操作时, 推荐使用作者定义好的辅助类`FileObj`, 其定义在`include/utils/files.h`中。

**构造 SSTBuilder 时的注意事项**

现阶段你只需要使用普通模式的构造函数：
```cpp
SSTBuilder builder(TomlConfig::getInstance().getLsmBlockSize(), true);
```
`vlog_` 和 WiscKey 相关逻辑是后续 Lab（键值分离）的内容，现阶段无需关注。

最后，`SST文件`的命名格式已经在`get_sst_path`中进行了详细的说明:
```cpp
std::string LSMEngine::get_sst_path(size_t sst_id, size_t target_level) {
  // sst的文件路径格式为: data_dir/sst_<sst_id>.<level>，sst_id格式化为32位数字
  std::stringstream ss;
  ss << data_dir << "/sst_" << std::setfill('0') << std::setw(32) << sst_id
     << '.' << target_level;
  return ss.str();
}
```
> 后缀标记了这个`SST文件`所属的`Level`, 在你实现`Compact`前, 这个后缀设置为0即可

**你必须严格遵守`SST`文件格式的命名规范, 如果你采用自己的文件命名方式, 那么请自行修改对应的单元测试函数(非常不推荐)。**

# 4 测试
由于我们目前仅实现了写入模块, 测试函数无法从引擎读取数据, 因此本小节没有单元测试, 当你实现[Lab 4.2 Engine 的读取](./lab4.2-Engine-read.md)后会进行统一的单元测试。

# 5 思考
现在请先思考一下几个问题，然后开启[Lab 4.2 Engine 的读取](./lab4.2-Engine-read.md)
