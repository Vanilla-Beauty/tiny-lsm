# Lab 1.1 跳表的 CRUD
# 1 准备工作
本`Lab`中, 你需要修改的代码文件为
- `src/skiplist/skipList.cpp`
- `include/skiplist/skiplist.h` (optional)

这里首先简单介绍本`Lab`已有的`SkipList`头文件定义:
```cpp
// include/skiplist/skiplist.h
struct SkipListNode {
  std::string key_;   // 节点存储的键
  std::string value_; // 节点存储的值
  uint64_t tranc_id_; // 事务 id, 目前可以忽略
  std::vector<std::shared_ptr<SkipListNode>>
      forward_; // 指向不同层级的下一个节点的指针数组
  std::vector<std::weak_ptr<SkipListNode>>
      backward_; // 指向不同层级的下一个节点的指针数组
  // ...
};
```
这里定义了跳表节点的基础数据, 包括`key`, `value`和目前可以忽略的事务`tranc_id_`

这里定义的跳表节点使用的是双向链表, `forward_`和`backward_`分别存储了各层链表节点的前向指针和后向指针, 其中为了避免循环引用, 这里结合使用了`weak_ptr`和`shared_ptr`, 详细熟悉现代`C++`的同学对此非常熟悉。

> 这里补充说明一下`weak_ptr`, 它的作用是避免`shared_ptr`循环引用, 即一个节点的`shared_ptr`指针指向另一个节点, 另一个节点的`shared_ptr`指针指向前者, 这样就会造成两个节点的析构都无法进行, 因为在析构时互相持有对方的引用计数, 类似死锁。但`weak_ptr`不参与类似`shared_ptr`的引用计数, 保证了析构的正确进行。但也正因为如此，`weak_ptr`不保证指针的有效性, 需要使用`.lock()`判断该指针是否有效。

然后我们看`SkipList`的头文件定义：
```cpp

class SkipList {
private:
  std::shared_ptr<SkipListNode>
      head;              // 跳表的头节点，不存储实际数据，用于遍历跳表
  int max_level;         // 跳表的最大层级数，限制跳表的高度
  int current_level;     // 跳表当前的实际层级数，动态变化
  size_t size_bytes = 0; // 跳表当前占用的内存大小（字节数），用于跟踪内存使用
  // std::shared_mutex rw_mutex; // ! 目前看起来这个锁是冗余的, 在上层控制即可,
  // 后续考虑是否需要细粒度的锁

  std::uniform_int_distribution<> dis_01;  // 随机层数的辅助生成器
  std::uniform_int_distribution<> dis_level;
  std::mt19937 gen;
};
```

这里我们定义了最大的链表层数`max_level`, 你的实现不能有超过`max_level`的链表数量, `current_level`定义当前链表的层数(注意是层数, 不是索引), 最后介绍最重要的`head`, 其只是个哨兵节点, 并不实际存储键值对。

后面三行的`gen`和`dis_01`和`dis_level`是随机数生成器，你可以使用它们来生成随机数。回想我们之前提到的问题, 你应该如何确定每一次插入节点时最高的连接链表的`Level`呢? 这里你可以利用这些随机生成器来确定这些层数, 当然你也可以选择自己的方式来实现。

# 2 put 的实现
你需要实现下面的`put`函数:
```cpp
// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {})", key, value, tranc_id);

  // TODO: Lab1.1  任务：实现插入或更新键值对
  // ? Hint: 你需要保证不同`Level`的步长从底层到高层逐渐增加
  // ? 你可能需要使用到`random_level`函数以确定层数, 其注释中为你通公路一种思路
}
```
目前, 你可以先忽略`tranc_id`这个参数。

此外，之前提到过，跳表的层数是动态增加的， 因此你实现下面的函数可能对你有帮助：
```cpp
int SkipList::random_level() {
  // TODO: 实现随机生成level函数
  // 通过"抛硬币"的方式随机生成层数：
  // - 每次有50%的概率增加一层
  // - 确保层数分布为：第1层100%，第2层50%，第3层25%，以此类推
  // - 层数范围限制在[1, max_level]之间，避免浪费内存
  return -1;
}
```
这里给出一个提示, 生成的整型值的每一个二级制位只包含0或1, 可以表示为`bool`类型, 因此你可以利用它来判断这个节点的最高层数:
1. `Level 0`底层链表: 一定连接新节点
2. `Level 1`链表: 判断`random_level()`生成整型数的第1位是否为1, 有`50%`的概率连接新节点, 为0则跳出该判断链
3. `Level 2`链表: 判断`random_level()`生成整型数的第2位是否为1, 有`50% * 50% =25%`的概率连接新节点, 为0则跳出该判断链
4. ...

> 当然, 上述只是一个建议的方案, 你可以选择别的实现方案, 并删除`random_level`函数
> 另外, 别忘记了更新`size_bytes`这个统计信息, 如果你不知道这个统计信息的运作规则, 你可以查看单元测试`SkipListTest.MemorySizeTracking`

# 3 remove 的实现
虽然我们的`LSM Tree`是以仅追加写入的方式使用我们的`SkipList`, 但为了这个数据结构的完整性, 也是一次手搓底层跳表的体验, 你需要实现正儿八经的`remove`函数:
```cpp
// 删除键值对
// ! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除,
// ! 这里只是为了实现完整的 SkipList 不会真正被上层调用
void SkipList::remove(const std::string &key) {
  // TODO: Lab1.1 任务：实现删除键值对
}
```
`remove`函数的实现的第一步是查询到指定的节点位置, 因此你可以尝试先实现`get`函数。

# 4 get 实现
接下来实现`get`函数:
```cpp
// 查找键值对
SkipListIterator SkipList::get(const std::string &key, uint64_t tranc_id) {
  // spdlog::trace("SkipList--get({}) called", key);
  // ? 你可以参照上面的注释完成日志输出以便于调试
  // ? 日志为输出到你执行二进制所在目录下的log文件夹

  // TODO: Lab1.1 任务：实现查找键值对,
  // TODO: 并且你后续需要额外实现SkipListIterator中的TODO部分(Lab1.2)
  return SkipListIterator{};
}
```

可以看到, 我们的`get`返回的是一个`SkipListIterator`, 它是一个迭代器, 可以用来遍历`SkipList`中的元素。这一部分涉及`SkipList Iterator`的实现不会在文档中进行展开, 你需要阅读源码, 这也是一项基本能力(这部分代码很简单, 别怕:smile:)

> 这里的迭代器调用构造函数就可以了, 目前的`SkipListIterator`实现了一部分, 关于自增等运算符重载, 你将在后续的任务中实现。

# 4 测试
当你完成上述操作后, `test/test_skiplist.cpp`中的部分单元测试你应该能够通过:
```bash
✗ cd tiny-lsm
✗ xmake
✗ xmake run test_skiplist
[==========] Running 12 tests from 1 test suite.
[----------] Global test environment set-up.
[----------] 12 tests from SkipListTest
[ RUN      ] SkipListTest.BasicOperations
[       OK ] SkipListTest.BasicOperations (0 ms)
[ RUN      ] SkipListTest.LargeScaleInsertAndGet
[       OK ] SkipListTest.LargeScaleInsertAndGet (6 ms)
[ RUN      ] SkipListTest.LargeScaleRemove
[       OK ] SkipListTest.LargeScaleRemove (6 ms)
[ RUN      ] SkipListTest.DuplicateInsert
[       OK ] SkipListTest.DuplicateInsert (0 ms)
[ RUN      ] SkipListTest.EmptySkipList
[       OK ] SkipListTest.EmptySkipList (0 ms)
[ RUN      ] SkipListTest.RandomInsertAndRemove
[       OK ] SkipListTest.RandomInsertAndRemove (5 ms)
[ RUN      ] SkipListTest.MemorySizeTracking
[       OK ] SkipListTest.MemorySizeTracking (0 ms)
[ RUN      ] SkipListTest.Iterator
^C
```
到`SkipListTest.Iterator`前的单元测试你应该都能够通过, 卡在`SkipListTest.Iterator`是因为我们很没有实现迭代器相关功能。

恭喜你, 你已经完成了`SkipList`的基础`CRUD`实现。接下来你可以进行[Lab1.2](./lab1.2-Iterator-query.md)了。