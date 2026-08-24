#!/usr/bin/env bash
# build.sh — 编译 launcher.cpp 为单个可执行文件 JadeightRunner
# Mac 上默认 clang++，Linux 上默认 g++；可用 CXX 环境变量覆盖。
set -e
cd "$(dirname "$0")"
CXX="${CXX:-c++}"
"$CXX" -std=c++20 -O2 -Wall launcher.cpp -o JadeightRunner
echo "生成: $(pwd)/JadeightRunner"
