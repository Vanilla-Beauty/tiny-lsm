# Lab 3.6 SST 查询
# 1 概述
首先, 需要声明, 这一小节的实验容量稍大, 你将同时实现`SST`的基础查询功能和迭代器`SstIterator`。

为什么需要这样设计呢？答案是因为，我们对组件的查询设计沿用了`STL`的迭代器风格，查询都是返回迭代器作为结果的。但我们的思路首先应该是实现`SST`本身, 后续再实现迭代器`SstIterator`。但`STL`风格的接口设计导致二者耦合了，因此，这里索性将二者同时实现了。

## 1.1 SST 的定义
同样，我们先看`SST`的定义：
```cpp
class SST : public std::enable_shared_from_this<SST> {
private:
  FileObj file;
  std::vector<BlockMeta> meta_entries;
  uint32_t bloom_offset; // 暂时忽略
  uint32_t meta_block_offset;
  size_t sst_id;
  std::string first_key;
  std::string last_key;
  std::shared_ptr<BloomFilter> bloom_filter; // 暂时忽略
  std::shared_ptr<BlockCache> block_cache; // 暂时忽略
  uint64_t min_tranc_id_ = UINT64_MAX; // 暂时忽略
  uint64_t max_tranc_id_ = 0; // 暂时忽略

  // WiscKey 键值分离字段（后续Lab内容，暂时忽略）
  uint8_t storage_mode_ = 0; // 0=inline, 1=WiscKey
  std::shared_ptr<VLog> vlog_;
  // ...
};
```
`SST`最关键的成员变量是`meta_entries`, 其本质上就是从硬盘中读取了`SST文件`的`Meta Section`部分解析后的`BlockMeta`数组。在接受外部查询请求时, 我们会根据`key`在`meta_entries`中查找对应的`BlockMeta`, 然后从硬盘中读取`Block`并解码得到内存中的`Block`, 最后再调用`Block`的查询接口完成查询。这里的`FileObj file`成员变量就是实现对应的`SST文件`的`IO`操作的类实例。

> 当然, 如果你完成了后续`Lab`, 这里的逻辑存在一些不同:
> 1. 如果后续实现了缓存池, 就可以从缓存池中查询`Block`, 而不是从硬盘中读取。
> 2. 如果后续实现了`BloomFilter`, 那么在查询时, 首先会通过`BloomFilter`判断`key`是否有存在的可能, 如果不可能存在, 则直接返回`nullptr`, 否则继续调用查询接口完成查询。

## 1.2 SstIterator 的定义
然后是`SstIterator`的定义:
```cpp
class SstIterator : public BaseIterator {
  // friend xxx

private:
  std::shared_ptr<SST> m_sst;
  size_t m_block_idx;
  uint64_t max_tranc_id_;
  std::shared_ptr<BlockIterator> m_block_it;
  mutable std::optional<value_type> cached_value; // 缓存当前值
  // ...
};
```
要实现`SST`的迭代器, 需要记录当前的`Block`索引, `以及Block`中的`Entry`索引, 因此也需要原`SST`类的`this`指针, 之前已经介绍过`enable_shared_from_this`了, 不再赘述。

这里使用`m_sst`, `m_block_idx`和`m_block_it`分别记录原始的`SST`类对象、当前`Block`在`SST`中的位置、当前迭代器在`Block`中的位置。`cached_value`仍然用做缓存值, 因为读取键值对涉及文件`IO`操作, 因此这里的`cached_value`就不仅仅是为了实现`->`的辅助成员变量了, 而是正儿八经的优化手段。

本小节`Lab`中, 你需要修改的代码文件:
- 实现`SST`需要修改的文件
  - `src/sst/sst.cpp`
  - `include/sst/sst.h` (Optional)
- 实现`SstIterator`需要修改的文件
  - `src/sst/sst_iterator.cpp`
  - `include/sst/sst_iterator.h` (Optional)

# 2 SST 基础代码实现
## 2.1 打开 SST 文件
你需要实现`SST：：open`函数:
```cpp
// 头文件中将其定义为静态函数
// vlog 参数默认为 nullptr，普通模式下忽略即可；WiscKey 模式下由上层传入（后续Lab内容）
std::shared_ptr<SST> SST::open(size_t sst_id, FileObj file,
                               std::shared_ptr<BlockCache> block_cache,
                               std::shared_ptr<VLog> vlog = nullptr) {
  // TODO Lab 3.6 打开一个SST文件, 返回一个描述类

  return nullptr;
}
```
尽管我们的`SST`对数据的查询是惰性地从文件系统中进行读取, 但必要的元信息需要我们加载到内存中。`SST::open`的工就是将`SST文件`的元信息进行解码和加载，返回一个描述类`SST`， 你可以将`SST`看做是`SST`文件的操作句柄，或者是文件描述符。

> 1. 如果你后续`Lab`实现了布隆过滤器, 那么布隆过滤器的`bit`数组也需要加载到内存中
> 2. `block_cache`是缓存池的指针, 你现在不需要管它是哪里来的, 只需要对类的成员变量进行简单赋值即可


## 2.2 加载 Block
在接受其他组件的查询请求后, `SST`会根据元信息定位请求的`key`可能位于哪一个`Block`(因为`BlockMeta`中存储了首尾的`key`), 接下来就是读取这个`Blcok`, 这就是你需要实现的`read_block`函数:
```cpp
std::shared_ptr<Block> SST::read_block(int64_t block_idx) {
  // TODO: Lab 3.6 根据 block 的 id 读取一个 `Block`
  return nullptr;
}
```
注意参数类型为 `int64_t`（有符号），这是为了与 `find_block_idx` 的返回值（返回 -1 表示未找到）保持一致。

> 实现缓存池后, 你的代码逻辑应该是
> 1. 从缓存池获取`Block`, 如果缓存命中, 直接返回。
> 2. 缓存未命中才从文件系统中读取
> 3. 返回前别忘了更新缓存池

## 2.3 根据 key 查询 Block
```cpp
size_t SST::find_block_idx(const std::string &key) {
  // 先在布隆过滤器判断key是否存在
  // TODO: Lab 3.6 二分查找
  // ? 给定一个 `key`, 返回其所属的 `block` 的索引
  // ? 如果没有找到包含该 `key` 的 Block，返回-1
  return 0;
}
```
`find_block_idx`函数的目的是根据`key`在`meta_entries`中查找对应的`BlockMeta`, 返回`BlockMeta`在`meta_entries`中的索引。如果`key`不存在于`SST`中, 则返回`-1`。

这里由于`Block`的数据是有序的, 因此你需要使用二分查找算法提速, 否者你的查询性能会非常差。


# 3 SstIterator 代码实现
## 3.1 SstIterator 定位函数
你需要实现下面的迭代器定位函数:
```cpp
void SstIterator::seek_first() {
  // TODO: Lab 3.6 将迭代器定位到第一个key
}

void SstIterator::seek(const std::string &key) {
  // TODO: Lab 3.6 将迭代器定位到指定key的位置
}
```
> Hint
> 这里的逻辑也很简单, 就是先使用记录在`sst`中的`meta_entries`找到包含要查找的`key`的`Block`(`find_block_idx`), 从文件中读取这个`Block`(`read_block`), 然后再读取的`Block`中调用获取指定`key`的迭代器的构造函数, 通过`BlockIterator`实现在`Block`中的定位。

`SST`创建迭代器时, 会在构造函数中选择是否偏移到指定的`key`, 你可以查看`SstIterator`的构造函数, 看看他们是如何与不同组件和函数见衔接的。

## 3.2 运算符重载函数
作为迭代器, 我们的惯例就行要实现下面几个运算符重载函数:
```cpp
BaseIterator &SstIterator::operator++() {
  // TODO: Lab 3.6 实现迭代器自增
  return *this;
}

bool SstIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab 3.6 实现迭代器比较
  return false;
}

bool SstIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab 3.6 实现迭代器比较
  return false;
}

SstIterator::value_type SstIterator::operator*() const {
  // TODO: Lab 3.6 实现迭代器解引用
  return {};
}
```

# 4 补全 SST
在实现了`SstIterator`后, 你可以补全以`SST`中以`SstIterator`作为返回值的几个函数:
```cpp
SstIterator SST::get(const std::string &key, uint64_t tranc_id) {
  // TODO: Lab 3.6 根据查询`key`返回一个迭代器
  // ? 如果`key`不存在, 返回一个无效的迭代器即可
  throw std::runtime_error("Not implemented");
}

// keep_all_versions=false 时只保留每个 key 的最新版本（事务可见版本）
// keep_all_versions=true 时用于 compact，保留全部历史版本
SstIterator SST::begin(uint64_t tranc_id, bool keep_all_versions = false) {
  // TODO: Lab 3.6 返回起始位置迭代器
  throw std::runtime_error("Not implemented");
}

SstIterator SST::end() {
  // TODO: Lab 3.6 返回终止位置迭代器
  throw std::runtime_error("Not implemented");
}
```

这几个函数都很简单, 因为具体的定位操作是在`SstIterator`内部完成的(虽然其反过来有调用了`SST`的`find_block_idx`等函数), 因此只需要调用`SstIterator`的构造函数即可。这里作为`Lab`的内容主要是为了让你对不同组件之间的交互有一个认真, 意思到这样一个设计思路: **迭代器是连接不同组件的桥梁**.

# 5 测试
此次测试包含之前[Lab 3.5](./lab3.5-SSTBuilder.md)的实现, 预期的结果是:
```bash
✗ xmake
[100%]: build ok, spent 0.517s
✗ xmake run test_sst  
[==========] Running 8 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 8 tests from SSTTest
[ RUN      ] SSTTest.BasicWriteAndRead
[       OK ] SSTTest.BasicWriteAndRead (3 ms)
[ RUN      ] SSTTest.BlockSplitting
[       OK ] SSTTest.BlockSplitting (1 ms)
[ RUN      ] SSTTest.KeySearch
[       OK ] SSTTest.KeySearch (0 ms)
[ RUN      ] SSTTest.Metadata
[       OK ] SSTTest.Metadata (0 ms)
[ RUN      ] SSTTest.EmptySST
[       OK ] SSTTest.EmptySST (0 ms)
[ RUN      ] SSTTest.ReopenSST
[       OK ] SSTTest.ReopenSST (0 ms)
[ RUN      ] SSTTest.LargeSST
[       OK ] SSTTest.LargeSST (0 ms)
[ RUN      ] SSTTest.LargeSSTPredicate
test/test_sst.cpp:235: Failure
Value of: result.has_value()
  Actual: false
Expected: true

unknown file: Failure
C++ exception with description "bad optional access" thrown in the test body.

[  FAILED  ] SSTTest.LargeSSTPredicate (1 ms)
[----------] 8 tests from SSTTest (8 ms total)

[----------] Global test environment tear-down
[==========] 8 tests from 1 test suite ran. (8 ms total)
[  PASSED  ] 7 tests.
[  FAILED  ] 1 test, listed below:
[  FAILED  ] SSTTest.LargeSSTPredicate

 1 FAILED TEST
error: execv(/home/vanilla-beauty/proj/tiny-lsm/build/linux/x86_64/release/test_sst ) failed(1)
```

如果仅仅是得到一个可以跑的`SST`, 那么现在你已经完成的`SST`的大部分功能了。这里的`LargeSSTPredicate`需要你在实现下一小节的谓词查询后才能通过。