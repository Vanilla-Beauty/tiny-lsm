# 阶段2-SST
在完成了`Block`组件和`BlockIterator`组件之后, 我们就可以开始实现`SST`组件了。

`SST`组件的核心工作就是管理器内部的多个`Block`组件, 利用`Block`组件和`BlockIterator`的各种`CRUD`和初始化、构建等基本接口，来对外提供各类功能。

> 提示: 强烈建议你自己创建一个分组实现`Lab`的内容, 并在每次新的`Lab`开始时进行如下同步操作:
> ```bash
> git pull origin lab2
> git checkout your_branch
> git merge lab2
> ```
> 如果你发现项目仓库的代码没有指导书中的 TODO 标记的话, 证明你需要运行上述命令更新代码了