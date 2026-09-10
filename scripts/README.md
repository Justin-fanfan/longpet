# Repository scripts

本目录保存构建、板端启动和开发辅助脚本：

- `build-loongarch.sh`：使用现有 Buildroot SDK 交叉构建；
- `run-board.sh`：板端启动 LongPet runtime；
- `run-vision-benchmark.sh`：运行已部署的 Vision benchmark；
- `vision/`：数据准备、Tinyissimo 训练/验证、上游准备和 FastestDet baseline 准备。

脚本不把本机用户名、训练数据或 `ai-work` 位置写成程序默认值。路径通过参数或环境变量传入；
命令示例使用 `D:\ai-work\longpet-vision`，但该目录不是程序必须路径。详见
[`vision/README.md`](vision/README.md)。
