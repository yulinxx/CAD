#!/usr/bin/env python3
"""2D 算法分层依赖守卫（只读）。

规则来自 Docs/06-算法/2D/00-算法框架分层设计.md §1.1：

  L1 核心基础算法层：Engine/2D/{Include, Src}/Engine2D/Geometry 与 Algorithm/Discretizer
  L2 图元算法层    ：Engine/2D/{Include, Src}/Engine2D/Algorithm（Discretizer 除外）

  L1/L2 都不得 include：
    - Qt / UI / Main                    （上层）
    - SceneManager / Layer* / Undo*     （场景与图层写入）
    - SceneEditService / LayerEditService
    - Raster / Engraving / CAM / Nesting / Vision （平行或下游模块）
    - Engine2D/CAPI                     （稳定门面在算法层之上）

违反即返回非 0，供 configure / CI 作为门禁。存量例外必须写入 EXEMPT 并注明原因，
新代码不允许追加例外。
"""

from __future__ import annotations

import argparse
import re
import sys
from pathlib import Path

INCLUDE_RE = re.compile(r'^\s*#\s*include\s*[<"]([^>"]+)[>"]')

# 禁止出现的子串（命中任一即违规）
FORBIDDEN = (
    "Qt", "QtCore", "QtGui", "QtWidgets",
    "UI/", "UiEngineAccess/", "Main/",
    "SceneManager.h", "SceneNotifier.h", "SceneEditService.h",
    "LayerManager.h", "LayerEditService.h", "ILayerManager.h",
    "UndoRedoManager.h", "DocumentTransaction.h",
    "Raster/", "Engraving/", "CAM/", "Nesting/", "Vision/",
    "Engine2D/CAPI/",
)

# 存量例外（棘轮基线，2026-11-27）：格式 (相对路径, 违规 include, 原因)
# 只允许减少，不允许增加。每一条都是 P2 阶段「计算/写入分离」的待办，
# 不是认可这种依赖；新代码复制这些写法会被守卫直接拦下。
EXEMPT: tuple[tuple[str, str, str], ...] = ()


def layer_of(path: Path, root: Path) -> str | None:
    """把仓库相对路径映射到层级；不属于 L1/L2 的文件返回 None。"""
    rel = path.relative_to(root).as_posix()
    if not rel.startswith("Engine/2D/"):
        return None
    if "/Include/Engine2D/Geometry/" in rel or "/Src/Geometry/" in rel:
        return "L1"
    if "/Algorithm/Discretizer/" in rel:
        return "L1"
    # Writers/ 与 Integration/ 是 L4 场景写入/编排实现，故意允许依赖场景设施；
    # 它们不属于 L1/L2，不参与纯度检查。
    if "/Algorithm/Writers/" in rel or "/Algorithm/Integration/" in rel:
        return None
    if "/Include/Engine2D/Algorithm/" in rel or "/Src/Algorithm/" in rel:
        return "L2"
    return None


def scan(root: Path) -> list[tuple[str, str, str, str]]:
    violations: list[tuple[str, str, str, str]] = []
    exempt = {(p, inc) for p, inc, _ in EXEMPT}
    for base in (root / "Engine/2D/Include", root / "Engine/2D/Src"):
        if not base.exists():
            continue
        for path in sorted(base.rglob("*")):
            if path.suffix.lower() not in {".h", ".hpp", ".cpp", ".cxx", ".cc"}:
                continue
            layer = layer_of(path, root)
            if layer is None:
                continue
            rel = path.relative_to(root).as_posix()
            try:
                text = path.read_text(encoding="utf-8-sig")
            except UnicodeDecodeError:
                continue
            for lineno, line in enumerate(text.splitlines(), 1):
                m = INCLUDE_RE.match(line)
                if not m:
                    continue
                inc = m.group(1)
                if any(bad in inc for bad in FORBIDDEN) and (rel, inc) not in exempt:
                    violations.append((layer, rel, inc, f"{lineno}"))
    return violations


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "root", nargs="?", default=str(Path(__file__).resolve().parent.parent),
        help="仓库根目录（默认：脚本所在目录的上一级）",
    )
    parser.add_argument("--quiet", action="store_true", help="只输出违规行")
    args = parser.parse_args()
    root = Path(args.root).resolve()

    violations = scan(root)
    if not args.quiet:
        print(f"[2D-layer-deps] scanned Engine/2D L1/L2, violations={len(violations)}")
    for layer, rel, inc, lineno in violations:
        print(f"  ERROR [{layer}] {rel}:{lineno} includes forbidden '{inc}'")
    if exempt_note := [f"  EXEMPT {p} -> {inc}: {why}" for p, inc, why in EXEMPT]:
        if not args.quiet:
            print("\n".join(exempt_note))
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
