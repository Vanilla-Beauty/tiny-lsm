# 阶段2-迭代器 && Compact
## 子任务1-迭代器
之前你已经完成了具备初步的简单`CURD`操作的`LSM Tree`存储引擎, 但你应该发现了, 诸如范围查询、全局迭代器等功能，我们并没有放在 [第一阶段](./Engine-Base.md), 这是因为以上接口都需要不同组件之间(不同`Level`的`SST`和`MemTable`)进行交互, 而这些组件之间的交互需要我们实现更高级的迭代器才可以完成, 因此这一小节你将实现各种迭代器。

你需要实现的迭代器包括：
- `TwoMergeIterator`
  - 其主要用于整合2个迭代器, 按照不同的优先级顺序对迭代器进行遍历
  - 这里的两个迭代器通常情况下就是内存`MemTable`的迭代器和`SST`部分的迭代器, 前者优先级更高
- `ConcactIterator`
  - 用于连接某一层`Level`的多个`SST`
  - 此处默认多个`SST`是不重叠且排序的, 因此使用于`Level > 0`的`SST`的连接
- `LevelIterator`
  - 类似`TwoMergeIterator`, 但整合的迭代器数量更多
  - 例如, 每个`Level`都有一个层间的迭代器

> 提示: 强烈建议你自己创建一个分组实现`Lab`的内容, 并在每次新的`Lab`开始时进行如下同步操作:
> ```bash
> git pull origin lab2
> git checkout your_branch
> git merge lab2
> ```
> 如果你发现项目仓库的代码没有指导书中的 TODO 标记的话, 证明你需要运行上述命令更新代码了

**思考**
为什么我们要在实现`Compact`之前先实现这些迭代器?

## 子任务2-Compact
这里的`Compact`操作需要我们的迭代器作为信息交互的接口, 具体原因请参见[Lab 4.3 ConcactIterator
](./lab4.3-ConcactIterator.md)

