# Lab 7.3 Engine 集成 WiscKey

在 Lab 7.1 和 Lab 7.2 中，我们分别实现了 VLog 的存储接口和 SST 层的 WiscKey 支持。现在需要在 LSMEngine 层把这两者连接起来——初始化 VLog，并在 flush 和 compact 时将它传入 SSTBuilder。

这一层的工作相对简单，但有几个细节值得注意。

---

## 1 配置项

WiscKey 的行为由 `config.toml` 中的配置项控制：

```toml
[lsm.wisckey]
# value 超过此阈值（字节）才会被分离到 VLog
# 设置为 0 表示禁用 WiscKey，完全使用内联模式
wisckey_value_threshold = 1024
```

在代码中通过 `TomlConfig::getInstance().getWisckeyValueThreshold()` 获取此值。

当阈值为 0 时，所有 value 都走内联路径，行为与引入 WiscKey 之前完全一致。这是默认的非破坏性配置，确保现有测试（`test_lsm`、`test_sst` 等）不受影响。

---

## 2 LSMEngine 构造函数

### 2.1 为什么无论如何都要初始化 VLog？

`LSMEngine` 在初始化时**必须**打开 VLog，即使 WiscKey 阈值为 0：

```cpp
// 无论是否启用 WiscKey，都初始化 vlog_
vlog_ = VLog::open(data_dir + "/vlog.data");
```

原因如下：

1. **重启恢复的正确性**：即使当前配置禁用了 WiscKey，磁盘上可能已经存在之前以 WiscKey 模式写入的 SST 文件。如果 `vlog_` 为空，这些 SST 在 `resolve_value` 时会因为 `vlog_` 为空而崩溃。

2. **开销可忽略**：`VLog::open` 只是打开（或创建）一个文件，不加载任何数据。对非 WiscKey 场景几乎没有影响。

### 2.2 加载 SST 时传入 VLog

加载 SST 文件时，将 `vlog_` 传入 `SST::open`：

```cpp
auto sst = SST::open(sst_id, FileObj::open(sst_path, false), block_cache, vlog_);
```

`SST::open` 内部会通过 footer magic 判断该文件是否为 WiscKey 格式，再决定是否使用传入的 `vlog_`。对于普通格式的 SST，`vlog_` 参数被忽略（`sst->vlog_` 设为 nullptr）。

---

## 3 flush 中的 SSTBuilder 选择

`LSMEngine::flush` 在构造 `SSTBuilder` 时，根据 WiscKey 配置选择不同的构造函数：

```cpp
size_t wk = TomlConfig::getInstance().getWisckeyValueThreshold();
SSTBuilder builder = (wk > 0 && vlog_)
    ? SSTBuilder(TomlConfig::getInstance().getLsmBlockSize(), true, vlog_, wk)
    : SSTBuilder(TomlConfig::getInstance().getLsmBlockSize(), true);
```

两个条件缺一不可：
- `wk > 0`：配置层面启用了 WiscKey
- `vlog_` 非空：VLog 已成功初始化

两者都满足时，使用 WiscKey 构造函数，`add` 会对超过阈值的 value 自动调用 `vlog_->append` 分流。

---

## 4 gen_sst_from_iter 中的 SSTBuilder 选择

Compact 时同样需要根据配置选择 WiscKey 或普通模式：

```cpp
std::vector<std::shared_ptr<SST>>
LSMEngine::gen_sst_from_iter(BaseIterator &iter, size_t target_sst_size,
                             size_t target_level) {
    size_t wk = TomlConfig::getInstance().getWisckeyValueThreshold();
    auto new_sst_builder = (wk > 0 && vlog_)
        ? SSTBuilder(block_size, true, vlog_, wk)
        : SSTBuilder(block_size, true);
    // ...
}
```

### 4.1 Compact 路径的特殊情况

Compact 时，迭代器遍历的 entry 其 value 可能已经是 12 字节的 vlog 引用（来自之前 flush 的 WiscKey 格式 SST）。此时 `SSTBuilder::add` 收到的 value 大小为 12 字节：

```
full_compact(level=0)
     │
     ▼
  gen_sst_from_iter(iter, ...)
     │
     │  iter 遍历 L0 + L1 的所有 entry
     │  entry.value = [X:8B][N:4B]  ← 仍是 vlog 引用（12字节）
     │
     ▼
  SSTBuilder(WiscKey 模式).add("key", [X:8B][N:4B], tranc_id)
     │
     │  value.size() == 12  ≤ threshold（vlog 引用本身很小）?
     │
     ├── 若 threshold > 12：引用被当作"小 value"直接内联 ← 此处有潜在问题
     │                                                     实现时需特别注意
     └── 若 threshold ≤ 12：引用再次被 append 到 VLog
                              ← 产生 VLog 空间放大（旧记录变垃圾）
                              ← GC 是进阶内容，本 Lab 不要求
```

这里有一个微妙的情况：当阈值大于 12 时，12 字节的 vlog 引用被视为"小 value"内联写入了新 SST，而不是再次追加到 VLog——这实际上是正确的行为，Compact 后新 SST 中仍然存储的是 vlog 引用，读取时 `resolve_value` 依然能正确解析。

> **注意**：Compact 过程中，若 threshold ≤ 12，已经存入 VLog 的 value 会被**再次**调用 `vlog_->append` 写入，这会产生 VLog 的空间放大。VLog 的垃圾回收（GC）是 WiscKey 的进阶内容，本 Lab 不要求实现，仅需保证功能正确性。

---

## 5 整体数据流

### 5.1 写路径：flush 时的分流

```
put("key", "large_value")    [large_value.size() > threshold]
         │
         ▼
  ┌─────────────┐
  │  MemTable   │  put("key", "large_value")    ← 内存中仍存完整 value
  └─────────────┘
         │  触发 flush（内存超限）
         ▼
  ┌───────────────────────────────────────────────────────┐
  │  SSTBuilder（WiscKey 模式）                            │
  │                                                       │
  │  add("key", "large_value", tranc_id)                  │
  │           │                                           │
  │           ├─ value.size() > threshold ?               │
  │           │         │ Yes                             │
  │           │         ▼                                 │
  │           │   VLog::append("key", "large_value")      │
  │           │         │                                 │
  │           │         └──► 写入 vlog.data               │
  │           │               返回 offset=X, size=N       │
  │           │                                           │
  │           └─► Block::add_entry(                       │
  │                 key   = "key",                        │
  │                 value = [X:8B][N:4B],  ← 12字节引用   │
  │                 tranc_id)                             │
  └───────────────────────────────────────────────────────┘
         │  build()
         ▼
  ┌──────────────────────────────────────────────────────────────┐
  │  sst_00000...001.0  （磁盘文件）                              │
  │                                                              │
  │  Block_0: [ "key" | offset=X,size=N | tranc_id ] ...        │
  │  ...                                                         │
  │  Meta Section | Bloom Section | Footer(26B, magic=0x4B)      │
  └──────────────────────────────────────────────────────────────┘

  ┌──────────────────────────────────────────────────────────────┐
  │  vlog.data （磁盘文件）                                       │
  │                                                              │
  │  offset=0         offset=X                                   │
  │      ↓                ↓                                      │
  │  [rec0: k0/v0] ... [rec: "key"/"large_value" | crc32] ...    │
  └──────────────────────────────────────────────────────────────┘
```

### 5.2 读路径：resolve_value 透明解引用

```
get("key")
     │
     ▼
 MemTable::get("key")  ── 未命中 ──►  SST::get("key")
                                           │
                                           │  find_block_idx → read_block
                                           ▼
                                    BlockIterator 定位到 entry
                                           │
                                           │ entry.value = [X:8B][N:4B]
                                           ▼
                                    SstIterator::operator*()
                                           │
                                           ▼
                                    sst_->resolve_value([X:8B][N:4B])
                                           │
                                    storage_mode_ == 1 ?
                                           │ Yes
                                           ▼
                                    解析 offset=X, size=N
                                           │
                                           ▼
                                    vlog_->read_value(X, N)
                                           │
                                           │  定位到 vlog.data[X]
                                           │  跳过 key_len + key + val_len
                                           │  读取 N 字节
                                           ▼
                                    返回 "large_value"
                                           │
                                    ◄──────┘  对调用方透明，
                                              与内联模式返回值格式完全相同
```

### 5.3 Compact 路径：key 重写，value 不动

```
full_compact(level=0)
     │
     ▼
  gen_sst_from_iter(iter, ...)
     │
     │  iter 遍历 L0 + L1 的所有 entry
     │  entry.value = [X:8B][N:4B]  ← 仍是 vlog 引用
     │
     ▼
  SSTBuilder(WiscKey 模式).add("key", [X:8B][N:4B], tranc_id)
     │
     │  value.size() == 12  ≤ threshold（vlog 引用本身很小）
     │  → 直接内联写入新 SST（不再次写 VLog）
     │
     ▼
  compact 结果：新 SST 中 key 重新排布，vlog.data 内容原地不变
```

Compact 后 VLog 中可能积累大量已无 SST 引用的"死记录"，这是 WiscKey 的**空间放大**代价。论文中通过在线 GC 线程解决，本 Lab 仅需实现写入和读取的正确性，GC 留作后续拓展。

---

## 6 代价与收益的再审视

完成所有实现后，可以从数据流的视角再次对比两种模式的代价：

**写放大的变化**：
- 内联模式：value 随 key 参与每次 Compact，每层一次重写
- WiscKey 模式：value 只在 flush 时写入 VLog 一次，此后 Compact 不再触碰 value 内容

**读放大的变化**：
- 内联模式：`get` → SST 随机读 1 次
- WiscKey 模式：`get` → SST 随机读 1 次 + VLog 随机读 1 次

**空间放大的变化**：
- 内联模式：Compact 会清除旧版本，空间放大受控
- WiscKey 模式：VLog 只追加不删除，旧版本数据在 GC 之前一直占用磁盘空间

WiscKey 最适合 value 远大于 key、且以写密集型负载为主的场景（如对象存储、时序数据库的大值存储）。对于 key/value 大小相近或读密集型场景，内联模式的综合性能可能更优。

---

## 7 测试

```bash
xmake run test_wisckey
```

你应该能通过所有 WiscKey 相关测试，同时原有的 `test_lsm`、`test_sst` 等测试不应受到影响（因为 WiscKey 阈值默认配置为 0 时完全退化为原有行为）。
