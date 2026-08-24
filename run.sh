#!/usr/bin/env bash
# run.sh — Linux 启动器：自动寻找 Jadeight 虚拟机，运行当前目录 byteCode/ 下的所有 .bc
# 与 launcher.cpp 行为一致。可用 JADEIGHT_VM / JADEIGHT_BC_DIR 环境变量覆盖。
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BC_DIR="${JADEIGHT_BC_DIR:-$PWD/byteCode}"

find_vm() {
    # 1) 环境变量
    if [ -n "${JADEIGHT_VM:-}" ] && [ -x "$JADEIGHT_VM" ]; then
        printf '%s\n' "$JADEIGHT_VM"; return 0
    fi
    # 2) 当前目录 / 脚本同目录 / 兄弟工程 / 系统目录
    local cands=(
        "$PWD/jadeight_vm"
        "$SCRIPT_DIR/jadeight_vm"
        "$SCRIPT_DIR/../JadeightCompiler/build/j8run"
        "$SCRIPT_DIR/../Jadeight2/cmake-build-debug/Jadeight2"
        "${HOME:-}/jadeight_vm"
        "/usr/local/bin/jadeight_vm"
        "/usr/bin/jadeight_vm"
    )
    local c
    for c in "${cands[@]}"; do
        [ -x "$c" ] && { printf '%s\n' "$c"; return 0; }
    done
    # 3) PATH
    local n p
    for n in jadeight_vm j8run Jadeight2; do
        p="$(command -v "$n" 2>/dev/null)" || continue
        [ -n "$p" ] && { printf '%s\n' "$p"; return 0; }
    done
    return 1
}

VM="$(find_vm)" || {
    echo "错误：找不到 Jadeight 虚拟机（可用 JADEIGHT_VM 指定，或复制为 ./jadeight_vm）" >&2
    exit 2
}
echo "Jadeight VM: $VM"

[ -d "$BC_DIR" ] || { echo "错误：找不到字节码目录：$BC_DIR" >&2; exit 3; }

failed=0
count=0
for bc in "$BC_DIR"/*.bc; do
    [ -f "$bc" ] || continue
    count=$((count + 1))
    echo
    echo "===== 运行 $(basename "$bc") ====="
    if ! "$VM" "$bc"; then
        echo "!! $(basename "$bc") 失败 (exit $?)" >&2
        failed=1
    fi
done

if [ "$count" -eq 0 ]; then
    echo "byteCode 目录为空：$BC_DIR"
fi
exit $failed
