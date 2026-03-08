# Tiny-LSM Lab
![logo](./logo/logo1-compact.jpg)

----


# 0 Tiny-LSM是什么?
[Tiny-LSM](https://github.com/Vanilla-Beauty/tiny-lsm)是一个基于`LSM Tree`的开源教学`KV`存储引擎, 除`LSM Tree`的基础功能外, 还支持`MVCC`、`WAL`、崩溃恢复、`Redis`兼容等功能。本实验是基于作者原本实验的代码进行改造后的`Lab`课程。

`LSM Tree`（`Log-Structured Merge-Tree`）是一种适用于磁盘存储的数据结构，特别适合于需要高吞吐量的写操作的场景。它由Patrick O'Neil等人于1996年提出，广泛应用于NoSQL数据库和文件系统中，如`LevelDB`、`RocksDB`和`Cassandra`等。`LSM Tree`的主要思想是将数据写入操作日志（Log），然后定期将日志中的数据合并到磁盘上的有序不可变文件（SSTable）中。这些SSTable文件按层次结构组织，数据在多个层次之间逐步合并和压缩，以减少读取时的查找次数和磁盘I/O操作。

有关`LSM Tree`的进一步背景和介绍请参见[LSM Tree 概览](lab0-background.md)

本实现项目`Tiny-LSM`完成了包括`内存表（MemTable）`、`不可变表（SSTable）`、`布隆过滤器（Bloom Filter）`、`合并和压缩（Compaction）`等`LSM Tree`的核心组件，并在此基础上添加了额外的功能博客, 包括:
- 实现了`ACID`事务
- 实现了`MVCC`多版本并发控制
- 实现了`WAL`日志和崩溃恢复
- 基于`KV`存储实现了`Redis`的`Resp`协议兼容层
- 基于`Resp`协议兼容层实现了`redis-server`服务

### ⭐ 请支持我们的项目！
> 如果您觉得本`Lab`不错, 请为[Tiny-LSM](https://github.com/Vanilla-Beauty/tiny-lsm)点一个⭐。项目实验制作耗费了我很大精力，作者非常需要您的鼓励❤️, 您的支持是我更新的动力😆

# 1 本实验的目的是什么?
本实验的最终目标是实现一个基于`LSM Tree`的单机`KV Store`引擎。其功能包括:
1. 基本的`KV`存储功能，包括`put`、`get`、`remove`等。
2. 持久化功能，构建的存储引擎的数据将持久化到磁盘中。
3. 事务功能，构建的存储引擎将支持`ACID`等基本事务特性
4. `MVCC`, 构建的存储引擎将支持`MVCC`对数据进行查询。
5. `WAL`与崩溃恢复, 数据写入前会先预写到`WAL`日志以支持崩溃恢复
6. `Redis`兼容, 本实验将实现`Redis`的`Resp`兼容层, 作为`Redis`后端。

# 2 本实验适合哪些人？
通过本实验，你可以学习到`LSM Tree`这一工业界广泛使用的`KV`存储架构, 适合数据库、存储领域的入门学习者。同时本实验包含了`Redis`的`Resp`协议兼容层、网络服务器搭建等内容，也适合后端开发的求职者。同时，本项目使用`C++ 17`特性, 使用`Xmake`作为构建工具，并具备完善的单元测试，也适合想通过项目进一步学习现代`C++`的同学。

# 3 本实验的前置知识？
本项目的知识包括：
1. **(必备)**: 到`C++17`为止的常见`C++`新特性，（项目的配置文件指定的标准为`C++20`, 但其只在单元测试中使用, 项目核心代码只要求`C++17`即可）
2. **(必备)**：常见的数据结构与算法知识
3. **(建议)**: 数据库的基本知识，包括事务特性、`MVCC`的基本概念
4. **(建议)**: `Linux`系统编程知识，本实验使用了系统底层的`mmap`等IO相关的系统调用
5. **(可选)**：`Xmake`的使用, 本实验的构建工具为[`Xmake`](https://xmake.io/#/zh-cn/), 若你想自定义单元测试或引入别的库, 需要手动在`Xmake`中配置。
6. **(可选)**: `Redis`基本知识, 本实验将利用`kv`存储接口实现`Redis`后端, 熟悉`Redis`有助于实验的理解。
7. **(可选)**: 单元测试框架`gtest`的使用, 如果你想自定义单元测试, 需要自行改配置。

# 4 实验流程
本实验的组织类似[`CMU15445 bustub`](https://github.com/cmu-db/bustub), `Lab`提供了整体的代码框架, 并将其中某些组件的关键函数挖空并标记为`TODO`, 参与`Lab`的同学需要按照每一个`Lab`的指南补全缺失的函数, 并通过对应的单元测试。

同样类似`CMU 15445`, 后面的`Lab`依赖于前一个`Lab`的正确性, 而实验提供的单元测试知识尽量考虑到了各种边界情况, 但不能完全确保你的代码正确, 因此必要时, 你需要自行进行单元测试补充以及`debug`。

> 本实验代码位于仓库 [Tiny-LSM](https://github.com/Vanilla-Beauty/tiny-lsm) 的 **`lab2` 分支**（`lab` 分支为旧版本），对应的文档位于 **`lab-doc-2` 分支**（`lab-doc` 分支为旧版本）。你确实可以从原仓库的 `complete` 分支直接查看`Lab`实验的答案, 但作者不希望你如此做, 这样你将无法深刻理解实验设计的思路和相关知识。并且, 作者自己实现的代码中的崩溃恢复部分存在`bug`, 且其余部分并非最佳方案。（猜猜作者是不是故意的😏）

在了解完这些以后, 你可以开启下一章[Lab 0 环境准备](./lab0-env.md)的学习。

# 5 项目交流与讨论
如果你对本`Lab`有疑问, 欢迎在[GitHub Issues](https://github.com/Vanilla-Beauty/tiny-lsm/issues)中提出问题。也欢迎加入次实验的[QQ讨论群](https://qm.qq.com/q/wDZQfaNNw6) 。如果你想参与`Lab`的开发, 欢迎通过QQ群或者作者邮件: [📧邮件](mailto:807077266@qq.com)  联系。

# 6 推荐课程
这里给出作者推荐的相关开源课程, 供你参考学习:
1. [mini-lsm](https://github.com/skyzh/mini-lsm) 迟先生用`Rust`实现的`LSM Tree`课程, 其代码质量非常高, 本课程的很多设计也参考了`Mini-LSM`, 适合想用`Rust`实现`LSM Tree`的同学学习。
2. [TinyCoroLab](https://sakurs2.gitbook.io/tinycorolab-docs/), 基于`io_uring`的现代`C++`协程的开源课程, 适合学习协程及的各种现代C++新特性。
3. [CMU 15445](https://15445.courses.cs.cmu.edu/fall2024/) 经典CMU的数据库课程, 不解释了。
4. [MIT 6.824](https://pdos.csail.mit.edu/6.824/) MIT的经典分布式课程。
5. [Tiny-KV](https://github.com/talent-plan/tinykv) PingCap出品的分布式KV数据库教程, 算是MIT 6.824的升级版。

# 7 贡献者
非常感谢本项目贡献过源码的网友:

<a href="https://github.com/Vanilla-Beauty/tiny-lsm/contributors">
  <img src="https://contrib.rocks/image?repo=Vanilla-Beauty/tiny-lsm" />
</a>
  
如果你在阅读本教程文档中发现错误, 可以直接点击文档右上角的![edit](./images/intro/edit.png), 其会引导你进入对应分支对当前的`md`文件进行编辑和修改, 然后提交`PR`即可。
