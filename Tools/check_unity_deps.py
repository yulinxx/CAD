#!/usr/bin/env python3
"""Unity 分块依赖盲区体检（只读）。

为什么需要这个体检：Ninja 对 unity 对象只有 ``unity_*.cxx`` 一条显式输入，
被 ``#include`` 进去的 ``.cpp`` 与头文件全靠 ``.ninja_deps`` 里的记录进入依赖图。
一旦某次编译把 deps 记成空（#deps 0），改这些 ``.cpp`` 就不会触发重编，
构建会静默判定 up-to-date —— 这正是 2026-11-05 螺旋填充修复没被编进去的原因。

本脚本只执行 ``ninja -t deps`` 与文件读取，绝不触发构建、不清理、不改写任何产物。
"""

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

DEP_HEADER_RE = re.compile(r"^(.+): #deps (\d+), deps mtime (\d+) \((VALID|STALE|MISSING)\)$")
INCLUDE_CPP_RE = re.compile(r'^\s*#include\s+"([^"]+\.cpp)"')

WIN_ABS_RE = re.compile(r"^[A-Za-z]:/")


def find_ninja(build_dir: Path):
    """优先用构建树自己记录的 ninja，其次找 PATH，都没有就返回 None。"""
    cache = build_dir / "CMakeCache.txt"
    if cache.is_file():
        try:
            for line in cache.read_text(encoding="utf-8", errors="replace").splitlines():
                if line.startswith("CMAKE_MAKE_PROGRAM:"):
                    candidate = line.split("=", 1)[1].strip().strip('"')
                    if candidate and Path(candidate).is_file():
                        return candidate
                    break
        except OSError:
            pass
    return shutil.which("ninja")


def normalize(raw: str, build_dir: Path) -> str:
    """把 ninja 里的相对/绝对路径统一成可比较的形式（处理 ..、斜杠、大小写）。"""
    text = raw.replace("\\", "/").strip()
    if WIN_ABS_RE.match(text) or text.startswith("/"):
        resolved = text
    else:
        resolved = os.path.join(str(build_dir), text)
    return os.path.normcase(os.path.normpath(resolved))


def read_deps(ninja: str, build_dir: Path):
    """导出 deps 记录，返回 {obj 相对路径: set(依赖绝对路径)}。"""
    proc = subprocess.run(
        [ninja, "-t", "deps"],
        cwd=str(build_dir),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    if proc.returncode != 0:
        raise RuntimeError("ninja -t deps 失败: " + proc.stderr.strip())

    result = {}
    current = None
    for line in proc.stdout.splitlines():
        match = DEP_HEADER_RE.match(line)
        if match:
            current = match.group(1).replace("\\", "/")
            result[current] = set()
            continue
        if current is not None and line.startswith(" ") and line.strip():
            result[current].add(normalize(line.strip(), build_dir))
        elif line.strip() == "":
            current = None
    return result


def unity_includes(unity_file: Path):
    """读取 unity 文件里全部 #include "*.cpp" 的路径。"""
    includes = []
    try:
        for line in unity_file.read_text(encoding="utf-8", errors="replace").splitlines():
            match = INCLUDE_CPP_RE.match(line)
            if match:
                includes.append(match.group(1).replace("\\", "/"))
    except OSError:
        pass
    return includes


def main() -> int:
    parser = argparse.ArgumentParser(description="检查 unity 分块的 ninja 依赖记录是否完整")
    parser.add_argument("--build-dir", default="build", help="构建树目录，默认 build")
    parser.add_argument("--ninja", default=None, help="ninja 可执行文件路径，默认从 CMakeCache/PATH 查找")
    parser.add_argument("--verbose", action="store_true", help="打印每个健康分块的 deps 条数")
    args = parser.parse_args()

    build_dir = Path(args.build_dir).resolve()
    if not build_dir.is_dir():
        print("[跳过] 构建树不存在: " + str(build_dir))
        return 0

    deps_file = build_dir / ".ninja_deps"
    if not deps_file.is_file():
        print("[跳过] 该构建树没有 .ninja_deps（多半是 VS/MSBuild 生成器，依赖跟踪走 .tlog）: " + str(build_dir))
        return 0

    ninja = args.ninja or find_ninja(build_dir)
    if not ninja:
        print("[跳过] 找不到 ninja（CMakeCache 的 CMAKE_MAKE_PROGRAM 与 PATH 都没有）")
        return 0

    try:
        deps = read_deps(ninja, build_dir)
    except RuntimeError as exc:
        print("[跳过] " + str(exc))
        return 0

    blind = []      # #deps 0 或无记录
    partial = []    # 记录存在但缺部分 .cpp
    healthy = 0
    skipped = 0

    unity_files = sorted(build_dir.rglob("unity_*_cxx.cxx"))
    for unity_file in unity_files:
        rel = unity_file.relative_to(build_dir).as_posix()
        obj_rel = rel + ".obj"
        if not (build_dir / obj_rel).is_file():
            skipped += 1
            continue

        includes = unity_includes(unity_file)
        recorded = deps.get(obj_rel)
        if recorded is None:
            blind.append((rel, -1, includes, includes, "无 deps 记录"))
            continue
        if len(recorded) == 0:
            blind.append((rel, 0, includes, includes, "#deps 0"))
            continue

        missing = [p for p in includes if normalize(p, build_dir) not in recorded]
        if missing:
            partial.append((rel, len(recorded), includes, missing, "缺 %d 个 .cpp" % len(missing)))
        else:
            healthy += 1
            if args.verbose:
                print("  健康 %s (#deps %d, include %d)" % (rel, len(recorded), len(includes)))

    total_built = healthy + len(blind) + len(partial)
    print("unity 分块统计: 已构建 %d（健康 %d / 盲区 %d / 部分缺失 %d），未构建跳过 %d"
          % (total_built, healthy, len(blind), len(partial), skipped))

    for title, rows in (("盲区", blind), ("部分缺失", partial)):
        if not rows:
            continue
        print("\n%s明细:" % title)
        for rel, count, includes, missing, note in rows:
            print("  - %s | deps=%s | include=%d | %s"
                  % (rel, "缺失" if count < 0 else count, len(includes), note))
            if missing:
                for item in missing:
                    print("      缺: " + item)

    if blind or partial:
        print("\n结论: 存在依赖记录盲区，改动上述 .cpp 不会触发对应 unity 分块重编。")
        print("处置: 删除对应 .obj 后重新构建，使其重新记录 deps（不要用 ninja -t clean 整树清理）。")
        return 1

    print("\n结论: 全部 unity 分块的 .cpp 依赖记录完整。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
