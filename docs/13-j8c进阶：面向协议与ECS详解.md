# 13. j8c 进阶：面向协议与 ECS 详解

> 本文是 [11-j8c语法精教程](11-j8c语法精教程.md) 第 8 课（面向协议）与第 9 课（ECS）的
> **深挖版**：每个关键词怎么用、底层发生了什么、会踩什么坑、会报什么错。
> 配套参考手册：[07-语言参考.md §8/§9/§10](07-语言参考.md)。
>
> 📌 **一切以实测为准**：本文所有示例都经 `build/j8c` + `build/j8run` 编译运行验证
> （`-O0` 与 `-O2` 输出一致），报错信息为编译器真实输出。
> 回归基线：`JadeightCompiler/tests/run_tests.sh` 18/18 全绿。

---

## 第一部分：面向协议（Protocol-Oriented）

### 第 1 课：协议解决什么问题

J8 没有继承。两个结构体要"有同一组行为"时，只能各写各的：

```j8
struct Circle { f64 r; }
struct Rect   { f64 w; f64 h; }

// 想算面积？Circle 写一份、Rect 写一份，调用方还得记住谁是谁
```

**协议 = 行为的契约**：它只约定"有哪些方法、签名是什么"，不碰数据。
任何结构体说"我遵守这个协议"（`impl`），就必须实现这些方法。
之后你就能**统一地**对待所有遵守协议的类型——这正是"面向协议"的核心。

---

### 第 2 课：`protocol` —— 声明协议

语法：

```
protocol 名字 {
    返回类型 方法名(this[, 参数...]) [; | { 默认实现 }]
}
```

**关键词详解：**

| 词 | 是什么 | 说明 |
|---|---|---|
| `protocol` | **关键字** | 声明一个协议（行为契约）。协议名进入类型表，可当类型用（第 5 课的存在类型盒） |
| `this` | **不是关键字** | 它是普通标识符，但**协议方法的第一个参数必须叫 `this`**（解析器按名字检查）。`this` 表示"调用者自己"，方法体里用 `this->字段` 访问调用者的字段 |
| `方法名(this)` | 方法声明 | 只写签名以 `;` 结尾 = "必须由 impl 实现"；写 `{ ... }` = 默认实现（第 3 课） |

**实测报错**：第一个参数不叫 `this` 时：

```
error:1:1: protocol method 'area' must take 'this' as first parameter
j8c: 语法分析失败
```

**示例**（`tests/t4_protocol.j8` 第 2-5 行）：

```j8
protocol Shape {
    f64 area(this);
    f64 perimeter(this) { return 0.0; }   // 默认实现（第 3 课）
}
```

---

### 第 3 课：默认实现 —— 方法体写在协议里

- **`f64 area(this);`** —— 只有声明：每个 `impl` 都必须自己实现，否则编译报错。
- **`f64 perimeter(this) { return 0.0; }`** —— 带默认实现：`impl` 可以不重写，直接继承协议里的方法体。

**实测**：默认实现里**可以访问 `this->字段`**（编译成每个 impl 的具体类型上下文）：

```j8
protocol P { f64 foo(this) { return this->w * 2.0; } }
struct S { f64 w; }
impl P for S { }                    // 什么都不写也行——用默认实现
void main() {
    S s = { 5.0 };
    print(s.foo());                 // 10.000（默认实现里 this->w = 5.0）
}
```

实现方想改行为，就在 `impl` 里**重写**同名方法（第 4 课示例的 `label`）。

---

### 第 4 课：`impl ... for ...` —— 让结构体遵守协议

语法：

```
impl 协议名 for 结构体名 {
    返回类型 方法名(this[, 参数...]) { ... }
}
```

**关键词详解：**

| 词 | 是什么 | 说明 |
|---|---|---|
| `impl` | **关键字** | 声明"实现块"：里面写协议方法的具体实现 |
| `for` | **关键字** | 连接协议名和结构体名：`impl P for S` = "结构体 S 实现协议 P" |

**一致性检查（实测）**：协议里无默认实现的方法，impl 必须全部提供，缺一个就报：

```
error:3:1: type 'S' does not implement protocol method 'P.per'
j8c: 语义分析失败
```

impl 里多写了协议没有的方法 → 仅警告，不报错。

**⚠️ 坑（实测）**：**重复 impl 同一个协议不报错**，后写的覆盖先写的（最后一次生效）：

```j8
impl P for S { f64 foo(this) { return 1.0; } }
impl P for S { f64 foo(this) { return 2.0; } }   // 生效的是这个
void main() { S s = { 0.0 }; print(s.foo()); }   // 2.000
```

别这么写——编译器不拦，但你自己会晕。

**完整示例**（`tests/t4_protocol.j8`，实测输出）：

```j8
protocol Shape {
    f64 area(this);
    f64 perimeter(this) { return 0.0; }   // 默认实现
    u32 sides(this);
}

struct Circle { f64 r; }
struct Rect   { f64 w; f64 h; }

impl Shape for Circle {
    f64 area(this) { return 3.14159 * this->r * this->r; }
    u32 sides(this) { return 0; }         // 圆无角
}

impl Shape for Rect {
    f64 area(this) { return this->w * this->h; }
    u32 sides(this) { return 4; }
}

void main() {
    Circle c = { 2.0 };
    Rect   r = { 3.0, 4.0 };
    print(c.area());        // 12.566（f64 打印固定 3 位小数，见附录 A）
    print(r.area());        // 12.000
    print(r.sides());       // 4
    print(c.perimeter());   // 0.000（Circle 没写 perimeter → 用默认实现）
}
```

---

### 第 5 课：静态派发与存在类型盒（动态派发）

**静态派发**：`c.area()` 这种"具体类型上的方法调用"，编译期就定死跳到
`impl_Shape_Circle_area`——快，零开销（实测 `tests/t4_protocol.j8`）。

**存在类型盒**：把具体结构体值赋给**协议类型变量**，得到 16 字节的"盒"
`{ 数据指针, witness 表指针 }`（witness 表 = 数据段里一张方法地址表，见 07 §8.4）：

```j8
Shape s1 = c;        // boxing：把 Circle 装进 Shape 盒
print(s1.area());    // 动态派发：查 witness 表找到 Circle.area → 12.566
```

**关键词/概念详解：**

| 词 | 是什么 |
|---|---|
| 存在类型盒 | 协议类型变量里装的"值 + 方法表"组合。编译期不知道具体类型，运行时查表 |
| 动态派发 | 通过 witness 表间接调用；比静态派发慢一点，但换来"一个变量装任意实现者" |

**盒可以拷贝**（实测）：

```j8
Shape box  = c;
Shape box2 = box;      // 盒拷贝
print(box2.area());    // 12.566
```

**盒上不能访问字段**（实测报错）：

```
error:4:21: protocol member 'w' must be called
j8c: 语义分析失败
```

**⚠️ 坑（实测，编译器 bug）**：**给已初始化的盒变量重新赋值一个具体结构体 → 段错误**：

```j8
Shape cur = s;     // ✓ 建盒
cur = s;           // ✗ 段错误！(j8run 退出码 139)
```

要"换一个对象"，用**新变量初始化**或**盒拷贝**，别往已有盒变量上赋值：

```j8
Shape cur  = s;    // ✓
Shape other = g;   // ✓ 新变量
Shape copy = cur;  // ✓ 盒拷贝
```

---

### 第 6 课：泛型约束 `<T: 协议>`

语法：`返回类型 函数名<T: 协议>(参数) { ... }`。约束 `T` 必须实现该协议，
函数体内就能对 `T` 的值调用协议方法（编译期按具体类型静态派发）。

```j8
f64 total_area<T: Shape>(T a, T b) {
    return a.area() + b.area();
}
void main() {
    Circle c = { 2.0 };
    print(total_area<Circle>(c, c));   // 25.132
}
```

**实测报错**：类型实参没实现协议时：

```
error:6:36: type 'T' does not conform to protocol 'P'
```

**注意**（07 §9.1）：当前版本**只支持单个泛型参数**，且调用必须显式写类型实参
`名<类型>(...)`。

---

### 第 7 课：综合 —— 用协议组织"敌人"

把"攻击、挨打、报血量"抽象成协议，史莱姆和哥布林各写各的实现，
上层逻辑（`threaten`）完全不关心具体是谁（实测输出 `7 12 9 17 7 12 7`）：

```j8
protocol Enemy {
    i32 hp(this);
    i32 atk(this);
    void hit(this, i32 dmg);
}

struct Slime  { i32 hp; }
struct Goblin { i32 hp; }

impl Enemy for Slime {
    i32 hp(this) { return this->hp; }
    i32 atk(this) { return 2; }
    void hit(this, i32 dmg) { this->hp = this->hp - dmg; }
}

impl Enemy for Goblin {
    i32 hp(this) { return this->hp; }
    i32 atk(this) { return 5; }
    void hit(this, i32 dmg) { this->hp = this->hp - dmg; }
}

i32 threaten<T: Enemy>(T e) { return e.hp() + e.atk(); }

void main() {
    Slime  s = { 10 };
    Goblin g = { 20 };
    s.hit(3);
    g.hit(8);
    print(s.hp());              // 7（静态派发）
    print(g.hp());              // 12（静态派发）
    print(threaten<Slime>(s));  // 9（泛型约束）
    print(threaten<Goblin>(g)); // 17（泛型约束）
    Enemy cur = s;              // 存在类型盒（动态派发）
    print(cur.hp());            // 7
    Enemy other = g;
    print(other.hp());          // 12
    Enemy copy = cur;           // 盒拷贝（安全；别用 cur = g 赋值，会段错误）
    print(copy.hp());           // 7
}
```

> 小技巧：协议方法名可以和字段名相同（`hp` 方法 vs `hp` 字段），实测不冲突。

---

## 第二部分：ECS 深挖

### 第 8 课：`component` —— 声明组件

**关键词详解：**

| 词 | 是什么 | 说明 |
|---|---|---|
| `component` | **关键字** | 声明一个组件（数据类型），语法和 `struct` 一样，但注册进"组件表"，参与 world 内存布局 |

```j8
component Pos { i32 x; i32 y; }     // 语法 = struct，语义 = 组件
component Vel { i32 vx; }
```

**内存真相**（07 §10.1）：所有组件活在 main 开头分配的一块堆数组 `world` 里，
对每个组件 C 有两段连续内存：

```
[0..4)   count（u32：实体数 = 下一个 spawn 下标）
[4..8)   cap（u32：容量，--ecs-capacity 默认 4096）
每个组件 C：
  [bitset 区] 每实体 1 字节 presence（0/1，占位标志）
  [数据区]    紧凑数组：实体 e 的数据在 dataOff + e * sizeof(C)
```

所以 `get<Pos>(e)` 就是一次**直接地址计算**，不是查表——这也是 ECS 快的根源。

---

### 第 9 课：`system` —— 声明系统

语法：

```
system 名字[: 组件名[, 组件名...]] {
    fn update(ent e) { ... }
}
```

**关键词详解：**

| 词 | 是什么 | 说明 |
|---|---|---|
| `system` | **关键字** | 声明一个系统：一个"处理一类实体"的函数集合 |
| `fn` | **关键字** | 系统里的方法声明。**当前版本只支持方法名 `update`**，其他名字报错（实测）：`system only supports 'fn update(ent e)' in this version` |
| 组件列表 | 冒号后 | **可省略**（实测：`system NoComp { fn update(ent e) {} }` 合法）；写了就表示"我只处理带这些组件的实体" |
| `ent e` | 参数 | 标准签名是 `fn update(ent e)`，e 是当前被处理的实体 |

**⚠️ 勘误（实测）**：07 §10.2 说 update 参数"必须是 ent（1 个）"——**不准确**。
实测 `fn update(u32 e)` 和 `fn update(ent e, u32 extra)` 都能编译运行
（多出的参数不会被 `run` 填充，值是未定义的）。**标准写法还是 `fn update(ent e)`**，
别依赖编译器的宽松。

```j8
component Pos { i32 x; i32 y; }
component Vel { i32 vx; }

system Physics : Pos, Vel {
    fn update(ent e) {
        get<Pos>(e)->x = get<Pos>(e)->x + get<Vel>(e)->vx;   // 写法一：get<T>
        get<Pos>(e)->y = get<Pos>(e)->y + get<Vel>(e)->vy;
    }
}
```

---

### 第 10 课：`ent` —— 实体句柄

**关键词详解：**

| 词 | 是什么 | 说明 |
|---|---|---|
| `ent` | **关键字**（类型） | 实体句柄。**本质就是一个数字（实体下标）**，`ent ↔ u32` 可隐式转换（07 §2.5） |

```j8
ent a = spawn();   // 正常：spawn 返回新实体
ent n = 7;         // 也行：直接写数字（实测通过）
print(n);          // 7
```

**⚠️ 坑（实测）**：ECS 内建的参数检查是**严格类型**检查，裸数字字面量不认：

```j8
print(alive(7));       // error: alive expects an entity
ent x = 7;
print(alive(x));       // ✓ 通过（x 是 ent 类型）
```

---

### 第 11 课：实体操作全表

| 调用 | 返回 | 语义（实测 + 07 §10.3） |
|---|---|---|
| `spawn()` | `ent` | 返回当前 count 作为新实体句柄，count+1。**满了**（count ≥ cap）打印 `world full` 并退出（文档所述） |
| `despawn(e)` | `void` | **只回收末尾实体**：若 `e+1 == count`，`count = e`；否则只是把 e 的所有组件 bitset 清 0，**count 不变，实体还"活着"** |
| `alive(e)` | `bool` | 实测就是 `e < count`（槽位检查，不是独立存活标志） |
| `entity_count()` | `u32` | 当前 count |

**实测 `despawn` 的"只回收末尾"语义**（`tests/t5_ecs.j8` + 第 15 课综合示例）：

```j8
ent a = spawn();              // a = 0, count = 1
ent b = spawn();              // b = 1, count = 2
despawn(a);                   // a 不是末尾（a+1 != 2）→ 只清组件
print(entity_count());        // 2（count 没变！）
print(alive(a));              // true（a 仍 < count，还"活着"）
```

要真正让 count 缩小，只能 despawn **最后一个**实体（或让过期对象尽量排在尾部）——
这是当前实现的一个已知限制，做对象池时要自己心里有数。

---

### 第 12 课：组件操作全表

| 调用 | 返回 | 语义（实测） |
|---|---|---|
| `add<C>(e, 字段值...)` | `void` | 置 bitset[e]=1，按字段顺序把值写进数据区；实参个数必须 = 1 + 字段数 |
| `remove<C>(e)` | `void` | bitset[e]=0（组件数据留在原地，只是"没有"了） |
| `has<C>(e)` | `bool` | bitset[e] |
| `get<C>(e)` | `C*` | 有组件 → 指向 e 的数据；**没有 → 空指针 0** |

**⚠️ 坑（实测）**：`get<C>(e)` 在实体没有该组件时返回**空指针**，
解引用**直接段错误**（j8run 退出码 139）。先 `has<C>(e)` 再 `get<C>(e)`：

```j8
if (has<Pos>(e)) { get<Pos>(e)->x = 1; }   // ✓ 安全
get<Pos>(e)->x = 1;                        // ✗ e 没有 Pos 时段错误
```

---

### 第 13 课：`run<S>()` —— 系统调度语义

`run<Physics>()` 遍历 `e ∈ [0, count)`，对**系统列出的每个组件 bitset 都为 1** 的实体
调用 `sys_Physics_update(e)`。**实测边界语义**（全部由 `tests/t9_ecs_run.j8` 覆盖）：

| 场景 | 实测行为 |
|---|---|
| 空 world | `run` 安全无副作用，count 不变 |
| 实体缺某个组件 | 不被处理（b 缺 Vel，x 不变） |
| **run 中 `spawn()`** | count 是**循环开始时的快照**，新实体**本轮不被处理**，下次 run 才轮到 |
| **嵌套 `run`** | update 里再调 `run<Other>()` 合法，每处理一个实体都会真执行一次内层 run |
| **run 中 `despawn(e)` 自己** | 末尾实体 → count 回收（本轮剩余迭代自动跳过越界实体）；非末尾 → 清 bitset，实体仍存活 |
| **`remove` 组件后** | 该实体立即不再被任何 run 处理 |
| update 里写 `get<C>(e)` | 与解释器普通函数调用约定一致，可放心读写组件 |

---

### 第 14 课：字段糖 `e.字段` 与无符号陷阱

系统里可以直接 `e.x`、`e.vx`（按系统组件列表解析到对应组件字段），不用写
`get<Pos>(e)->x`：

```j8
system Physics : Pos, Vel {
    fn update(ent e) {
        e.x = e.x + e.vx;    // e.x → Pos.x，e.vx → Vel.vx
    }
}
```

**限制（实测报错）：**

- 只能在 `system` 的 `update` 里用：外部写 `e.x = 1` 报
  `entity field access only allowed inside a system update`；
- 字段必须属于该系统列出的组件，否则报 `no component field 'x' in system 'S'`；
- 多个组件含同名字段 → 报 `ambiguous component field 'x'`（实测）。

**⚠️ 无符号陷阱（实测）**：字段是 `i32`，但**整数字面量默认是无符号**。
`e.life = e.life - 1` 按 `u32` 算：减到 -1 时存回 `i32` 字段值是 -1（截断回绕，值正确），
但**比较**就出事了——`e.life <= 0` 会被提升成无符号比较，-1 变成 4294967295，**永远不触发**：

```j8
system Expire : Bullet {
    fn update(ent e) {
        if (e.life <= 0) { despawn(e); }        // ✗ life 为 -1 时不触发（实测）
        if (e.life <= (i32)0) { despawn(e); }   // ✓ 强转成有符号比较（实测）
        if (e.life == 0) { despawn(e); }        // ✓ 倒计时到 0 更省心（推荐）
    }
}
```

---

### 第 15 课：综合 —— ECS 子弹系统（全实测）

三颗子弹各自带速度飞行，寿命递减，到期销毁。注意看 `despawn` 的两种结果
（实测输出：`3 0 3 false true 3 2 2 false`）：

```j8
component Bullet { i32 x; i32 y; i32 vx; i32 vy; i32 life; }

system BulletMove : Bullet {
    fn update(ent e) {
        e.x = e.x + e.vx;
        e.y = e.y + e.vy;
        e.life = e.life - 1;     // 减到 0 为止（判断用 == 0，见第 14 课）
    }
}

system BulletExpire : Bullet {
    fn update(ent e) {
        if (e.life == 0) { despawn(e); }
    }
}

void main() {
    ent a = spawn(); add<Bullet>(a, 0, 0, 3, 2, 4);     // life = 4
    ent b = spawn(); add<Bullet>(b, 10, 10, -1, 1, 1);  // life = 1
    ent c = spawn(); add<Bullet>(c, 100, 0, 0, 5, 2);   // life = 2

    run<BulletMove>();                // life: a=3, b=0, c=1
    print(get<Bullet>(a)->x);         // 3
    print(get<Bullet>(b)->life);      // 0
    print(entity_count());            // 3

    run<BulletExpire>();              // b 到期：b 非末尾 → 只清组件
    print(has<Bullet>(b));            // false
    print(alive(b));                  // true（b 非末尾，count 没回收）
    print(entity_count());            // 3

    run<BulletMove>();                // life: a=2, c=0
    print(get<Bullet>(a)->life);      // 2
    run<BulletExpire>();              // c 到期：c 是末尾 → count 回收
    print(entity_count());            // 2
    print(alive(c));                  // false
}
```

---

## 第三部分：关键词速查总表

### 本文涉及的关键字（全部来自 lexer.cpp 关键字表）

| 关键字 | 类别 | 一句话 |
|---|---|---|
| `protocol` | 协议 | 声明行为契约：`protocol P { 返回类型 名(this); }` |
| `impl` | 协议 | 声明实现块：`impl P for S { ... }` |
| `for` | 协议 | 连接协议与结构体：`impl P for S` |
| `component` | ECS | 声明组件（数据），进 world 布局 |
| `system` | ECS | 声明系统（行为）：`system S : C1, C2 { fn update(ent e) {...} }` |
| `fn` | ECS | 系统方法声明，当前只支持 `update` |
| `ent` | ECS | 实体句柄类型，本质是 u32 下标 |
| `sizeof` | 其他 | 大小运算符（07 §2.7），结构体/类型大小 |

**`this` 不是关键字**——它是普通标识符，但协议方法的第一个参数必须叫这个名字。

### ECS 内建函数（全部经 sema `checkEcsCall` 检查）

| 调用 | 作用 | 注意 |
|---|---|---|
| `spawn()` | 创建实体 | 满容量打印 `world full` 退出 |
| `despawn(e)` | 销毁实体 | **只回收末尾实体**；非末尾只清组件 |
| `alive(e)` | 是否存活 | 即 `e < count`；参数必须显式 `ent` 类型 |
| `entity_count()` | 实体数 | 即 count |
| `add<C>(e, 值...)` | 挂组件 | 参数个数 = 1 + 字段数 |
| `remove<C>(e)` | 摘组件 | 只清 bitset |
| `has<C>(e)` | 查组件 | bool |
| `get<C>(e)` | 取组件指针 | 没有时返回空指针，解引用段错误 |
| `run<S>()` | 跑系统 | 快照遍历 + bitset 过滤 |

### 相关保留字（07 §1.3 实测确认）

| 关键字 | 真相 |
|---|---|
| `world` | **保留关键字，当前无语法支持**（parser 无对应规则） |
| `delete` | **保留关键字，当前无语法支持** |
| `const` | 能解析（`const u32 x = 5;`），但**没有任何地方强制只读**——`x = 6;` 编译通过 |
| `new` | 走自动注册的 `malloc` extern；本环境实测经 libffi 调 malloc 会段错误（07 §12，待确认） |

### j8c 相关命令行选项（`j8c --help` 实测）

| 选项 | 说明 |
|---|---|
| `--ecs-capacity N` | ECS world 实体容量（默认 4096） |
| `--stack N` | 覆盖 VM 栈大小（字节） |
| `-O0 / -O1 / -O2` | 优化级别（`-O2` 为实验性/不稳定级别，`-O0` 为可信基线） |

---

## 附录 A：实测记录与勘误

**实测环境**：`JadeightCompiler/build/j8c` + `build/j8run`；本文所有示例均在
`-O0` 与 `-O2` 下输出一致。测试脚本剥离 j8run 第一行横幅
（`===== j8run: xxx.bc =====`，见 `tests/run_tests.sh` 的 `tail -n +2`）。

**f64 打印**：固定 **3 位小数且截断**（不是四舍五入）——`25.13272` 打印 `25.132`。
负数逐字符打印含负号（`-1` 打印为 `-` 和 `1` 两行）。

**对既有文档的勘误：**

1. **07 §10.2**："update 参数必须是 ent（1 个）"——不准确。实测 `fn update(u32 e)`
   与两参数 `fn update(ent e, u32 extra)` 均编译运行通过（多出参数值未定义）。
   标准写法仍是 `fn update(ent e)`。
2. **tests/t4_protocol.j8 注释**：`print(c.area()); // 12.566360` —— 实际输出
   `12.566`（f64 3 位截断），注释应改为 `12.566`。

**已知编译器 bug / 限制（均实测）：**

| 现象 | 详情 | 绕过 |
|---|---|---|
| 盒变量重新赋值段错误 | `Shape cur = s; cur = s;` 第二次赋值 j8run 段错误（139） | 用新变量初始化或盒拷贝 |
| 重复 impl 不报错 | 后写的覆盖先写的（最后一次生效） | 别重复 impl |
| `despawn` 只回收末尾 | 非末尾实体只清组件，count 不变 | 自己管理 spawn/despawn 顺序 |
| `get` 无组件返回空指针 | 解引用段错误 | 先 `has<C>(e)` 再 `get` |
| update 签名不检查 | 参数类型/个数宽松 | 标准写法 `fn update(ent e)` |

---

## 附录 B：还想更深？

- **world 内存布局**：07 §10.1（bitset 区 + 紧凑数据区的字节级说明）；
- **run 的实现**：`JadeightCompiler/src/codegen2.inc` 的 `emitEcs`（run 分支：
  count 快照 → 逐实体查 bitset → `SCOPE_PUSH` + 动态跳转调用 `sys_S_update`）；
- **witness 表 / 动态派发**：07 §8.4 + `codegen.cpp` 的 `emitDataSection`；
- **边界测试**：`JadeightCompiler/tests/t9_ecs_run.j8`（空 world / 快照 / 嵌套 run /
  run 中 despawn / remove 后不处理 / 批量遍历，18/18 全绿）。
