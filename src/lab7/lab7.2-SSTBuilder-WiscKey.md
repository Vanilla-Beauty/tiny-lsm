# Lab 7.2 SSTBuilder 和 SST 的 WiscKey 支持

在 Lab 7.1 中，我们实现了 VLog 的底层存储接口。现在需要让 SST 层"感知"VLog 的存在，在写入时将大 value 分流到 VLog，在读取时透明地完成解引用。

这一层的核心挑战是**透明性**：上层调用方（MemTable、Compact 迭代器）不应感知到 value 被分离了——`get` 返回的 value 必须和内联模式下完全一致。

---

## 1 SSTBuilder WiscKey 构造函数

`SSTBuilder` 已有一个普通构造函数（Lab 3.5 中实现）。WiscKey 模式通过新增一个重载构造函数来引入：

```cpp
// 普通模式（已有）
SSTBuilder::SSTBuilder(size_t block_size, bool has_bloom);

// WiscKey 模式：value 超过阈值时写入 vlog，SST 中只存 12 字节引用
SSTBuilder::SSTBuilder(size_t block_size, bool has_bloom,
                       std::shared_ptr<VLog> vlog,
                       size_t wisckey_threshold)
    : block(block_size), vlog_(std::move(vlog)),
      wisckey_threshold_(wisckey_threshold), storage_mode_(1) {
    // ...
}
```

注意 `storage_mode_` 被初始化为 `1`，这个值在 `build` 时会写入 SST footer，以便重启后的 `SST::open` 能正确识别文件格式（详见第 4 节）。

### `add` 方法的 WiscKey 分支

WiscKey `SSTBuilder` 的 `add` 方法在满足以下全部条件时触发 value 分离：

- `storage_mode_ == 1`（WiscKey 模式）
- `vlog_` 非空
- `value` 非空（删除标记不分离）
- `value.size() > wisckey_threshold_`

此时核心逻辑变为：

```cpp
// WiscKey: 将大 value 写入 vlog，SST 中存储 12 字节引用
uint64_t offset = vlog_->append(key, value);
// 构造 12 字节的 vlog 引用
vlog_ref.resize(sizeof(uint64_t) + sizeof(uint32_t));
uint32_t val_size = static_cast<uint32_t>(value.size());
memcpy(vlog_ref.data(), &offset, sizeof(uint64_t));
memcpy(vlog_ref.data() + sizeof(uint64_t), &val_size, sizeof(uint32_t));
actual_value = &vlog_ref;  // 用引用代替原始 value 写入 block
```

12 字节的构成：
- 前 8 字节：`vlog_offset`（`uint64_t`），即 `VLog::append` 返回的起始偏移量
- 后 4 字节：`value_size`（`uint32_t`），原始 value 的字节数（用于告知 `read_value` 读多少字节）

---

## 2 SST::open 的 WiscKey 检测

`SST::open` 在读取 footer 时需要先探测文件格式：

```cpp
std::shared_ptr<SST> SST::open(size_t sst_id, FileObj file,
                               std::shared_ptr<BlockCache> block_cache,
                               std::shared_ptr<VLog> vlog) {
    // ...（原有 Lab 3.6 逻辑）...

    // 新增：检测 WiscKey footer
    // 1. 若 file_size >= 26 且 file[-1] == WISCKEY_MAGIC (0x4B)
    //    则使用 26 字节 footer，读取 storage_mode_ = file[-2]
    // 2. 否则使用 24 字节老 footer

    // 新增：将 vlog 赋值给 sst->vlog_
}
```

对于普通模式的 SST，`vlog` 参数为 `nullptr`，`storage_mode_` 保持 0，`resolve_value` 直接返回原始值，完全向后兼容。

---

## 3 SST::resolve_value

`resolve_value` 是 WiscKey 的"解引用"接口，在读取 SST 时被迭代器调用，对上层完全透明：

```cpp
std::string SST::resolve_value(const std::string &raw_value) const {
    if (storage_mode_ == 0 || raw_value.empty()) {
        return raw_value;  // 普通模式，直接返回
    }
    // WiscKey 模式：raw_value 是 12 字节引用
    // 解析 [offset:8][size:4] 并调用 vlog_->read_value
    uint64_t off = 0;
    uint32_t sz = 0;
    memcpy(&off, raw_value.data(), sizeof(uint64_t));
    memcpy(&sz, raw_value.data() + sizeof(uint64_t), sizeof(uint32_t));
    return vlog_->read_value(off, sz);
}
```

这个函数由 `SstIterator` 在对外返回 value 时调用。调用方得到的 value 与内联模式下完全相同，不需要感知 WiscKey 的存在。

---

## 4 build 中的 WiscKey footer

### 4.1 为什么要扩展 footer？

回顾你在 Lab 3.5 中实现的**普通 SST footer（24 字节）**：

```
SST 文件末尾（普通模式，24 字节 footer）：
┌─────────────────────────────────────────────────────────────────┐
│  ...Block Section...  │  Meta Section  │  Bloom Section  │ Footer│
└─────────────────────────────────────────────────────────────────┘

Footer 内部布局（共 24 字节，从 size-24 开始读）：
┌──────────────────┬──────────────────┬──────────────────┬──────────────────┐
│ meta_offset      │ bloom_offset     │ min_tranc_id     │ max_tranc_id     │
│   uint32 (4B)    │   uint32 (4B)    │   uint64 (8B)    │   uint64 (8B)    │
└──────────────────┴──────────────────┴──────────────────┴──────────────────┘
 ↑ file[size-24]    ↑ file[size-20]    ↑ file[size-16]    ↑ file[size-8]
```

这个 footer 记录了各 Section 的位置和事务 id 范围，使得 `SST::open` 能从文件末尾反向定位所有内容。

---

**WiscKey 模式的问题**：重启后 `SST::open` 如何知道这个 SST 文件存的是内联 value 还是 vlog 引用？如果判断错了，会把 12 字节的 vlog 引用当成真实 value 返回，或者反过来把真实 value 当成 vlog 引用去读，数据就会损坏。

### 4.2 解决方案：追加 2 个标志字节

在原有 24 字节 footer 末尾追加 2 个字节，形成 **26 字节的 WiscKey footer**：

```
SST 文件末尾（WiscKey 模式，26 字节 footer）：
┌──────────────────┬──────────────────┬──────────────────┬──────────────────┬──────┬───────┐
│ meta_offset      │ bloom_offset     │ min_tranc_id     │ max_tranc_id     │ mode │ magic │
│   uint32 (4B)    │   uint32 (4B)    │   uint64 (8B)    │   uint64 (8B)    │  1B  │  1B   │
└──────────────────┴──────────────────┴──────────────────┴──────────────────┴──────┴───────┘
 ↑ file[size-26]    ↑ file[size-22]    ↑ file[size-18]    ↑ file[size-10]   ↑ -2   ↑ -1
                                                                          = 0x01  = 0x4B
```

- `mode`：存储模式，`0x01` 表示 WiscKey，`0x00` 表示内联（普通模式不写这两字节）
- `magic`：固定值 `0x4B`（即字符 `'K'`，取自 WiscKey 首字母），作为格式标识符

### 4.3 SST::open 的格式探测

`SST::open` 的检测逻辑基于最后 1 字节：

```
SST::open 的 footer 检测流程：

file[-1] == 0x4B ?
    ├── Yes → WiscKey footer（从 size-26 开始读）
    │         → storage_mode_ = file[-2]（= 1）
    │         → sst->vlog_ = vlog 参数
    └── No  → 普通 footer（从 size-24 开始读）
              → storage_mode_ = 0
              → sst->vlog_ = nullptr
```

这个检测对旧格式完全向后兼容：旧 SST 文件的最后 1 字节是 `max_tranc_id` 的低字节，通常不等于 `0x4B`，因此不会被误判为 WiscKey 格式。

### 4.4 build 时写入 WiscKey footer

`SSTBuilder::build` 在 WiscKey 模式下写 footer 时，在原有 24 字节后追加这 2 个字节：

```cpp
// WiscKey 额外的 2 字节（追加在普通 24 字节 footer 之后）
if (storage_mode_ == 1) {
    file_content.push_back(storage_mode_);  // 0x01
    file_content.push_back(WISCKEY_MAGIC);  // 0x4B
}
```

注意字段顺序：`mode` 在前，`magic` 在后。读取时从文件末尾往前读，先遇到 `magic`（`file[-1]`），再读 `mode`（`file[-2]`）。

---

## 5 测试

完成本节实现后，可以运行：

```bash
xmake run test_wisckey
[==========] Running N tests from 1 test suite.
[ RUN      ] WiscKeyTest.BasicPutGet
[       OK ] WiscKeyTest.BasicPutGet (X ms)
[ RUN      ] WiscKeyTest.LargeValue
[       OK ] WiscKeyTest.LargeValue (X ms)
[ RUN      ] WiscKeyTest.Persistence
[       OK ] WiscKeyTest.Persistence (X ms)
[ RUN      ] WiscKeyTest.MixedInlineAndVLog
[       OK ] WiscKeyTest.MixedInlineAndVLog (X ms)
```

完成 SST 层的 WiscKey 支持后，继续阅读 [Lab 7.3](./lab7.3-Engine-WiscKey.md)，了解如何在 LSMEngine 中将 VLog 初始化并接入到 flush 和 compact 路径中。
