# Lab 3.5 SSTBuilder
# 1 概述
`SSTBuilder`是`SST`文件的构造器, 它将`MemTable`中的数据进行编码并写入磁盘形成`SST`。不过这里我们并没有设计到不同组件数据的控制，这是由更上层的结构控制的。

**SST和SSTBuilder的关系是什么？**
区别在于，`SSTBuilder`这个类的实例只在`SST文件`构建过程中存在, 其是可写的数据结构, 构建过程可不断添加键值对进行编码。在其调用`Build`后，其会将自身数据编码为`SST文件`， 并转化为一个`SST`类实例, `SST`类本质上就是`SST文件`的控制结构。

这样说起来可能不好理解, 让我们结合代码将这个过程具体化, 先看其`SSTBuilder`和`SST`的头文件定义:
```cpp
class SST : public std::enable_shared_from_this<SST> {
  // ...  
private:
  FileObj file;
  std::vector<BlockMeta> meta_entries;
  uint32_t bloom_offset;
  uint32_t meta_block_offset;
  size_t sst_id;
  std::string first_key;
  std::string last_key;
  std::shared_ptr<BloomFilter> bloom_filter;
  std::shared_ptr<BlockCache> block_cache;
  uint64_t min_tranc_id_ = UINT64_MAX;
  uint64_t max_tranc_id_ = 0;

public:
  // ...
};

class SSTBuilder {
private:
  Block block;
  std::string first_key;
  std::string last_key;
  std::vector<BlockMeta> meta_entries;
  std::vector<uint8_t> data;
  size_t block_size;
  std::shared_ptr<BloomFilter> bloom_filter; // 后续Lab内容
  uint64_t min_tranc_id_ = UINT64_MAX; // 后续Lab内容
  uint64_t max_tranc_id_ = 0; // 后续Lab内容

  // WiscKey 键值分离字段（后续Lab内容）
  uint8_t storage_mode_ = 0;          // 0=inline, 1=WiscKey
  std::shared_ptr<VLog> vlog_;
  size_t wisckey_threshold_ = 0;

public:
  // 创建一个sst构建器, 指定目标block的大小（普通模式）
  SSTBuilder(size_t block_size, bool has_bloom);
  // WiscKey 模式：value 超过阈值时写入 vlog，SST 中只存引用（后续Lab内容）
  SSTBuilder(size_t block_size, bool has_bloom,
             std::shared_ptr<VLog> vlog, size_t wisckey_threshold);
  // 添加一个key-value对
  void add(const std::string &key, const std::string &value, uint64_t tranc_id);
  // 完成当前block的构建, 即将block写入data, 并创建新的block
  void finish_block();
  // 构建sst, 将sst写入文件并返回SST描述类
  std::shared_ptr<SST> build(size_t sst_id, const std::string &path,
                             std::shared_ptr<BlockCache> block_cache);
};
```

**构建流程**

1. 当`MemTable`的大小超过阈值后，准备将`MemTable`中最旧的`Frozen Table`刷出为`SST`。
2. 先创建一个`SSTBuilder`, 按照迭代器的顺序遍历`Frozen Table`，将`key-value`对添加到`SSTBuilder`中:
   1. `SSTBuilder`会有一个当前的`block`, 其`add`函数首先会调用`Block::add_entry`将迭代器的`kv`对插入
   2. 如果当前的`block`容量超出阈值`block_size`, 就调用`finish_block`将其编码到`data`, 并清楚当前`block`相关数据, 开启下一个`block`的构建
   3. 遍历完成迭代器的所有`kv`对的插入后, 调用`build`将所有的数据刷到文件系统, 并返回一个`SST`描述类

**读取流程**

1. `SST`构造函数会绑定一个文件描述符(这里是我自定义封装的文件读取类`FileObj file`)
2. `SST`中的`meta entries`从第一次读取后就常驻内存(第一次读取可以是构造函数, 也可以是第一次`get`)
3. 上层调用`get`时, 会从元数据`meta_entries`中进行二分查找, 找到对应的`block`的偏移量, 然后调用文件描述对象`file`从磁盘中读取
4. 读取后的字节流交由`Block::decode`解码得到内存中的`Block`
5. 内存中的`Block`调用之前实现的查询函数完成二分查询

# 2 代码实现
## 2.1 SSTBuilder::add 函数
> Hint: 建议你先看完下一个`finish_block`函数的描述后再开始写代码, 因为这个函数中需要使用`finish_block`函数

`SSTBuilder`中的`block`成员变量即为当前正在构建的`Block`, `add`函数不断接受上部组件传递的键值对, 并将键值对添加到当前正在构建的`Block`中, 当`Block`容量达到阈值时, 将`Block`写入`data`数组, 并创建一个新的`Block`继续构建。
构建结束后，这个`data`数组就包含了多个`Block`的编码字节, 经进一步处理后即可刷盘形成`SST`:
```cpp
void SSTBuilder::add(const std::string &key, const std::string &value,
                     uint64_t tranc_id) {
  // TODO: Lab 3.5 添加键值对
}
```

> 这里的一些阈值参数你同样可以采取`TomlConfig::getInstance().getxxx()`的方法获取配置文件`config.toml`中定义的常量


## 2.2 SSTBuilder::finish_block 函数
根据前文介绍可知, `SSTBuilder`只有一个活跃的`block`支持插入键值对进行构建, 超出阈值后其将会编码为`Block`并写入`data`数组, 这个过程就是`SSTBuilder::finish_block`函数的功能:
```cpp
void SSTBuilder::finish_block() {
  // TODO: Lab 3.5 构建块
  // ? 当 add 函数发现当前的`block`容量超出阈值时，需要将其编码到`data`，并清空`block`
}
```

## 2.3 SSTBuilder::build 函数
当上层组件已经将所有键值对插入到`SSTBuilder`中后，调用`SSTBuilder::build`函数即可完成`SST`文件的构建, 其会返回一个`SST`指针:
```cpp
std::shared_ptr<SST>
SSTBuilder::build(size_t sst_id, const std::string &path,
                  std::shared_ptr<BlockCache> block_cache) {
  // TODO 3.5 构建一个SST
  return nullptr;
}
```

参数列表中, `sst_id`表示`SST`的编号， `path`表示`SST`文件的存储路径， `block_cache`表示`Block`的缓存池。你相比也意识到, 当`SST`从内存持久化为文件后, 其`IO`必然收到缓存池的管理, 这也是我们之后的内容, 这里你也不需要考虑缓存池的指针, 当它为`nullptr`即可

> 这里涉及到文件IO的操作, 作者已经在`include/utils`中封装了一个文件IO管理类`FileObj`, 你需要阅读`include/utils/files.h`即`src/utils/files.cpp`来了解其使用方法

# 3 测试
到目前位置, 我们只是实现了`SST`的构建工具类`SSTBuilder`, 但由于我们很没有实现`SST`的查询功能, 所以现在我们还无法通过查询接口验证我们`SST`构建的正确性, 因此单元测试需要完成后续`SST`相关`Lab`才能实现。