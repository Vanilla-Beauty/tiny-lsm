# Lab 4.2 Engine 的读取
# 1 概述
从之前 [Lab 4.1 Engine 的写入](./lab4.1-Engine-write.md)中, 我们已经实现了`SST`的构建流程, 这一章我们将实现`Engine`的读取流程。(这里的读取还包括引擎的初始化流程中对`SST文件`的读取)

同样地, 我们想从逻辑上梳理引擎的读取和查询流程:

**1 存储引擎启动时**

遍历`data_dir`下的`SST`文件, 将`SST`文件的元信息加载到内存中

**2 接受查询请求**

1. 查询当前活跃的`MemTable`, 如果查到有效记录或删除记录, 则返回
2. 若查询当前活跃的`MemTable`未命中, 则遍历冻结的`MemTable`, 由于冻结的`MemTable`也存在次序, 需要先查询最近冻结的`MemTable`
3. 若查询冻结的`MemTable`未命中, 则遍历`SST`, 由于`SST`也存在次序, 需要先查询最近创建的`SST`
   1. `SST`的顺序先按照`Level`排序, `Level`越低的`SST`越新, 需要先查询
   2. 相同`Level`的`SST`按照`sst_id`排序, 这里的逻辑有所不同:
      1. 如果是`Level 0`的`SST`, 则按照`sst_id`排序, 从大到小查询, 越大的`sst_id`表示这个`SST`越新, 需要有限查询
      2. 如果是其他`Level`以上的`SST`, 其所有的`SST`的`key`都是有序分布且不重叠的, 既然`key`不重叠也就无所谓谁的优先级更高、谁会覆盖谁的`key`, 可以采用二分查询实现更高的效率, 下面是一个`SST文件`的案例:
            ```bash
            Level 0: sst_15(key000-key050), sst_14(key005-key030), sst_13(key020-key040)
            Level 1: sst_10(key100-key120), sst_11(key121-key140), sst_12(key141-key160)
            Level 2: sst_08(key100-key120), sst_09(key121-key140)
            ```
4. 整个`SST`文件遍历完成后, 若仍未命中, 则返回空指针表示`key`没有找到


> **补充**
> - 在后续实现`WAL`后, 在上述所有流程前, 会有一个对`WAL`日志进行检查并实现崩溃恢复的流程

# 2 代码实现
本小节你需要更改的代码文件为:
- `src/lsm/engine.cpp`
- `include/lsm/engine.h`

## 2.1 引擎的初始化
上一章[Lab 4.1 Engine 的写入](./lab4.1-Engine-write.md)中, 我们在`put`操作中惰性触发了`SST`的刷盘操作, 因此在`Engine`启动时, 我们需要遍历`data_dir`下的`SST`文件, 将`SST`文件的元信息加载到内存中, 以便后续的查询操作:
```cpp
LSMEngine::LSMEngine(std::string path) : data_dir(path) {
  // 初始化日志
  init_spdlog_file();

  // TODO: Lab 4.2 引擎初始化
}
```

**说明**

1. 后续实现缓存池后, 构造函数中需要对缓存池进行初始化, 现阶段你的构造函数, 只需要将`block_cache`初始化为`nullptr`即可
2. 第一次启动引擎时, 需要创建数据目录
3. `init_spdlog_file`函数用于初始化日志, 其内部是对`std::call_once`的封装, 因此其只有第一次调用时会执行
4. 引擎初始化时需要初始化 `vlog_`：
   ```cpp
   vlog_ = VLog::open(data_dir + "/vlog.data");
   ```
   `VLog` 是 WiscKey 键值分离的核心组件（后续Lab内容），这里**必须**在引擎初始化时打开，即使 WiscKey 阈值为 0，也能保证文件存在、正常重启读取。
5. 加载 SST 文件时，需将 `vlog_` 传入 `SST::open`：
   ```cpp
   auto sst = SST::open(sst_id, FileObj::open(sst_path, false), block_cache, vlog_);
   ```
   对于普通模式（非 WiscKey）的 SST，`vlog_` 不会被使用，传入后忽略即可。

> **Hint**
> 1. 你需要从`SST文件`的命名格式中对`next_sst_id`和`cur_max_level`进行更新
> 2. `level_sst_ids`映射的数组需要你自己维护其优先级顺序, 不同`Level`的`SST`文件优先级可能不同, 现在你不需要关心`Level 0`以外的`SST`

## 2.2 查询接口
### 2.2.1 get
```cpp
std::optional<std::pair<std::string, uint64_t>>
LSMEngine::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.2 查询

  return std::nullopt;
}
```
这里传入的`uint64_t tranc_id`是为了在实现事务功能后控制不同事务的可见性的, 也就是实现事务基础属性中的**隔离性**, 现阶段你可以忽略它

此外, 这里的返回值是一个由`optional`包裹的`pair`, `pair`的第一个元素是`value`, 第二个元素是`tranc_id`, `value`表示查询到的值, `tranc_id`表示这个键值对最新的修改事务的的`tranc_id`(现阶段同样可以忽略), 如果查询不到, 则返回`std::nullopt`

### 2.2.2 get_batch
```cpp
std::vector<
    std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
LSMEngine::get_batch(const std::vector<std::string> &keys, uint64_t tranc_id) {
  // TODO: Lab 4.2 批量查询

  return {};
}
```
没啥好说的, 就是在`get`的基础上变成了批量查询

### 2.2.3 sst_get_
通过后缀你可以看出, 这个查询是专门在`SST`中进行查询的接口, 其`_`表示这个函数是不需要进行加锁操作的, 其加锁逻辑是其他上层组件控制的:
```cpp
std::optional<std::pair<std::string, uint64_t>>
LSMEngine::sst_get_(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 4.2 sst 内部查询
  return std::nullopt;
}
```

> 思考: 什么情况下会单独对`SST`部分进行查询?

# 3 测试
完成[Lab 4.1 Engine 的写入](./lab4.1-Engine-write.md)和本节`Lab`后, 你应该能通过`test/test_lsm.cpp`中`IteratorOperations`前的所有单元测试:
```bash
✗ xmake
[100%]: build ok, spent 1.936s
✗ xmake run test_lsm
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from LSMTest
[ RUN      ] LSMTest.BasicOperations
[       OK ] LSMTest.BasicOperations (0 ms)
[ RUN      ] LSMTest.Persistence
[       OK ] LSMTest.Persistence (228 ms)
[ RUN      ] LSMTest.LargeScaleOperations
[       OK ] LSMTest.LargeScaleOperations (1 ms)
[ RUN      ] LSMTest.MixedOperations
[       OK ] LSMTest.MixedOperations (0 ms)
[ RUN      ] LSMTest.IteratorOperations
unknown file: Failure
C++ exception with description "Not implemented" thrown in the test body.

[  FAILED  ] LSMTest.IteratorOperations (0 ms)
```
