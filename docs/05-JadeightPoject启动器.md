# JadeightPoject 启动器

自动寻找 Jadeight 虚拟机，运行当前目录 `byteCode/` 下的所有 `.bc` 字节码。
本工程是 Jadeight 生态的「最后一公里」：编译产物（.bc）由它一键跑起来。

## 文件

| 文件 | 说明 |
|---|---|
| `launcher.cpp` | 跨平台启动器源码（宏适配 Mac / Linux / Windows），编译为单个 exe |
| `build.sh` | 编译 `launcher.cpp` → 单可执行文件 `JadeightRunner`（零依赖） |
| `run.sh` | Linux 启动脚本（与 launcher 行为一致） |
| `byteCode/` | 字节码目录（示例含 t1_basic.bc、t3_functions.bc） |

## 使用

```bash
# Linux（直接跑脚本）
./run.sh

# Mac / 其他平台：先构建单 exe，再运行
./build.sh
./JadeightRunner
```

行为：打印找到的 VM 路径 → 依次运行 `./byteCode/*.bc`（按文件名排序）→
任一 .bc 运行失败则整体退出码非 0。

## 平台适配（launcher.cpp 的宏）

- `_WIN32`：路径分隔符 `\`、可执行后缀 `.exe`、PATH 分隔符 `;`
- `__APPLE__` / `__linux__`：POSIX 路径、可执行权限检查、`~` 与 `/usr/local/bin` 搜索
- 编译期用 `std::filesystem`（C++20），无第三方依赖

## 虚拟机自动寻找顺序

1. 环境变量 `JADEIGHT_VM`（最高优先）
2. `./jadeight_vm`、程序同目录的 `jadeight_vm`
3. 兄弟工程：`../JadeightCompiler/build/j8run`、`../Jadeight2/cmake-build-debug/Jadeight2`
   （本机自动命中 `j8run`——唯一接受 `.bc` 路径参数的 VM 运行器；
   Jadeight2 独立二进制 `main()` 不带参数、只跑内置演示，慎用）
4. `~/jadeight_vm`、`/usr/local/bin`、`/usr/bin`
5. `PATH` 中的 `jadeight_vm` / `j8run` / `Jadeight2`

## 环境变量

- `JADEIGHT_VM` — 指定虚拟机路径，如 `JADEIGHT_VM=/opt/jadeight/jadeight_vm ./run.sh`
- `JADEIGHT_BC_DIR` — 指定字节码目录（默认 `$PWD/byteCode`）

## 快速上手（全链路）

```bash
# 1) 编译 .j8 → .bc
../JadeightCompiler/build/j8c -O0 -o byteCode/demo.bc ../JadeightCompiler/examples/t1_basic.j8

# 2) 运行
./run.sh            # Linux
./JadeightRunner    # Mac / 其他（先 ./build.sh）
```

## 与其他文档的关系

- 字节码来源：见 [04-j8c编译器.md](04-j8c编译器.md)（j8c）与 [03-汇编器与字节码格式.md](03-汇编器与字节码格式.md)（.bc 格式）
- 执行字节码的 VM：见 [02-Jadeight2虚拟机.md](02-Jadeight2虚拟机.md)
