# Lab 7.1 VLog 实现

## 1 VLog 的设计目标

在开始写代码之前，先思考 VLog 需要满足哪些约束：

1. **只追加**：写入操作只在文件末尾追加，不修改已有记录。这使得 VLog 天然适合顺序写，也简化了并发控制——多个写入者只需保证追加的原子性，不需要锁整个文件。

2. **重启恢复**：系统重启后，VLog 文件中已有的记录必须保留（`open` 时不截断），否则 SST 中指向旧记录的引用将失效。

3. **随机读**：`get` 操作需要根据 SST 中存储的偏移量直接跳到 VLog 文件的任意位置读取 value，不需要顺序扫描。

4. **数据校验**：每条记录附带 CRC32 校验，以检测磁盘静默错误或写入中断导致的数据损坏。

这些约束共同决定了 VLog 的接口和文件格式设计。

先看头文件定义：

```cpp
// include/vlog/vlog.h
class VLog {
public:
    // 打开或创建 VLog 文件（不截断已有内容）
    static std::shared_ptr<VLog> open(const std::string &path);

    // 追加一条 KV 记录，返回该记录在文件中的起始偏移量
    uint64_t append(const std::string &key, const std::string &value);

    // 从指定偏移量读取 value（需要提供 value 的长度）
    std::string read_value(uint64_t offset, uint32_t value_size);

    // 返回当前文件的末尾偏移（== 文件大小）
    uint64_t tail_offset() const;

    void sync();
    void del_vlog();

private:
    FileObj file_;
    std::string path_;
    mutable std::mutex append_mtx_;  // 保护并发 append
};
```

接口设计上有一个值得注意的细节：`read_value` 接受 `value_size` 参数，而不是自己从 VLog 记录中解析出 value 长度。这是因为调用方（`SST::resolve_value`）已经从 vlog 引用中拿到了 value 大小，避免一次多余的读取；同时也使 `read_value` 的职责更单一——它只负责"在给定位置读取给定长度的数据"。

---

## 2 代码实现

需要修改 `src/vlog/vlog.cpp`。

### 2.1 VLog::open

`open` 是 VLog 的工厂函数，负责打开或创建 VLog 文件：

```cpp
std::shared_ptr<VLog> VLog::open(const std::string &path) {
    // TODO: Lab 7.1 打开或创建 VLog 文件
    // ? 1. 若文件不存在则创建空文件
    // ? 2. 用 FileObj::open(path, false) 打开（不截断，保留已有记录）
    // ? 3. 记录 path_ 和 file_
    return nullptr;
}
```

这里使用 `FileObj::open(path, false)` 而非截断模式，目的是**重启后不丢失**已有的 vlog 记录。如果在 open 时截断，所有 SST 中指向旧 VLog 偏移的引用就会失效，导致数据损坏。

### 2.2 VLog::append

`append` 是 VLog 的核心写入接口，将一条 KV 记录顺序追加到文件末尾：

```cpp
uint64_t VLog::append(const std::string &key, const std::string &value) {
    // TODO: Lab 7.1 追加一条 KV 记录到 VLog，返回记录起始偏移量
    // ? 加 append_mtx_ 互斥锁（支持并发写）
    // ? offset = file_.size()（追加前的文件大小即为本次记录的起始偏移）
    // ? 记录格式: [key_len:uint16][key][val_len:uint32][value][crc32:uint32]
    // ? CRC32 覆盖除自身之外的所有字段
    // ? 使用 file_.append(buf) 写入
    return 0;
}
```

记录格式的字节布局，结合 Lab 7 概述中的格式描述：

```
offset + 0                    : key_len (uint16_t, 2 bytes)
offset + 2                    : key (key_len bytes)
offset + 2 + key_len          : val_len (uint32_t, 4 bytes)
offset + 2 + key_len + 4      : value (val_len bytes)
offset + 2 + key_len + 4 + val_len : crc32 (uint32_t, 4 bytes)
```

CRC32 的计算需要覆盖从 `key_len` 字段到 `value` 末尾的所有字节——也就是除 crc32 本身之外的所有内容。这样一旦任何字段发生损坏，校验就会失败。

**注意**：`append` 返回的是写入**前**的 `file_.size()`，也就是本次记录的起始偏移量。这个值将被 `SSTBuilder` 编码进 SST Block 的 vlog 引用中（8 字节的 `vlog_offset` 字段）。

### 2.3 VLog::read_value

`read_value` 根据偏移量随机读取 VLog 中的 value：

```cpp
std::string VLog::read_value(uint64_t offset, uint32_t value_size) {
    // TODO: Lab 7.1 从指定 offset 读取 value
    // ? 先读 key_len (uint16_t, 2 bytes) 以跳过 key
    // ? value 起始位置 = offset + 2 + key_len + 4
    // ? 读取 value_size 个字节返回
    return "";
}
```

读取流程：
1. 从 `offset` 处读 2 字节，得到 `key_len`
2. 跳过 `key_len` 字节的 key 内容
3. 再跳过 4 字节的 `val_len` 字段（我们不需要用它，因为调用方已经传入了 `value_size`）
4. 从当前位置读取 `value_size` 字节，返回 value

这里故意**不校验 CRC**，理由是：
- 写入路径已经通过 CRC 保证了数据完整性
- 若 VLog 末尾有不完整的写入（如崩溃时），对应的 SST 引用在 WAL 恢复阶段已被处理，`read_value` 不会被调用到那些损坏的偏移
- CRC 校验主要用于 GC 扫描时检测死记录，本 Lab 不涉及

---

## 3 测试

完成 VLog 实现后，可以单独验证 `append` 和 `read_value` 的正确性：

```cpp
// 验证思路
auto vlog = VLog::open("/tmp/test.data");
uint64_t off = vlog->append("key1", "hello_world");
assert(vlog->read_value(off, 11) == "hello_world");
```

需要同时完成 Lab 7.2 和 Lab 7.3 后，才能运行完整的端到端测试：

```bash
xmake run test_wisckey
```

完成 VLog 实现后，继续阅读 [Lab 7.2](./lab7.2-SSTBuilder-WiscKey.md)，了解如何在 SST 层接入 VLog，以及 WiscKey footer 的格式设计。
