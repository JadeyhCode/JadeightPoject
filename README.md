# JadeightPoject

Jadeight 生态（虚拟机 + 汇编层 + 编译器 + 启动器）的文档与启动器工程。

- **启动器**：`./run.sh`（Linux）或 `./build.sh && ./JadeightRunner`（单 exe），
  自动寻找 Jadeight 虚拟机、运行当前目录 `byteCode/*.bc`、动态链接 `lib/*.so`（.dylib/.dll）。
  详见 [docs/05-JadeightPoject启动器.md](docs/05-JadeightPoject启动器.md)。

## 完整文档（docs/）

概览：

| 文档 | 内容 |
|---|---|
| [01-总体架构.md](docs/01-总体架构.md) | 四个工程的角色与数据流总览 |
| [02-Jadeight2虚拟机.md](docs/02-Jadeight2虚拟机.md) | 虚拟机：指令集、内存模型、调用约定、.bc 格式 |
| [03-汇编器与字节码格式.md](docs/03-汇编器与字节码格式.md) | 汇编层：jadeight_asm API、jasm 语法、.bc 布局 |
| [04-j8c编译器.md](docs/04-j8c编译器.md) | 编译器：语言要点、优化器、代码生成、测试 |
| [05-JadeightPoject启动器.md](docs/05-JadeightPoject启动器.md) | 启动器：使用、VM 自动寻找、快速上手 |

深度参考：

| 文档 | 内容 |
|---|---|
| [06-指令集参考.md](docs/06-指令集参考.md) | 全部 174 条 opcode：编号、编码、jasm 写法、语义 |
| [07-语言参考.md](docs/07-语言参考.md) | .j8 语言完整手册：词法、类型、语句、函数、协议、泛型、ECS |
| [08-VM机制与字节码格式.md](docs/08-VM机制与字节码格式.md) | .bc 逐字节格式、内存模型、执行循环、调用约定、JIT、多线程 |
| [09-工具链与FFI指南.md](docs/09-工具链与FFI指南.md) | j8c/j8run/jasm 全部参数、extern/FFI 完整指南 |

数据流：`.j8` →（j8c）→ `.jasm` →（jadeight_asm）→ `.bc` →（Jadeight2 VM）→ 输出。
