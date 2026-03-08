# Lab 7 WiscKey 键值分离

## 1 背景：LSM Tree 的写放大问题

### 1.1 写放大的根源

在前几个 Lab 中，我们已经完整地实现了 LSM Tree 的核心流程：

```
put(k, v) → MemTable → flush → L0 SST → Compact → L1/L2/... SST
```

每次 Compact，系统需要把多个 SST 文件读入内存，归并排序后再写回磁盘。这个"读 + 重写"的过程对 key 来说是必要的（因为要合并多个版本、消除重复），但对 value 来说却是纯粹的冗余开销——**value 本身并没有发生任何变化，但它随着 key 被反复读写**。

以一个具体例子量化这个问题。假设：
- LSM Tree 共 3 层（L0、L1、L2），层间放大系数为 10
- 平均 value 大小为 1 KB，key 大小为 16 B
- 一条数据从 L0 最终沉降到 L2，需要参与约 **30 次 Compact**

那么每写入 1 KB 的有效数据，实际产生的磁盘写入约为 **30 KB**，**写放大系数达 30 倍**。当 value 更大（如视频、图片的元数据、JSON blob），写放大会随之线性增长，成为系统吞吐量的主要瓶颈。

### 1.2 问题的本质

观察 Compact 做的两件事：

1. **key 的合并**：消除旧版本，对多个 SST 的 key 排序归并 → 这是 LSM Tree 的核心语义，**不可省略**
2. **value 的重写**：将 value 随 key 一起拷贝到新 SST → 这是附带代价，**可以避免**

写放大的本质是：Compact 的逻辑复杂度由 key 决定，但 I/O 开销由 key + value 共同决定。当 value 占主导时，大量 I/O 用于搬运与合并逻辑无关的数据。

### 1.3 WiscKey 的解法

**WiscKey**（来自论文 *WiscKey: Separating Keys from Values in SSD-Conscious Storage*，FAST 2016）提出了一个直接的解法：

> **将 key 和 value 分开存储。SST 中只存 key 和一个 12 字节的位置引用；value 本体追加写入一个独立的顺序日志文件（Value Log，简称 VLog）。**

这样 Compact 时只需重写体量很小的 key 部分。value 始终待在 VLog 中原地不动，写放大因此大幅降低。

这个思路简单，但实现涉及多个组件的协调，以及一些微妙的边界情况。本 Lab 将带你一步步实现它。

---

## 2 整体架构

### 2.1 组件拓扑对比

引入 WiscKey 前后，系统多了一个顺序追加的 VLog 文件。其余组件（MemTable、SST 层级、Compact 流程）的结构完全不变，变化仅在于 **SST 文件中存什么**：

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         原有架构（内联模式）                                  │
│                                                                             │
│  put(k, v) ──► MemTable ──► flush ──► SST  ◄── get(k)                      │
│                                        │                                    │
│                              Block: [ key | value(完整) | tranc_id ]         │
└─────────────────────────────────────────────────────────────────────────────┘

┌─────────────────────────────────────────────────────────────────────────────┐
│                       WiscKey 架构（大 value 分离）                           │
│                                                                             │
│  put(k, v) ──► MemTable ──► flush ──► SST  ◄── get(k)                      │
│                               │        │          │                         │
│                          (大value时)  Block:    resolve_value()              │
│                               │     [ key | vlog_ref(12B) | tranc_id ]      │
│                               │          │              │                   │
│                               ▼          └──────────────┘                   │
│                            VLog                  │                          │
│                      ┌─────────────┐             │ read_value(offset, size) │
│                      │ append-only │ ◄───────────┘                          │
│                      │  .data file │                                        │
│                      └─────────────┘                                        │
└─────────────────────────────────────────────────────────────────────────────┘
```

**关键区别**：
- 小 value（`<= wisckey_threshold`）继续内联存入 SST Block，行为与原来完全一致
- 大 value（`> wisckey_threshold`）在 flush 时被分流写入 VLog，SST Block 中只存 12 字节的位置引用
- Compact 时 SST 中的 key 部分照常重写，但 VLog 中的 value **原地不动**，写放大因此降低

### 2.2 为什么设置阈值？

你可能会问：既然分离 value 能降低写放大，为什么不把所有 value 都分离？

原因在于读放大的代价。对于内联存储，`get` 操作在找到 SST 条目后立刻得到 value；对于 WiscKey 模式，`get` 需要额外一次随机磁盘读（读 VLog）。对于小 value，这次额外的随机读代价远超写放大收益。

因此 WiscKey 采用**阈值策略**：
- 小 value（如短字符串、数字）：内联存储，读性能不变
- 大 value（如 JSON、二进制 blob）：分离到 VLog，显著降低 Compact I/O

在我们的实现中，阈值由 `config.toml` 的 `wisckey_value_threshold` 配置，默认为 0（禁用，完全内联）。

### 2.3 写入与读取路径概览

```
写入路径（大 value）：
  put("k", "large_value")
       │
       ▼
  MemTable.put("k", "large_value")         ← 内存中仍存完整 value
       │
       │  (触发 flush)
       ▼
  SSTBuilder.add("k", "large_value")
       │
       ├──► VLog.append("k", "large_value") ──► 返回 offset=X, size=N
       │
       └──► Block.add_entry("k", [X:8B][N:4B], tranc_id)
                                  └─ 12字节 vlog 引用写入 SST

读取路径：
  get("k")
       │
       ▼
  MemTable.get("k")  ── 未命中 ──►  SST.get("k")
                                        │
                                        ▼
                                 SstIterator 定位到 entry
                                        │
                                        ▼
                                 (*iter).second
                                   = resolve_value([X:8B][N:4B])
                                        │
                                        ▼
                                 VLog.read_value(offset=X, size=N)
                                        │
                                        ▼
                                  返回 "large_value"
```

注意 MemTable 中始终存储完整 value——分离只发生在 **flush 到磁盘**时，而不是写入时。这样可以保证内存中的读取路径不受影响。

---

## 3 VLog 文件格式

VLog 是一个**只追加**的二进制文件，所有记录顺序写入，偏移量单调递增。

### 3.1 为什么存 key？

你可能注意到 VLog 的记录格式中包含 key，而 key 已经在 SST 中存储了。重复存储 key 的原因是：

1. **数据校验**：CRC32 覆盖 key + value，可以检测 VLog 文件的物理损坏
2. **垃圾回收（GC）**：VLog GC 时需要顺序扫描 VLog，判断某条记录是否仍被 SST 引用。有了 key，GC 才能在不读 SST 的情况下定位当前最新的 vlog 引用（本 Lab 不要求实现 GC，但格式设计需要支持它）

### 3.2 单条记录的字节布局

每次调用 `VLog::append(key, value)` 追加一条记录，格式如下：

```
VLog 文件内部结构（每条记录）：

 ┌──────────┬─────────────┬──────────┬──────────────┬──────────┐
 │ key_len  │     key     │ val_len  │    value     │  crc32   │
 │ uint16   │ key_len 字节 │ uint32   │ val_len 字节  │ uint32   │
 │  (2B)    │             │  (4B)    │              │  (4B)    │
 └──────────┴─────────────┴──────────┴──────────────┴──────────┘
  ↑ offset                            ↑ value 起始位置            ↑ 校验覆盖范围结束
                                      = offset + 2 + key_len + 4

  CRC32 校验覆盖：从 key_len 到 value 末尾的所有字节（不含 crc32 本身）
```

`VLog::append` 返回该条记录的**起始偏移量**（即写入前 `file_.size()` 的值），这个值会被存入 SST Block 的 vlog 引用中。

### 3.3 VLog 文件全局视图

```
VLog 文件（vlog.data）：

 offset=0           offset=A           offset=B           offset=C
     ↓                   ↓                   ↓                   ↓
 ┌───────────────┬───────────────────┬───────────────────┬────────── ...
 │  record for   │   record for      │   record for      │
 │  key1/value1  │   key2/value2     │   key3/value3     │   ...
 └───────────────┴───────────────────┴───────────────────┴────────── ...
  └─ SST中存 A=0  └─ SST中存 B=...    └─ SST中存 C=...
```

SST Block 中对应的 vlog 引用（12 字节）：

```
 ┌─────────────────────┬────────────────┐
 │    vlog_offset      │   value_size   │
 │     uint64 (8B)     │   uint32 (4B)  │
 └─────────────────────┴────────────────┘
  记录在 VLog 中的起始偏移   value 的原始字节长度
  （即 VLog::append 的返回值）（用于定位 value 结束位置）
```

读取时，`resolve_value` 持有这 12 字节，调用 `VLog::read_value(offset, size)` 即可取回原始 value，整个过程对上层迭代器完全透明。

---

## 4 SST 文件格式扩展（WiscKey Footer）

### 4.1 为什么需要扩展 footer？

系统重启时，`SST::open` 需要从磁盘加载所有 SST 文件。此时有一个关键问题：

> 这个 SST 文件里存的是完整 value，还是 12 字节的 vlog 引用？

如果判断错误——把 vlog 引用当作真实 value 返回，或者把真实 value 当作 vlog 引用去读——数据将发生损坏。

一个简单的解法是：把存储模式写入 SST footer。这样只要看 footer 就能知道该如何解析 Block 中的 value 字段。

### 4.2 两种 footer 格式对比

```
老格式 footer（24 字节）：
  [meta_offset : uint32]  @ size-24
  [bloom_offset: uint32]  @ size-20
  [min_tranc_id: uint64]  @ size-16
  [max_tranc_id: uint64]  @ size-8

WiscKey footer（26 字节）：
  [meta_offset : uint32]  @ size-26
  [bloom_offset: uint32]  @ size-22
  [min_tranc_id: uint64]  @ size-18
  [max_tranc_id: uint64]  @ size-10
  [storage_mode: uint8 ]  @ size-2   (1 = WiscKey)
  [magic       : uint8 ]  @ size-1   (0x4B，作为格式标识)
```

最后 1 字节的 magic 值 `0x4B`（即字符 `'K'`，取自 WiscKey 首字母）是格式探测的锚点——`SST::open` 通过检查这一字节来决定使用哪种 footer 解析方式，从而实现与旧格式的**向后兼容**。

`SST::open` 在读取时，会检查文件末尾字节是否等于 `0x4B`（`WISCKEY_MAGIC`）来决定使用哪种 footer 解析方式。

---

## 5 代价与权衡

WiscKey 不是免费的午餐，引入它会带来新的代价：

| 指标 | 内联模式 | WiscKey 模式 |
|------|----------|-------------|
| 写放大 | 高（value 随 key 多次重写） | 低（value 只写一次） |
| 读放大 | 低（单次 SST 读取） | 略高（SST + VLog 随机读） |
| 空间放大 | 低（Compact 清理旧版本） | 高（VLog 死记录不自动清理） |

**空间放大**是 WiscKey 的主要代价：Compact 后，新 SST 可能不再引用 VLog 中的某些旧记录，但那些记录仍然占据磁盘空间。解决方案是 **VLog GC（垃圾回收）**，它通过扫描 VLog 并与当前 SST 中的引用对比，回收无效记录。本 Lab 不要求实现 GC，但了解这个代价有助于理解 WiscKey 的整体设计逻辑。

---

## 6 涉及的组件和文件

本 Lab 你需要修改或实现的文件：

| 文件 | 主要内容 |
|------|---------|
| `src/vlog/vlog.cpp` | VLog 的打开、追加、读取 |
| `src/sst/sst.cpp` | `SSTBuilder` WiscKey 构造函数、`SST::open` footer 检测、`resolve_value` |
| `src/lsm/engine.cpp` | `LSMEngine` 构造函数初始化 VLog，`flush` 和 `gen_sst_from_iter` 按配置选择 WiscKey 模式 |
| `include/vlog/vlog.h` | VLog 类定义（已提供，只需实现 cpp） |
| `config.toml` | `wisckey_value_threshold` 配置项 |

建议按顺序完成：
1. **Lab 7.1**：实现 VLog 的三个核心接口（open / append / read_value）
2. **Lab 7.2**：在 SSTBuilder 和 SST 中接入 VLog，处理 WiscKey footer
3. **Lab 7.3**：在 LSMEngine 中初始化 VLog，在 flush 和 compact 路径中选择正确的 SSTBuilder

每个 Lab 完成后可以运行 `xmake run test_wisckey` 验证进度。
