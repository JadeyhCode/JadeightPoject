#!/usr/bin/env bash
# build.sh — 编译 launcher.cpp 为单个可执行文件 JadeightRunner
# Mac 上默认 clang++，Linux 上默认 g++；可用 CXX 环境变量覆盖。
# 用法: ./build.sh [--debug]
set -e
cd "$(dirname "$0")"
CXX="${CXX:-c++}"

mode="-O2"
for arg in "$@"; do
    case "$arg" in
        --debug) mode="-O0 -g" ;;
        -h|--help) echo "用法: $0 [--debug]（CXX 环境变量可覆盖编译器）"; exit 0 ;;
        *) echo "未知选项: $arg（-h 查看帮助）" >&2; exit 2 ;;
    esac
done

"$CXX" -std=c++20 $mode -Wall launcher.cpp -o JadeightRunner
echo "生成: $(pwd)/JadeightRunner"
