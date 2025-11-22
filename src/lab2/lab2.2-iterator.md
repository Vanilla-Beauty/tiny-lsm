# Lab 2.2 迭代器
# 1 迭代器的作用
我们需要实现整个`MemTable`的迭代器, 这算是本`Lab`的一个难点, 因为新的`SkipList`中的元素会导致旧的`SkipList`的部分元素失效, 因此不能简单地将不同`SkipList`的遍历结果拼接起来就完事儿。

试想下面这个场景：
```bash
SkipList0: ("k1", "v1") -> ("k4", "") -> ("k5", "v3")
SkipList1: ("k2", "v2") -> ("k3", "v3") -> ("k4", "v4")
```
`SkipList0`中的`("k4", "")`表示删除了`"k4"`, 因此如果我们先消耗了`SkipList0`的迭代器, 那么`SkipList1`中就无法获取`"k4"`的不合法性, 因此需要对不同`SkipList`的迭代器进行`merge`操作来删除这些无效的元素。

本实验的建议方案是:
可以维护一个堆，堆首先根据`key`排序, 然后根据`SkipList id`排序, 因此相同的`key`, 后插入的记录肯定更靠近堆顶(因为`SkipList id`越小表示其越新), 因此堆顶的某个`key`一定是整个`MemTable`中该`key`的最新记录, 迭代器对该`key`只需要堆顶的这一个元素, 其余在取出堆顶后即可全部移除(因为首先按照`key`排序, 所以他们一定连续出现在堆顶)

# 2 堆去重的原理
我们假设有以下两个 `SkipList：`
```text
SkipList0 (id=0): ("k1", "v1") -> ("k4", "") -> ("k5", "v3")   // 最新的
SkipList1 (id=1): ("k2", "v2") -> ("k3", "v3") -> ("k4", "v4")
```
我们借助之前`Skiplist`的迭代器, 遍历各个`Skiplist`, 把所有键值对按 `key` 排序后放入堆，排序依据是：
- 先按 `key` 升序，即`key`越小越靠近堆顶
- 对于相同的 `key`，选择`tranc_id` 较大（越新）的优先（越靠近堆顶）(这一条你可暂时哦忽略, 测试中`tranc_id`都是0)
- 按照`SkipList`来源的新旧排序, 新`SkipList`的键值对更靠近堆顶(这里的`id`是构建堆时手动赋予的)

> 最后的排序依据中, 这个新旧的顺序需要你手动指定, 无论你实现的排序是更大的`id`表示更新的`SKiplist`还是更小的`id`表示更新的`SKiplist`, 自身逻辑自洽即可
> 建议将更新的`Skiplist`用更大的`id`标识, 因为`id`随新的`Skiplist`增长是很正常的事情

遍历迭代器, 并逐一构建堆元素插入堆中, 最终的堆的示意图为:

<div align="center">
  <img src="../images/Lab2/Heap.png" width="400">
</div>

这里可以看到, `k4`的键值对中, 更新的跳表中的键值对更靠近堆顶, 当我们遍历这个堆并不断弹出元素时, 相同`key`的元素只能被迭代器(这个堆肯定是迭代器封装的一个成员变量)对外暴露一次, 其余相同`key`的键值对进行丢弃即可, 这样我们就能利用类似堆排序的功能, 同时完成了排序和去重。

# 3 基于堆的迭代器实现
## 3.1 代码框架介绍
因此，本实验你首先要基于上述原理的介绍实现一个基于堆的迭代器。

本小节需要你修改的代码：
- `src/iterator/iterator.cpp`
- `include/iterator/iterator.h` (Optional)
- `src/memtable/memtable.cpp`
- `include/memtable/memtable.h` (Optional)

首先是`SearchItem`, 我们来看定义:
```cpp
// *************************** SearchItem ***************************
struct SearchItem {
  std::string key_;
  std::string value_;
  uint64_t tranc_id_;
  int idx_;
  int level_; // 来自sst的level

  SearchItem() = default;
  SearchItem(std::string k, std::string v, int i, int l, uint64_t tranc_id)
      : key_(std::move(k)), value_(std::move(v)), idx_(i), level_(l),
        tranc_id_(tranc_id) {}
};

bool operator<(const SearchItem &a, const SearchItem &b);
bool operator>(const SearchItem &a, const SearchItem &b);
bool operator==(const SearchItem &a, const SearchItem &b);
```
其就是我们之前提到的每个堆节点的数据结构, 这里的构造函数中, `k, v, i`即为`key`, `value`, `id`(跳表的), `l`表示来源的层级, 现在我们都是在内存操作, 设为`0`即可, `tranc_id`不需要你理解, 直接赋值即可。

然后是你要实现的迭代器`HeapIterator`的定义:
```cpp
class HeapIterator : public BaseIterator {
public:
  HeapIterator() = default;
  HeapIterator(std::vector<SearchItem> item_vec, uint64_t max_tranc_id);

  // ...

private:
  std::priority_queue<SearchItem, std::vector<SearchItem>,
                      std::greater<SearchItem>>
      items;
  mutable std::shared_ptr<value_type> current; // 用于存储当前元素
  uint64_t max_tranc_id_ = 0;
};
```
这里的`priority_queue`就是我们之前提到的堆, `C++`的堆实际上就是优先队列。

> 在`C++`中，当使用` std::priority_queue` 来实现小根堆（min-heap）时，你需要使用` std::greater<SearchItem>` 作为比较函数对象。感兴趣的同学可以查一查为什么要这么设计。

## 3.2 实现 `SearchItem` 的比较规则
```cpp
bool operator<(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  return true;
}

bool operator>(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  return true;
}

bool operator==(const SearchItem &a, const SearchItem &b) {
  // TODO: Lab2.2 实现比较规则
  return true;
}
```

这里你需要按照之前介绍的比较规则进行代码补全。

## 3.3 实现构造函数
接下来你需要实现`HeapIterator`的构造函数, 其参数就是已经遍历了所有`Skiplist`的迭代器构造的`vector`, `max_tranc_id`你可以暂时忽略:
```cpp
HeapIterator::HeapIterator(bool skip_delete)
    : max_tranc_id_(0), skip_delete_(skip_delete) {
  // TODO: Lab2.2 实现 HeapIterator 构造函数
}
HeapIterator::HeapIterator(std::vector<SearchItem> item_vec,
                           uint64_t max_tranc_id, bool skip_delete)
    : max_tranc_id_(max_tranc_id), skip_delete_(skip_delete) {
  // TODO: Lab2.2 实现 HeapIterator 构造函数
}
```

> Hint 1: 构造完堆后, 是否需要额外的一些初始化的滤除?
> Hint 2: （重要！！！）这里说明下`skip_delete`参数，这个参数用于控制迭代器是否删除`value`为空的键值对。`value`为空在逻辑上表示删除标记，这样的键值对是否保留取决于具体场景。当作为借口暴露给外部时，我们应该跳过删除的键值对；但用于数据刷盘时，删除标记是有效的存储记录，是不能被跳过的。

## 3.4 实现自增函数
接下来自增函数是最重要的, 自增函数的逻辑是:
1. 自增后的`key`不能是之前相同的`key`, 如果是(以为着实际上被前者覆写了), 则跳过
2. 自增后的键值对不能是删除标记, 即`value`为空

```cpp
BaseIterator &HeapIterator::operator++() {
  // TODO: Lab2.2 实现 ++ 重载
  return *this;
}
```

同时, 这些辅助函数的实现有助于你完成`:operator++()`和之前的构造函数:
```cpp
bool HeapIterator::top_value_legal() const {
  // TODO: Lab2.2 判断顶部元素是否合法
  // ? 被删除的值是不合法
  // ? 不允许访问的事务创建或更改的键值对不合法(暂时忽略)
  return true;
}

void HeapIterator::skip_by_tranc_id() {
  // TODO: Lab2.2 后续的Lab实现, 只是作为标记提醒
}
```

## 3.4 其他运算符重载函数
其他运算符重载函数就简单了很多, 但仍然是对你代码理解的考验:
```cpp
HeapIterator::pointer HeapIterator::operator->() const {
  // TODO: Lab2.2 实现 -> 重载
  return nullptr;
}

HeapIterator::value_type HeapIterator::operator*() const {
  // TODO: Lab2.2 实现 * 重载
  return {};
}

BaseIterator &HeapIterator::operator++() {
  // TODO: Lab2.2 实现 ++ 重载
  return *this;
}

bool HeapIterator::operator==(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 == 重载
  return true;
}

bool HeapIterator::operator!=(const BaseIterator &other) const {
  // TODO: Lab2.2 实现 != 重载
  return true;
}
```

其中`->`运算符重载, 你可以直接利用已有的成员变量`mutable std::shared_ptr<value_type> current`, 返回器地址, 但你需要在构造函数和自增函数中对其进行正确的初始化和重置, 下面这个函数即为初始化和重置的逻辑实现:
```cpp
void HeapIterator::update_current() const {
  // current 缓存了当前键值对的值, 你实现 -> 重载时可能需要
  // TODO: Lab2.2 更新当前缓存值
}
```

# 4 MemTable的迭代器
接下来, 有了`HeapIterator`, 你可以实现`MemTable`组件的全局迭代器了:
```cpp
HeapIterator MemTable::begin(uint64_t tranc_id) {
  // TODO Lab 2.2 MemTable 的迭代器
  return {};
}

HeapIterator MemTable::end() {
  // TODO Lab 2.2 MemTable 的迭代器
  return HeapIterator{};
}
```

这里的逻辑就是利用之前实现的`HeapIterator`对整个`MemTable`进行遍历。

# 5 测试
当你完成上述所有功能后, 你可以通过如下测试:
```bash
✗ xmake
✗ xmake run test_memtable
[==========] Running 9 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 9 tests from MemTableTest
[ RUN      ] MemTableTest.BasicOperations
[       OK ] MemTableTest.BasicOperations (0 ms)
[ RUN      ] MemTableTest.RemoveOperations
[       OK ] MemTableTest.RemoveOperations (0 ms)
[ RUN      ] MemTableTest.FrozenTableOperations
[       OK ] MemTableTest.FrozenTableOperations (0 ms)
[ RUN      ] MemTableTest.LargeScaleOperations
[       OK ] MemTableTest.LargeScaleOperations (0 ms)
[ RUN      ] MemTableTest.MemorySizeTracking
[       OK ] MemTableTest.MemorySizeTracking (0 ms)
[ RUN      ] MemTableTest.MultipleFrozenTables
[       OK ] MemTableTest.MultipleFrozenTables (0 ms)
[ RUN      ] MemTableTest.IteratorComplexOperations
[       OK ] MemTableTest.IteratorComplexOperations (0 ms)
[ RUN      ] MemTableTest.ConcurrentOperations
[       OK ] MemTableTest.ConcurrentOperations (601 ms)
[ RUN      ] MemTableTest.PreffixIter
[       OK ] MemTableTest.PreffixIter (0 ms)
[----------] 9 tests from MemTableTest (602 ms total)

[----------] Global test environment tear-down
[==========] 9 tests from 1 test suite ran. (602 ms total)
[  PASSED  ] 9 tests.
```

接下来你可以开启下一小节的[Lab](./lab2.3-range-query.md)

