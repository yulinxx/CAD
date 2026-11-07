#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
Docs 链接与过期检查。

检查内容：
  1. Markdown 相对链接/图片是否指向存在的文件；
  2. 可选检查链接锚点是否存在于目标 Markdown 的标题中；
  3. 基于 git 最后提交日期和文档内的“状态说明（YYYY-MM-DD）”做过期提醒；
  4. 可选检查 Docs 下的文档是否至少被一个索引文档链接。

设计原则：
  - 只依赖 Python 标准库和 git，不引入额外包；
  - 默认只把“断链”当错误；“过期/未入索引”默认只告警，避免存量文档一次性卡死 CI；
  - 需要严格模式时显式加 --fail-on-stale / --fail-on-unindexed。

用法：
    python3 Tools/docs_check.py
    python3 Tools/docs_check.py --check-anchors
    python3 Tools/docs_check.py --fail-on-stale --max-age-days 365 --review-max-age-days 180
"""

from __future__ import annotations

import argparse
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import date, datetime
from pathlib import Path
from typing import Dict, Iterable, List, Optional, Sequence, Set, Tuple
from urllib.parse import unquote, urlsplit


MARKDOWN_SUFFIXES = {".md", ".markdown"}
EXTERNAL_SCHEMES = {"http", "https", "mailto", "tel", "ftp", "ftps", "data"}

# [text](target) / ![alt](target)
LINK_RE = re.compile(r"(!?)\[([^\]\n]*)\]\(([^)\n]*)\)")
# [label]: target
REF_DEF_RE = re.compile(r"^\s{0,3}\[([^\]]+)\]:\s*(\S+)(?:\s+.*)?$")
# <a href="..."> / <img src="...">
HTML_LINK_RE = re.compile(r"""(?:href|src)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
# 标题（用于锚点检查）
HEADING_RE = re.compile(r"^(#{1,6})\s+(.+?)\s*#*\s*$")
# 显式锚点：<a name="x"></a> / id="x"
EXPLICIT_ANCHOR_RE = re.compile(r"""(?:name|id)\s*=\s*["']([^"']+)["']""", re.IGNORECASE)
# 状态说明（2026-09-18）
STATUS_DATE_RE = re.compile(r"状态说明\s*[（(]\s*(\d{4}-\d{2}-\d{2})\s*[）)]")
# front matter 中的日期字段
FRONT_MATTER_DATE_RE = re.compile(
    r"^\s*(?:last_reviewed|last_updated|reviewed|updated)\s*:\s*[\"']?(\d{4}-\d{2}-\d{2})",
    re.IGNORECASE | re.MULTILINE,
)
FENCE_RE = re.compile(r"^\s*(```+|~~~+)")


@dataclass
class CheckResult:
    errors: List[str] = field(default_factory=list)
    warnings: List[str] = field(default_factory=list)
    checked_files: int = 0
    checked_links: int = 0
    skipped_links: int = 0

    def add_error(self, message: str) -> None:
        self.errors.append(message)

    def add_warning(self, message: str) -> None:
        self.warnings.append(message)


@dataclass
class Link:
    source: Path
    line: int
    target: str


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="检查 Docs 的 Markdown 链接和文档过期情况。"
    )
    parser.add_argument(
        "--root",
        default=".",
        help="仓库根目录（默认当前目录）",
    )
    parser.add_argument(
        "--docs-dir",
        default="Docs",
        help="文档目录，相对仓库根目录（默认 Docs）",
    )
    parser.add_argument(
        "--max-age-days",
        type=int,
        default=365,
        help="git 最后修改超过该天数时告警（默认 365，0 表示关闭）",
    )
    parser.add_argument(
        "--review-max-age-days",
        type=int,
        default=180,
        help="文档内状态日期超过该天数时告警（默认 180，0 表示关闭）",
    )
    parser.add_argument(
        "--fail-on-stale",
        action="store_true",
        help="存在过期文档时返回非 0（默认只告警）",
    )
    parser.add_argument(
        "--fail-on-unindexed",
        action="store_true",
        help="存在未入索引文档时返回非 0（默认只告警）",
    )
    parser.add_argument(
        "--check-anchors",
        action="store_true",
        help="同时检查链接 #anchor 是否存在于目标 Markdown",
    )
    parser.add_argument(
        "--no-index-check",
        action="store_true",
        help="跳过“文档是否被索引链接”检查",
    )
    parser.add_argument(
        "--as-of",
        default=None,
        help="按指定日期计算过期（YYYY-MM-DD，默认今天；用于可重复测试）",
    )
    parser.add_argument(
        "--quiet",
        action="store_true",
        help="只输出错误和最终统计",
    )
    return parser.parse_args(argv)


def is_markdown(path: Path) -> bool:
    return path.suffix.lower() in MARKDOWN_SUFFIXES


def read_text(path: Path) -> str:
    try:
        return path.read_text(encoding="utf-8", errors="replace")
    except OSError as exc:
        return f"<!-- read error: {exc} -->"


def strip_html_comments(text: str) -> str:
    return re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)


def strip_inline_code(line: str) -> str:
    # 只去掉成对的 inline code，避免把 `[x](y)` 当成链接
    return re.sub(r"`[^`\n]*`", "", line)


def iter_markdown_lines(text: str) -> Iterable[Tuple[int, str]]:
    """逐行返回非代码块内容，保留原始行号。"""
    in_fence = False
    fence_marker = ""
    for lineno, line in enumerate(text.splitlines(), start=1):
        match = FENCE_RE.match(line)
        if match:
            marker = match.group(1)
            if not in_fence:
                in_fence = True
                fence_marker = marker
                continue
            if marker.startswith(fence_marker[0]) and len(marker) >= len(fence_marker):
                in_fence = False
                fence_marker = ""
                continue
        if not in_fence:
            yield lineno, line


def parse_link_target(raw: str) -> str:
    """从 Markdown 链接括号内容中取出目标，尽量兼容 <...> 和 title。"""
    raw = raw.strip()
    if not raw:
        return ""

    if raw.startswith("<"):
        end = raw.find(">")
        if end != -1:
            return raw[1:end].strip()

    # 常见形式：path "title" / path 'title' / path (title)
    match = re.match(r"^(\S+)\s+(?:[\"'(]).*$", raw)
    if match:
        return match.group(1)
    return raw


def normalize_label(label: str) -> str:
    return " ".join(label.strip().lower().split())


def collect_links(path: Path, text: str) -> Tuple[List[Link], List[Link]]:
    """返回 (普通链接, 引用定义)。"""
    links: List[Link] = []
    ref_defs: List[Link] = []
    text = strip_html_comments(text)

    for lineno, line in iter_markdown_lines(text):
        cleaned = strip_inline_code(line)

        for match in LINK_RE.finditer(cleaned):
            target = parse_link_target(match.group(3))
            if target:
                links.append(Link(path, lineno, target))

        for match in HTML_LINK_RE.finditer(cleaned):
            target = match.group(1).strip()
            if target:
                links.append(Link(path, lineno, target))

        ref_match = REF_DEF_RE.match(cleaned)
        if ref_match:
            target = parse_link_target(ref_match.group(2))
            if target:
                ref_defs.append(Link(path, lineno, target))

    return links, ref_defs


def github_slug(text: str) -> str:
    """近似 GitHub 标题锚点：小写、去标点、空格转 -，保留中文。"""
    text = re.sub(r"<[^>]+>", "", text)
    text = re.sub(r"[`*_~]", "", text)
    text = text.strip().lower()
    text = re.sub(r"[^\w\u4e00-\u9fff\- ]", "", text, flags=re.UNICODE)
    text = re.sub(r"\s+", "-", text)
    return text.strip("-")


def collect_anchors(path: Path) -> Set[str]:
    anchors: Set[str] = set()
    seen: Dict[str, int] = {}
    text = strip_html_comments(read_text(path))

    for _, line in iter_markdown_lines(text):
        match = HEADING_RE.match(line)
        if match:
            base = github_slug(match.group(2))
            if not base:
                continue
            count = seen.get(base, 0)
            anchors.add(base if count == 0 else f"{base}-{count}")
            seen[base] = count + 1

        for match in EXPLICIT_ANCHOR_RE.finditer(line):
            anchors.add(match.group(1).strip().lower())

    return anchors


def split_target(target: str) -> Tuple[str, str]:
    """拆分 path#anchor。"""
    if "#" in target:
        path_part, anchor = target.split("#", 1)
        return path_part, anchor
    return target, ""


def is_machine_absolute_target(target: str) -> bool:
    """历史文档里可能有 C:\\Users\\... 这类机器路径，无法在仓库内校验。"""
    return bool(re.match(r"^[A-Za-z]:[\\/]", target)) or target.startswith("\\\\")


def is_external(target: str) -> bool:
    if target.startswith("//"):
        return True
    split = urlsplit(target)
    if split.scheme and split.scheme.lower() in EXTERNAL_SCHEMES:
        return True
    if is_machine_absolute_target(target):
        return True
    return bool(split.scheme) and not target.startswith("#")


def resolve_local_path(source: Path, target_path: str, root: Path) -> Path:
    target_path = unquote(target_path.strip())
    if target_path.startswith("/"):
        candidate = (root / target_path.lstrip("/")).resolve()
    else:
        candidate = (source.parent / target_path).resolve()

    # 目录链接允许指向目录；其余情况要求文件存在
    if candidate.exists():
        return candidate

    # 兼容省略 .md 的链接
    if not candidate.suffix:
        with_md = candidate.with_suffix(".md")
        if with_md.exists():
            return with_md

    return candidate


def check_links(
    files: Sequence[Path],
    root: Path,
    result: CheckResult,
    check_anchors: bool,
    quiet: bool,
) -> None:
    anchor_cache: Dict[Path, Set[str]] = {}

    for path in files:
        text = read_text(path)
        links, ref_defs = collect_links(path, text)
        all_links = links + ref_defs

        for link in all_links:
            target = link.target.strip()
            if not target:
                result.skipped_links += 1
                continue
            if target.startswith("#"):
                if check_anchors:
                    if path not in anchor_cache:
                        anchor_cache[path] = collect_anchors(path)
                    anchor = unquote(target[1:]).lower()
                    result.checked_links += 1
                    if anchor and anchor not in anchor_cache[path]:
                        result.add_error(
                            f"{display_path(path, root)}:{link.line}: "
                            f"锚点不存在 #{anchor}"
                        )
                else:
                    result.skipped_links += 1
                continue
            if is_external(target):
                result.skipped_links += 1
                continue

            target_path, anchor = split_target(target)
            if not target_path:
                result.skipped_links += 1
                continue

            resolved = resolve_local_path(path, target_path, root)
            result.checked_links += 1

            if not resolved.exists():
                result.add_error(
                    f"{display_path(path, root)}:{link.line}: "
                    f"链接目标不存在: {target} -> {resolved}"
                )
                continue

            if check_anchors and anchor and resolved.is_file() and is_markdown(resolved):
                if resolved not in anchor_cache:
                    anchor_cache[resolved] = collect_anchors(resolved)
                normalized = unquote(anchor).lower()
                if normalized and normalized not in anchor_cache[resolved]:
                    result.add_error(
                        f"{display_path(path, root)}:{link.line}: "
                        f"目标文件存在但锚点不存在: {target}"
                    )


def display_path(path: Path, root: Path) -> str:
    try:
        return path.resolve().relative_to(root).as_posix()
    except ValueError:
        return path.as_posix()


def git_last_commit_date(root: Path, path: Path, cache: Dict[Path, Optional[date]]) -> Optional[date]:
    if path in cache:
        return cache[path]

    try:
        relative = path.resolve().relative_to(root).as_posix()
    except ValueError:
        cache[path] = None
        return None

    try:
        completed = subprocess.run(
            ["git", "-C", str(root), "log", "-1", "--format=%cs", "--", relative],
            check=False,
            stdout=subprocess.PIPE,
            stderr=subprocess.DEVNULL,
            text=True,
            encoding="utf-8",
        )
        value = completed.stdout.strip()
        parsed = datetime.strptime(value, "%Y-%m-%d").date() if value else None
    except (OSError, ValueError):
        parsed = None

    cache[path] = parsed
    return parsed


def metadata_date(text: str) -> Optional[date]:
    dates: List[date] = []

    status_match = STATUS_DATE_RE.search(text)
    if status_match:
        try:
            dates.append(datetime.strptime(status_match.group(1), "%Y-%m-%d").date())
        except ValueError:
            pass

    for match in FRONT_MATTER_DATE_RE.finditer(text):
        try:
            dates.append(datetime.strptime(match.group(1), "%Y-%m-%d").date())
        except ValueError:
            pass

    return max(dates) if dates else None


def check_staleness(
    files: Sequence[Path],
    root: Path,
    result: CheckResult,
    as_of: date,
    max_age_days: int,
    review_max_age_days: int,
    quiet: bool,
) -> None:
    git_cache: Dict[Path, Optional[date]] = {}

    for path in files:
        text = read_text(path)
        review_date = metadata_date(text)
        git_date = git_last_commit_date(root, path, git_cache)

        if review_max_age_days > 0 and review_date is not None:
            age = (as_of - review_date).days
            if age > review_max_age_days:
                result.add_warning(
                    f"{display_path(path, root)}: 状态日期 {review_date.isoformat()} "
                    f"距今 {age} 天，超过复核阈值 {review_max_age_days} 天"
                )

        if max_age_days > 0 and git_date is not None:
            age = (as_of - git_date).days
            if age > max_age_days:
                result.add_warning(
                    f"{display_path(path, root)}: 最后修改 {git_date.isoformat()} "
                    f"距今 {age} 天，超过修改阈值 {max_age_days} 天"
                )

        if review_date is None and git_date is None and not quiet:
            result.add_warning(
                f"{display_path(path, root)}: 没有可用的修改日期（未入库且无状态日期）"
            )


def index_files(root: Path, docs_dir: Path) -> List[Path]:
    candidates = [
        root / "README.md",
        docs_dir / "00-索引与总览" / "文档总览.md",
        docs_dir / "00-索引与总览" / "架构文档地图.md",
        docs_dir / "00-索引与总览" / "界面区块索引.md",
    ]
    return [path for path in candidates if path.exists()]


def check_index(
    files: Sequence[Path],
    root: Path,
    docs_dir: Path,
    result: CheckResult,
) -> None:
    indexes = index_files(root, docs_dir)
    linked: Set[Path] = set()

    for index in indexes:
        text = read_text(index)
        links, ref_defs = collect_links(index, text)
        for link in links + ref_defs:
            target = link.target.strip()
            if not target or target.startswith("#") or is_external(target):
                continue
            target_path, _ = split_target(target)
            if not target_path:
                continue
            resolved = resolve_local_path(index, target_path, root)
            if resolved.exists():
                linked.add(resolved)

    index_set = {path.resolve() for path in indexes}
    for path in files:
        resolved = path.resolve()
        if resolved in index_set or resolved in linked:
            continue
        if resolved.suffix.lower() not in MARKDOWN_SUFFIXES:
            continue
        result.add_warning(
            f"{display_path(path, root)}: 未被 README 或 Docs/00-索引与总览 中的文档链接"
        )


def find_files(root: Path, docs_dir: Path) -> List[Path]:
    files: List[Path] = []
    readme = root / "README.md"
    if readme.exists():
        files.append(readme)

    if docs_dir.exists():
        for suffix in MARKDOWN_SUFFIXES:
            files.extend(docs_dir.rglob(f"*{suffix}"))

    # 去重、排序，跳过常见生成/缓存目录
    unique: Dict[Path, Path] = {}
    for path in files:
        resolved = path.resolve()
        parts = set(resolved.parts)
        if parts & {".git", "build", "build-engraving", "ThirdParty", ".codebuddy", ".cache"}:
            continue
        unique[resolved] = path

    return sorted(unique.values(), key=lambda p: p.as_posix())


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    root = Path(args.root).resolve()
    docs_dir = (root / args.docs_dir).resolve()

    if args.as_of:
        try:
            as_of = datetime.strptime(args.as_of, "%Y-%m-%d").date()
        except ValueError:
            print(f"ERROR: --as-of 必须是 YYYY-MM-DD: {args.as_of}", file=sys.stderr)
            return 2
    else:
        as_of = date.today()

    files = find_files(root, docs_dir)
    if not files:
        print(f"ERROR: 没有找到 Markdown 文件（root={root}, docs={docs_dir}）", file=sys.stderr)
        return 2

    result = CheckResult(checked_files=len(files))

    check_links(files, root, result, args.check_anchors, args.quiet)
    check_staleness(
        files,
        root,
        result,
        as_of,
        args.max_age_days,
        args.review_max_age_days,
        args.quiet,
    )
    if not args.no_index_check:
        check_index(files, root, docs_dir, result)

    for error in result.errors:
        print(f"ERROR: {error}")

    if not args.quiet:
        for warning in result.warnings:
            print(f"WARN:  {warning}")

    print(
        f"\nDocs check: {result.checked_files} 个文件, "
        f"{result.checked_links} 个本地链接, "
        f"{result.skipped_links} 个外部/锚点链接跳过, "
        f"{len(result.errors)} 个错误, {len(result.warnings)} 个告警"
    )

    failed = bool(result.errors)
    if args.fail_on_stale and result.warnings:
        failed = True
    if args.fail_on_unindexed and any("未被 README" in warning for warning in result.warnings):
        failed = True

    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
