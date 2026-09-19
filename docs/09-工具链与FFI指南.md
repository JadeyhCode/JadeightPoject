# 09. 工具链完整参考 + FFI（外部函数）指南

本文档是 Jadeight 生态**工具链的完整命令参考**与 **FFI（外部函数调用）实战指南**：
j8c 编译器、j8run 宿主运行器、jasm 汇编器的全部命令行参数与退出码，
`.j8` 里声明 extern、写 C/C++ 动态库、`-emit-externs` 清单 → `--externs`/`--lib` 加载的完整链路，
以及 JadeightRunner 启动器（run.sh）的环境变量与 VM 寻找顺序。

> 信息来源：`JadeightCompiler/src/driver.cpp`、`JadeightCompiler/runtime/j8run.cpp`、
> `JadeightAbstractionCode/main.cpp` 与 `jadeight_asm.hpp`（ISA v3 汇编器/反汇编器）、
> `Jadeight2ReWrite/isa.hpp`（ISA v3 **唯一事实源**）与 `Jadeight2ReWrite/jit.hpp`（模板 JIT）、
> `JadeightPoject/launcher.cpp`、`run.sh`、四个工程的 `CMakeLists.txt`、
> `JadeightPoject/ffi-test/`（zr3.j8 / cppshim.cpp / README.md），并吸收 [05-JadeightPoject启动器.md](05-JadeightPoject启动器.md)。
>
> **状态**：整条工具链已迁移到 **ISA v3**（68 条 opcode；TD 类型参数、全小端、标签跳转、
> 原生多函数/线程/进程、3 条 JIT 指令）。opcode 与编码的唯一事实源是
> `Jadeight2ReWrite/isa.hpp`，逐条说明见 [06-指令集参考.md](06-指令集参考.md)；
> JIT 细节见 [14-JIT.md](14-JIT.md)。本文命令示例均按 v3 的实际构建目录写成，已在本机实测。

---

## 1. 四个工程总览

| 工程 | 路径 | 角色 | 主要产物 | 构建方式 |
|---|---|---|---|---|
| **Jadeight2ReWrite** | `../Jadeight2ReWrite` | ISA v3 虚拟机（解释器 + copy-and-patch 模板 JIT） | `build-isa3/Jadeight2`（**VM 自测 + JIT 基准**；`main()` 不带参数、**不接受 `.bc` 路径参数**） | CMake；`find_package(PkgConfig)` 找 libffi；`add_executable(Jadeight2 Jadeight2.cpp isa.hpp jit.hpp)` |
| **JadeightAbstractionCode** | `../JadeightAbstractionCode` | 汇编层：jasm 文本 ↔ v3 模块 | `jadeight_asm.hpp`（header-only，ISA v3 汇编器/反汇编器，`#include "isa.hpp"`）、CMake 目标名 **`jasm`** | CMake `add_executable(jasm main.cpp)`；或手工 `g++ -std=c++20 -O2 -I../Jadeight2ReWrite main.cpp -o jasm` |
| **JadeightCompiler** | `../JadeightCompiler` | 编译器 + 宿主运行器 | `build/j8c`（编译器）、`build/j8run`（宿主运行器） | CMake：j8c = `src/*.cpp`（内嵌 `jadeight_asm.hpp`）；j8run = `runtime/j8run.cpp` + `#include ../Jadeight2ReWrite/Jadeight2.cpp`，链接 `ffi dl Threads` |
| **JadeightPoject** | 本目录 | 启动器：自动找 VM、跑 `.bc` | `JadeightRunner`（单 exe，由 `launcher.cpp` 编译）、`run.sh`；CMake 目标名 **`JadeightPoject`**，源码同为 `launcher.cpp` | `./build.sh`：`c++ -std=c++20 -O2 launcher.cpp -o JadeightRunner`；CMake 目标的 `cmake_minimum_required` 已从 4.3 降到 **3.22** |

> **注意**：`JadeightPoject/main.cpp` 已不存在（旧版启动器已删除）；`JadeightRunner`
> 与 CMake 目标 `JadeightPoject` 都由现行 `launcher.cpp` 编译，行为一致。

### 构建命令

```bash
# Jadeight2ReWrite（ISA v3 VM：自测 + JIT 基准）
cmake -S Jadeight2ReWrite -B Jadeight2ReWrite/build-isa3
cmake --build Jadeight2ReWrite/build-isa3          # 产物：build-isa3/Jadeight2

# JadeightAbstractionCode（jasm；CMake 目标名就是 jasm）
cmake -S JadeightAbstractionCode -B JadeightAbstractionCode/build
cmake --build JadeightAbstractionCode/build        # 产物：build/jasm
# 或手工（要能 include 到 ../Jadeight2ReWrite/isa.hpp，故需 -I）：
cd JadeightAbstractionCode && g++ -std=c++20 -O2 -I../Jadeight2ReWrite main.cpp -o jasm

# JadeightCompiler（j8c + j8run）
cmake -S JadeightCompiler -B JadeightCompiler/build
cmake --build JadeightCompiler/build               # 产物：build/j8c、build/j8run

# JadeightPoject（JadeightRunner 启动器）
cd JadeightPoject && ./build.sh                    # → ./JadeightRunner
# 或 CMake（目标名 JadeightPoject，源码同为 launcher.cpp）
cmake -S JadeightPoject -B JadeightPoject/build && cmake --build JadeightPoject/build
```

数据流：`.j8 源码 → j8c（词法→语法→语义→优化→代码生成 jasm 文本→内嵌 ISA v3 汇编器）→ .bc（v3 模块：magic "J3BC" + 函数目录 + 码流）→ j8run --jit / JadeightRunner → 输出`。

---

## 2. j8c —— Jadeight 编译器 CLI 完整参考

源码：`JadeightCompiler/src/driver.cpp`。用法（`-h`/`--help` 输出原文）：

```
j8c input.j8 [选项]
  -o out.bc          输出文件（默认 input.bc）
  -S                 保留生成的 Jadeight 汇编（out.jasm）
  -O0 / -O1 / -O2    优化级别（默认 -O2）
  --no-unroll        禁用循环展开
  --unroll-limit N   全展开迭代上限（默认 8）
  --no-inline        禁用内联（当前版本无内联，保留兼容）
  --ecs-capacity N   ECS 世界实体容量（默认 4096）
  --stack N          覆盖栈大小（字节，默认自动计算）
  -emit-externs f    输出外部函数清单（供 j8run 注册）
  -v / --verbose     详细输出
  --dump-ast         打印 AST
```

### 2.1 参数表（语义与默认值）

| 参数 | 语义 | 默认值 |
|---|---|---|
| `input.j8` | 输入源文件 | **必填**（缺省打印 usage 并返回 1） |
| `-o out.bc` | 输出 `.bc` 路径 | 从输入名推导（去掉扩展名 + `.bc`，如 `prog.j8` → `prog.bc`） |
| `-S` | 保留生成的 jasm 汇编文本，写到「输出名去扩展名 + `.jasm`」（如 `-o out.bc` → `out.jasm`） | 关 |
| `-O0` / `-O1` / `-O2` | 优化级别（常量折叠/传播、循环展开、DCE、强度削减） | `-O2`（`common.h` 中 `optLevel = 2`；`tests/run_tests.sh` 顶部注释仍称 `-O2` 为实验性、`-O0` 为可信基线，但两类级别各 9 项当前均全绿） |
| `--no-unroll` | 禁用循环展开 | 关 |
| `--unroll-limit N` | 全展开迭代上限（`optimize.cpp` 中 `n <= limit` 才展开） | 8 |
| `--no-inline` | 禁用内联（**当前版本无内联实现，仅保留兼容**） | 关 |
| `--ecs-capacity N` | ECS 世界实体容量 | 4096 |
| `--stack N` | 覆盖 VM 栈大小（字节） | 自动计算（`stackSize = 0` 时按调用图 DFS 计算；04 文档记录默认递归栈约 1MB，约 1.5 万层深递归） |
| `-emit-externs f` | 输出外部函数清单（每行 `名字:返回类型(参数,...)`，供 j8run `--externs` 注册） | 不输出 |
| `-v` / `--verbose` | 详细输出：打印汇编文本路径、清单路径、输出大小/entry、函数实例数、栈大小、警告数 | 关 |
| `--dump-ast` | 打印 AST（每个声明一行 `decl: <名字> (kind=N)`） | 关 |
| `-h` / `--help` | 打印用法并退出 | — |

**解析规则**（driver.cpp）：参数按出现顺序扫描；第一个非选项参数为输入；`-o`/`--unroll-limit`/`--ecs-capacity`/`--stack`/`-emit-externs` 后跟一个值参数。

**管线**：词法 → 语法 → 语义（协议/泛型/ECS 检查）→ 优化 → 代码生成（`codegen.cpp` 产出 jasm 文本，每个函数用 `.FUNC` 划出）→ **内嵌 `jadeight_asm.hpp` 汇编器当场汇编**（`codegen.cpp` 以 `pureBinary = true` 调 `Assembler::assemble()`，无外部进程，只拿纯码流 + 函数目录）→ driver 用 `jadeight::ModuleImage::serialize()` 拼出 **v3 模块**写盘。

**`.bc` = v3 模块格式**（全小端；定义在 `Jadeight2ReWrite/isa.hpp` 的 `ModuleImage`）：

```
[0..4)    magic "J3BC"(4B)
[4..8)    u32 version = 3
[8..12)   u32 funcCount
[12..16)  u32 codeSize
[16..20)  u32 entryFunc        ← 入口**函数下标**（j8c 的 main 不一定是 0 号函数）
[20..)    funcCount × 20B：{ offset, size, argSize, retSize, entry } 各 u32
...       codeSize 字节码流（函数首尾相接，标签偏移是函数相对的）
```

即文件真实大小 = `20 + 20 × funcCount + codeSize`。指令编码、TD、指令长度见
[06-指令集参考.md](06-指令集参考.md)。

### 2.2 退出码

| 退出码 | 含义 |
|---|---|
| 0 | 成功（含 `-h`/`--help`） |
| 1 | 失败：无输入、无法打开源码、词法/语法/语义/代码生成失败、无法写入输出或清单 |

`-v` 成功时输出示例（本机实测：`j8c tests/t3_functions.j8 -o t3.bc -O0 -S -v`）：

```
j8c: v3 模块 19 个函数，入口 #10，码流 5459 字节      ← stderr
汇编文本: t3.jasm                                    ← stdout（仅 -S 时）
j8c: 编译成功                                        ← stdout
  输出: t3.bc (5471 字节, entry=3331)                ← stdout
  函数实例: 18
  栈大小: 1048576 字节
  警告: 0
```

> **注意 1（输出字节数）**：`输出: ... (N 字节, ...)` 里的 N 是 **`12 + codeSize`** 的旧口径
> （driver.cpp 仍写 `12 + bc.size()`），**不是文件真实大小**。上例文件真实大小是
> `20 + 20×19 + 5459 = 5859` 字节。
> **注意 2（entry 的两层含义）**：`entry=3331` 是 main 在**码流里的绝对偏移**；
> 模块头里的 `entryFunc` 是**函数下标**（上例 `#10`），由 driver 按「entry 落在哪个函数区间」换算。
> **注意 3（流）**：`j8c: v3 模块 ...` 走 **stderr**，`汇编文本:`/`输出:`/`编译成功` 走 **stdout**，合并重定向时顺序可能交错。

---

## 3. j8run —— 宿主运行器 CLI 完整参考

源码：`JadeightCompiler/runtime/j8run.cpp`。**j8run 不是独立 VM**：它 `#define main jadeight2_vm_main` 后 `#include ../../Jadeight2ReWrite/Jadeight2.cpp`（ISA v3 VM 全量：解释器 + 模板 JIT），并叠加 libffi 外部函数注册。它是**当前唯一可直接运行 `.bc` 路径参数的入口**（Jadeight2ReWrite 的 `Jadeight2` 独立二进制 `main()` 不带参数，只跑 VM 自测与 JIT 基准）。

```
j8run <main.bc> [--externs manifest.txt] [--lib lib.so]... [--threads N] [--jit]
```

| 参数 | 语义 |
|---|---|
| `main.bc` | 编译器产出的 **v3 模块**（magic `"J3BC"`：函数目录 + 连续码流；`VM::module.loadFromFile` 载入） |
| `--externs f` | 外部函数清单（j8c `-emit-externs` 生成），j8run 用 libffi 注册到 `externFn[]` 表；清单中的 `tid`/`shared_buf` 由宿主内建 thunk 注册（见下） |
| `--lib path` | 额外共享库，**可多次出现**；对每个 extern 按「默认库 → 用户库」顺序查找符号 |
| `--threads N` | **多线程 SPMD**：N 个线程用 `std::thread` 各跑一次 `vm.launch(线程, 入口函数)`，共享同一份模块；宿主自动提供 `tid()`（当前线程号 0..N-1）与 `shared_buf()`（128 字节共享缓冲）两个 extern，配合 `atomic_*` 内建做同步（`tests/t10_atomic.j8` + `tests/t10_atomic.threads`）。`N<=1` 时单线程直接 `vm.launch` |
| `--jit` | **模板 JIT（copy-and-patch）执行**：先 `vm.jitCompile(入口函数)` 编入口，之后被 `CALL` 到的函数**首次调用时惰性编译**；某函数编译失败（如含 `JMP_IND`）只回退该函数到解释器。无 LLVM 依赖，实现见 `Jadeight2ReWrite/jit.hpp` 与 [14-JIT.md](14-JIT.md)。`--threads N` 时各线程跑同一原生入口（SPMD） |

**默认库查找顺序**（`libs = {"", "libc.so.6"}`，然后按出现顺序追加 `--lib`）：
1. 空串 → `dlsym(RTLD_DEFAULT, name)`（全局作用域）
2. `libc.so.6`（dlopen 后 dlsym）
3. 每个 `--lib`（`dlopen(lib, RTLD_NOW | RTLD_GLOBAL)` 后 dlsym；句柄保存在 `g_handles` 防回收）

> 注意：用户 `--lib` 排在 libc 之后查找——若符号在 libc 中存在（如 `malloc`/`memcpy`/`free`/`sqrt`），会先命中 libc 而非你的库；自定义符号（如 `cpp_find`）则一路查到用户库。`--lib` 用 `RTLD_GLOBAL` 打开，后续符号也可被 `RTLD_DEFAULT` 命中。

### 3.1 manifest（外部函数清单）格式

每行一个签名（`parseManifest` 解析）：

```
名字:返回类型(参数类型1,参数类型2,...)
```

- `#` 开头或空行跳过；不含 `:` 或 `(`/`)` 的行跳过
- 参数列表为空表示无参数（`name:ret()`，如 `tid:u32()`）
- 示例 A：纯 `.j8` 程序（不含任何显式 extern）经 `j8c -emit-externs` 的真实输出——
  7 个自动注册项一行不少（本机实测 `void main(){print(1);}`）：

```
malloc:ptr(u64)
free:void(ptr)
memcpy:void(ptr,ptr,u64)
dlopen:ptr(ptr,i32)
dlsym:ptr(ptr,ptr)
tid:u32()
shared_buf:ptr()
```

- 示例 B：仓库里已有的 `JadeightPoject/lib/externs.txt`（手工维护，只列 libc 侧符号，未含 `tid`/`shared_buf`）：

```
malloc:ptr(u64)
free:void(ptr)
memcpy:void(ptr,ptr,u64)
dlopen:ptr(ptr,i32)
dlsym:ptr(ptr,ptr)
```

**行顺序即 extern 表索引**：字节码里 `EXTERN_CALL` 用索引引用 extern，所以**清单必须与编译时 `sema.externs` 顺序一致**（即源码中 extern 声明顺序，自动注册项追加在末尾，顺序固定为 malloc→free→memcpy→dlopen→dlsym→tid→shared_buf）——用 `-emit-externs` 生成即可保证，手工编辑清单时不要打乱顺序。

### 3.2 支持的类型表（ffiType）

j8run 把清单类型名映射到 libffi 类型：

| manifest 类型 | libffi 类型 | 对应 C 类型 |
|---|---|---|
| `u8` / `i8` | `ffi_type_uint8` | `uint8_t` / `int8_t` |
| `u16` / `i16` | `ffi_type_uint16` | `uint16_t` / `int16_t` |
| `u32` / `i32` | `ffi_type_uint32` | `uint32_t` / `int32_t` |
| `u64` / `i64` | `ffi_type_uint64` | `uint64_t` / `int64_t` |
| `f32` | `ffi_type_float` | `float` |
| `f64` | `ffi_type_double` | `double` |
| `ptr` / `void` | `ffi_type_pointer` | `void*`（void 见下方注意） |

未知类型 → 该 extern 报 `bad signature` 并跳过。

> **注意 1（void 映射）**：源码把 `void` 也映射为 `ffi_type_pointer`（`j8run.cpp:55`）。由于编译器对 void 返回不分配返回值区、不读取（`retBytes = 0`），实测 `free` 等 void 函数正常工作；但这属于**实现现状而非严格 ABI 语义**，严格说 void 应映射 `ffi_type_void` —— 待确认后续是否修正。
>
> **注意 2（f32）**：j8run 类型表支持 `f32`，但 **j8c 不支持 f32 类型**（编译报错「f32 不支持（v1 只支持 f64），请用 f64」），因此从 `.j8` 正常编译无法产生 f32 的 extern 清单；f32 条目只能出现在手写清单/手写字节码场景。
>
> **注意 3（上限）**：VM 侧 `externFn[256]`（`Jadeight2ReWrite/Jadeight2.cpp`），j8run 注册循环 `idx < sigs.size() && idx < 256`，**超过 256 个 extern 静默截断**；未注册就被调用的 extern，VM 见 `externFn[idx].cif == nullptr` → `TRAP`，线程 `state = 2`。

### 3.3 行为与退出码

- stdout 会打印 banner `===== j8run: <bc路径> =====`（在**程序自身的输出之前**）。
- **`--externs` 的注册行也是 stdout，且排在 banner 之前**：每成功注册一个 extern 打印一行 `j8run: registered extern[N] <名字>`（本机实测 `j8run zr3.bc --externs zr3.txt` 会先打 7 行注册信息再打 banner）。因此 `tail -n +2` 只在**不传 `--externs`** 时等于「丢掉 banner」——`tests/run_tests.sh` 正是这种情况；带 `--externs` 时它会丢错行。
- `J8RUN_VERBOSE=1` 时额外诊断信息走 **stderr**：`j8run: <N> 个函数，解释器|模板 JIT[, SPMD]`，`--jit` 时再加一行 `j8run: JIT 已编译入口函数|入口函数不支持，回退解释器`。
- 找不到符号：stderr 打印 `extern[N] 'xxx' not found` **继续**（不中止）
- 加载失败（`vm.module.loadFromFile` 返回 false，例如旧格式文件）→ 退出码 1；线程 `state == 2`（TRAP，如调用了未注册的 extern）→ 退出码 1

| 退出码 | 含义 |
|---|---|
| 0 | 正常运行结束 |
| 1 | 无法加载 `.bc`（不是 v3 模块），或线程以 TRAP 结束（state=2） |
| 2 | 用法错误（未给 `main.bc`） |

---

## 4. jasm —— 汇编器 CLI 完整参考

源码：`JadeightAbstractionCode/main.cpp`（CMake 目标名 `jasm`；同目录 `jasm.cpp` 与它只差一行错误信息文本）。核心逻辑在 header-only 的 `jadeight_asm.hpp` / `isa.hpp`（`Assembler` 类 + ISA v3 的 **68 条 opcode** + 标签 + 数值格式 + 伪指令）。

```
jasm input.asm [-o output.bc] [-d] [-v] [-f] [-e]
```

| 参数 | 语义 | 默认 |
|---|---|---|
| `input.asm` | 汇编模式输入 jasm 文本；`-d` 时输入 `.bc` | 必填 |
| `-d` | **反汇编模式**：输入 `.bc`，输出可读指令清单（到 stdout，或 `-o` 指定文件） | 汇编模式 |
| `-o 文件` | 输出文件名（反汇编时为输出文本文件） | 输入名去扩展名 + `.bc`（反汇编时默认 stdout） |
| `-v` | 详细输出（走 **stderr**）：逐条打印 `[偏移] 原文 (长度)`；成功后打印输出路径、字节数、入口函数的 `argSize/retSize/entry` | 关 |
| `-f` | 输出**纯码流**（无 v3 模块头，即只有 `codeSize` 字节的码流；j8c 内部走的就是这条路） | 输出完整 v3 模块 |
| `-e` / `--le` | **兼容开关，v3 下无实际作用**（`Assembler::littleEndianHeader` 字段仍在，但 v3 一律小端） | 无作用 |

**字节序规则**：v3 **全小端**——模块头、指令操作数、立即数一律小端（`isa.hpp` 的 `Asm`/`ModuleImage` 直接按 LE 拼字节）。`-e` 只是为旧命令行保留的参数，加不加结果相同。

> `-v` 成功时会打印一行 `Header (BE): argSize=... retSize=... entry=...`；这个 `BE`/`LE` 标签是 v2 遗留文案（按 `-e` 是否出现打印 BE/LE），**与 v3 无关**——v3 输出恒为小端模块。该行数字取的是**入口函数**的 `argSize/retSize`（不传 `.ENTRY` 时入口默认 0 号函数）。

**伪指令**（`parseAsmLines` + `assembleModule` 实际支持的全部）：`.FUNC 名 [参数字节] [返回字节]`、`.ARGS n`、`.RETS n`、`.ENTRY <函数名|偏移>`、`.STACK`/`.SCOPE`（接受并忽略，栈由运行时 `STACK_INIT` 决定）、`.BYTE v...`、`.FILL n [v]`。

**汇编语法：助记名 + 类型操作数**（ISA v3 把类型从 opcode 挪进了参数，见 [06-指令集参考.md](06-指令集参考.md)）：

```asm
.FUNC add3 12 4          ; 函数名、参数字节、返回字节
  STACK_ALLOC 28         ; 帧：ret+params+locals
  LOAD u32 R0, 0, 0      ; td, reg, mode, off
  PUSH_REG u32 R0
  PUSH_IMM u32 1
  ADD u32                ; 运算只带一个 TD
  POP_REG u32 R1
  STORE u32 R1, 0, 0
  RET                    ; 取代 v2 的 STACK_PTR_MOVE + JMP_IND

.FUNC main 0 0
  STACK_INIT 4096 64
  MOVI u64 R0, 123       ; td, reg, imm
  CMP LT u32             ; 比较：子操作 LT/LE/EQ/NE/GT/GE + TD
  BRANCH L1              ; 只有标签跳转（函数相对偏移）
  JMP L2
L1:
L2:
  CALL add3, 4, 0        ; funcIdx, argBaseOff, retOff
  CVT u32, f64           ; 源 TD, 目标 TD
  COUT u32
  HALT
```

**退出码**：0 成功；1 失败（缺输入、打不开文件、`-o` 后缺参数、汇编失败、写不出输出）。

### 4.1 与 j8c 的协作

- **内嵌调用**：j8c 的 `codegen.cpp` 直接 `#include "jadeight_asm.hpp"`，以 `pureBinary = true` 调 `Assembler::assemble()`，从汇编器取回**纯码流 + 函数目录**（`asmblr.funcs`），再由 driver 用 `ModuleImage::serialize()` 拼成 v3 模块落盘——**不需要外部 jasm 进程**。
- **`-S` 保留 `.jasm`**：j8c `-S` 写出的 `out.jasm`（文件名取自**输出**名：`-o t3.bc` → `t3.jasm`）可人工检查，也可用独立 jasm 再汇编。
- **不再有字节序差异**：j8c 与独立 jasm 产的 `.bc` 都是 v3 模块，**直接互换**，不需要任何 `-e`。本机实测：`j8c t3_functions.j8 -S -o t3.bc` 后用 `jasm t3.jasm -o roundtrip.bc` 重汇编，两个文件**逐字节相同**（5859 字节），`j8run roundtrip.bc` 输出与 `.expected` 完全一致。
- `-f` 输出的纯码流**不带模块头**，`j8run` 无法直接加载（它要求 `"J3BC"` 模块）；`-f` 只用于把码流嵌进别的东西。

```bash
# 汇编（默认即产出 VM 可加载的 v3 模块，无需 -e）
../JadeightAbstractionCode/build/jasm prog.jasm -o prog.bc -v
../JadeightCompiler/build/j8run prog.bc

# 反汇编 j8c 产物（v3 模块，无需 -e）
../JadeightAbstractionCode/build/jasm prog.bc -d | head -30

# 只要纯码流（无模块头）
../JadeightAbstractionCode/build/jasm prog.jasm -f -o prog.code
```

---

## 5. FFI 完整指南（重点）

FFI 链路全景：

```
.j8 源码:  extern i32 mymul_int(i32, i32);
   │  j8c -emit-externs externs.txt        （语义检查 + 按 extern 声明顺序登记索引）
   ▼
externs.txt: mymul_int:i32(i32,i32)        ← 与字节码中的 extern 索引一一对应
   │  j8run --externs externs.txt --lib libmylib.so
   ▼
libffi: ffi_prep_cif → externFn[idx] = {cif, dlsym(符号)}
   │  字节码 EXTERN_CALL(idx, argOff, retOff) → ffi_call 原样调用 C 函数
   ▼
C 函数执行，返回值写回 VM 栈
```

### 5.1 在 `.j8` 里声明 extern

语法（`parser.cpp parseExtern`）：

```
extern 返回类型 函数名(参数类型[, 参数类型...]);
```

- **参数名可选**（解析器跳过标识符），可以只写类型：`extern u64 compressBound(u64);`
- **类型表**（与清单一致）：`u8 / u16 / u32 / u64 / i8 / i16 / i32 / i64 / f64 / ptr / void`，以及 `T*` 指针类型（如 `u64*`、`char*`，写清单时归一化为 `ptr`）；`bool`/`char` 按 u8 处理；**`f32` 不支持**（编译报错）
- **8 参数上限**：超过 8 个参数报错 `internal: too many extern arguments (>8)`（`codegen2.inc emitExternCall`）
- 语义检查（sema）：参数个数必须**精确匹配**声明；每个实参类型须可赋值给形参（允许数值隐式转换、`char*`→`ptr`、整数字面量→`ptr` 等）
- 调用即同步阻塞；参数按值传递（指针参数传地址值）

```c
// 示例：声明三个 extern（zr3.j8 开头）
extern u64 compressBound(u64);
extern i32 compress2(ptr, ptr, ptr, u64, i32);   // 第 2 个 ptr 收 &dstLen（输出参数）
extern i32 uncompress(ptr, ptr, ptr, u64);
extern i64 cpp_find(ptr, ptr);                   // 自定义库函数

void main() {
    i32 r = cpp_find("hello world", "world");    // 字符串字面量直接作 ptr 参数
    print(r);
}
```

### 5.2 自动注册的 extern（7 个）

即使源码不声明，sema 也会自动注册（`sema.cpp addAutoExtern`，**若用户已显式声明同名 extern 则不重复注册**，用户可覆盖签名）。顺序固定，且追加在所有用户 extern 之后：

| 名字 | 清单签名 | 说明 |
|---|---|---|
| `malloc` | `malloc:ptr(u64)` | 堆分配（`new T[n]` 也走它，codegen `emitNew`） |
| `free` | `free:void(ptr)` | 堆释放 |
| `memcpy` | `memcpy:void(ptr,ptr,u64)` | 内存拷贝（参数自动 coerce 为 u64） |
| `dlopen` | `dlopen:ptr(ptr,i32)` | 运行期动态链接（`dload` 内建调用它，见 §10） |
| `dlsym` | `dlsym:ptr(ptr,ptr)` | 运行期符号解析 |
| `tid` | `tid:u32()` | **宿主 extern**（非 libc）：当前线程号 0..N-1，j8run 内置 thunk |
| `shared_buf` | `shared_buf:ptr()` | **宿主 extern**：128 字节进程内共享缓冲，j8run 内置 thunk |

- `malloc`/`free`/`memcpy` 走 codegen 专用发射路径（`externMemcpy`/`externFree`/`emitNew`），先求值参数到 R4.. 寄存器、再 `SCOPE_PUSH` 压栈（修复过作用域基址 bug，见 §5.7）。
- `tid`/`shared_buf` 不在任何 .so 里：j8run 的 `hostExtern()` 直接返回宿主 thunk（注册时优先于 dlsym），无 manifest 时 `registerHostExterns()` 也会把它们占位注册进 `externFn[]` 的前两个空槽。
- 因为 7 项**必然出现在清单里**，即使纯 `.j8` 程序（无 extern 声明）`-emit-externs` 也输出这 7 行（实测见 §3.1 示例 A）；`malloc/free/memcpy/dlopen/dlsym` 由 j8run 从 libc（`RTLD_DEFAULT`/`libc.so.6`）解析，无需额外 `--lib`。

### 5.3 如何写 C/C++ 库并编译动态库

**C 库**（直接导出，无名字修饰问题）：

```c
// mylib.c
#include <stdint.h>
int32_t mymul_int(int32_t a, int32_t b) { return a * b; }
```

**C++ 库**（必须用 `extern "C"` 封装，避免 C++ 名字修饰；封装里可以自由使用标准库）：

```cpp
// cppshim.cpp（ffi-test/ 实际示例）
#include <cstring>
#include <string>
extern "C" {
int64_t cpp_find(const char* hay, const char* needle) {
    std::string h(hay ? hay : "");
    size_t p = h.find(needle ? needle : "");
    return p == std::string::npos ? -1 : (int64_t)p;
}
}
```

**编译动态库**（j8run 经 `dlopen` 加载，`RTLD_NOW | RTLD_GLOBAL`）：

```bash
# Linux → .so
gcc  -shared -fPIC -O2 mylib.c -o libmylib.so
g++  -shared -fPIC -O2 -std=c++20 cppshim.cpp -o libcppshim.so
# macOS → .dylib
g++  -shared -fPIC -O2 -std=c++20 mylib.cpp -o libmylib.dylib
# Windows → .dll（需 __declspec(dllexport) 导出符号，具体命令待确认）
```

### 5.4 生成清单并加载（j8c → j8run 协作）

```bash
# 1) 编译 .j8，同时生成外部函数清单
../JadeightCompiler/build/j8c -O0 -emit-externs externs.txt -o out.bc prog.j8

# 2) 运行：加载清单 + 动态库
../JadeightCompiler/build/j8run out.bc --externs externs.txt --lib ./libmylib.so
# 可多次 --lib（如 zlib 系统库 + 自编库并存）：
../JadeightCompiler/build/j8run zr3.bc --externs zr3.txt \
    --lib /lib/x86_64-linux-gnu/libz.so --lib ./libcppshim.so
```

完整最小示例：

```c
// demo.j8
extern i32 mymul_int(i32, i32);
void main() { print(mymul_int(6, 7)); }   // 输出 42
```

```bash
gcc -shared -fPIC mylib.c -o libmylib.so
../JadeightCompiler/build/j8c -O0 -emit-externs externs.txt -o out.bc demo.j8
cat externs.txt                            # mymul_int:i32(i32,i32)
../JadeightCompiler/build/j8run out.bc --externs externs.txt --lib libmylib.so
```

### 5.5 参数/返回值的 ABI 对应

VM 侧把 `Stack[SCOPE_BASE + argOff]` 起按 arg_types 顺序拆成 `avalue[]`（紧凑排布、无对齐填充），原样调 `ffi_call`，返回值写回 `Stack[SCOPE_BASE + retOff]`。

| j8 / manifest 类型 | C/C++ 类型 | 字节宽 | 说明 |
|---|---|---|---|
| `u8` / `i8` | `uint8_t` / `int8_t`（`char`/`bool` 也可） | 1 | 8 位读写 |
| `u16` / `i16` | `uint16_t` / `int16_t` | 2 | 16 位读写 |
| `u32` / `i32` | `uint32_t` / `int32_t` | 4 | 32 位读写（C 的 `int` 在 LP64 上即 4 字节） |
| `u64` / `i64` | `uint64_t` / `int64_t` | 8 | 64 位读写 |
| `f64` | `double` | 8 | 双精度浮点 |
| `ptr` | `void*` / `T*` | 8 | 指针按 8 字节传（x86-64） |
| `void`（仅返回） | `void` | 0 | 编译器不分配/不读返回值区 |

- **小整数实参**：j8c 对实参做 `coerceOnStack`（按形参宽度），如 `u32` 实参传给 `u64` 形参自动零扩展
- **整数 → 指针**：`0` 字面量/`u32` 值可赋给 `ptr` 形参（`assignable` 允许 `Ptr ← U32`）
- **f64 传参**：C 侧必须是 `double`（不是 `float`）；需要单精度请改用 `f32` 场景（见 §3.2 注意 2）

### 5.6 字符串字面量与 `&局部变量` 作指针实参

**字符串字面量 → `ptr`/`char*`**：字符串池位于字节码数据段且 **NUL 结尾**（`layoutData`：`dataCursor_ += size + 1`）。`StrLit` 表达式发射 `bc_base + (dataStart_ + rel)` 作为指针值，因此可以直接当 C 字符串传给 extern：

```c
extern i64 cpp_find(ptr, ptr);
print(cpp_find(out, "world"));   // 第二个参数是字节码数据段里的 NUL 结尾 C 字符串
```

> 这些指针在 EXTERN_CALL 期间稳定有效（字节码常驻内存、只读数据），可放心传给只读形参；**不要向其中写入**（语义未定义）。

**`&局部变量` → 输出参数**：`UnaryOp::AddrOf` 走 `emitAddress`，对局部变量发射 `OP_LEA`（栈槽地址），把**当前作用域栈上的地址**传给 C 函数，C 侧通过指针写回（典型输出参数模式）：

```c
u64 dstLen = bound;
i32 rc = compress2(dst, &dstLen, src, srcLen, 9);   // C 侧写回 *dstLen
```

> **生命周期警告**：C 调用是同步的，调用期间该地址有效；**C 函数不得在返回后继续持有该指针**（栈会在 EXTERN_CALL 返回后弹掉，属于悬垂指针——历史上踩过坑，见 §5.7）。

### 5.7 实测示例（ffi-test/）

仓库 `JadeightPoject/ffi-test/` 提供完整可跑示例：

- **`zr3.j8`** —— zlib `compress2`/`uncompress` 完整往返 + 数据校验：
  ```c
  extern u64 compressBound(u64);
  extern i32 compress2(ptr, ptr, ptr, u64, i32);
  extern i32 uncompress(ptr, ptr, ptr, u64);
  extern i64 cpp_find(ptr, ptr);
  void main() {
      u64 srcLen = 24;
      ptr src = malloc(srcLen);
      memcpy(src, "hello world hello world", 24);
      u64 bound = compressBound(srcLen);
      ptr dst = malloc(bound);
      u64 dstLen = bound;
      i32 rc = compress2(dst, &dstLen, src, srcLen, 9);
      print(rc); print(dstLen);
      ptr out = malloc(srcLen);
      u64 outLen = srcLen;
      i32 rc2 = uncompress(out, &outLen, dst, dstLen);
      print(rc2); print(outLen);
      print(cpp_find(out, "world"));
      free(src); free(dst); free(out);
  }
  ```
- **`cppshim.cpp`** —— C++ 标准库的 `extern "C"` 薄封装（`std::string`）
- 运行（README 记录的命令，期望输出 `rc=0 dstLen=23 rc2=0 outLen=24 cpp_find(world)=6`）：

```bash
cd JadeightPoject/ffi-test
g++ -shared -fPIC -O2 -std=c++20 cppshim.cpp -o libcppshim.so
../../JadeightCompiler/build/j8c -O0 -emit-externs zr3.txt -o zr3.bc zr3.j8
../../JadeightCompiler/build/j8run zr3.bc --externs zr3.txt \
    --lib /lib/x86_64-linux-gnu/libz.so --lib ./libcppshim.so
```

**其他已验证**（04 文档 + ffi-test README）：
- **C++ 标准库**：`std::string`（cpp_find）、`std::map`、`std::sort`、`std::cmath`——实测可通过 FFI 调用；注意当前仓库 `cppshim.cpp` 只含 `cpp_find`（std::string），std::map/std::sort/std::cmath 的封装未随仓库保存（README 记载），如需复现请自行封装（待确认原始封装源码）
- **libm 直调**：`sqrt`/`pow`/`floor`/`fabs`/`sin`/`cos`（经 `RTLD_DEFAULT`/libc 解析；注意 j8c 内置 `sqrt`/`log` 走 ISA v3 的 `SQRT <td>`/`LOG <td>` 指令，同名 extern 声明会与内建冲突，需避免）
- **zlib**：`compressBound`/`compress2`/`uncompress` 往返（zr3.j8）

### 5.8 注意事项与已修复的 bug（简述，细节见 [04-j8c编译器.md](04-j8c编译器.md)）

**已修复**：
- **j8run 注册悬垂指针**（两个）：`&g_cifs.back()` 被 vector 扩容失效；`ffi_prep_cif` 只存指针不拷贝，局部 `atypes` 数组随函数返回销毁 → 已改用 `deque<ffi_cif>` + 全局 `g_argTypeSets` 保存 arg_types 数组
- **memcpy/free 基址错位**：`externMemcpy`/`externFree` 曾在 `SCOPE_PUSH` **之后**才求值参数，mode0 加载用错作用域基址、free 收到非法指针 → 已改为**先求值参数到 R4.. 寄存器，再 SCOPE_PUSH 压栈**

**注意事项**：
- extern 索引是编译期确定的（声明顺序），清单顺序不能改（见 §3.1）
- extern 上限 256 个（VM `externFn[256]`）；单函数参数上限 8 个
- `void` 返回值在 j8run 被映射为 `ffi_type_pointer`（§3.2 注意 1）；C 侧 void 函数实测可用，但语义是实现现状
- 不要向字符串字面量指针写入；不要把 `&局部变量` 的地址存到 C 侧跨调用使用
- 小整数/指针实参存在隐式转换（coerce），C 侧形参宽度必须与清单一致（如 C 的 `int` ↔ j8 的 `i32`）

---

## 6. JadeightRunner 启动器参考（JadeightPoject）

源码：`launcher.cpp`（跨平台单 exe，宏适配 `__APPLE__`/`__linux__`/`_WIN32`）与 `run.sh`（Linux 脚本）。**两者的参数与环境变量集相同**，但 VM 候选路径列表略有差异（见 §6.3）。作用：自动寻找 VM → 运行 `byteCode/*.bc`（按文件名排序，或只跑命令行指定的 `.bc`）→ 自动发现 `lib/externs.txt` 传给 VM。

### 6.1 用法

```bash
# Linux（脚本）
./run.sh                                   # 跑 byteCode/ 下全部 .bc
./run.sh demo.bc                           # 只跑指定的（相对路径按 byteCode/ 解析）
./run.sh --list                            # 只列出，不运行
./run.sh --vm ../JadeightCompiler/build/j8run --externs lib/externs.txt --lib ./lib/libmylib.so

# Mac / 其他：先构建单 exe（launcher.cpp）
./build.sh                                 # → ./JadeightRunner
./JadeightRunner [--vm PATH] [--externs PATH] [--lib PATH]... [--list] [--stop-on-error] [--quiet] [文件.bc ...]
# CMake 目标 JadeightPoject 也是同一份 launcher.cpp：
cmake -S . -B build && cmake --build build && ./build/JadeightPoject
```

| 选项 | 语义 |
|---|---|
| `--vm PATH` | 指定 VM（优先于环境变量） |
| `--externs PATH` | 外部函数清单（缺省自动找 `$JADEIGHT_LIB_DIR/externs.txt`） |
| `--lib PATH` | 预加载动态库，可多次（透传给 VM 的 `--lib`）——启动器**不**自动扫描 `lib/` |
| `--list` | 只列出 `byteCode/` 下的 `.bc` |
| `--stop-on-error` | 遇失败立即停止（默认跑完所有并汇总） |
| `--quiet` | 不打印每条运行的横幅 |

### 6.2 环境变量

| 变量 | 语义 | 默认 |
|---|---|---|
| `JADEIGHT_VM` | 显式指定虚拟机路径（最高优先级） | 自动寻找 |
| `JADEIGHT_BC_DIR` | 字节码目录 | `$PWD/byteCode`（启动器为当前目录） |
| `JADEIGHT_LIB_DIR` | 动态库目录 | `$PWD/lib` |
| `JADEIGHT_EXTERNS` | 外部函数清单路径（优先） | `lib/externs.txt`（存在时） |

示例：`JADEIGHT_VM=/opt/jadeight/jadeight_vm JADEIGHT_BC_DIR=build JADEIGHT_LIB_DIR=lib ./run.sh`

### 6.3 VM 寻找顺序（逐级取第一个可执行的）

`launcher.cpp` 的 `findVM()`：

1. **环境变量** `JADEIGHT_VM`（最高优先）
2. **程序同目录 / 当前目录**：`jadeight_vm`
3. **兄弟工程**（顺序即优先级）：
   `../JadeightCompiler/build/j8run`（推荐；**唯一接受 `.bc` 路径参数**的入口）
   → `../Jadeight2ReWrite/build-isa3/Jadeight2`
   → `../Jadeight2ReWrite/build/Jadeight2`
   → 历史路径 `../Jadeight2/{cmake-build-debug,build,cmake-build-release}/Jadeight2`
4. **用户/系统目录**：`~/jadeight_vm`、`/usr/local/bin/jadeight_vm`、`/usr/bin/jadeight_vm`
5. **PATH** 中的 `jadeight_vm` / `j8run` / `Jadeight2`

`run.sh` 的 `find_vm()` 基本一致，但**少 `../Jadeight2ReWrite/build-isa3/Jadeight2` 这一项**（候选为 `$PWD/jadeight_vm`、脚本同目录的 `jadeight_vm`、`../JadeightCompiler/build/j8run`、`../Jadeight2/cmake-build-debug/Jadeight2`、`../Jadeight2/build/Jadeight2`、用户/系统目录、PATH）。

> 注意：`Jadeight2ReWrite` 的 `Jadeight2` 二进制 `main()` **不带参数、不接受 `.bc` 路径**（只跑内置自测 + JIT 基准）。它出现在候选里只是历史上曾可用；实际运行 `.bc` 必须命中 **j8run**。

### 6.4 行为与退出码

- 打印找到的 VM 路径 → 自动发现 externs 清单（`--externs` → `JADEIGHT_EXTERNS` → `$JADEIGHT_LIB_DIR/externs.txt`，存在才传）→ 依次运行 `byteCode/*.bc`（或命令行指定的文件），每个：
  ```
  <VM> <bc文件> [--externs <清单>] [--lib <lib1>] [--lib <lib2>] ...
  ```
- 任一 `.bc` 运行失败（非 0）则继续跑后续（加 `--stop-on-error` 则立即停），整体退出码非 0

| 退出码 | 含义 |
|---|---|
| 0 | 全部成功（或 byteCode 目录为空） |
| 1 | 至少一个 `.bc` 运行失败 |
| 2 | 找不到虚拟机 / 未知选项 / 选项缺参数 |
| 3 | 找不到字节码目录或指定的 `.bc` 文件 |

### 6.5 启动器全链路示例（extern 场景）

```bash
# 1) 编译 .j8 → byteCode/，并生成清单 → lib/
../JadeightCompiler/build/j8c -O0 -emit-externs lib/externs.txt -o byteCode/demo.bc demo.j8

# 2) 把 C 动态库放入 lib/
gcc -shared -fPIC -O2 mylib.c -o lib/libmylib.so

# 3) 一键运行（自动找 VM、自动传 --externs 与 --lib）
./run.sh
```

---

## 7. 端到端工作流（写 → 编译 → 汇编 → 运行，含 extern 库）

```bash
# ========== 0) 构建工具链 ==========
cmake -S JadeightCompiler -B JadeightCompiler/build && cmake --build JadeightCompiler/build
cd JadeightAbstractionCode && g++ -std=c++20 -O2 -I../Jadeight2ReWrite main.cpp -o jasm   # 或 cmake 构建

# ========== 1) 写源码 ==========
# 示意示例（机制与仓库已验证的 zr3.j8 / extern_test / cpp_find 相同，组合本身未单独实测）
cat > demo.j8 <<'EOF'
extern i32 mymul_int(i32, i32);
extern u64 strlen_ptr(ptr);
void main() {
    print(mymul_int(6, 7));          // 42
    print(strlen_ptr("jadeight"));   // 8（字符串字面量作 ptr）
}
EOF

# ========== 2) C 动态库 ==========
cat > mylib.c <<'EOF'
#include <stdint.h>
#include <string.h>
int32_t mymul_int(int32_t a, int32_t b) { return a * b; }
uint64_t strlen_ptr(const char* s)    { return (uint64_t)strlen(s); }
EOF
gcc -shared -fPIC -O2 mylib.c -o libmylib.so

# ========== 3) 编译 .j8 → .bc（v3 模块；-S 保留 .jasm；-emit-externs 生成清单） ==========
../JadeightCompiler/build/j8c -O0 -S -emit-externs externs.txt -o demo.bc demo.j8
cat externs.txt
# mymul_int:i32(i32,i32)
# strlen_ptr:u64(ptr)
# malloc:ptr(u64)          ← 以下 7 项为自动注册，即使源码未用也会列出
# free:void(ptr)
# memcpy:void(ptr,ptr,u64)
# dlopen:ptr(ptr,i32)
# dlsym:ptr(ptr,ptr)
# tid:u32()
# shared_buf:ptr()

# ========== 4)（可选）人工检查/重汇编 .jasm（v3 全小端，无需 -e） ==========
../JadeightAbstractionCode/build/jasm demo.bc -d | head -30      # 反汇编 j8c 产物
../JadeightAbstractionCode/build/jasm demo.jasm -o demo2.bc      # 重汇编（默认即 v3 模块）

# ========== 5) 运行（解释器 / 模板 JIT 两条路径） ==========
../JadeightCompiler/build/j8run demo.bc --externs externs.txt --lib ./libmylib.so
../JadeightCompiler/build/j8run demo.bc --externs externs.txt --lib ./libmylib.so --jit
# 期望输出：42 / 8

# ========== 6) 或用启动器一键运行 ==========
mkdir -p byteCode lib
cp demo.bc byteCode/ && cp libmylib.so lib/ && cp externs.txt lib/
../JadeightPoject/run.sh --lib ./lib/libmylib.so
```

**要点回顾**：
- j8c 与独立 jasm 都产 **v3 模块**（全小端，magic `"J3BC"`），可互相替换，**不需要 `-e`**（`-e` 在 v3 下无作用）
- 清单必须与编译顺序一致，用 `-emit-externs` 生成、不要手改顺序
- 自动注册的 extern 共 **7 个**（`malloc`/`free`/`memcpy`/`dlopen`/`dlsym`/`tid`/`shared_buf`），无需声明即可用；纯 `.j8` 程序清单也会包含它们
- j8run 是当前唯一可直接运行 `.bc` 的入口；`Jadeight2ReWrite` 的 `Jadeight2` 独立二进制不接受 `.bc` 参数
- 启动器**不会**自动扫描 `lib/` 下的 `.so`，动态库要用 `--lib` 逐个传（`lib/externs.txt` 才会自动发现）

---

## 与其他文档的关系

- 编译器细节（语言、优化器、代码生成、测试）：[04-j8c编译器.md](04-j8c编译器.md)
- **ISA v3 指令集**（68 条 opcode、TD、编码约定、长度表）与 jasm 语法：[06-指令集参考.md](06-指令集参考.md)
- **JIT**（copy-and-patch 模板 JIT、`JIT_SUBMIT`/`JIT_COMPILE`/`CALL_NATIVE`、性能）：[14-JIT.md](14-JIT.md)
- 汇编层与 `.bc` 格式：[03-汇编器与字节码格式.md](03-汇编器与字节码格式.md)（**已随 ISA v3 更新**：68 条 opcode、v3 `"J3BC"` 模块、小端操作数、`jasm -d` 逐字节回环）
- VM 机制（EXTERN_CALL/libffi、内存模型、调用约定）：[08-VM机制与字节码格式.md](08-VM机制与字节码格式.md) 是 **ISA v3 当前实现**；[02-Jadeight2虚拟机.md](02-Jadeight2虚拟机.md) 是 **v2 旧 VM** 的对照文档，两者不要混读
- 启动器：[05-JadeightPoject启动器.md](05-JadeightPoject启动器.md)
- .j8 语言完整手册：[07-语言参考.md](07-语言参考.md)

## 10. 运行时动态链接（dlopen / dl_reg / dl_call）

启动时 `--lib` 预加载是静态路径；要**运行期加载任意第三方库并调用**，用三条指令闭环：

```c
void main() {
    ptr h  = dload("libfoo.so");              // 1) 加载 lib/ 下的库（= dlopen("lib/libfoo.so",2)）
    ptr f  = dlsym(h, "my_func");             // 2) 解析符号 → 函数指针
    u32 id = dl_reg(f, "i32(i32,i32)");       // 3) DL_REG：按签名注册进 externFn 表 → 索引
    i32 r  = dl_call(id, "i32(i32,i32)", 1, 2); // 4) DL_CALL：按运行时索引调用
}
```
- `dload`/`dlopen`/`dlsym` 均为**自动 extern**（j8c 自动注册、j8run 从 libc 解析），无需手写 extern 声明；动态库默认根目录 = `lib/`（`dload` 编译期拼接前缀）
- 启动器不会自动预加载 `lib/` 下的库，也不要求库在启动时存在——运行期按需加载
- 指令编码：`DL_REG` = 0x83（13 字节，`fnOff, sigOff, outOff` 三个 u32）、`DL_CALL` = 0x84（13 字节，`idxOff, argBase, retOff`）

- **签名串格式**：`返回类型(参数类型,...)`，类型名 `u8/i8/u16/i16/u32/i32/u64/i64/f32/f64/ptr/void`（.j8 侧无 f32，用 f64）
- `dl_call` 的签名串在**编译期**决定参数/返回值布局（编译器解析字面量）；`dl_reg` 的签名串在**运行期**由 VM 解析生成 ffi_cif——两处必须一致
- 已注册索引可复用：同一函数只需 dl_reg 一次，dl_call 多次
- 实测：运行期 dlopen 加载 .so，dlsym 取 tp_add/tp_mul/tp_echo/tp_neg，dl_call 返回 42 / 7.000 / 123456789 / -7 全对
- 配套：`GET_SYSTEM` 指令（ISA v3 的 0x81，v2 时代编号 175）可在字节码内判断平台后选择加载哪套库
