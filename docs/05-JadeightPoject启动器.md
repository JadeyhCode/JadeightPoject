# JadeightPoject 启动器

自动寻找 Jadeight 虚拟机，运行当前目录 `byteCode/` 下的所有 `.bc`（**ISA v3 模块**），
并把外部函数清单以 `--externs`、预加载动态库以 `--lib` **透传给 VM**。
本工程是 Jadeight 生态的「最后一公里」：编译产物（`.bc`）由它一键跑起来。

## 文件

| 文件 | 说明 |
|---|---|
| `launcher.cpp` | 跨平台启动器源码（宏适配 Linux / macOS / Windows），一个源文件编成单 exe |
| `CMakeLists.txt` | `cmake_minimum_required(VERSION 3.22)`、C++20，目标名 **`JadeightPoject`** |
| `build.sh` | 不走 CMake 的单文件编译：`launcher.cpp` → `JadeightRunner`（`--debug` 出调试版） |
| `run.sh` | Linux 启动脚本（与 launcher 行为一致，参数相同） |
| `byteCode/` | 字节码目录（现存 **8 个 v3 示例**：`t2_control.bc`、`t3_functions.bc`、`t4_protocol.bc`、`t5_ecs.bc`、`t6_fold.bc`、`t7_unroll.bc`、`t8_pointers.bc`、`t9_ecs_run.bc`）。**不入库**（`.gitignore` 忽略，可由 `j8c` 一条命令重新生成，见下） |
| `lib/` | 动态库与清单目录（`externs.txt` 等；同样不入库） |

## 使用

```bash
# CMake 构建（推荐；cmake_minimum_required 已降到 3.22，本机可编）
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target JadeightPoject -j4
./build/JadeightPoject

# 或者：零依赖单文件编译，产出 JadeightRunner
./build.sh
./JadeightRunner

# Linux 也可以直接用脚本（行为与 launcher 一致）
./run.sh
```

行为：打印找到的 VM 路径 → 依次运行 `./byteCode/*.bc`（按文件名排序）→
任一 `.bc` 运行失败则整体退出码非 0。

> ✅ `byteCode/` 里现存的 8 个示例都是 **v3 模块**（文件头 `J3BC`），`./run.sh` 直接可跑：
> 本机实测 **8/8 全部成功，退出码 0**（`run.sh`、`build/JadeightPoject`、`./JadeightRunner`
> 三条路径都一样）。它们由 `j8c` 从 `../JadeightCompiler/tests/` 重新生成：
>
> ```bash
> for t in t2_control t3_functions t4_protocol t5_ecs t6_fold t7_unroll t8_pointers t9_ecs_run; do
>   ../JadeightCompiler/build/j8c -O2 -o byteCode/$t.bc ../JadeightCompiler/tests/$t.j8
> done
> ```
>
> 唯一没入选的是 `t10_atomic`：它需要 `--threads 4`（启动器不传该参数，单线程下会挂住）。
> 历史那批 v2 遗留产物（`t1_basic.bc`、`dl_test.bc`、`final.bc` 等，文件开头是 `00 00 00 00`）
> 当前 VM 无法装载（`cannot load v3 module`），已从 `byteCode/` 移出。

### 命令行选项（run.sh 与 CMake 目标 / JadeightRunner 相同）

| 选项 | 说明 |
|---|---|
| `[文件.bc ...]` | 不带参数跑 byteCode/ 下全部；带文件名只跑指定的（相对路径按 byteCode/ 解析，绝对路径直接用） |
| `--vm PATH` | 指定 VM 可执行文件（优先级高于自动寻找与 JADEIGHT_VM） |
| `--externs PATH` | 外部函数清单（默认 `lib/externs.txt`，JADEIGHT_EXTERNS 次之） |
| `--lib PATH` | 预加载动态库，可多次（透传给 VM 的 `--lib`） |
| `--list` | 只列出 byteCode/ 下的 .bc，不运行 |
| `--stop-on-error` | 遇到失败立即停止（默认跑完所有并汇总） |
| `--quiet` | 不打印 VM 路径/横幅等提示 |
| `-h, --help` | 帮助 |

```bash
./run.sh                              # 跑 byteCode/ 全部（当前 8 个，实测全成功）
./run.sh --list                       # 列出有哪些程序
./run.sh t2_control.bc t6_fold.bc     # 只跑这两个
./build/JadeightPoject --quiet --externs lib/externs.txt t8_pointers.bc
```

## 平台适配（launcher.cpp 的宏）

- `_WIN32`：路径分隔符 `\`、可执行后缀 `.exe`、PATH 分隔符 `;`（用 `system()` 拼命令）
- `__APPLE__` / `__linux__`：POSIX 路径、可执行权限检查（`owner/group/others_exec`）、
  `$HOME` 与 `/usr/local/bin`、`/usr/bin` 搜索；用 `fork` + `execvp` 直接起 VM
  （不做 shell 拼接，路径带空格也安全），退出码取 `WEXITSTATUS`，被信号杀死返回 `128 + 信号`
- 编译期用 `std::filesystem`（C++20），无第三方依赖

## 虚拟机自动寻找顺序

`--vm` 优先于一切；未给 `--vm` 时进入 `findVM()`，按下面的顺序返回第一个**可执行**的候选
（先按「程序自身所在目录 `exeDir`」，再按「当前目录 `cwd`」）：

1. 环境变量 `JADEIGHT_VM`
2. `<exeDir>/jadeight_vm`、`<cwd>/jadeight_vm`
3. 兄弟工程（源码里的实际顺序）：
   - `<exeDir|cwd>/../JadeightCompiler/build/j8run` —— ISA v3 宿主运行器，**本机命中的就是它**；
   - `<exeDir>/../Jadeight2ReWrite/build-isa3/Jadeight2` —— ISA v3 独立 VM（**新增路径**）；
   - `<exeDir>/../Jadeight2ReWrite/build/Jadeight2`；
   - `<exeDir>/../Jadeight2/<历史构建目录>/Jadeight2`（源码里依次试三个：`build` 与
     两个 IDE 风格构建目录）—— v2 旧实现的历史产物，只作兜底
4. 仅 POSIX：`$HOME/jadeight_vm`、`/usr/local/bin/jadeight_vm`、`/usr/bin/jadeight_vm`
5. `PATH` 里的 `jadeight_vm` → `j8run` → `Jadeight2`

两点必须注意：

- `../Jadeight2ReWrite/build-isa3/Jadeight2` 的 `main()` **不带参数**，跑的是内置自测，
  **不会**执行启动器传进去的 `.bc`；候选表里 `j8run` 排在它前面，正常情况命中的是 `j8run`。
  若用 `--vm` 显式指向它，`.bc` 参数会被忽略。
- `run.sh` 的候选表目前只列到 `../JadeightCompiler/build/j8run` 与历史 `Jadeight2` 产物，
  **尚未包含** `build-isa3/Jadeight2`；要用它请走 `--vm` 或 `JADEIGHT_VM`（launcher.cpp 已经有）。

找不到时给出构建提示（与 launcher 的报错一致）：

```bash
cd ../JadeightCompiler && cmake --build build --target j8run -j4
```

顺手把整条链的产物都构建出来（都已经验证过 configure 可过）：

```bash
cd ../Jadeight2ReWrite       && cmake -S . -B build-isa3 && cmake --build build-isa3 --target Jadeight2 -j4
cd ../JadeightAbstractionCode && cmake -S . -B build     && cmake --build build --target jasm -j4
cd ../JadeightCompiler        && cmake -S . -B build     && cmake --build build --target j8c j8run -j4
```

## 环境变量

- `JADEIGHT_VM` — 指定虚拟机路径，如 `JADEIGHT_VM=/opt/jadeight/j8run ./run.sh`
- `JADEIGHT_BC_DIR` — 指定字节码目录（默认 `$PWD/byteCode`）
- `JADEIGHT_LIB_DIR` — 指定动态库目录（默认 `$PWD/lib`，用于找 externs.txt）
- `JADEIGHT_EXTERNS` — 指定外部函数清单（默认取 `lib/externs.txt`）

## 动态链接（字节码自行加载 lib/）

启动器**不预加载动态库**——VM 有 `DL_REG`/`DL_CALL` 运行时动态链接指令后，
字节码用 `dload("x.so")`（= `dlopen("lib/x.so", 2)`，**默认根目录即 `lib/`**）在运行期
自己加载第三方库，配合 `dlsym`/`dl_reg`/`dl_call` 完成 加载→解析→调用 闭环（见 09 文档）。
启动器只负责把 `lib/externs.txt`（j8c `-emit-externs` 生成的外部函数签名清单）以
`--externs` 传给 VM，供 `EXTERN_CALL`/`dl_reg` 注册外部函数；`--lib` 是给 VM 的额外
`dlopen` 句柄（透传，不改变上面的默认行为）。

```bash
# 生成外部函数清单（j8c）并放入 lib/
../JadeightCompiler/build/j8c -O0 -emit-externs lib/externs.txt -o byteCode/demo.bc demo.j8
gcc -shared -fPIC -O2 mylib.c -o lib/libmylib.so   # 动态库放入 lib/
./run.sh
```

示例：`lib/externs.txt` 自动以 `--externs` 传给 VM，供字节码里的
`EXTERN_CALL`/`dl_reg` 注册 `malloc`/`free`/`memcpy`/`dlopen`/`dlsym` 等外部函数
（本机实测：j8run 启动时确实按清单注册这 5 个）。

`--externs`/`--lib` 透传本机实测（`ffi-test/zr3.j8`：zlib `compress2`/`uncompress` 往返 +
C++ shim 校验 `cpp_find`），经启动器跑通、输出与 `ffi-test/README.md` 的期望一致：

```bash
g++ -shared -fPIC -O2 -std=c++20 ffi-test/cppshim.cpp -o lib/libcppshim.so
../JadeightCompiler/build/j8c -O0 -emit-externs /tmp/zr3.txt -o /tmp/zr3.bc ffi-test/zr3.j8
./run.sh --externs /tmp/zr3.txt --lib /lib/x86_64-linux-gnu/libz.so --lib ./lib/libcppshim.so /tmp/zr3.bc
# → 0 / 23 / 0 / 24 / 6    （rc、压缩后长度、rc2、解压后长度、cpp_find("world")）
```

## 快速上手（全链路）

```bash
# 1) 编译 .j8 → v3 模块（magic "J3BC"）
../JadeightCompiler/build/j8c -O0 -o byteCode/demo.bc ../JadeightCompiler/tests/t3_functions.j8

# 2) 运行
./run.sh                       # Linux（自动找 VM）
./build/JadeightPoject demo.bc # CMake 目标
./JadeightRunner               # build.sh 产出的单 exe

# 3) 也可以绕开启动器直接跑 VM
../JadeightCompiler/build/j8run byteCode/demo.bc --jit
```

## 与其他文档的关系

- 字节码来源：见 [04-j8c编译器.md](04-j8c编译器.md)（j8c 命令行）、
  [12-j8c使用指南.md](12-j8c使用指南.md)（使用指南）与
  [03-汇编器与字节码格式.md](03-汇编器与字节码格式.md)（jasm 与 `.bc` / v3 模块格式）
- ISA v3 指令表：[06-指令集参考.md](06-指令集参考.md)；JIT：[14-JIT.md](14-JIT.md)
- 执行字节码的 VM：见 [02-Jadeight2虚拟机.md](02-Jadeight2虚拟机.md)
  （VM 本体源码在 `Jadeight2ReWrite`）
