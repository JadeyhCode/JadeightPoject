# 09. 工具链完整参考 + FFI（外部函数）指南

本文档是 Jadeight 生态**工具链的完整命令参考**与 **FFI（外部函数调用）实战指南**：
j8c 编译器、j8run 宿主运行器、jasm 汇编器的全部命令行参数与退出码，
`.j8` 里声明 extern、写 C/C++ 动态库、`-emit-externs` 清单 → `--externs`/`--lib` 加载的完整链路，
以及 JadeightRunner 启动器（run.sh）的环境变量与 VM 寻找顺序。

> 信息来源：`JadeightCompiler/src/driver.cpp`、`JadeightCompiler/runtime/j8run.cpp`、
> `JadeightAbstractionCode/jasm.cpp`（与 `main.cpp` 内容相同）、
> `JadeightPoject/launcher.cpp`、`run.sh`、三个工程的 `CMakeLists.txt`、
> `JadeightPoject/ffi-test/`（zr3.j8 / cppshim.cpp / README.md），并吸收 [05-JadeightPoject启动器.md](05-JadeightPoject启动器.md)。

---

## 1. 四个工程总览

| 工程 | 路径 | 角色 | 主要产物 | 构建方式 |
|---|---|---|---|---|
| **Jadeight2** | `../Jadeight2` | 字节码虚拟机（解释器 + 可选 LLVM JIT） | `Jadeight2` 可执行文件（`main()` 不带参数，只跑内置演示；**不能直接跑 .bc 参数**） | CMake（`find_package(PkgConfig)` 找 libffi，LLVM-15 可选 → JIT） |
| **JadeightAbstractionCode** | `../JadeightAbstractionCode` | 汇编层：jasm 文本 ↔ 字节码 | `jadeight_asm.hpp`（header-only 库，174 条 opcode）、CLI `jasm` / `jasm_dbg` | CMake `add_executable(JadeightAbstractionCode main.cpp)`；或手工 `g++ -std=c++20 -O2 main.cpp -o jasm` |
| **JadeightCompiler** | `../JadeightCompiler` | 编译器 + 宿主运行器 | `build/j8c`（编译器）、`build/j8run`（宿主运行器） | CMake：j8c = `src/*.cpp`（内嵌 `jadeight_asm.hpp`）；j8run = `runtime/j8run.cpp` + `#include ../Jadeight2/main.cpp`，链接 `ffi dl` |
| **JadeightPoject** | 本目录 | 启动器：自动找 VM、跑字节码 | `JadeightRunner`（单 exe，由 `launcher.cpp` 编译）、`run.sh` | `./build.sh`：`c++ -std=c++20 -O2 launcher.cpp -o JadeightRunner`（另有 CMake 目标 `JadeightPoject`，构建旧版 `main.cpp` 启动器） |

> **JadeightPoject 的 main.cpp 注意**：`JadeightPoject/main.cpp` 是**旧版**启动器（只支持 `JADEIGHT_VM`/`JADEIGHT_BC_DIR`，无 lib/externs 支持）；
> 现行启动器是 `launcher.cpp`（编译为 `JadeightRunner`）。本机实际使用的二进制由 `build.sh` 产出。

### 构建命令

```bash
# JadeightCompiler（j8c + j8run）
cmake -S JadeightCompiler -B JadeightCompiler/build
cmake --build JadeightCompiler/build          # 产物：build/j8c、build/j8run

# JadeightAbstractionCode（jasm）
cmake -S JadeightAbstractionCode -B JadeightAbstractionCode/cmake-build-debug
cmake --build JadeightAbstractionCode/cmake-build-debug
# 或手工（main.cpp 顶部注释给出的命令）：
cd JadeightAbstractionCode && g++ -std=c++20 -O2 main.cpp -o jasm

# Jadeight2（VM，需要 libffi；LLVM-15 可选）
cmake -S Jadeight2 -B Jadeight2/cmake-build-debug
cmake --build Jadeight2/cmake-build-debug

# JadeightPoject（JadeightRunner 启动器）
cd JadeightPoject && ./build.sh
```

数据流：`.j8 源码 → j8c（词法→语法→语义→优化→代码生成 jasm 文本→内嵌汇编器）→ .bc（LE 头 + 指令流）→ j8run / JadeightRunner → 输出`。

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
| `-O0` / `-O1` / `-O2` | 优化级别（常量折叠/传播、循环展开、DCE、强度削减） | `-O2`（`common.h` 中 `optLevel = 2`；**注意**：04 文档提示 `-O2` 为实验性/不稳定，实际建议 `-O0`） |
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

**管线**：词法 → 语法 → 语义（协议/泛型/ECS 检查）→ 优化 → 代码生成（`codegen.cpp` 产出 jasm 文本）→ **内嵌 `jadeight_asm.hpp` 汇编器当场汇编**（`codegen.cpp` 直接调 `Assembler::assemble()`，无外部进程）→ 写 `.bc`。输出头由 driver 用 `wrLE` 写 12 字节：`argSize`/`retSize`/`entry` 各 u32 **小端**（与 `FunctionSave::loadFromFile` 兼容）。

### 2.2 退出码

| 退出码 | 含义 |
|---|---|
| 0 | 成功（含 `-h`/`--help`） |
| 1 | 失败：无输入、无法打开源码、词法/语法/语义/代码生成失败、无法写入输出或清单 |

`-v` 成功时输出示例：

```
j8c: 编译成功
  输出: out.bc (1234 字节, entry=42)
  函数实例: 3
  栈大小: 65536 字节
  警告: 0
```

---

## 3. j8run —— 宿主运行器 CLI 完整参考

源码：`JadeightCompiler/runtime/j8run.cpp`。**j8run 不是独立 VM**：它 `#define main jadeight2_vm_main` 后 `#include ../Jadeight2/main.cpp`，直接复用 VM 全量实现（解释语义逐字节一致），并叠加 libffi 外部函数注册。它是**当前唯一可直接运行 `.bc` 路径参数的入口**（Jadeight2 独立二进制 `main()` 不带参数）。

```
j8run <main.bc> [--externs manifest.txt] [--lib lib.so]...
```

| 参数 | 语义 |
|---|---|
| `main.bc` | 编译器产出的程序（FunctionSave 文件格式：LE 头 + 指令流） |
| `--externs f` | 外部函数清单（j8c `-emit-externs` 生成），j8run 用 libffi 注册到 `externFn[]` 表 |
| `--lib path` | 额外共享库，**可多次出现**；对每个 extern 按「默认库 → 用户库」顺序查找符号 |

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
- 参数列表为空表示无参数（`name:ret()`）
- 示例（`JadeightPoject/lib/externs.txt`）：

```
add3i:i32(i32,i32,i32)
malloc:ptr(u64)
free:void(ptr)
memcpy:void(ptr,ptr,u64)
```

**行顺序即 extern 表索引**：字节码里 `EXTERN_CALL` 用索引引用 extern，所以**清单必须与编译时 `sema.externs` 顺序一致**（即源码中 extern 声明顺序，自动注册的 malloc/free/memcpy 追加在末尾）——由 `-emit-externs` 生成即可保证，手工编辑清单时不要打乱顺序。

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
> **注意 3（上限）**：VM 侧 `externFn[256]`（`Jadeight2/main.cpp`），j8run 注册循环 `idx < sigs.size() && idx < 256`，**超过 256 个 extern 静默截断**；未注册就被调用的 extern，VM 报 `externFn[idx].cif == nullptr` → 程序异常退出（end=2）。

### 3.3 行为与退出码

- 启动即打印 `===== j8run: <bc路径> =====`，随后依次打印 `j8run: registered extern[N] <名字>`（每成功注册一个 extern）
- 找不到符号：打印 `extern[N] 'xxx' not found` **继续**（不中止）
- 加载失败（`bytecode.size == 0`）→ 退出码 1；程序异常退出（`state.end == 2`）→ 退出码 1

| 退出码 | 含义 |
|---|---|
| 0 | 正常运行结束 |
| 1 | 无法加载 `.bc`，或程序异常退出（end=2，如调用了未注册的 extern） |
| 2 | 用法错误（未给 `main.bc`） |

---

## 4. jasm —— 汇编器 CLI 完整参考

源码：`JadeightAbstractionCode/jasm.cpp`（与 `main.cpp` 内容相同，jasm.cpp 为近期恢复的规范源）。核心逻辑在 header-only 的 `jadeight_asm.hpp`（`Assembler` 类 + 174 条 opcode + 标签 + 数值格式 + 伪指令 `.STACK .ARGS .RETS .ENTRY .ENTRYOFF .BYTE .FILL`）。

```
jasm input.asm [-o output.bc] [-d] [-v] [-f] [-e]
```

| 参数 | 语义 | 默认 |
|---|---|---|
| `input.asm` | 汇编模式输入 jasm 文本；`-d` 时输入 `.bc` | 必填 |
| `-d` | **反汇编模式**：输入 `.bc`，输出可读指令清单（到 stdout，或 `-o` 指定文件） | 汇编模式 |
| `-o 文件` | 输出文件名（反汇编时为输出文本文件） | 输入名去扩展名 + `.bc`（反汇编时默认 stdout） |
| `-v` | 详细输出：打印汇编行/反汇编结果；成功后打印输出路径、字节数、头部 `argSize/retSize/entry` | 关 |
| `-f` | 输出**纯字节码**（无 12 字节 FunctionSave 头部） | 带头部 |
| `-e` / `--le` | 头部按**小端序**读写（与 Jadeight2 VM `FunctionSave::loadFromFile` 兼容） | **大端序**（与自身反汇编回环一致） |

**字节序规则**（关键）：头部位序由 `-e` 切换；**指令内部的多字节操作数恒为大端**（`rdBE`/`wrBE`），与头部字节序无关。

**退出码**：0 成功；1 失败（缺输入、打不开文件、`-o` 后缺参数、汇编失败、写不出输出）。

### 4.1 与 j8c 的协作

- **内嵌调用**：j8c 的 `driver.cpp`/`codegen.cpp` 直接 `#include "jadeight_asm.hpp"`，代码生成阶段当场 `Assembler::assemble()` 把 jasm 文本变成字节码——**不需要外部 jasm 进程**。
- **`-S` 保留 `.jasm`**：j8c `-S` 写出的 `out.jasm` 可人工检查，也可用独立 jasm 再汇编。
- **头部字节序差异**：j8c 产出的 `.bc` 是 **LE 头**（driver 用 `wrLE`），而独立 jasm **默认写 BE 头**。因此：
  - 用独立 jasm 汇编给 VM 跑 → **必须加 `-e`**（`jasm prog.jasm -e -o prog.bc`）
  - 反汇编 j8c 产出的 `.bc` → **必须加 `-d -e`**（`jasm prog.bc -d -e`）
  - 不带头部的纯字节码（`-f`）反汇编时无此问题。

```bash
# 汇编（VM 兼容：-e 小端头）
../JadeightAbstractionCode/jasm prog.jasm -e -o prog.bc -v

# 反汇编 j8c 产物（-e 才能正确读 LE 头）
../JadeightAbstractionCode/jasm prog.bc -d -e
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

### 5.2 自动注册的 extern：malloc / free / memcpy

即使源码不声明，sema 也会自动注册（`sema.cpp addAutoExtern`，**若用户已显式声明同名 extern 则不重复注册**，用户可覆盖签名）：

| 名字 | 清单签名 | 说明 |
|---|---|---|
| `malloc` | `malloc:ptr(u64)` | 堆分配（`new T[n]` 也走它，codegen `emitNew`） |
| `free` | `free:void(ptr)` | 堆释放 |
| `memcpy` | `memcpy:void(ptr,ptr,u64)` | 内存拷贝（参数自动 coerce 为 u64） |

这三个走 codegen 专用发射路径（`externMemcpy`/`externFree`/`emitNew`），先求值参数到 R4.. 寄存器、再 `SCOPE_PUSH` 压栈（修复过作用域基址 bug，见 §5.7）。由于它们**必然出现在清单里**，即使纯 `.j8` 程序（无 extern 声明）`-emit-externs` 也会输出这三行——j8run 无需额外 `--lib` 即可从 libc（`RTLD_DEFAULT`/`libc.so.6`）解析到它们。

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
- **libm 直调**：`sqrt`/`pow`/`floor`/`fabs`/`sin`/`cos`（经 `RTLD_DEFAULT`/libc 解析；注意 j8c 内置 `sqrt`/`log` 走 `OP_SQRT_F64`/`OP_LOG_F64` 指令，同名 extern 声明会与内建冲突，需避免）
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

源码：`launcher.cpp`（跨平台单 exe，宏适配 `__APPLE__`/`__linux__`/`_WIN32`）与 `run.sh`（Linux 脚本，行为与 launcher 一致）。作用：自动寻找 VM → 依次运行 `byteCode/*.bc`（按文件名排序）→ 收集 `lib/` 下动态库与 externs 清单传给 VM。

### 6.1 用法

```bash
# Linux
./run.sh
# Mac / 其他：先构建单 exe
./build.sh
./JadeightRunner
```

### 6.2 环境变量

| 变量 | 语义 | 默认 |
|---|---|---|
| `JADEIGHT_VM` | 显式指定虚拟机路径（最高优先级） | 自动寻找 |
| `JADEIGHT_BC_DIR` | 字节码目录 | `$PWD/byteCode`（启动器为当前目录） |
| `JADEIGHT_LIB_DIR` | 动态库目录 | `$PWD/lib` |
| `JADEIGHT_EXTERNS` | 外部函数清单路径（优先） | `lib/externs.txt`（存在时） |

示例：`JADEIGHT_VM=/opt/jadeight/jadeight_vm JADEIGHT_BC_DIR=build JADEIGHT_LIB_DIR=lib ./run.sh`

### 6.3 VM 寻找顺序（launcher.cpp `findVM` / run.sh `find_vm`，逐级取第一个可执行的）

1. **环境变量** `JADEIGHT_VM`（最高优先）
2. **程序同目录 / 当前目录**：`jadeight_vm`
3. **兄弟工程**：`../JadeightCompiler/build/j8run`、`../Jadeight2/cmake-build-debug/Jadeight2`（本机自动命中 j8run——唯一接受 `.bc` 路径参数的 VM 运行器；Jadeight2 独立二进制不带参数、只跑内置演示，慎用）
4. **用户/系统目录**：`~/jadeight_vm`、`/usr/local/bin/jadeight_vm`、`/usr/bin/jadeight_vm`
5. **PATH** 中的 `jadeight_vm` / `j8run` / `Jadeight2`

### 6.4 行为与退出码

- 打印找到的 VM 路径 → 收集 `lib/` 下 `.so`/`.dylib`/`.dll`（按文件名排序）→ 打印动态库清单与 externs 清单 → 依次运行 `byteCode/*.bc`：
  ```
  <VM> <bc文件> --externs <清单> --lib <lib1> --lib <lib2> ...
  ```
- 任一 `.bc` 运行失败（非 0）则继续跑后续，但整体退出码非 0

| 退出码 | 含义 |
|---|---|
| 0 | 全部成功（或 byteCode 目录为空） |
| 1 | 至少一个 `.bc` 运行失败 |
| 2 | 找不到虚拟机 |
| 3 | 找不到字节码目录 |

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
cd JadeightAbstractionCode && g++ -std=c++20 -O2 main.cpp -o jasm   # 或 cmake 构建

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

# ========== 3) 编译 .j8 → .bc（-S 保留 .jasm；-emit-externs 生成清单） ==========
../JadeightCompiler/build/j8c -O0 -S -emit-externs externs.txt -o demo.bc demo.j8
cat externs.txt
# mymul_int:i32(i32,i32)
# strlen_ptr:u64(ptr)
# malloc:ptr(u64)          ← 自动注册的 extern（即使源码未用也会列出）
# free:void(ptr)
# memcpy:void(ptr,ptr,u64)

# ========== 4)（可选）人工检查/重汇编 .jasm ==========
../JadeightAbstractionCode/jasm demo.jasm -d -e | head -30   # 反汇编 j8c 产物（-e：LE 头）
../JadeightAbstractionCode/jasm demo.jasm -e -o demo2.bc     # 重汇编（-e：VM 兼容 LE 头）

# ========== 5) 运行 ==========
../JadeightCompiler/build/j8run demo.bc --externs externs.txt --lib ./libmylib.so
# 期望输出：42 / 8

# ========== 6) 或用启动器一键运行 ==========
mkdir -p byteCode lib
cp demo.bc byteCode/ && cp libmylib.so lib/ && cp externs.txt lib/
../JadeightPoject/run.sh
```

**要点回顾**：
- j8c 产 `.bc` 是 **LE 头**；独立 jasm 默认 **BE 头**——手工汇编/反汇编 j8c 产物都要 `-e`
- 清单必须与编译顺序一致，用 `-emit-externs` 生成、不要手改顺序
- `malloc/free/memcpy` 自动注册，无需声明即可用；纯 `.j8` 程序清单也会包含它们
- j8run 是当前唯一可直接运行 `.bc` 的入口；Jadeight2 独立二进制与 JadeightPoject/main.cpp（旧版）均不适用

---

## 与其他文档的关系

- 编译器细节（语言、优化器、代码生成、测试）：[04-j8c编译器.md](04-j8c编译器.md)
- jasm 语法与 .bc 字节码格式：[03-汇编器与字节码格式.md](03-汇编器与字节码格式.md)
- VM 机制（EXTERN_CALL/libffi、内存模型、调用约定）：[02-Jadeight2虚拟机.md](02-Jadeight2虚拟机.md) 与 [08-VM机制与字节码格式.md](08-VM机制与字节码格式.md)
- 启动器：[05-JadeightPoject启动器.md](05-JadeightPoject启动器.md)
- .j8 语言完整手册：[07-语言参考.md](07-语言参考.md)
