# 阶段1-Engine 基础功能
在这一阶段中, 你将实现最基本的`LSM Tree`存储引擎的`CRUD`接口, 在之后的阶段二中你将实现性能优化部分的内容。

阶段一的内容包括：
- 数据的写入 && `SST文件`的构造
- `SST文件`的加载 && 数据查询

> 提示: 强烈建议你自己创建一个分组实现`Lab`的内容, 并在每次新的`Lab`开始时进行如下同步操作:
> ```bash
> git pull origin lab2
> git checkout your_branch
> git merge lab2
> ```
> 如果你发现项目仓库的代码没有指导书中的 TODO 标记的话, 证明你需要运行上述命令更新代码了