#!/usr/bin/env bash
# run.sh — Linux 启动器：自动寻找 Jadeight 虚拟机，运行 byteCode/ 下的 .bc
# 与 launcher.cpp（JadeightRunner）行为一致。可用 JADEIGHT_VM / JADEIGHT_BC_DIR 环境变量覆盖。
#
# 用法: ./run.sh [选项] [文件.bc ...]
#   不带文件参数：运行 byteCode/ 下全部 .bc；带文件名：只运行指定的
#   选项: --vm PATH | --externs PATH | --lib PATH | --list | --stop-on-error | --quiet | -h/--help
set -u

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BC_DIR="${JADEIGHT_BC_DIR:-$PWD/byteCode}"
LIB_DIR="${JADEIGHT_LIB_DIR:-$PWD/lib}"

# ---------- 参数解析 ----------
VM_OVERRIDE=""
EXTERNS_OVERRIDE=""
LIBS=()
FILE_ARGS=()
LIST_ONLY=0
STOP_ON_ERROR=0
QUIET=0
while [ $# -gt 0 ]; do
    case "$1" in
        -h|--help)
            echo "用法: $0 [选项] [文件.bc ...]"
            echo "  不带文件参数：运行 byteCode/ 下全部 .bc；带文件名：只运行指定的（相对路径按 byteCode/ 解析）"
            echo "  选项:"
            echo "    --vm PATH          指定 VM 可执行文件（默认自动寻找，JADEIGHT_VM 优先）"
            echo "    --externs PATH     外部函数清单（默认 lib/externs.txt）"
            echo "    --lib PATH         预加载动态库，可多次（透传给 VM 的 --lib）"
            echo "    --list             只列出 byteCode/ 下的 .bc，不运行"
            echo "    --stop-on-error    遇到失败立即停止（默认跑完所有并汇总）"
            echo "    --quiet            不打印每条运行的横幅"
            echo "    -h, --help         显示本帮助"
            echo "  环境变量: JADEIGHT_VM / JADEIGHT_BC_DIR / JADEIGHT_LIB_DIR / JADEIGHT_EXTERNS"
            exit 0 ;;
        --list) LIST_ONLY=1 ;;
        --stop-on-error) STOP_ON_ERROR=1 ;;
        --quiet) QUIET=1 ;;
        --vm) shift; [ $# -ge 1 ] || { echo "--vm 需要参数" >&2; exit 2; }; VM_OVERRIDE="$1" ;;
        --externs) shift; [ $# -ge 1 ] || { echo "--externs 需要参数" >&2; exit 2; }; EXTERNS_OVERRIDE="$1" ;;
        --lib) shift; [ $# -ge 1 ] || { echo "--lib 需要参数" >&2; exit 2; }; LIBS+=("$1") ;;
        -*)
            echo "未知选项: $1（-h 查看帮助）" >&2; exit 2 ;;
        *) FILE_ARGS+=("$1") ;;
    esac
    shift
done

# ---------- 寻找 VM ----------
find_vm() {
    # 1) --vm / 环境变量
    if [ -n "${VM_OVERRIDE:-}" ]; then
        [ -x "$VM_OVERRIDE" ] || { echo "错误：--vm 指定的文件不可执行: $VM_OVERRIDE" >&2; exit 2; }
        printf '%s\n' "$VM_OVERRIDE"; return 0
    fi
    if [ -n "${JADEIGHT_VM:-}" ] && [ -x "$JADEIGHT_VM" ]; then
        printf '%s\n' "$JADEIGHT_VM"; return 0
    fi
    # 2) 当前目录 / 脚本同目录 / 兄弟工程 / 系统目录
    local cands=(
        "$PWD/jadeight_vm"
        "$SCRIPT_DIR/jadeight_vm"
        "$SCRIPT_DIR/../JadeightCompiler/build/j8run"
        "$SCRIPT_DIR/../Jadeight2/cmake-build-debug/Jadeight2"
        "$SCRIPT_DIR/../Jadeight2/build/Jadeight2"
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
    echo "错误：找不到 Jadeight 虚拟机（可用 JADEIGHT_VM 或 --vm 指定，或先构建："
    echo "  cd ../JadeightCompiler && cmake --build build --target j8run -j4）" >&2
    exit 2
}
[ "$QUIET" -eq 1 ] || echo "Jadeight VM: $VM"

# ---------- 外部函数清单 ----------
externs_args=()
if [ -n "${EXTERNS_OVERRIDE:-}" ]; then
    [ -f "$EXTERNS_OVERRIDE" ] || { echo "错误：找不到外部函数清单: $EXTERNS_OVERRIDE" >&2; exit 3; }
    externs_args=(--externs "$EXTERNS_OVERRIDE")
elif [ -n "${JADEIGHT_EXTERNS:-}" ] && [ -f "$JADEIGHT_EXTERNS" ]; then
    externs_args=(--externs "$JADEIGHT_EXTERNS")
elif [ -f "$LIB_DIR/externs.txt" ]; then
    externs_args=(--externs "$LIB_DIR/externs.txt")
fi
if [ "${#externs_args[@]}" -gt 0 ] && [ "$QUIET" -eq 0 ]; then
    echo "外部函数清单: ${externs_args[1]}"
fi

# ---------- 收集 .bc 文件 ----------
files=()
if [ "${#FILE_ARGS[@]}" -gt 0 ]; then
    for f in "${FILE_ARGS[@]}"; do
        case "$f" in
            /*) p="$f" ;;
            *) p="$BC_DIR/$f" ;;
        esac
        [ -f "$p" ] || { echo "错误：找不到字节码文件: $p" >&2; exit 3; }
        files+=("$p")
    done
elif [ -d "$BC_DIR" ]; then
    for f in "$BC_DIR"/*.bc; do
        [ -f "$f" ] && files+=("$f")
    done
    # 排序（与 launcher.cpp 一致）
    IFS=$'\n' files=($(printf '%s\n' "${files[@]}" | sort)); unset IFS
else
    echo "错误：找不到字节码目录：$BC_DIR" >&2
    exit 3
fi

# ---------- 列出 ----------
if [ "$LIST_ONLY" -eq 1 ]; then
    echo "byteCode/（${#files[@]} 个）:"
    for f in "${files[@]}"; do printf '  %s\n' "$(basename "$f")"; done
    exit 0
fi

[ "${#files[@]}" -gt 0 ] || { echo "byteCode 目录为空：$BC_DIR"; exit 0; }

# ---------- 运行 ----------
failed=0
for bc in "${files[@]}"; do
    if [ "$QUIET" -eq 0 ]; then echo; echo "===== 运行 $(basename "$bc") ====="; fi
    vm_args=("$VM" "$bc" "${externs_args[@]}")
    for l in "${LIBS[@]}"; do vm_args+=(--lib "$l"); done
    if ! "${vm_args[@]}"; then
        rc=$?
        echo "!! $(basename "$bc") 失败 (exit $rc)" >&2
        failed=1
        [ "$STOP_ON_ERROR" -eq 1 ] && break
    fi
done
if [ "$QUIET" -eq 0 ]; then
    echo
    echo "共 ${#files[@]} 个程序，$([ "$failed" -eq 1 ] && echo 存在失败 || echo 全部成功)"
fi
exit $failed
