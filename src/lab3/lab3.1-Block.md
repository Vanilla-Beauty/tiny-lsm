# Lab 3.1 Block 实现
# 1 准备工作
老套路, 我们先理一下`Block`的数据结构, 看看头文件定义:
```cpp
// include/block/block.h
class Block : public std::enable_shared_from_this<Block> {
  friend BlockIterator;

private:
  std::vector<uint8_t> data; // 对应架构图的 Data
  std::vector<uint16_t> offsets; // 对应架构图的 Offset
  size_t capacity;

  struct Entry {
    std::string key;
    std::string value;
    uint64_t tranc_id;
  };
  // ...
};
```

这里可能涉及C++的新特性:
```cpp
public std::enable_shared_from_this<Block>
```
`std::enable_shared_from_this` 是 C++11 引入的一个标准库特性，它允许一个对象安全地创建指向自身的`std::shared_ptr`。这里先简单说明一下, 后续实现迭代器的时候就知道其作用了。

这里主要对`data`和`offsets`这两个数据结构进行说明, 他们在构建阶段和读取阶段存在一定区别, 首先还是给出架构图:

![Block](../images/lab3/Block.png)

**构建阶段**
当我们从`Skiplist`拿到数据构建一个`SST`时, `SST`需要逐个构建`Block`, 这个`Block`在构建时步骤如下:
1. 逐个将编码的键值对(也就是`Entry`)写入`data`数组, 同时将每个`Entry`的偏移量记录在内存中的`offsets`数组中。
2. 当这个`Block`容量达到阈值时, `Block`构建完成, 你需要将`offsets`数组写入到`Block`的末尾。
3. 还需要再`Block`末尾写入一个`Entry Num`值, 用于标识这个`Block`中键值对的数量, 从而在解码时获取`Offset`的其实位置(因为每个`Entry Offset`大小是固定的整型值)
4. 当前`Block`构建完成, `SST`开始构建下一个`Block`。

> 这里之所以将先将键值对持久化到`data`数组, 而元信息暂存于内存的`offsets`数组, 是因为`Data`是在数据部分之后的的`Offset`部分的偏移需要再键值对完全写入`Data`部分后才能确定

**解码阶段**
解码阶段, 直接将`Data`、`Offset`解码形成内存中的`Block`的实例以为上层组件提供查询功能，同时如果实现了缓存池，需要再缓存池中进行记录。

# 2 代码实现
你需要修改的函数都在`src/block/block.cpp`中。

## 2.1 Block 编码和解码
这里你先不要管这个`Block`是哪里来的, 就当它已经存在, 实现编码和解码的功能:
```cpp
std::vector<uint8_t> Block::encode(bool with_hash) {
  // TODO Lab 3.1 编码单个类实例形成一段字节数组
  return {};
}

std::shared_ptr<Block> Block::decode(const std::vector<uint8_t> &encoded,
                                     bool with_hash) {
  // TODO Lab 3.1 解码字节数组形成类实例
  return nullptr;
}
```
两个函数都接受 `with_hash` 参数：
- `encode(with_hash)`: 若为 `true`，在编码末尾额外追加 CRC32 校验值（4字节）
- `decode(encoded, with_hash)`: 若为 `true`，先校验 CRC32，校验失败则抛出异常

> 这样统一设计，使得 `SSTBuilder` 在构建时可以选择是否写入校验码，`read_block` 在读取时按同样的标志进行校验，编解码行为完全对称。

> 编解码时你需要注意数据的格式, 如果校验格式错误, 你需要抛出异常, 否则错误将非常难以`Debug`

## 2.2 局部数据编解码函数
对于二进制数据, 你需要按照设计的编码结构获取其`key`, `value`和`tranc_id`, 这里我们实现几个辅助函数:
```cpp
// 从指定偏移量获取entry的key
std::string Block::get_key_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的key
  return "";
}

// 从指定偏移量获取entry的value
std::string Block::get_value_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的value
  return "";
}

uint64_t Block::get_tranc_id_at(size_t offset) const {
  // TODO Lab 3.1 从指定偏移量获取entry的tranc_id
  // ? 你不需要理解tranc_id的具体含义, 直接返回即可
  return 0;
}
```

## 2.3 构建 Block
`Block`构建是由`SST`控制的, 其会不断地调用下面这个函数添加键值对:
```cpp
bool Block::add_entry(const std::string &key, const std::string &value,
                      uint64_t tranc_id, bool force_write) {
  // TODO Lab 3.1 添加一个键值对到block中
  // ? 返回值说明：
  // ? true: 成功添加
  // ? false: block已满, 拒绝此次添加
  return false;
}
```
这里需要注意, `force_write`参数表示是否强制写入, 如果为`true`, 则不管`Block`是否已满, 都强制写入, 否则如果`Block`已满, 则拒绝此次写入。

`Block`是否已满的判断将当前数据容量与成员变量`capacity`进行比较, `capacity`在`Block`初始化时由`SST`传入, 表示一个`Block`的最大容量。

> 如果你需要一些使用`config.toml`中预定义的一些阈值变量或者其他常来那个, 你也可以通过`TomlConfig::getInstance().getXXX`的方式获取

## 2.4 二分查询
`Block`构建时是通过`SST`遍历`Skiplist`的迭代器调用`add_entry`实现的, 因此`Block`的数据是有序的, 你需要实现一个二分查找函数, 用于在`Block`中查找指定`key`所属的`Entry`在`offset`元数据中的索引:
```cpp
std::optional<size_t> Block::get_idx_binary(const std::string &key,
                                            uint64_t tranc_id) {
  // TODO Lab 3.1 使用二分查找获取key对应的索引
  return std::nullopt;
}
```

`get_value_binary`函数中会调用`get_idx_binary`函数, 并返回指定`key`的`value`:
```cpp
// 使用二分查找获取value
// 要求在插入数据时有序插入
std::optional<std::string> Block::get_value_binary(const std::string &key,
                                                   uint64_t tranc_id) {
  auto idx = get_idx_binary(key, tranc_id);
  if (!idx.has_value()) {
    return std::nullopt;
  }

  return get_value_at(offsets[*idx]);
}
```

# 3 测试
如果成功完成了上述的所有函数, 你应该如下运行测试并得到结果:
```bash
✗ xmake
[100%]: build ok, spent 1.94s
✗ xmake run test_block
[==========] Running 10 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 10 tests from BlockTest
[ RUN      ] BlockTest.DecodeTest
[       OK ] BlockTest.DecodeTest (0 ms)
[ RUN      ] BlockTest.EncodeTest
[       OK ] BlockTest.EncodeTest (0 ms)
[ RUN      ] BlockTest.BinarySearchTest
[       OK ] BlockTest.BinarySearchTest (0 ms)
[ RUN      ] BlockTest.EdgeCasesTest
[       OK ] BlockTest.EdgeCasesTest (0 ms)
[ RUN      ] BlockTest.LargeDataTest
[       OK ] BlockTest.LargeDataTest (0 ms)
[ RUN      ] BlockTest.ErrorHandlingTest
[       OK ] BlockTest.ErrorHandlingTest (1 ms)
[ RUN      ] BlockTest.IteratorTest
test/test_block.cpp:225: Failure
Expected equality of these values:
  count
    Which is: 0
  test_data.size()
    Which is: 100
```
你应该能通过`BlockTest.IteratorTest`之前的所有单元测试, `BlockTest.IteratorTest`这个测试会测试`Block`的迭代器功能, 因为`Block`的迭代器功能还没有实现, 所以会失败, 这是符合预期的。

# 4 下一步
现在进入下一步前, 你可以先思考:
1. 如何实现`Block`的迭代器
2. 为什么我们需要让`Block`类继承`std::enable_shared_from_this<Block>`?

带着这些疑问, 欢迎开启下一章 

