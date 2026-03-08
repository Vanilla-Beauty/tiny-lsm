# Lab 5.5 崩溃恢复

# 1 崩溃恢复运行机制
当某一时刻存储引擎发送崩溃时, 需要进行崩溃恢复，以恢复数据状态。崩溃恢复的关键是**回放日志**，以确定已提交的事务，并执行其操作。在这个描述中我们不难得到一个信息, 即成功执行的事务一定要先将操作记录持久化到日志中, 然后再在内存中进行操作, 最后返回成功信息给客户端或者调用者。这也是为什么这个机制称为**预写式日志**。其崩溃恢复的工作流程包括：

1. **日志回放**：从最后一个检查点开始扫描日志，按顺序处理所有已提交事务（`COMMIT`标记后的操作）。
2. **Redo阶段**：重新执行所有已提交事务的`PUT/REMOVE`操作，覆盖当前数据状态。
3. **Undo阶段（可选）**：若事务未提交（无`COMMIT`标记），则直接丢弃其操作记录。

**示例场景**
假设事务`TX100`依次执行`PUT key1=value1`和`REMOVE key2`，其日志内容如下：
```plaintext
BEGIN TX100
PUT key1 5 value1
DELETE key2
COMMIT TX100
```

事务`TX100`在调用`commit`函数后, 需要将上述日志刷入`wal`文件完成预写这一步骤后, 才会返回`client`事务提交成功。一开始提交的事务，其数据一定是只存在于`MemTable`中的, 此时如果存储引擎崩溃, 刚刚完成的事务是没法刷入到`sst`文件的, 但我们重启时可以检查`wal`文件的内容, 将其与数据库持久化的状态进行比对(持久化的状态包括最大已完成的事务id、最大已经刷盘的事务id， 见上一章的内容)，如果其事务`id`比目前已经持久化到`sst`的最大事务`id`大，则说明该事务需要进行重放, 另一方面, 如果事务记录的最后一个条目是`Rollback`，则说明该事务需要被回滚, 则不需要在崩溃恢复时进行重放.

将上述流程总结如下:

**情形1: 正常提交事务、SST刷盘正常**
1. 事务开始, 写入`BEGIN`标记到`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
2. 执行若干`PUT/DELETE`操作, 将操作记录写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   1. 如果隔离级别是`Read Uncommitted`, 可以将`PUT/DELETE`操作直接应用到数据库
   2. 其他隔离级别则将`PUT/DELETE`操作暂存到事务管理的上下文内存中, 等待事务提交时再应用到数据库
3. 提交事务:
   1. 将`COMMIT`标记写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   2. 将`WAL`日志刷入磁盘。(此时`WAL`日志已经刷入磁盘)
   3. 如果隔离级别不是`Read Uncommitted`, 则将暂存的`PUT/DELETE`操作应用到数据库
   4. 返回给`client`成功或失败
4. 之前事务的`PUT/DELETE`操作的变化应用到数据库仍然是位于`MemTable`中的, 其会稍后输入`SST`

**情形2: 正常提交事务、SST刷盘崩溃**
1. 事务开始, 写入`BEGIN`标记到`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
2. 执行若干`PUT/DELETE`操作, 将操作记录写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   1. 如果隔离级别是`Read Uncommitted`, 可以将`PUT/DELETE`操作直接应用到数据库
   2. 其他隔离级别则将`PUT/DELETE`操作暂存到事务管理的上下文内存中, 等待事务提交时再应用到数据库
3. 提交事务:
   1. 将`COMMIT`标记写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   2. 将`WAL`日志刷入磁盘。(此时`WAL`日志已经刷入磁盘)
   3. 如果隔离级别不是`Read Uncommitted`, 则将暂存的`PUT/DELETE`操作应用到数据库
   4. 返回给`client`成功或失败
4. 之前事务的`PUT/DELETE`操作的变化应用到数据库仍然是位于`MemTable`中的, 其稍后刷入`SST`奔溃
5. 数据库重启后执行崩溃回复
   1. 检查`WAL`文件的记录
   2. 整合事务`id`每条记录, 忽略以`Rollback`结尾的事务
   3. 若事务以`Commit`结尾, 则将事务`id`与已经刷盘的`SST`中的最大事务`id`进行比对
      1. 若事务`id`大于`SST`的最大事务`id`, 执行重放操作
      2. 若事务`id`小于`SST`的最大事务`id`, 则忽略该事务, 因为其已经被数据库执行过了

**情形3: 事务回滚**
1. 事务开始, 写入`BEGIN`标记到`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
2. 执行若干`PUT/DELETE`操作, 将操作记录写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   1. 如果隔离级别是`Read Uncommitted`, 可以将`PUT/DELETE`操作直接应用到数据库
   2. 其他隔离级别则将`PUT/DELETE`操作暂存到事务管理的上下文内存中, 等待事务提交时再应用到数据库
3. 回滚事务:
   1. 将`Rollback`标记写入`WAL`日志中。(此时的`WAL`日志可能存在于缓冲区, 没有刷入文件)
   2. 将`WAL`日志刷入磁盘。(此时`WAL`日志已经刷入磁盘)
   3. 如果隔离级别不是`Read Uncommitted`, 则将暂存的`PUT/DELETE`操作简单丢弃即可
   4. 如果隔离级别是`Read Uncommitted`, 则将操作前的数据库状态进行还原(作者的设计是利用`TranContext`中的`rollback_map_`进行还原, 当然这取决于你之前的`Lab`实现)
   5. 返回给`client`成功或失败

# 2 崩溃恢复代码实现
本小节实验, 你需要更改的代码文件包括:
- `src/lsm/engine.cpp`
- `include/lsm/engine.h` (Optional)
- `src/wal/wal.cpp`
- `include/wal/wal.h` (Optional)
- `src/lsm/transation.cpp`
- `include/lsm/transation.h` (Optional)

## 2.1 WAL::recover

这里的崩溃恢复需要在引擎启动时进行判断, 因此其与不同组件的构造函数息息相关, 由于这里不同组件的耦合程度较高, 故先统一介绍这流程:
```text
-> LSM::LSM 构造函数启动
   -> 调用 LSMEngine 的构造函数
   -> 调用 TranManager 的构造函数
      -> TranManager 初始化除了 WAL 之外的组件
      -> TranManager 从磁盘读取已持久化的事务 id 信息（checkpoint_tranc_id）
   -> 调用 TranManager::check_recover 检查是否需要重放WAL日志
   -> 将重放的 WAL 日志应用到 LSMEngine
   -> 调用 TranManager::init_new_wal 初始化 WAL 组件
-> LSM::LSM 的其他逻辑...
-> LSM::LSM 构造函数结束
```

```cpp
std::map<uint64_t, std::vector<Record>>
WAL::recover(const std::string &log_dir, uint64_t checkpoint_tranc_id) {
  // TODO: Lab 5.5 检查需要重放的WAL日志
  return {};
}
```
这个函数是一个静态函数，第二个参数 `checkpoint_tranc_id` 是已经安全持久化到 SST 的最大事务 id（即 `TranManager` 从磁盘读取的 `checkpoint_tranc_id_`）。WAL 文件中 `tranc_id <= checkpoint_tranc_id` 的记录**已经刷盘**，无需重放；只有 `tranc_id > checkpoint_tranc_id` 的记录才需要考虑重放。举个例子:
```text
T1 ctx1 running, ctx2 running
T2 ctx1 commit, ctx2 running
T3 crash, both data from in ctx1 and ctx2 are not flushed to sst (in `MemTable`)
T4 recover
```
在这个例子中, `ctx1`在崩溃前成功提交但数据没有刷入`SST`, 存在与内存的`MemTable`中; `ctx2`在崩溃时仍未提交, 因此在`T4`进行崩溃恢复时, 属于`ctx1`的`WAL`日志需要进行重放, 而属于`ctx2`的`WAL`日志则不需要进行重放(因为其根本没有提交)

`WAL::recover`函数就是整理需要重放的`WAL`日志, 返回一个`map`, 其中`key`为事务`id`, `value`为该事务的所有`WAL`操作记录

## 2.2 TranManager::check_recover
```cpp
std::map<uint64_t, std::vector<Record>> TranManager::check_recover() {
  // TODO: Lab 5.5
  return {};
}
```
`TranManager::check_recover`的目的是调用底层`WAL`的`recover`函数, 将其返回的`map`保存到上层组件中, 由上层组件进行重放。

这里需要注意的是, 之前的`WAL::recover`函数是静态函数, 其会在`WAL`的类的实例化之前进行调用, 因此在`WAL`的实例化过程中, 需要调用`WAL::recover`函数, 并将返回的`map`保存到上层组件中使其进行重放, 这里在`recover`崩溃恢复之后进行`WAL`的初始化的函数是由`TranManager::init_new_wal`函数进行的:

## 2.3 WAL 初始化
在重放完成后，需要重新初始化 WAL，以便后续事务的日志记录：
```cpp
void TranManager::init_new_wal() {
  // TODO: Lab 5.5 初始化 wal
}
```

这里你也需要回顾一下`TranManager`的头文件定义:
```cpp
class TranManager : public std::enable_shared_from_this<TranManager> {
public:
  // ...

private:
  // ...
  std::shared_ptr<WAL> wal;
  // ...
};
```

> 这里的组件构成是: `TranManager`内部管理的`WAL`这个组件, 他们内部的耦合度还是比较高的, 后续的实验版本也需要进行优化

## 2.4 LSM 的构造函数
你需要在`LSM`的构造函数中调用之前实现的`WAL`重放检查相关的函数, 并将重放的`WAL`日志应用到`LSM`中, 在之后你需要重新初始化`WAL`组件:
```cpp
LSM::LSM(std::string path)
    : engine(std::make_shared<LSMEngine>(path)),
      tran_manager_(std::make_shared<TranManager>(path)) {
  // TODO: Lab 5.5 控制WAL重放与组件的初始化
}
```

**重放时的关键细节：跳过已刷盘的事务**

`check_recover()` 返回的 map 中可能包含已经刷盘到 SST 的事务（例如 WAL 文件中记录了 TX100，但 TX100 在崩溃前恰好完成了 flush）。在重放时，你需要跳过这些已经持久化的事务：

```cpp
auto check_recover_res = tran_manager_->check_recover();
for (auto &[tranc_id, records] : check_recover_res) {
    // 如果该事务 id 已经在 flushed_tranc_ids 集合中，说明已刷盘，跳过
    if (tran_manager_->get_flushed_tranc_ids().count(tranc_id)) {
        continue;
    }
    // 重放该事务的所有操作
    for (auto &record : records) {
        if (record.getOperationType() == OperationType::OP_PUT) {
            engine->put(record.getKey(), record.getValue(), tranc_id);
        } else if (record.getOperationType() == OperationType::OP_DELETE) {
            engine->remove(record.getKey(), tranc_id);
        }
    }
}
tran_manager_->init_new_wal();
```

`TranManager::get_flushed_tranc_ids()` 返回的是一个 `std::set<uint64_t>`，记录了所有已经安全刷盘到 SST 的事务 id 集合，这比仅用单个 `max_flushed_tranc_id` 更精确，避免了"部分刷盘"导致的数据缺失问题。

# 3 事务id信息的维护
这里补充说明一个非常重要的细节。当前实现中使用 `flushed_tranc_ids_`（一个 `std::set<uint64_t>`）替代了单一的 `max_flushed_tranc_id_`，来跟踪哪些事务已经安全持久化到 SST。

**为什么用集合而不是单个最大值？**

考虑如下场景：
1. 事务 TX100 写入 key1、key2，事务 TX101 写入 key3
2. flush 发生时，只有 key1 进入了最终的 SST（例如 TX100 的部分 key 在旧 MemTable 中被 flush 了）
3. 若用单一最大值，flush 后 `max_flushed_tranc_id = 100`，下次恢复时 TX100 被跳过
4. 但 key2 可能还在 MemTable 中未刷盘，崩溃后 key2 丢失

`flushed_tranc_ids_` 集合的语义是：**只有当某个事务的所有操作都已随同一次 flush 完整写入 SST 时，该事务 id 才会被加入集合**。具体来说，`MemTable::flush_last` 在刷盘时会收集 "begin 标记记录"（`key="" value=""` 的特殊记录）对应的 `tranc_id`，并通过 `LSMEngine::flush` 传递给 `TranManager::add_flushed_tranc_id`。

**`checkpoint_tranc_id_` 的作用**

`checkpoint_tranc_id_` 是 WAL 清理的依据，其含义是"已知安全持久化的最大事务 id 的保守估计"，用于 WAL 的 `cleanWALFile` 函数判断某个历史 WAL 文件是否可以删除。`TranManager` 在恢复完成后会将其从磁盘读取并传给 WAL 构造函数。

实验不限制你对上述问题的解决方案, 你能通过后续测试即可

# 4 测试
现在你应该可以通过之前所有的测试了:
```bash
✗ xmake
[100%]: build ok, spent 0.773s
✗ xmake run test_lsm
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from LSMTest
[ RUN      ] LSMTest.BasicOperations
[       OK ] LSMTest.BasicOperations (1003 ms)
[ RUN      ] LSMTest.Persistence
[       OK ] LSMTest.Persistence (2020 ms)
[ RUN      ] LSMTest.LargeScaleOperations
[       OK ] LSMTest.LargeScaleOperations (1000 ms)
[ RUN      ] LSMTest.IteratorOperations
[       OK ] LSMTest.IteratorOperations (1027 ms)
[ RUN      ] LSMTest.MixedOperations
[       OK ] LSMTest.MixedOperations (1001 ms)
[ RUN      ] LSMTest.MonotonyPredicate
[       OK ] LSMTest.MonotonyPredicate (1016 ms)
[ RUN      ] LSMTest.TrancIdTest
[       OK ] LSMTest.TrancIdTest (18 ms)
[ RUN      ] LSMTest.TranContextTest
[       OK ] LSMTest.TranContextTest (0 ms)
[ RUN      ] LSMTest.Recover
[       OK ] LSMTest.Recover (1001 ms)
[----------] 9 tests from LSMTest (8091 ms total)

[----------] Global test environment tear-down
[==========] 9 tests from 1 test suite ran. (8091 ms total)
[  PASSED  ] 9 tests.
```
