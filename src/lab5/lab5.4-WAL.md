# Lab 5.4 WAL 运行机制
上一小节的`Lab`你已经实现了单条`WAL`记录`Record`的设计, 这一小节我们将整合`Record`, 完成`WAL`组件的设计。

# 1 WAL 文件设计
首先，`WAL`文件的内容本质上分就是`Record`的数组。但这里却不仅仅是对`Record`的简单存储，而是需要考虑`WAL`文件的时效性对其进行清理, 以及写入文件的方式。设计要点包括：

1. 刷盘的高效性
   - 我们都知道，当一个事务完成时，必须保证其对应的`WAL`记录被写入磁盘，否则在系统崩溃时，事务的修改将无法恢复。因此，`WAL`记录的写入必须保证原子性。但保证原子性的开销是什么呢? 你需要保证你的`WAL`组件写入磁盘时的效率(例如设置缓冲区, 或者是异步刷盘)
2. 过时`WAL`记录的清理
   - 事务操作的记录都会记录到`WAL`文件中进行持久化, 但其本身对数据库的操作也会随着刷盘形成`SST`完成真正的持久化, 此时之前的`WAL`记录已经不再需要, 需要被清理。因此，`WAL`文件需要有一个机制来清理过时的`WAL`记录。

# 2 WAL 组件的设计思路
老规矩, 我们先看看`WAL`组件的定义:
```cpp
class WAL {
public:
  WAL(const std::string &log_dir, size_t buffer_size,
      uint64_t checkpoint_tranc_id, uint64_t clean_interval,
      uint64_t file_size_limit);
  ~WAL();

  static std::map<uint64_t, std::vector<Record>>
  recover(const std::string &log_dir, uint64_t checkpoint_tranc_id);

  // 将记录添加到缓冲区
  void log(const std::vector<Record> &records, bool force_flush = false);

  // 写入 WAL 文件
  void flush();

  void set_checkpoint_tranc_id(uint64_t checkpoint_tranc_id);

private:
  void cleaner();
  void cleanWALFile();
  void reset_file();

protected:
  std::string active_log_path_;
  FileObj log_file_;
  size_t file_size_limit_;
  std::mutex mutex_;
  std::vector<Record> log_buffer_;
  size_t buffer_size_;
  std::thread cleaner_thread_;
  uint64_t checkpoint_tranc_id_;  // 已安全刷盘的最大事务id（原 max_finished_tranc_id_）
  uint64_t clean_interval_;
};
```

> **注意**: 原`lab`版本中该字段名为 `max_finished_tranc_id_`，当前版本已重命名为 `checkpoint_tranc_id_`，语义相同——记录已经安全持久化到 SST 的最大事务 id，低于此 id 的 WAL 记录可以被清理。

这里我们定义了`WAL`组件的几个关键成员变量和接口, 其设计思路为:
1. `active_log_path_`: 当前写入的`WAL`文件路径
2. `log_file_`: 当前写入的`WAL`文件对象
3. `file_size_limit_`: `WAL`文件的大小限制（选择性使用）
4. `log_buffer_`: `WAL`记录的缓冲区（选择性使用）
5. `buffer_size_`: 缓冲区的大小（选择性使用）
6. `cleaner_thread_`: 清理线程（选择性使用）

这里的成员变量只是给你一些提示, 你不一定需要使用, 但这些成员函数是必须的:
1. `WAL`: 构造函数, 初始化`WAL`组件
2. `~WAL()`: 析构函数, 关闭`WAL`组件, 你需要保证析构时所有`WAL`内容都被持久化
3. `recover`: 恢复`WAL`文件, 返回所有未完成的`WAL`记录(这是下一个`Lab`的内容)
4. `log`: 将记录添加到缓冲区或者刷入磁盘 (取决于你的策略选择性使用)
5. `flush`: 强制将缓冲区中的记录刷入磁盘 (取决于你的策略选择性使用)
6. `cleaner`: 清理旧数据的线程（选择性使用）

# 3 代码实现
- `src/wal/wal.cpp`
- `include/wal/wal.h` (Optional)

## 3.1 WAL 组件的接口实现
你只需要实现下面几个必须实现的函数, 你可以选择性地添加其他功能函数:
```cpp
WAL::WAL(const std::string &log_dir, size_t buffer_size,
         uint64_t checkpoint_tranc_id, uint64_t clean_interval,
         uint64_t file_size_limit) {
  // TODO Lab 5.4 : 实现WAL的初始化流程
}

WAL::~WAL() {
  // TODO Lab 5.4 : 实现WAL的清理流程
}

void WAL::log(const std::vector<Record> &records, bool force_flush) {
  // TODO Lab 5.4 : 实现WAL的写入流程
}

// commit 时 强制写入
void WAL::flush() {
  // TODO Lab 5.4 : 强制刷盘
  // ? 取决于你的 log 实现是否使用了缓冲区或者异步的实现
}

void WAL::cleaner() {
  // TODO Lab 5.4 : 实现WAL的清理线程
}
```

## 3.2 TranContext 逻辑更新
之前你实现的`TranContext`的`put`, `get`,`remove`, `commit`和`abort`等函数中, 你的实现仅仅是将操作记录记录在了`operations`数组中(甚至没有记录, 因为那时你可能不知道这个成员变量是做什么的)。

现在你已经实现的`WAL`的刷盘接口, 因此你需要更新`TranContext`的这些函数, 使其能够将操作记录写入`WAL`文件中。不过这里你需要尤其注意冲突检测的问题, 不同的策略的冲突检测实现难度大不相同

- `commit`时统一进行冲突检测并写入`WAL`文件, 这种方式实现最简单, 但性能较差
- `put`, `get`, `remove`时进行就分批写入`WAL`文件, 这种方式实现需要你在从图检测时需要考虑`WAL`文件中的记录的有效性控制, 实现难度较大, 但性能较好

你在更新`TranContext`的`put`, `get`,`remove`, `commit`和`abort`等函数中, 下面这个辅助函数也许对你有用:
```cpp
bool TranManager::write_to_wal(const std::vector<Record> &records) {
  // TODO: Lab 5.4

  return true;
}
```

# 4 测试
`WAL`组件的测试代码在`test/lab5/test_wal.cpp`中, 你需要保证你的`WAL`组件能够通过这些测试, 但这个测试文件编写其实非常粗糙, 因为本节`Lab`对你的实现方案没有做任何限制, 因此你的实现的元数据也不好测试。因此, 这个测试看看就行, 在你完成下一小节(也是本章最后一个`Lab`)的逻辑后, 你可以通过`test
_lsm`的`LSMTest.Recover`判断你的实现是否正确。
