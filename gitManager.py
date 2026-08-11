#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Git 仓库/子模块批量管理脚本 v2.0
人性化设计：默认全选、多远程支持、批量自动处理、减少交互
"""

import os
import sys
import subprocess
import argparse
import json
from pathlib import Path
from typing import List, Dict, Tuple, Optional


class Colors:
    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    DIM = "\033[2m"


def c(text: str, color: str = Colors.OKGREEN) -> str:
    return f"{color}{text}{Colors.ENDC}"


def run_cmd(cmd: List[str], cwd: str, check: bool = False) -> Tuple[int, str, str]:
    try:
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        return 1, "", str(e)


def find_git_repos(root_path: str, max_depth: int = 5) -> List[Dict]:
    repos = []
    root = Path(root_path).resolve()

    # 读取 .gitmodules
    submodules = {}
    gitmodules_path = root / ".gitmodules"
    if gitmodules_path.exists():
        current_name = None
        with open(gitmodules_path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if line.startswith("[submodule"):
                    current_name = line.split('"')[1] if '"' in line else None
                elif current_name and line.startswith("path ="):
                    submodules[current_name] = line.split("=", 1)[1].strip()

    for dirpath, dirnames, _ in os.walk(root):
        current_depth = len(Path(dirpath).relative_to(root).parts)
        if current_depth > max_depth:
            del dirnames[:]
            continue

        git_dir = Path(dirpath) / ".git"
        if not git_dir.exists():
            continue

        rel_path = str(Path(dirpath).relative_to(root))
        name = rel_path if rel_path != "." else os.path.basename(root)

        is_submodule = False
        for sub_name, sub_path in submodules.items():
            if Path(dirpath).resolve() == (root / sub_path).resolve():
                is_submodule = True
                name = f"📦 {sub_name}"
                break

        if not is_submodule and rel_path == ".":
            name = f"🏠 {name}"
        elif not is_submodule:
            name = f"📁 {name}"

        # 获取所有 remote
        _, remotes_out, _ = run_cmd(["git", "remote"], dirpath)
        remotes = [r.strip() for r in remotes_out.strip().splitlines() if r.strip()]

        remote_urls = {}
        for remote in remotes:
            _, url_out, _ = run_cmd(["git", "remote", "get-url", remote], dirpath)
            remote_urls[remote] = url_out.strip() if url_out.strip() else "(无)"

        # 当前分支
        _, branch_out, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], dirpath)
        current_branch = branch_out.strip()

        # 是否有未提交变更
        _, diff_out, _ = run_cmd(["git", "status", "--porcelain"], dirpath)
        has_changes = bool(diff_out.strip())

        repos.append({
            "path": dirpath,
            "name": name,
            "is_submodule": is_submodule,
            "rel_path": rel_path,
            "remotes": remotes,
            "remote_urls": remote_urls,
            "current_branch": current_branch,
            "has_changes": has_changes,
        })

        if not is_submodule and rel_path != ".":
            dirnames[:] = []

    repos.sort(key=lambda x: x["rel_path"])
    return repos


def show_repo_list(repos: List[Dict]):
    print()
    print(c("═" * 70, Colors.HEADER))
    print(c(f"  共发现 {len(repos)} 个 Git 仓库", Colors.HEADER + Colors.BOLD))
    print(c("═" * 70, Colors.HEADER))

    for i, repo in enumerate(repos, 1):
        color = Colors.OKCYAN if repo["is_submodule"] else Colors.OKGREEN
        change_mark = c(" ●", Colors.WARNING) if repo["has_changes"] else ""
        print(f"  [{i:2d}] {color}{repo['name']}{Colors.ENDC}{change_mark}")
        print(f"       分支: {c(repo['current_branch'], Colors.OKBLUE)} | 路径: {repo['rel_path']}")

        if repo["remotes"]:
            for remote, url in repo["remote_urls"].items():
                print(f"       {remote}: {c(url, Colors.DIM)}")
        else:
            print(f"       {c('(无远程仓库)', Colors.DIM)}")

    print(c("═" * 70, Colors.HEADER))
    print(c("  ● = 有未提交变更", Colors.WARNING + Colors.DIM))
    print()


def select_repos(repos: List[Dict], prompt: str = "选择仓库", default_all: bool = True) -> List[Dict]:
    """
    选择仓库。默认全选（回车直接选全部）
    输入: 1,3,5 或 1-3 或 all
    """
    hint = "[回车=全选]" if default_all else "[回车=取消]"
    print(f"{prompt} {hint}")
    print("  输入编号 (如: 1,3,5 或 1-3), 'all'=全选")
    choice = input(">>> ").strip()

    if not choice:
        if default_all:
            print(c(f"  → 默认全选 {len(repos)} 个仓库", Colors.DIM))
            return repos
        return []

    if choice.lower() == "all":
        return repos

    selected = []
    for part in choice.split(","):
        part = part.strip()
        if "-" in part:
            try:
                start, end = map(int, part.split("-"))
                for i in range(start, end + 1):
                    if 1 <= i <= len(repos):
                        selected.append(repos[i - 1])
            except ValueError:
                pass
        else:
            try:
                idx = int(part)
                if 1 <= idx <= len(repos):
                    selected.append(repos[idx - 1])
            except ValueError:
                pass

    seen = set()
    unique = []
    for r in selected:
        if r["path"] not in seen:
            seen.add(r["path"])
            unique.append(r)
    return unique


def select_remotes(repo: Dict, prompt: str = "选择远程") -> List[str]:
    """选择远程仓库，默认全选"""
    remotes = repo["remotes"]
    if not remotes:
        print(c(f"  ⚠️ {repo['name']} 没有配置远程仓库", Colors.WARNING))
        return []

    if len(remotes) == 1:
        return remotes

    print(f"  {repo['name']} 的远程仓库:")
    for i, remote in enumerate(remotes, 1):
        url = repo["remote_urls"].get(remote, "")
        print(f"    [{i}] {remote} → {c(url, Colors.DIM)}")

    print("  选择远程 [回车=全选]:")
    choice = input("  >>> ").strip()

    if not choice or choice.lower() == "all":
        return remotes

    selected = []
    for part in choice.split(","):
        try:
            idx = int(part.strip())
            if 1 <= idx <= len(remotes):
                selected.append(remotes[idx - 1])
        except ValueError:
            pass
    return selected if selected else remotes


def git_status(repos: List[Dict]):
    for repo in repos:
        print(c(f"📊 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        rc, out, _ = run_cmd(["git", "status", "-sb"], repo["path"])
        if out:
            print(out)

        rc2, out2, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        if out2:
            modified = [l for l in out2.splitlines() if l.startswith(" M") or l.startswith("M ")]
            staged = [l for l in out2.splitlines() if l.startswith("A ") or l.startswith("M ") or l.startswith("D ")]
            untracked = [l for l in out2.splitlines() if l.startswith("??")]
            print(f"  已暂存: {len(staged)} | 已修改: {len(modified)} | 未跟踪: {len(untracked)}")
        else:
            print(c("  工作区干净", Colors.DIM))


def git_pull(repos: List[Dict], auto_all: bool = False):
    """拉取：支持多远程，批量自动处理"""
    for repo in repos:
        print(c(f"📥 {repo['name']}", Colors.BOLD + Colors.OKGREEN))

        remotes = select_remotes(repo, "选择要拉取的远程")
        if not remotes:
            continue

        branch = repo["current_branch"]
        for remote in remotes:
            print(f"  → 从 {c(remote, Colors.OKBLUE)} 拉取 {branch}...", end=" ")
            rc, out, err = run_cmd(["git", "pull", remote, branch], repo["path"])
            if rc == 0:
                print(c("✅ 成功", Colors.OKGREEN))
                if "Already up to date" in out or "已经是最新" in out:
                    print(c("     (已是最新)", Colors.DIM))
                elif out.strip():
                    for line in out.strip().splitlines()[:5]:
                        print(f"     {line}")
            else:
                print(c("❌ 失败", Colors.FAIL))
                if err:
                    print(c(f"     {err}", Colors.FAIL))


def git_fetch(repos: List[Dict]):
    for repo in repos:
        print(c(f"🌐 {repo['name']}", Colors.BOLD + Colors.OKGREEN))

        remotes = select_remotes(repo, "选择要获取的远程")
        if not remotes:
            continue

        for remote in remotes:
            print(f"  → 获取 {c(remote, Colors.OKBLUE)}...", end=" ")
            rc, out, err = run_cmd(["git", "fetch", remote], repo["path"])
            if rc == 0:
                print(c("✅ 成功", Colors.OKGREEN))
            else:
                print(c("❌ 失败", Colors.FAIL))
                if err:
                    print(c(f"     {err}", Colors.FAIL))


def git_add(repos: List[Dict]):
    for repo in repos:
        print(c(f"📦 {repo['name']}", Colors.BOLD + Colors.OKCYAN))

        rc, out, _ = run_cmd(["git", "status", "--short"], repo["path"])
        if not out.strip():
            print(c("  没有变更需要暂存", Colors.DIM))
            continue

        print("当前变更:")
        for line in out.strip().splitlines()[:15]:
            print(f"  {line}")
        if len(out.strip().splitlines()) > 15:
            print(c(f"  ... 还有 {len(out.strip().splitlines()) - 15} 个文件", Colors.DIM))

        print("  [1] 全部暂存(add -A)  [2] 交互式暂存(-p)  [3] 指定文件  [回车] 跳过")
        choice = input("  >>> ").strip()

        if choice == "1" or not choice:
            rc, out, err = run_cmd(["git", "add", "-A"], repo["path"])
            if rc == 0:
                print(c("  ✅ 全部暂存完成", Colors.OKGREEN))
            else:
                print(c(f"  ❌ 失败: {err}", Colors.FAIL))
        elif choice == "2":
            print(c("  进入交互式暂存模式 (按提示操作，q退出)...", Colors.WARNING))
            os.system(f'cd "{repo["path"]}" && git add -p')
        elif choice == "3":
            files = input("  文件路径(相对仓库根目录，空格分隔): ").strip()
            if files:
                rc, out, err = run_cmd(["git", "add"] + files.split(), repo["path"])
                if rc == 0:
                    print(c("  ✅ 暂存完成", Colors.OKGREEN))
                else:
                    print(c(f"  ❌ 失败: {err}", Colors.FAIL))


def git_commit(repos: List[Dict], batch_msg: str = None):
    for repo in repos:
        print(c(f"💾 {repo['name']}", Colors.BOLD + Colors.OKCYAN))

        rc, _, _ = run_cmd(["git", "diff", "--cached", "--quiet"], repo["path"])
        if rc == 0:
            print(c("  没有暂存的变更，跳过", Colors.DIM))
            continue

        if batch_msg:
            msg = batch_msg
            print(f"  使用提交信息: {c(msg, Colors.OKBLUE)}")
        else:
            print("  [1] 输入提交信息  [2] 默认信息  [3] amend  [回车] 跳过")
            choice = input("  >>> ").strip()

            if choice == "1":
                msg = input("  提交信息: ").strip()
                if not msg:
                    continue
            elif choice == "2":
                msg = "update: batch commit"
            elif choice == "3":
                rc, out, err = run_cmd(["git", "commit", "--amend", "--no-edit"], repo["path"])
                if rc == 0:
                    print(c("  ✅ amend 完成", Colors.OKGREEN))
                else:
                    print(c(f"  ❌ 失败: {err}", Colors.FAIL))
                continue
            else:
                continue

        rc, out, err = run_cmd(["git", "commit", "-m", msg], repo["path"])
        if rc == 0:
            print(c("  ✅ 提交成功", Colors.OKGREEN))
        else:
            print(c(f"  ❌ 失败: {err}", Colors.FAIL))


def git_push(repos: List[Dict], force: bool = False):
    for repo in repos:
        print(c(f"🚀 {repo['name']}", Colors.BOLD + Colors.OKGREEN))

        remotes = select_remotes(repo, "选择要推送的远程")
        if not remotes:
            continue

        branch = repo["current_branch"]

        for remote in remotes:
            print(f"  → 推送到 {c(remote, Colors.OKBLUE)}/{branch}...", end=" ")

            cmd = ["git", "push"]
            if force:
                cmd.append("--force")
            cmd += [remote, branch]

            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                print(c("✅ 成功", Colors.OKGREEN))
            else:
                print(c("❌ 失败", Colors.FAIL))
                if err:
                    print(c(f"     {err}", Colors.FAIL))


def git_branch(repos: List[Dict]):
    for repo in repos:
        print(c(f"🌿 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        rc, out, _ = run_cmd(["git", "branch", "-a"], repo["path"])
        if out:
            print(out)

        print("  [1] 新建分支  [2] 切换分支  [3] 删除分支  [4] 合并分支  [回车] 跳过")
        choice = input("  >>> ").strip()

        if choice == "1":
            name = input("  新分支名: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "checkout", "-b", name], repo["path"])
                print(c("  ✅ 已创建" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            name = input("  切换至: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "checkout", name], repo["path"])
                print(c("  ✅ 已切换" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            name = input("  删除分支: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "branch", "-d", name], repo["path"])
                if rc != 0:
                    if input("  未合并，强制删除? (y/n): ").strip().lower() == "y":
                        rc, out, err = run_cmd(["git", "branch", "-D", name], repo["path"])
                print(c("  ✅ 已删除" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "4":
            name = input("  合并分支: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "merge", name], repo["path"])
                print(c("  ✅ 合并成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))


def git_log(repos: List[Dict]):
    for repo in repos:
        print(c(f"📜 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        rc, out, _ = run_cmd(["git", "log", "--oneline", "--graph", "--decorate", "-15"], repo["path"])
        if out:
            print(out)


def git_stash(repos: List[Dict]):
    for repo in repos:
        print(c(f"📂 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        rc, out, _ = run_cmd(["git", "stash", "list"], repo["path"])
        if out:
            print("当前 stash:")
            print(out)
        else:
            print(c("  暂无 stash", Colors.DIM))

        print("  [1] 保存stash  [2] 弹出最新  [3] 查看内容  [回车] 跳过")
        choice = input("  >>> ").strip()

        if choice == "1":
            msg = input("  备注(可选): ").strip()
            cmd = ["git", "stash", "push"]
            if msg:
                cmd += ["-m", msg]
            rc, _, err = run_cmd(cmd, repo["path"])
            print(c("  ✅ 保存成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            rc, _, err = run_cmd(["git", "stash", "pop"], repo["path"])
            print(c("  ✅ 弹出成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            idx = input("  查看第几个 (0=最新, 默认0): ").strip() or "0"
            rc, out, _ = run_cmd(["git", "stash", "show", "-p", f"stash@{{{idx}}}"], repo["path"])
            if out:
                print(out)


def git_reset(repos: List[Dict]):
    for repo in repos:
        print(c(f"↩️  {repo['name']}", Colors.BOLD + Colors.WARNING))
        print(c("  ⚠️ 重置可能丢失变更！", Colors.WARNING))
        print("  [1] 软重置(保留工作区)  [2] 混合重置(取消暂存)  [3] 硬重置(丢弃变更)  [回车] 跳过")
        choice = input("  >>> ").strip()

        if choice == "1":
            rc, _, err = run_cmd(["git", "reset", "--soft", "HEAD~1"], repo["path"])
            print(c("  ✅ 完成" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            rc, _, err = run_cmd(["git", "reset", "--mixed", "HEAD"], repo["path"])
            print(c("  ✅ 完成" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            if input("  ⚠️ 丢弃所有未提交变更，确认? (y/n): ").strip().lower() == "y":
                rc, _, err = run_cmd(["git", "reset", "--hard", "HEAD"], repo["path"])
                print(c("  ✅ 完成" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))


def git_remote_url(repos: List[Dict]):
    print(c("🔗 远程URL列表", Colors.BOLD + Colors.HEADER))
    for repo in repos:
        color = Colors.OKCYAN if repo["is_submodule"] else Colors.OKGREEN
        print(f"  {color}{repo['name']}{Colors.ENDC}")
        if repo["remotes"]:
            for remote, url in repo["remote_urls"].items():
                print(f"    {remote}: {url}")
        else:
            print(c("    (无远程)", Colors.DIM))




def get_default_branch(repo_path: str) -> str:
    """自动检测仓库的默认分支（main / master / 其他）"""
    # 先尝试获取当前分支
    _, branch, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], repo_path)
    current = branch.strip()

    # 尝试获取远程默认分支
    _, out, _ = run_cmd(["git", "symbolic-ref", "refs/remotes/origin/HEAD"], repo_path)
    if out.strip() and "refs/remotes/origin/" in out:
        return out.strip().split("/")[-1]

    # 检查本地分支
    _, out, _ = run_cmd(["git", "branch", "--list", "main", "master"], repo_path)
    branches = [b.strip().lstrip("* ") for b in out.strip().splitlines() if b.strip()]
    if "main" in branches:
        return "main"
    if "master" in branches:
        return "master"

    # 检查远程分支
    _, out, _ = run_cmd(["git", "branch", "-r"], repo_path)
    remote_branches = [b.strip() for b in out.strip().splitlines() if b.strip()]
    for candidate in ["origin/main", "origin/master"]:
        if candidate in remote_branches:
            return candidate.split("/")[-1]

    return current if current != "HEAD" else "main"


def checkout_latest(repos: List[Dict]):
    """
    将所有仓库切换到最前端（默认分支最新提交）
    流程: 保存当前变更(stash) → 切默认分支 → 拉取最新
    """
    print(c("\n🎯 切换到最前端模式", Colors.BOLD + Colors.OKGREEN))
    print("  流程: 检测默认分支 → 切换 → 拉取最新")
    print("  如果工作区有未提交变更，会先自动 stash\n")

    auto_stash = input("  有未提交变更时自动 stash? (y/n, 默认y): ").strip().lower() != "n"
    auto_pull = input("  切换后自动拉取最新? (y/n, 默认y): ").strip().lower() != "n"

    results = []
    for repo in repos:
        print(c(f"\n  ── {repo['name']} ──", Colors.OKCYAN))
        result = {"name": repo["name"], "steps": []}

        # 1. 检测默认分支
        default_branch = get_default_branch(repo["path"])
        print(f"    默认分支: {c(default_branch, Colors.OKBLUE)}")

        # 2. 检查是否有未提交变更
        _, diff_out, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        has_changes = bool(diff_out.strip())

        if has_changes:
            if auto_stash:
                print(f"    检测到未提交变更，自动 stash...", end=" ")
                rc, _, err = run_cmd(["git", "stash", "push", "-m", "auto-stash before checkout-latest"], repo["path"])
                if rc == 0:
                    print(c("✅", Colors.OKGREEN))
                    result["steps"].append(("stash", True))
                else:
                    print(c(f"❌ {err}", Colors.FAIL))
                    result["steps"].append(("stash", False))
                    results.append(result)
                    continue
            else:
                print(c("    ⚠️ 有未提交变更，跳过 (未启用自动stash)", Colors.WARNING))
                result["steps"].append(("stash", None))
                results.append(result)
                continue

        # 3. 切换分支
        print(f"    切换到 {default_branch}...", end=" ")
        rc, _, err = run_cmd(["git", "checkout", default_branch], repo["path"])
        if rc == 0:
            print(c("✅", Colors.OKGREEN))
            result["steps"].append(("checkout", True))
        else:
            print(c(f"❌ {err}", Colors.FAIL))
            result["steps"].append(("checkout", False))
            results.append(result)
            continue

        # 4. 拉取最新
        if auto_pull:
            for remote in repo["remotes"]:
                print(f"    从 {remote} 拉取 {default_branch}...", end=" ")
                rc, out, err = run_cmd(["git", "pull", remote, default_branch], repo["path"])
                if rc == 0:
                    print(c("✅", Colors.OKGREEN))
                    result["steps"].append((f"pull({remote})", True))
                else:
                    print(c("❌", Colors.FAIL))
                    if err:
                        print(c(f"      {err}", Colors.FAIL))
                    result["steps"].append((f"pull({remote})", False))

        results.append(result)

    # 汇总
    print(c("\n" + "═" * 50, Colors.HEADER))
    print(c("  切换到最前端结果汇总", Colors.HEADER + Colors.BOLD))
    print(c("═" * 50, Colors.HEADER))
    for r in results:
        print(f"\n  {r['name']}:")
        for step, ok in r["steps"]:
            if ok is True:
                print(f"    ✅ {step}")
            elif ok is False:
                print(f"    ❌ {step}")
            else:
                print(f"    ⏭️  {step} (跳过)")

def git_submodule_update(repos: List[Dict]):
    main_repos = [r for r in repos if not r["is_submodule"] or r["rel_path"] == "."]
    for repo in main_repos:
        print(c(f"🔄 {repo['name']}", Colors.BOLD + Colors.OKGREEN))
        print("  [1] 初始化并更新  [2] 仅更新  [3] 递归更新  [回车] 跳过")
        choice = input("  >>> ").strip()

        if choice == "1":
            os.system(f'cd "{repo["path"]}" && git submodule update --init')
        elif choice == "2":
            os.system(f'cd "{repo["path"]}" && git submodule update')
        elif choice == "3":
            os.system(f'cd "{repo["path"]}" && git submodule update --init --recursive')


def quick_sync(repos: List[Dict]):
    """快速同步：暂存 -> 提交 -> 拉取 -> 推送，批量自动"""
    print(c("⚡ 快速同步模式", Colors.BOLD + Colors.OKGREEN))
    print("  流程: 暂存 → 提交 → 拉取 → 推送")
    print("  所有仓库将自动处理，无需逐个确认")

    msg = input("  统一提交信息 (回车使用默认): ").strip()
    if not msg:
        msg = "update: batch sync"

    force = input("  是否强制推送? (y/n, 默认n): ").strip().lower() == "y"

    results = []
    for repo in repos:
        print(c(f"  ── {repo['name']} ──", Colors.OKCYAN))
        result = {"name": repo["name"], "steps": []}

        # 1. 暂存
        rc, _, _ = run_cmd(["git", "add", "-A"], repo["path"])
        result["steps"].append(("暂存", rc == 0))

        # 2. 提交
        rc, _, _ = run_cmd(["git", "diff", "--cached", "--quiet"], repo["path"])
        if rc != 0:
            rc, _, err = run_cmd(["git", "commit", "-m", msg], repo["path"])
            result["steps"].append(("提交", rc == 0))
        else:
            result["steps"].append(("提交", None))  # None = 无变更

        # 3. 拉取（所有远程）
        branch = repo["current_branch"]
        pull_ok = True
        for remote in repo["remotes"]:
            rc, _, err = run_cmd(["git", "pull", remote, branch], repo["path"])
            if rc != 0:
                pull_ok = False
                result["steps"].append((f"拉取({remote})", False))
            else:
                result["steps"].append((f"拉取({remote})", True))

        # 4. 推送（所有远程）
        push_ok = True
        for remote in repo["remotes"]:
            cmd = ["git", "push"]
            if force:
                cmd.append("--force")
            cmd += [remote, branch]
            rc, _, err = run_cmd(cmd, repo["path"])
            if rc != 0:
                push_ok = False
                result["steps"].append((f"推送({remote})", False))
            else:
                result["steps"].append((f"推送({remote})", True))

        results.append(result)

    # 汇总
    print(c("" + "═" * 50, Colors.HEADER))
    print(c("  快速同步结果汇总", Colors.HEADER + Colors.BOLD))
    print(c("═" * 50, Colors.HEADER))
    for r in results:
        print(f"  {r['name']}:")
        for step, ok in r["steps"]:
            if ok is True:
                print(f"    ✅ {step}")
            elif ok is False:
                print(f"    ❌ {step}")
            else:
                print(f"    ⏭️  {step} (无变更)")


def batch_pull(repos: List[Dict]):
    """批量拉取所有远程"""
    print(c("📥 批量拉取模式", Colors.BOLD + Colors.OKGREEN))
    print("  将自动从所有远程拉取当前分支")

    for repo in repos:
        print(c(f"  ── {repo['name']} ──", Colors.OKCYAN))
        branch = repo["current_branch"]

        if not repo["remotes"]:
            print(c("    无远程仓库，跳过", Colors.DIM))
            continue

        for remote in repo["remotes"]:
            print(f"    → {remote}/{branch}...", end=" ")
            rc, out, err = run_cmd(["git", "pull", remote, branch], repo["path"])
            if rc == 0:
                print(c("✅", Colors.OKGREEN))
            else:
                print(c("❌", Colors.FAIL))
                if err:
                    print(c(f"      {err}", Colors.FAIL))


def batch_push(repos: List[Dict]):
    """批量推送到所有远程"""
    print(c("🚀 批量推送模式", Colors.BOLD + Colors.OKGREEN))
    print("  将自动推送到所有远程的当前分支")

    force = input("  强制推送? (y/n, 默认n): ").strip().lower() == "y"

    for repo in repos:
        print(c(f"  ── {repo['name']} ──", Colors.OKCYAN))
        branch = repo["current_branch"]

        if not repo["remotes"]:
            print(c("    无远程仓库，跳过", Colors.DIM))
            continue

        for remote in repo["remotes"]:
            print(f"    → {remote}/{branch}...", end=" ")
            cmd = ["git", "push"]
            if force:
                cmd.append("--force")
            cmd += [remote, branch]
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                print(c("✅", Colors.OKGREEN))
            else:
                print(c("❌", Colors.FAIL))
                if err:
                    print(c(f"      {err}", Colors.FAIL))


def main():
    parser = argparse.ArgumentParser(description="Git 仓库/子模块批量管理工具 v2.0")
    parser.add_argument("--path", "-p", default=".", help="工程根路径")
    parser.add_argument("--depth", "-d", type=int, default=5, help="扫描深度")
    parser.add_argument("--no-color", action="store_true", help="禁用颜色")
    args = parser.parse_args()

    if args.no_color:
        for attr in dir(Colors):
            if not attr.startswith("_"):
                setattr(Colors, attr, "")

    root_path = os.path.abspath(args.path)
    if not os.path.exists(root_path):
        print(c(f"路径不存在: {root_path}", Colors.FAIL))
        sys.exit(1)

    print(c("🔍 正在扫描Git仓库...", Colors.OKCYAN))
    repos = find_git_repos(root_path, args.depth)

    if not repos:
        print(c("未找到任何Git仓库", Colors.FAIL))
        sys.exit(1)

    show_repo_list(repos)

    while True:
        print(c("" + "═" * 52, Colors.HEADER))
        print(c("  Git 批量管理菜单", Colors.HEADER + Colors.BOLD))
        print(c("═" * 52, Colors.HEADER))
        print("""
  [1]  📊 查看状态 (status)
  [2]  📥 拉取代码 (pull)        ← 支持多远程，逐个确认
  [3]  🌐 获取远程 (fetch)       ← 支持多远程，逐个确认
  [4]  📦 暂存文件 (add)
  [5]  💾 提交变更 (commit)
  [6]  🚀 推送代码 (push)        ← 支持多远程，逐个确认
  [7]  🌿 分支管理 (branch)
  [8]  📜 查看日志 (log)
  [9]  📂 Stash管理
  [10] ↩️  重置操作 (reset)
  [11] 🔗 查看所有远程URL
  [12] 🔄 子模块更新
  ──────────────────────────────────────────────────
  [21] ⚡ 快速同步 (add+commit+pull+push)  ← 全自动批量
  [22] 📥 批量拉取 (所有远程)              ← 全自动批量
  [23] 🚀 批量推送 (所有远程)              ← 全自动批量
  [24] 📦 批量暂存 (所有仓库)              ← 全自动批量
  [25] 💾 批量提交 (所有仓库)              ← 全自动批量
  [26] 🎯 切换到最前端 (切默认分支+拉取)    ← 全自动批量
  ──────────────────────────────────────────────────
  [99] 📝 重新扫描仓库
  [0]  ❌ 退出
        """)

        choice = input("请选择操作 >>> ").strip()

        if choice == "0":
            print(c("👋 再见！", Colors.OKGREEN))
            break

        if choice == "99":
            print(c("🔍 重新扫描...", Colors.OKCYAN))
            repos = find_git_repos(root_path, args.depth)
            show_repo_list(repos)
            continue

        # 需要选择仓库的操作（默认全选）
        if choice in ("11",):
            selected = repos
        elif choice in ("21", "22", "23", "24", "25"):
            # 批量模式默认全选
            selected = select_repos(repos, "选择仓库", default_all=True)
        elif choice in ("12",):
            selected = repos
        else:
            selected = select_repos(repos, "选择仓库", default_all=True)

        if not selected and choice not in ("11", "12"):
            continue

        if choice == "1":
            git_status(selected)
        elif choice == "2":
            git_pull(selected)
        elif choice == "3":
            git_fetch(selected)
        elif choice == "4":
            git_add(selected)
        elif choice == "5":
            git_commit(selected)
        elif choice == "6":
            git_push(selected)
        elif choice == "7":
            git_branch(selected)
        elif choice == "8":
            git_log(selected)
        elif choice == "9":
            git_stash(selected)
        elif choice == "10":
            git_reset(selected)
        elif choice == "11":
            git_remote_url(selected)
        elif choice == "12":
            git_submodule_update(selected)
        elif choice == "21":
            quick_sync(selected)
        elif choice == "22":
            batch_pull(selected)
        elif choice == "23":
            batch_push(selected)
        elif choice == "24":
            for repo in selected:
                print(c(f"\n📦 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
                rc, _, err = run_cmd(["git", "add", "-A"], repo["path"])
                print(c("  ✅ 全部暂存" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "25":
            msg = input("统一提交信息 (回车使用默认): ").strip() or "update: batch commit"
            git_commit(selected, batch_msg=msg)
        elif choice == "26":
            checkout_latest(selected)
        else:
            print(c("无效选项", Colors.FAIL))

        input("\n按回车继续...")


if __name__ == "__main__":
    main()