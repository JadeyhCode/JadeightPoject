# JadeightPoject

Jadeight 生态（虚拟机 + 汇编层 + 编译器 + 启动器）的文档与启动器工程。

- **启动器**：`./run.sh`（Linux）或 `./build.sh && ./JadeightRunner`（单 exe），
  自动寻找 Jadeight 虚拟机并运行当前目录 `byteCode/*.bc`。详见
  [docs/05-JadeightPoject启动器.md](docs/05-JadeightPoject启动器.md)。

## 完整文档（docs/）

| 文档 | 内容 |
|---|---|
| [01-总体架构.md](docs/01-总体架构.md) | 四个工程的角色与数据流总览 |
| [02-Jadeight2虚拟机.md](docs/02-Jadeight2虚拟机.md) | 虚拟机：指令集、内存模型、调用约定、.bc 格式 |
| [03-汇编器与字节码格式.md](docs/03-汇编器与字节码格式.md) | 汇编层：jadeight_asm API、jasm 语法、.bc 布局 |
| [04-j8c编译器.md](docs/04-j8c编译器.md) | 编译器：语言参考、优化器、代码生成、测试 |
| [05-JadeightPoject启动器.md](docs/05-JadeightPoject启动器.md) | 启动器：使用、VM 自动寻找、快速上手 |

数据流：`.j8` →（j8c）→ `.jasm` →（jadeight_asm）→ `.bc` →（Jadeight2 VM）→ 输出。
