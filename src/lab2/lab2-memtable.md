# Lab 2 MemTabele

本`Lab`中, 你将基于之前实现的`Skiplist`, 将其组织成内存中负责存储键值对的组件`MemTable`。

> 提示: 强烈建议你自己创建一个分组实现`Lab`的内容, 并在每次新的`Lab`开始时进行如下同步操作:
> ```bash
> git pull origin lab2
> git checkout your_branch
> git merge lab2
> ```
> 如果你发现项目仓库的代码没有指导书中的 TODO 标记的话, 证明你需要运行上述命令更新代码了

## 1 MemTable的构造原理

再次回顾我们的整体架构图：

![Fig 1](../images/intro/tiny-lsm-arch.drawio.png)

`MemTable`负责存储内存中的键值对数据，其存储的基础容器即为`Skiplist`。`SkipList`被划分为2组：`current_table`和`frozen_table`。`current_table`可读可写，并是唯一写入的`SkipList`，`frozen_table`是只读状态，用于存储已经写入的键值对数据。`current_table`容量超出阈值即转化为`frozen_table`中的一个。

为什么要如此设计呢? 答案是为了提升并发性, 我们的查询与写入的逻辑如下图所示:

![Fig 2](../images/Lab2/MemTable.drawio.png)

我们在写入时始终只对活跃的`current_table`进行写入，而查询时则同时对`current_table`和`frozen_table`进行查询。这样, 如果我们不将内存表进行划分的话, 查询和写入将同时对一张大的`SkipList`进行操作, 这将导致并发度降低。反之, 我们将`MemTable`划分为`current_table`和`frozen_tables`后, 我们可以在写入`current_table`的同时对`frozen table`进行查询, 大幅度提升了并发量。

## 2 `SkipList`查询的优先级

由之前的理论描述所知, 我们的`frozen_tables`包含多份`Skiplist`, 而`LSM Tree`的写入是追加写入, 后写入的数据会覆盖前面的数据。因此，查询时，我们应该按照从新到旧的顺序查询`frozen_tables`。如同架构图中`Get`的查询路径中`Path 1.1, Path 1.2, Path 1.3, Path 1.4`分别按照从新到旧的顺序查询`Skiplist`, 一旦查询成功即返回。

## 3 思考
> 本实验的惯例是先介绍对应`Lab`的基本原理, 再抛出一些思考题，你可以简单地对思考题给出一个心智层面的解决方案, 然后开启后续的`Lab`。

现在又到了我们的思考环节, 根据之前的描述, 将`SkipList`组织为`MemTable`好像很简单。但是别忘了，我们之前讲述的都是最基本的单点查询和写入，如果是更复杂的情形呢？ 比如：

1. 要查询前缀为`xxx`的所有键值对
   1. 很多个`Skiplist`中都可能存在这样的键值对, 我们需要依次遍历吗?
   2. 有些旧`Skiplist`中的键值对已经被更新的`Skiplist`的数据覆盖了, 是否需要过滤? 如何过滤?
2. 如何为`MemTable`实现迭代器?
   1. `KV`数据库的迭代器肯定是按照`key`的顺序从小大大排布
   2. 多个`Skiplist`的键值对如何进行整合, 从而也实现从小大大的排布?

通过本节对`MemTable`的基本介绍, 以及进阶问题的思考, 你可以开始[Lab ]
