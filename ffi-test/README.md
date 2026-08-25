# ffi-test — FFI 调用 C++/外部库示例

验证 Jadeight 字节码经 EXTERN_CALL（libffi）调用外部 C/C++ 代码。

## 文件

- `cppshim.cpp` — C++ `extern "C"` 薄封装：`cpp_find`（std::string::find，用于校验解压数据完整性）
- `zr3.j8` — zlib `compress2`/`uncompress` 完整往返 + 数据校验（malloc/memcpy/free + `&局部变量`）

## 运行

```bash
g++ -shared -fPIC -O2 -std=c++20 cppshim.cpp -o libcppshim.so
../../JadeightCompiler/build/j8c -O0 -emit-externs zr3.txt -o zr3.bc zr3.j8
../../JadeightCompiler/build/j8run zr3.bc --externs zr3.txt \
    --lib /lib/x86_64-linux-gnu/libz.so --lib ./libcppshim.so
# 期望: rc=0 dstLen=23 rc2=0 outLen=24 cpp_find(world)=6
```

## 历史验证记录（当时全部通过，shim 已精简）

- C++ 标准库（std::string/std::map/std::sort/std::cmath 封装）输出全对
- libm 直调（sqrt/pow/floor/fabs/sin/cos）
- zlib compressBound(100)=113、compress2/uncompress 往返 + 数据校验
- 相关 extern 缺陷修复见 docs/04 的修复历史
