本实验目前处于持续迭代中，已完成的更新和后续计划如下：

## 已更新（相较于 1.0 版本）

1. **接口同步**：将所有实验文档中的函数签名与 `master` 分支最新代码对齐，包括：
   - `MemTable::flush_last` 新增 `flushed_tranc_ids` 输出参数
   - `Block::encode` / `Block::decode` 统一 `with_hash` 参数
   - `Block::get_tranc_id_at` 返回类型由 `uint16_t` 修正为 `uint64_t`
   - `SST::open` 新增 `vlog` 参数（默认 `nullptr`，向后兼容）
   - `SST::read_block` 参数类型改为 `int64_t`（支持返回 -1）
   - `SST::begin` 新增 `keep_all_versions` 参数
   - `WAL` 构造函数参数 `max_finished_tranc_id` 重命名为 `checkpoint_tranc_id`
2. **WAL 崩溃恢复机制更新**：用 `flushed_tranc_ids`（`std::set<uint64_t>`）替代单一最大值，更精确地追踪哪些事务已完全持久化，避免"部分刷盘"导致的数据缺失。详见 [Lab 5.5](./lab5/lab5.5-Recover.md)。
3. **新增 Lab 7 WiscKey 键值分离**：讲解 WiscKey 论文的核心思路、VLog 文件格式、SSTBuilder 和 SST 的 WiscKey 支持、Engine 集成方式。

## 后续更新计划

1. 自由度提升: `1.0`版本的`Lab`, 基本上就是在既有的项目代码下进行关键函数的挖空, 在组件设计层面没有给实验参与者太多的发挥空间, 后续应该添加更多的自由度, 允许实验者自由设计组件, 并且在组件设计中添加更多的测试用例。
2. 测试用例覆盖率不足: `1.0`版本的`Lab`中, 测试用例的覆盖率比较低, 比如对崩溃恢复的各种边界条件的考虑不足, 当然这确实也比较难控制就是了。
3. 各个`Lab`工作量不尽相同, 现在的`Lab`设计是按照功能模块进行划分的, 但这导致[Lab 5](./lab5/lab5-Tranc-MVCC.md)和[Lab 6](./lab6/lab6-Redis.md)的测代码量和难度远大于之前的`Lab`, 难度曲线可能不太合理, 后续应考虑对各个`Lab`进行更加均衡的划分。
4. 进一步补充一些背景理论知识, 尤其是实际场景中的各种性能优化方案。
5. ~~Lab 7 WiscKey 键值分离~~ ✅ 已完成（见 [Lab 7](./lab7/lab7-WiscKey.md)）
6. Lab 7 VLog GC（垃圾回收）章节待补充。

如果你有兴趣参与本实验的建设，欢迎在下面的分支上提交PR:

- [Lab代码分支（lab2，当前版本）](https://github.com/Vanilla-Beauty/tiny-lsm/tree/lab2)
- [Lab文档分支（lab-doc-2，当前版本）](https://github.com/Vanilla-Beauty/tiny-lsm/tree/lab-doc-2)
- [Lab代码分支（lab，旧版本）](https://github.com/Vanilla-Beauty/tiny-lsm/tree/lab)
- [Lab文档分支（lab-doc，旧版本）](https://github.com/Vanilla-Beauty/tiny-lsm/tree/lab-doc)
- [代码开发分支](https://github.com/Vanilla-Beauty/tiny-lsm/tree/master)

如果你有什么问题，可以通过 [QQ讨论群](https://qm.qq.com/q/wDZQfaNNw6) 或者 [📧邮件](mailto:807077266@qq.com) 联系到作者。


