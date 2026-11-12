#!/usr/bin/env python3
"""DLL 导出符号基线比对（多个库共用一套规则）。

用途：快照动态库的全局已定义符号，并在后续构建上做增量比对。
规则：**只允许新增**。删除或改名一律视为破坏性变更，脚本以非零码退出。

为什么需要：DLL 的公开面在下游链接期才被使用，删一个导出，本模块自己照样
编译通过，受害方是"别人编不过"。等链接报错时往往已经跨了几个模块，很难
判断是谁删的。把符号集存成基线后，任何删除都会在比对这一步被点名。

覆盖范围（刻意只收有外部消费者的库，避免给无人链接的库制造噪音）：
    Renderx      —— 宿主经 RenderBridge 链接的 C ABI
    Engine2D     —— UI / Main 链接
    Engine3D     —— UI / Main 链接
    RenderBridge —— 主应用与测试链接

局限：C 函数的**参数/返回值变化**不会改变符号名，nm 看不出来（C++ 的签名
变化会改 mangle 名，能看出来）。这类改动必须配合公开头审查与 ABI 版本号
协商一起判断，本脚本只能保证「符号集合不缩水」。

用法：
    # 按库名自动探测产物，使用 Tools/Abi/baseline/<target>.txt
    python3 Tools/Abi/check_exports.py Renderx
    python3 Tools/Abi/check_exports.py Engine2D

    # 显式指定产物（多构建目录、打包前的正式产物）
    python3 Tools/Abi/check_exports.py Engine2D -l <path>/libEngine2D.dylib

    # 显式接受当前导出集（仅在该变更已被评审为向后兼容时执行）
    python3 Tools/Abi/check_exports.py Engine2D --update -l <path>/libEngine2D.dylib

    # 完全手动：不传库名，只给产物与基线文件
    python3 Tools/Abi/check_exports.py -l <dylib> -b <baseline.txt>

符号归一化：Mach-O 的符号带一个前导下划线（_rxRuntimeCreate / __ZN...），
ELF 不带（rxRuntimeCreate / _ZN...）。基线统一剥离一个前导下划线，保证同一份
文件跨平台可比。
"""

import argparse
import glob
import os
import subprocess
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.abspath(os.path.join(SCRIPT_DIR, "..", ".."))
BASELINE_DIR = os.path.join(SCRIPT_DIR, "baseline")

# 只登记有外部消费者的库：库名 -> 产物主文件名
TARGETS = {
    "Renderx": "libRenderX",
    "Engine2D": "libEngine2D",
    "Engine3D": "libEngine3D",
    "RenderBridge": "libRenderBridge",
}

# 自动探测构建产物时尝试的布局（根工程构建 + Renderx 独立构建）
LIBRARY_PATTERNS = [
    "build*/bin_Qt6/Release/{lib}.dylib",
    "build*/bin_Qt6/Debug/{lib}.dylib",
    "build*/bin_Qt6/Release/{lib}.so",
    "build*/bin_Qt6/Debug/{lib}.so",
    "Renderx/build*/bin_Qt6/Release/{lib}.dylib",
    "Renderx/build*/bin_Qt6/Debug/{lib}.dylib",
    "build*/{lib}.dylib",
    "build*/{lib}.so",
]


def find_library(target):
    """按常见布局探测目标库，取最新修改的一个。"""
    lib = TARGETS[target]
    candidates = []
    for pattern in LIBRARY_PATTERNS:
        candidates.extend(glob.glob(os.path.join(REPO_ROOT, pattern.format(lib=lib))))
    if not candidates:
        return None
    return max(candidates, key=os.path.getmtime)


def read_symbols(library):
    """用 nm 读取全局已定义符号，归一化为不带前导下划线的名字集合。"""
    if not os.path.isfile(library):
        sys.exit(f"[abi] library not found: {library}")

    if sys.platform == "darwin":
        # -g 只看外部符号，-U 排除未定义符号
        cmd = ["nm", "-gU", library]
    else:
        cmd = ["nm", "-D", "--defined-only", library]

    result = subprocess.run(cmd, capture_output=True, text=True)
    if result.returncode != 0:
        sys.exit(f"[abi] nm failed ({result.returncode}): {result.stderr.strip()}")

    symbols = set()
    for line in result.stdout.splitlines():
        fields = line.split()
        # 格式：<地址> <类型> <名字>；无地址的行（如归档成员头）跳过
        if len(fields) < 3:
            continue
        name = fields[-1]
        # Mach-O 的 C/C++ 符号统一带一个前导下划线，剥掉后与 ELF 对齐
        if name.startswith("_"):
            name = name[1:]
        if name:
            symbols.add(name)
    return symbols


def load_baseline(path):
    """读基线文件：跳过注释与空行。"""
    if not os.path.isfile(path):
        sys.exit(f"[abi] baseline not found: {path}\n[abi] create it with --update")
    symbols = set()
    with open(path, "r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if not line or line.startswith("#"):
                continue
            symbols.add(line)
    return symbols


def write_baseline(path, symbols, library):
    """写基线文件：固定的头注释 + 排序后的符号。"""
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "w", encoding="utf-8") as handle:
        handle.write("# 导出符号基线（nm 全局已定义符号，已剥离 Mach-O 前导下划线）\n")
        # 只记文件名，避免把生成者的本机绝对路径写进仓库
        handle.write(f"# 来源：{os.path.basename(library)}\n")
        handle.write("# 更新：python3 Tools/Abi/check_exports.py <target> --update --library <lib>\n")
        handle.write("# 规则：只允许新增；删除或改名视为破坏，必须先确认再显式更新本文件。\n")
        handle.write("# 注意：C 函数签名变化不会改变符号名，本文件查不出来，需配合公开头与 ABI 版本审查。\n")
        for name in sorted(symbols):
            handle.write(f"{name}\n")


def main():
    parser = argparse.ArgumentParser(description="Compare DLL exported symbols against a baseline.")
    parser.add_argument("target", nargs="?", choices=sorted(TARGETS), help="library name (Renderx/Engine2D/Engine3D/RenderBridge)")
    parser.add_argument("-l", "--library", help="path to the built library (auto-detected when omitted)")
    parser.add_argument("-b", "--baseline", help="baseline file (defaults to Tools/Abi/baseline/<target>.txt)")
    parser.add_argument("-u", "--update", action="store_true", help="rewrite the baseline from the library")
    args = parser.parse_args()

    if args.target:
        baseline_path = args.baseline or os.path.join(BASELINE_DIR, f"{args.target}.txt")
        library = args.library or find_library(args.target)
    else:
        if not (args.library and args.baseline):
            parser.error("pass a target name, or both --library and --baseline")
        baseline_path = args.baseline
        library = args.library

    if not library:
        sys.exit("[abi] cannot locate the library; pass --library explicitly")

    current = read_symbols(library)

    if args.update:
        write_baseline(baseline_path, current, library)
        print(f"[abi] baseline written: {baseline_path} ({len(current)} symbols)")
        return 0

    baseline = load_baseline(baseline_path)
    removed = sorted(baseline - current)
    added = sorted(current - baseline)

    for name in added:
        print(f"[abi] added (allowed): {name}")

    if removed:
        for name in removed:
            print(f"[abi] REMOVED: {name}")
        print(f"[abi] FAIL: {len(removed)} exported symbol(s) disappeared from {library}")
        return 1

    print(f"[abi] OK: {len(baseline)} baseline symbols present, {len(added)} added")
    return 0


if __name__ == "__main__":
    sys.exit(main())
