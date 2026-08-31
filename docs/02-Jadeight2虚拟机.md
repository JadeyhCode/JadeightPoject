# Jadeight2 虚拟机（字节码解释器）技术文档

Jadeight2 是 Jadeight 生态的核心字节码虚拟机：一个用 C++20 实现的、追求极致速度的**栈式 + 寄存器混合字节码解释器**（内置快速模板 JIT 将字节码编译为原生码）。它从一份动画软件的附属运行时中独立出来（动画软件停在 v0.03，VM 当时已是 v2.10），如今是 j8c 编译产物与 jadeight_asm 汇编产物的唯一执行者。本 VM 的显著特点是：类型只在编译期确定（每个"运算×类型"组合是独立 opcode）、解释器不做任何越界检查（靠哨兵安全退出）、字节码构造后 mprotect 冻结为只读、经 libffi 原样暴露任意 C 函数。

---

## 1. 概述与定位

- **形态**：`Jadeight2/main.cpp` 单文件虚拟机（约 3200 行，含解释器、多线程执行器、VM 函数、libffi 外部调用、快速模板 JIT、演示程序）。
- **定位**：字节码解释器（bytecode interpreter），执行 `.bc` 字节码流。类型信息全部硬编码进 opcode，运行期解释器**没有"类型"概念**，只按每条指令自己的 `sizeof(T)` 读写字节。
- **宿主关系**：`int main()` 本身是一个**不带参数的手写字节码演示程序**；真正运行 `.bc` 文件的入口是 `j8run`（JadeightCompiler/runtime/j8run.cpp，通过 `#define main jadeight2_vm_main` + `#include "main.cpp"` 复用本 VM 的全部实现）。

### 1.1 版本史（来自 main.cpp 顶部注释）

| 版本 | 核心变更 |
|---|---|
| 前史 | 本是一份动画软件的附属运行时，后独立成虚拟机；动画软件版本 0.03 时 VM 已是 2.10 |
| **v2.00 类型全展开** | 移除全部 `memcpy`：字节码取数用模板 `rdBE/wrBE`（大端序手工拼字节，编译期展开）；栈/堆内存用 `loadRaw/storeRaw` 按宽度逐字节读写（对齐安全）；每个 `(运算, 类型)` 组合是独立 opcode（如 `ADD_U32`/`ADD_F64`/`SQRT_F64`），运行期不读任何类型字节；ptr 只能寻址（`ADD_PTR`/`SUB_PTR`，禁止乘除/开方/log，`LEA` 取地址）；两种寻址方式 mode0/mode1；分配器不关心类型（`NEW_ARRAY(size:u64, slotOff:u32)` 按 `uint8_t[]` 分配） |
| **v2.03** | REG 寄存器指令族（16 个 64 位寄存器 R0..R15；`MOVI`/`MOV`/`PUSH`/`POP`/`LOAD`/`STORE`）；系统地址 `GET_ADDRS` 与间接跳转 `JMP_IND`；多线程 `executoringHarness` + 原子指令 `ATOMIC_*`（共 10 条）；外部调用 `EXTERN_CALL`（libffi 原样暴露）；VM 函数 `FunctionSave` + `FUNC_CALL`；`MEMCPY` 指令；`collect_externs` 枚举动态链接库函数 |
| **v2.12** | 快速模板 JIT（`JadeightJIT::submit(save*)` → 原生函数指针，微秒级编译，同 save 地址缓存复用）；字节码只读冻结（构造完成后 mprotect 置只读，运行时任何写入触发 SIGSEGV）；`JIT_SUBMIT` 指令（客人程序可自行把任意 save 即时编译成原生函数） |

> 旧的 LLVM MCJIT 后端仍在源码中保留，但仅在 `-DJADEIGHT_WITH_LLVM=ON` 时编译，且运行期不经过 LLVM；默认 JIT 后端是快速模板 JIT（`fastjit.inc`）。

### 1.2 类型哲学

- 类型只在**编译期（汇编器）**确定：宽度与解释行为全部硬编码进 opcode。
- `CHAR8/16/32` 与 `U8/U16/U32` 位模式一致（v2.03 起只有 `COUT` 保留 char，按字符输出）。
- `ADD`/`SUB` 只保留 uint + float；`MUL`/`DIV` 覆盖全部 10 种数值类型；`<< & | ~` 只给 uint；`>>` 给 uint（逻辑）和 int（算术）各一套。

---

## 2. 架构

### 2.1 核心对象与职责

| 对象 | 职责 |
|---|---|
| `save` | **字节码容器**。mmap 页对齐分配（构造期可写），拷入字节码并追加 3 个 `OP_ERR_END` 哨兵后 `mprotect` 冻结为只读。`size = n + 3`（含哨兵）。拷贝构造同样走"拷贝 + 冻结"；移动构造原样交接不触碰映射 |
| `Manager` | **共享管理器**（多线程共享同一实例，宿主可自由扩展字段）。固定布局：偏移 0 `counter`（`std::atomic<uint64_t>`）、8 `magic`（'MANG'）、16 `hostFn`（宿主交给客人程序的 `FunctionSave*`）、24 `rwxBuf`（页对齐缓冲，mprotect 演示用）、32 `jitSave`、40 `jitFn`（JIT 演示用） |
| `DataSave` | **单次执行的 VM 状态**：`Stack`（uint8_t* 数据栈）、`stackSize`、`stackPtr`、`Regs[16]`（R0..R15，64 位，初始 0）、`manager`（shared_ptr）、`end`（0=运行中 / 1=正常退出 / 2=越界非法退出）、`silent`（1=函数执行模式，END/出错不打印）、`commandCode`、`count`（指令指针）、`zuoYongYv`（作用域深度）、`zuoYongYvStackPtr`（作用域基址栈）。不可拷贝、可移动 |
| `executoring` | **解释器本体**。持有一个 `DataSave* ptr`；`F8BFLRead(const save&)` 执行字节码。用 256 项跳转表 + computed goto（标签地址）做线程化分发；表先全填 `ITP_ERR_END` 再覆盖已定义指令 |
| `executoringHarness` | **多线程执行器**：每线程一份独立的 `DataSave`/`executoring`，跑同一份只读字节码，共享同一个 `Manager`（共有指针）。`runThreads(prog, n)` 启动，`join()` 收尾 |
| `FunctionSave` | **VM 函数对象**（save 类对象，save 一行未改）：内部持有一个 `save`（函数字节码）+ 独立的 `DataSave state` + `executoring engine` + `entry`/`argSize`/`retSize`。可从文件动态加载（`loadFromFile`）或从内存构造；默认构造 = 空函数（仅一条 `END`） |
| `externFn[256]` | **外部函数表**：`{ffi_cif*, 函数指针}`，宿主用 `ffi_prep_cif` 预生成后登记；`EXTERN_CALL` 直接读它 |
| `JadeightJIT` | **JIT 提交接口**。`submit(const save&)` → 原生函数指针（内部走快速模板 JIT，按字节码地址缓存复用）；旧 LLVM MCJIT 后端仅可选用，默认不编译 |

### 2.2 解释器执行模型

- 分发方式：computed goto（GNU 标签地址扩展）。`interpretedPTR[256]` 全部初始化为 `&&ITP_ERR_END`，再按 opcode 覆盖；主循环 `NEXT(countAdd)` 读下一条指令并 `goto *interpretedPTR[commandCode]`。
- 执行状态：`count` 是字节码偏移（指令指针）；`commandCode` 是当前 opcode。
- 退出状态 `end`：1 = 遇到 `END` 正常退出；2 = 读到哨兵/非法指令/未注册外部函数/空函数指针 → 越界退出。退出信息（"意外退出"/"正常退出"/"越界退出"）在 `silent==0` 时打印。
- **安全策略**：解释器内**不做任何越界检查**（追求极致速度）；字节码末尾追加 3 个 `OP_ERR_END` 哨兵，指令计数越界读到哨兵即跳 `ErrorEnd` 安全退出。
- **只读冻结**：save 构造完成后 `mprotect(PROT_READ)`；解释器、JIT 以及客人程序（经 `GET_ADDRS` 拿到的字节码指针）都只能读，运行时任何写入触发 SIGSEGV（main 里有 fork 子进程验证）。

### 2.3 多线程 executoringHarness

- 每个线程一份独立的 `DataSave`/`executoring`（各自独立的栈与寄存器），**共享同一个 `Manager`**。
- 跨线程同步靠 `GET_ADDRS`（拿到管理器地址，管理器偏移 0 就是 `counter`）＋ `ATOMIC_*` 原子指令（`std::atomic_ref` + `memory_order_seq_cst`）。
- 演示：4 个线程各 25 次 `ATOMIC_ADD_U64` 自增共享计数器，期望总和 100。

### 2.4 内存模型

**栈（Stack）**

- `STACK_INIT(stackSize:u32, scopeSize:u32)`：`Stack = new uint8_t[stackSize]`；作用域栈 `zuoYongYvStackPtr = new unsigned long long[scopeSize]`，`scope[0]=0`（作用域 0 基址 = 0）。
- `stackPtr` 从 0 **向上增长**：压栈 `pushT` 先写再 `sp += sizeof(T)`；弹栈 `popT` 先 `sp -= sizeof(T)` 再读。
- `STACK_PTR_MOVE(dec:u32)`：`stackPtr -= dec`；`NEW_STACK(bytes:u64)`：`stackPtr += bytes`（预留空间）。
- 函数调用时栈每次全新分配/释放（`ITP_END`/`ITP_ERR_END` 里 `delete[] Stack`）。

**作用域（SCOPE_BASE / SCOPE_PUSH / SCOPE_POP）**

- `SCOPE_PUSH`：把当前 `stackPtr` 记入作用域栈并深度 +1；`SCOPE_POP`：深度 -1 并把 `stackPtr` 恢复为 `scope[depth]`（**无越界检查**，下溢由调用方保证）。
- 所有带 `off` 的寻址都相对 `SCOPE_BASE = zuoYongYvStackPtr[zuoYongYv - 1]` 计算（即"当前作用域基址"）。JIT 版语义一致（`scope[depth-1]`）。
- **动态扩容（v2 补丁）**：`SCOPE_PUSH` 与 `NEW_STACK` 在原容量不足时自动扩容（仅对解释器
  `STACK_INIT` 自有的分配生效，JIT 路径不受影响），修复深递归时作用域栈/数据栈越界写导致的
  堆损坏（"corrupted size vs. prev_size"）。

**堆（分配器无类型，只认 size 和 where）**

- `NEW_ARRAY(size:u64, slotOff:u32)`：`new uint8_t[size]`，指针（8 字节）写入 `Stack[base+slotOff]` —— 只在乎多大、写哪。
- `FREE_ARRAY(slotOff:u32)`：从 `Stack[base+slotOff]` 读指针并 `delete[]`。
- `NEW_HEAP(slotOff:u32, size:u64)` / `DEL_HEAP(slotOff:u32)`：另一套等价分配/释放。

**寄存器 R0–R15**

- 16 个 64 位寄存器（初始 0），寄存器下标 1 字节、**低 4 位有效**（R0..R15），无越界检查。
- `REG_LOAD`/`REG_STORE` 的 mode 与 `LEA` 一致：mode0 = 直接读写 `Stack[base+off]`；mode1 = 先解引用栈槽里的指针再读写。

**寻址模式**

- mode0：直接读栈内 `[基址 + off]`；mode1：从栈槽读出指针（8 字节）再解引用读写 —— 可寻址堆、管理器等任意内存。

---

## 3. 指令集

opcode 为 1 字节（`enum : uint8_t`，取值 0..175）。`REG` 指令族刻意放在最前（opcode 最小 1..21），处理器代码也放解释器最前，利于指令缓存命中。各指令操作数宽度见下表"编码"列（1 字节 opcode 之后）。

### 3.1 基础 / 控制流

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 哨兵 | `OP_ERR_END`（0） | 1 | 字节码末尾 ×3 的哨兵；越界/非法指令读到即安全退出（end=2） |
| 基础 | `END`（22） | 1 | 正常退出（end=1；函数模式下 silent 抑制打印） |
| 基础 | `STACK_INIT`（23） | 1+4+4 | 初始化数据栈（stackSize）与作用域栈（scopeSize） |
| 控制流 | `JMP`（24） | 1+4 | 绝对地址跳转 `count = addr:u32` |
| 控制流 | `SHORT_JMP`（25） | 1+1 | 短相对跳转（i8：>0x7F 向后 `-(d-129)`，否则向前 `+d+2`） |
| 控制流 | `IF_GOTO`（32） | 1+1+4 | cond 字节 ≠0 顺序执行；==0 跳 addr |
| 控制流 | `JMP_IND`（160） | 1 | 间接跳转：弹栈 u64 **绝对地址**，减去字节码基址换算成偏移后跳转 |
| 作用域 | `SCOPE_PUSH`（26） | 1 | 记录当前 stackPtr，作用域深度 +1 |
| 作用域 | `SCOPE_POP`（27） | 1 | 作用域深度 -1，恢复 stackPtr |
| 栈 | `STACK_PTR_MOVE`（28） | 1+4 | `stackPtr -= dec:u32` |
| 栈 | `NEW_STACK`（29） | 1+8 | `stackPtr += bytes:u64`（预留空间） |
| 系统 | `GET_ADDRS`（159） | 1 | 一条指令依次压入 4 个 8 字节地址：字节码缓冲区地址、size 字段地址、DataSave 对象地址、管理器地址（管理器在栈顶） |

### 3.2 REG 寄存器指令族（opcode 1..21，热点）

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 立即数→寄存器 | `REG_MOVI_U8/U16/U32/U64`（1..4） | 1+1+W | 立即数写寄存器（零扩展） |
| 寄存器拷贝 | `REG_MOV`（5） | 1+1+1 | 寄存器间 64 位拷贝 |
| 寄存器→栈 | `REG_PUSH_U8/U16/U32/U64`（6..9） | 1+1 | 寄存器低 W 位压栈 |
| 栈→寄存器 | `REG_POP_U8/U16/U32/U64`（10..13） | 1+1 | 弹栈 W 字节写寄存器（零扩展） |
| 内存→寄存器 | `REG_LOAD_U8/U16/U32/U64`（14..17） | 1+1+1+8 | 内存→寄存器（mode 同 LEA） |
| 寄存器→内存 | `REG_STORE_U8/U16/U32/U64`（18..21） | 1+1+1+8 | 寄存器→内存（mode 同 LEA） |

### 3.3 立即数与寻址

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 立即数压栈 | `MOVI_U32`（34） | 1+4 | 压入 4 字节立即数（大端序）——当前常量入栈的唯一来源 |
| 寻址 | `LEA`（33） | 1+1+8 | mode0 压入栈内 `[base+off]` 的地址；mode1 压入栈槽里存的指针 |

### 3.4 算术运算（类型全展开；ADD/SUB 只 uint+float，MUL/DIV 全 10 种）

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 加法 | `ADD_U8/U16/U32/U64/F32/F64`（35..40） | 1 | 弹 b、弹 a，压 `a+b`（按各自宽度） |
| 减法 | `SUB_U8/U16/U32/U64/F32/F64`（41..46） | 1 | 压 `a-b` |
| 乘法 | `MUL_U8,I8,U16,I16,U32,I32,U64,I64,F32,F64`（47..56） | 1 | 压 `a*b`（10 种数值类型） |
| 除法 | `DIV_U8,I8,U16,I16,U32,I32,U64,I64,F32,F64`（57..66） | 1 | 压 `a/b`；整数除零得 0（浮点除零按 IEEE 得 inf/nan）；`INT_MIN/-1` 回绕为 a |
| 指针 | `ADD_PTR`（67）/ `SUB_PTR`（68） | 1 | ptr 只能寻址：指针加减偏移（弹 b、弹 a，压 `a±b`） |
| 数学函数 | `SQRT_<10类型>`（69..78）/ `LOG_<10类型>`（79..88） | 1 | 按 double 计算，结果一律 f64 压栈 |

### 3.5 输出 / 比较 / 位运算 / 转换

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 输出 | `COUT_CHAR8`（89） | 1 | 弹栈顶字节按**字符**打印（+换行） |
| 输出 | `COUT_CHAR16`（90）/ `COUT_CHAR32`（91） | 1 | 无 ostream 字符重载，按**数值**打印 |
| 比较 | `CMP_LT_*`（92..102）、`CMP_EQ_*`（103..113）、`CMP_GT_*`（114..124） | 1 | 除 char 外全部类型 + PTR；`<=`/`>=`/`!=` 由 `< = >` 与 `& | ~` 组合；结果统一压 U8 的 0/1 |
| 位运算 | `SHL_U8/U16/U32/U64`（125..128） | 1 | 左移（移位量按宽度取模，只 uint） |
| 位运算 | `AND_U8..U64`（129..132）/ `OR_U8..U64`（133..136）/ `NOT_U8..U64`（137..140） | 1 | 按位与/或/取反（只 uint） |
| 位运算 | `SHR_U8/U16/U32/U64`（141..144，逻辑右移）、`SHR_I8/I16/I32/I64`（145..148，算术右移） | 1 | 右移 |
| 类型转换 | `CVT_F32_U32/F32_I32/F64_U32/F64_I32/U32_F32/U32_F64/I32_F32/I32_F64`（149..156） | 1 | 浮点↔整数（只 32 位；越界/负数转整数的行为由平台转换指令决定） |

### 3.6 分配器 / 系统 / 原子 / 调用 / 拷贝

| 指令族 | 指令/操作码名 | 编码 | 用途 |
|---|---|---|---|
| 分配器 | `NEW_ARRAY`（157） | 1+8+4 | 按 `uint8_t[size]` 分配（不关心类型），指针写 `Stack[base+slotOff]` |
| 分配器 | `FREE_ARRAY`（158） | 1+4 | 从 `Stack[base+slotOff]` 读指针并 `delete[]` |
| 系统 | `GET_ADDRS`（159）/ `JMP_IND`（160） | 1 | 见 3.1 |
| 原子变量 | `ATOMIC_LOAD/STORE/XCHG/CAS/ADD_U32,U64`（161..170，共 10 条） | 1+1+1+8 | 原子变量操作（`std::atomic_ref` + seq_cst，目标需 4/8 字节自然对齐）。LOAD：reg=原子读；STORE：原子写 reg 低 W 位；XCHG：reg↔内存交换，reg=旧值；CAS：弹栈 expected，相等则内存=reg，reg=实际旧值并压 U8 成功标志；ADD：fetch_add，reg=旧值 |
| 外部调用 | `EXTERN_CALL`（171） | 1+1+4+4 | `(idx:u8, argBaseOff:u32, retOff:u32)`：从全局 externFn 表取 `{ffi_cif, 函数指针}`，把 `Stack[base+argBaseOff]` 起按 arg_types 顺序拆成 avalue[]（先 8 位再 16 位…），原样调用 `ffi_call`，返回值写 `Stack[base+retOff]` |
| VM 函数 | `FUNC_CALL`（172） | 1+4+4+4 | `(fnPtrOff, argBaseOff, retOff)`：从栈槽读 `FunctionSave*` 并以 argBaseOff 为参数基址、retOff 为返回值地址调用（约定：R15=参数基址、R14=返回值地址） |
| JIT | `JIT_SUBMIT`（173） | 1+4+4 | `(fnPtrOff, retOff)`：从栈槽读 `save*`，`JadeightJIT::submit` 即时编译（快速模板 JIT，同地址缓存复用），函数指针写回 retOff（8 字节） |
| 内存拷贝 | `MEMCPY`（174） | 1+1+8+1+8+8 | `(dstMode, dstOff, srcMode, srcOff, size)`：bulk 拷贝，语义同 C memcpy（重叠未定义）；mode0 = `Stack[base+off]` 本身，mode1 = 解引用栈槽指针 |
| 系统信息 | `GET_SYSTEM`（175） | 1 | 压入 1 个 u64 系统信息：低 16 位=平台（1=Linux 2=Windows 3=macOS 4=BSD 5=Other），次 16 位=架构（1=x86_64 2=arm64 3=riscv64 4=Other） |
| 动态注册 | `DL_REG`（176） | 1+4+4 | `(fnPtrOff, sigPtrOff)`：运行时注册 C 函数（签名串 `"rettype(argtypes)"`），压入 u32 索引；-1=失败 → 越界退出 |
| 动态调用 | `DL_CALL`（177） | 1+4+4+4 | `(idxOff, argBaseOff, retOff)`：按运行时索引调 C 函数（其余同 EXTERN_CALL） |

---

## 4. 字节码文件格式（.bc）

**内存容器 `save`**

- 字节码是**原始指令流**：1 字节 opcode + 定长操作数（大端序），逐指令线性排列。
- `save.size = 指令字节数 + 3`：末尾追加 3 个 `OP_ERR_END` 哨兵，供越界安全退出。
- 构造期（文件/内存加载）可写，构造完成后 mprotect 冻结为只读。

**文件格式（`FunctionSave::loadFromFile` / `saveToFile`）**

```
┌──────────────┬──────────────┬──────────────┬──────────────────┐
│ argSize:u32  │ retSize:u32  │ entry:u32    │ 函数字节码指令流  │
│ （小端 LE）   │ （小端 LE）   │ （小端 LE）   │ （大端序操作数）  │
└──────────────┴──────────────┴──────────────┴──────────────────┘
```

- **文件头 12 字节为小端序（LE）**：`[argSize:u32][retSize:u32][entry:u32]`，分别描述参数缓冲总字节、返回值字节数、入口偏移（loadFromFile 直接 `fread` 进本地 u32 数组，即本机小端）。
- **指令流内的立即数/偏移为大端序（BE）**：解释器用模板 `rdBE<T>`（读）与 `wrBE<T>`（写）手工拼字节，编译期展开、零函数调用。`GET_ADDRS`/`JMP_IND` 涉及的绝对地址是原始指针值（本机字节序）。
- 栈/堆内存读写用 `loadRaw`/`storeRaw`（小端位模式、按宽度逐字节、对齐安全）。
- j8c 与 jadeight_asm 产出的 `.bc` 即此格式（LE 头 + 字节码，FunctionSave 兼容）。

---

## 5. 调用约定

**参数传递（压栈式）**

- 调用前，调用方把参数按顺序连续写入自己栈内的参数缓冲（`Stack[base+argBaseOff]` 起，按类型宽度紧凑排布，无对齐填充）。
- `FUNC_CALL(fnPtrOff:u32, argBaseOff:u32, retOff:u32)`：从栈槽 fnPtrOff 读出 `FunctionSave*`，以 argBaseOff 为参数基址、retOff 为返回值地址发起调用。

**被调方视角（callFunctionSave 预置）**

- `R15 = 参数基址指针`（argPtr）；`R14 = 返回值地址指针`（retPtr）。
- 函数状态重置：`stackPtr=0`、`zuoYongYv=1`、`count=entry`、`end=0`、`silent=1`（函数执行模式，END/出错不打印退出信息）。
- 函数字节码以 `STACK_INIT` 开头（自建栈）、`END` 收尾；每次调用栈全新分配/释放（END/ERR_END 里 delete[]），无泄漏、无残留状态。

**递归支持（据源码行为）**

- 不同 `FunctionSave` 实例之间的嵌套调用（含相互递归）各自持有独立 `DataSave state` 与独立栈，正常工作。
- **同一 FunctionSave 实例的自递归不安全**：`callFunctionSave` 每次调用都会重置该函数自己的 `state`（stackPtr/count 等），会覆盖外层帧——源码注释与演示均未展示自递归用例，此点标注**待确认**（如需自递归，编译侧应每层生成独立 FunctionSave 实例）。

---

## 6. 外部能力

### 6.1 EXTERN_CALL：libffi 薄透传

- `externFn[256]` 全局表：`{ffi_cif*, 函数指针}`。宿主用 `ffi_prep_cif` 预生成 cif（存全局/栈）后登记（`regExtern`/`REG_FN` 宏）。
- `EXTERN_CALL` 只按 `arg_types` 顺序把参数缓冲拆成 `avalue[]`（上限 64 个参数），然后原样调用**原始 `ffi_call`** —— 零封装、不重实现。未注册下标 → 越界退出。
- 客人程序甚至可以调用 libffi 自身（演示注册了 `ffi_prep_cif`/`ffi_prep_cif_var`/`ffi_call`/`ffi_raw_*`/`ffi_java_*` 等）。

### 6.2 快速模板 JIT（默认后端，无 LLVM 依赖）

- 后端文件：`fastjit.inc`（被 `main.cpp` 包含）。CMake 默认不查找 LLVM；旧 LLVM MCJIT 后端仅当显式 `-DJADEIGHT_WITH_LLVM=ON` 时编译，运行期也不经过 LLVM。
- `JadeightJIT::submit(const save&)`：把 save 的字节码逐条 opcode 翻译成 **x86-64 原生指令**（编译微秒级），同字节码地址缓存复用（`fastjit::submit`），编译一次。
- 调用约定与 FUNC_CALL 一致：`void fn(uint8_t* argPtr, uint8_t* retPtr, void* manager)`。
- 数据栈/作用域栈/寄存器文件预分配在原生栈帧；复杂指令（`EXTERN_CALL`/`FUNC_CALL`/原子/COUT/堆分配等）调用 `main.cpp` 里的运行期辅助，行为与解释器逐字节一致。
- 约束：返回的函数指针只在该 save 存活期间有效（原生码捕获了字节码基址）。
- `JIT_SUBMIT` 指令让**客人程序**自行把任意 save 即时编译成原生函数指针。

### 6.3 dl（动态链接）

- `dlopen`/`dlsym`/`dlclose` 经 EXTERN_CALL 原样暴露给客人程序（`dlopen_std` 辅助处理客人伪造的 `std::string` 路径，并记录成功 dlopen 的库的加载基址）。
- `collect_externs(out, cap)`：用 `dl_iterate_phdr` 遍历已加载对象，只挑"刚刚 dlopen 过"的库（按加载基址匹配），解析其 `.dynsym`（DT_SYMTAB/DT_STRTAB/DT_HASH），把所有 `STT_FUNC`/`STT_GNU_IFUNC` 符号写成 16 字节条目 `{名字指针(8) + 运行地址(8)}` 写入客人缓冲区，返回条数。

---

## 7. 用法

**Jadeight2 独立二进制**：`int main()` **不接受任何命令行参数**，是一个手写字节码的演示程序，覆盖：

- 算术/比较/位运算/转换/指针寻址/COUT 输出；
- REG 寄存器族、GET_ADDRS、JMP_IND、原子指令冒烟测试；
- EXTERN_CALL（host_mix、libffi 自身、mprotect、dlopen/dlsym/ffi_call 调 strlen、collect_externs）；
- MEMCPY、FUNC_CALL（动态加载 fn_sum.bc 求和函数）、快速模板 JIT 对照运行；
- 字节码只读冻结验证（fork 子进程写冻结页 → SIGSEGV）；
- JIT_SUBMIT 指令、多线程 harness（4 线程 × 25 次原子自增 = 100）。

**运行 .bc 文件的入口是 j8run**（JadeightCompiler/runtime/j8run.cpp）：`#define main jadeight2_vm_main` 后 `#include "main.cpp"` 复用整套 VM 实现，再提供自己的 `main`：

```bash
j8run <main.bc> [--externs manifest.txt] [--lib lib.so]
```

- `main.bc`：j8c 编译 / jadeight_asm 汇编出的 `.bc`（FunctionSave 文件格式：LE 头 + 字节码）；
- `--externs`：外部函数清单（j8c `-emit-externs` 生成，每行 `name:ret(arg1,arg2,...)`），j8run 用 libffi 注册进 externFn 表；
- `--lib`：额外的共享库（缺省尝试 libc.so.6 与 RTLD_DEFAULT）。
- j8run 用 `FunctionSave::loadFromFile` 装载 .bc、给 state 装一个共享 Manager（GET_ADDRS 需要），再 `callFunctionSave(&prog, nullptr, nullptr)` 执行；`state.end == 2` 时报"abnormal exit"。

> 调试小工具：`DIV_U64` 指令在设置环境变量 `J8_VMTRACE` 时会向 stderr 打印 `DIV64 a=… b=…`（JadeightCompiler/tools/j8vm_traced.cpp 等基于它做跟踪）。

---

## 8. 与生态其他部分的关系

| 生态成员 | 与本 VM 的关系 |
|---|---|
| **JadeightCompiler（j8c）** | C 风格编译器 `.j8` → `.jasm` → `.bc`（driver.cpp 内嵌 `jadeight_asm.hpp` 一步出字节码）；`.bc` 就是本 VM 执行的 FunctionSave 格式（LE 头 + 指令流）；`-O2` 优化（常量折叠/传播、循环展开、DCE 等）产出的指令直接由本 VM 解释 |
| **JadeightAbstractionCode（jadeight_asm）** | 汇编层：jasm 文本 → 字节码；其产出的 `.bc` 由本 VM 执行；本 VM 的 opcode 枚举是汇编层编码的目标（类型全展开的语义即"汇编器确定类型"） |
| **j8run**（JadeightCompiler/runtime/j8run.cpp） | 不是独立 VM，而是宿主运行器：`#define main jadeight2_vm_main` + `#include "main.cpp"` 复用本 VM 全部实现（save/FunctionSave/DataSave/executoring/externFn），解释语义与 VM 完全同源；接受 `.bc` 路径参数，是**当前唯一可直接运行 .bc 的入口** |
| **JadeightPoject 启动器** | 自动寻找可运行 .bc 的 VM（本机命中 j8run），依次运行 `./byteCode/*.bc` |

---

## 与其他文档的关系

- **本 VM 是执行者**：执行 [j8c 编译器](../../JadeightCompiler)（`04-j8c编译器.md`）编译、[jadeight_asm 汇编器](../../JadeightAbstractionCode)（`03-汇编器与字节码格式.md`）汇编出的 `.bc` 字节码。
- **上游格式约定**：`.bc` 的文件布局（LE 头 `argSize/retSize/entry` + 大端操作数字节码）由汇编层定义，与本 VM `FunctionSave::loadFromFile/saveToFile` 兼容 —— 详见 `03-汇编器与字节码格式.md`。
- **下游入口**：`j8run` 复用本文件描述的 VM 实现来运行 .bc；`05-JadeightPoject启动器.md` 把"找 VM → 跑 .bc"串成最后一公里。
- 本文件（`02-Jadeight2虚拟机.md`）是 `01-总体架构.md` 的第二节，聚焦指令集、内存模型、调用约定与 .bc 格式本身。
## AI 使用情况声明

| 使用环节 | AI 工具 | 使用方式 | 人工核验 |
|---|---|---|---|
| 架构设计 | 未使用 AI | 字节码格式、opcode 编码方案、VM 执行模型都是人类原创设计 | — |
| 核心算法 | 未使用 AI | 类型全展开编译策略、FFI via libffi 的调用约定是人类的原创 | — |
| 代码编写 | Deepseek-V4-Flash | 帮忙补了指令分发的样板代码、动态扩容之类的补丁 | 生成完人逐行看过、测过（堆越界那类 bug 就是这么抓出来的） |
| 文档撰写 | Deepseek-V4-Flash | 初稿是 AI 写的 | 技术语义部分人重写了一遍，跟实现对齐了 |

