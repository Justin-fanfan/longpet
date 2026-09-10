# First-party components

本目录保存由 LongPet 团队直接维护、但可相对独立运行的第一方组件。

- `longpet-kws/`：Python 关键词唤醒 runtime、入口、依赖和必要模型资源。

组件可以有自己的运行入口与依赖，但仍接受 LongPet 仓库的代码审查、文档和发布约束。外部上游
来源或模型资产必须在组件 README 中记录版本、哈希与许可证状态；不因存在上游来源就把整个
第一方维护组件放回 `third_party/`。
