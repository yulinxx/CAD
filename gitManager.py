#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
Git 仓库/子模块批量管理脚本
支持: 状态查看、拉取、暂存、提交、推送、分支管理、日志查看等
"""

import os
import sys
import subprocess
import argparse
from pathlib import Path
from typing import List, Dict, Tuple, Optional


class Colors:
    """终端颜色"""
    HEADER = "\033[95m"
    OKBLUE = "\033[94m"
    OKCYAN = "\033[96m"
    OKGREEN = "\033[92m"
    WARNING = "\033[93m"
    FAIL = "\033[91m"
    ENDC = "\033[0m"
    BOLD = "\033[1m"
    UNDERLINE = "\033[4m"


def print_color(text: str, color: str = Colors.OKGREEN):
    """打印带颜色的文本"""
    print(f"{color}{text}{Colors.ENDC}")


def run_cmd(cmd: List[str], cwd: str, check: bool = False) -> Tuple[int, str, str]:
    """执行shell命令，返回 (returncode, stdout, stderr)"""
    try:
        result = subprocess.run(
            cmd,
            cwd=cwd,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace"
        )
        if check and result.returncode != 0:
            print_color(f"命令失败: {' '.join(cmd)}", Colors.FAIL)
            if result.stderr:
                print_color(result.stderr, Colors.FAIL)
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        return 1, "", str(e)


def find_git_repos(root_path: str, max_depth: int = 5) -> List[Dict]:
    """
    递归查找所有Git仓库（包括子模块）
    返回: [{"path": str, "name": str, "is_submodule": bool, "remote_url": str}]
    """
    repos = []
    root = Path(root_path).resolve()

    # 先读取 .gitmodules 获取子模块信息
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

    # 扫描目录
    for dirpath, dirnames, filenames in os.walk(root):
        current_depth = len(Path(dirpath).relative_to(root).parts)
        if current_depth > max_depth:
            del dirnames[:]
            continue

        git_dir = Path(dirpath) / ".git"
        if git_dir.exists():
            rel_path = str(Path(dirpath).relative_to(root))
            name = rel_path if rel_path != "." else os.path.basename(root)

            # 判断是否是子模块
            is_submodule = False
            for sub_name, sub_path in submodules.items():
                if Path(dirpath).resolve() == (root / sub_path).resolve():
                    is_submodule = True
                    name = f"[子模块] {sub_name}"
                    break

            if not is_submodule and rel_path == ".":
                name = f"[主仓库] {name}"
            elif not is_submodule:
                name = f"[仓库] {name}"

            # 获取远程URL
            _, stdout, _ = run_cmd(["git", "remote", "get-url", "origin"], dirpath)
            remote_url = stdout.strip() if stdout.strip() else "(无远程)"

            repos.append({
                "path": dirpath,
                "name": name,
                "is_submodule": is_submodule,
                "remote_url": remote_url,
                "rel_path": rel_path
            })

            # 不再递归进入该仓库的子目录（避免重复扫描嵌套仓库内部）
            # 但保留子模块的扫描，因为子模块目录下还有.git
            if not is_submodule and rel_path != ".":
                dirnames[:] = []

    # 按路径排序
    repos.sort(key=lambda x: x["rel_path"])
    return repos


def show_repo_list(repos: List[Dict]):
    """显示仓库列表"""
    print_color("\n" + "=" * 70, Colors.HEADER)
    print_color("  发现的Git仓库列表", Colors.HEADER + Colors.BOLD)
    print_color("=" * 70, Colors.HEADER)

    for i, repo in enumerate(repos, 1):
        color = Colors.OKCYAN if repo["is_submodule"] else Colors.OKGREEN
        print(f"  [{i:2d}] {color}{repo['name']}{Colors.ENDC}")
        print(f"       路径: {repo['rel_path']}")
        print(f"       远程: {Colors.WARNING}{repo['remote_url']}{Colors.ENDC}")

    print_color("=" * 70 + "\n", Colors.HEADER)


def select_repos(repos: List[Dict], prompt: str = "选择仓库") -> List[Dict]:
    """让用户选择仓库，支持多选（逗号分隔）或 all"""
    print(f"\n{prompt}")
    print("  输入编号 (如: 1,3,5 或 1-3), 输入 'all' 选择全部, 回车取消")
    choice = input(">>> ").strip()

    if not choice:
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

    # 去重
    seen = set()
    unique = []
    for r in selected:
        if r["path"] not in seen:
            seen.add(r["path"])
            unique.append(r)
    return unique


def git_status(repos: List[Dict]):
    """查看状态"""
    for repo in repos:
        print_color(f"\n📁 {repo['name']}", Colors.BOLD + Colors.OKCYAN)
        print_color(f"   路径: {repo['path']}", Colors.OKCYAN)
        rc, out, err = run_cmd(["git", "status", "-sb"], repo["path"])
        if out:
            print(out)
        if err:
            print_color(err, Colors.FAIL)

        # 显示未跟踪文件
        rc2, out2, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        if out2:
            modified = [l for l in out2.splitlines() if l.startswith(" M") or l.startswith("M ")]
            staged = [l for l in out2.splitlines() if l.startswith("A ") or l.startswith("M ") or l.startswith("D ")]
            untracked = [l for l in out2.splitlines() if l.startswith("??")]
            print(f"   已暂存: {len(staged)} | 已修改: {len(modified)} | 未跟踪: {len(untracked)}")


def git_pull(repos: List[Dict]):
    """拉取代码"""
    for repo in repos:
        print_color(f"\n📥 正在拉取: {repo['name']}", Colors.BOLD + Colors.OKGREEN)
        rc, out, err = run_cmd(["git", "pull"], repo["path"])
        if out:
            print(out)
        if err:
            print_color(err, Colors.WARNING)
        if rc == 0:
            print_color("   ✅ 拉取完成", Colors.OKGREEN)
        else:
            print_color("   ❌ 拉取失败", Colors.FAIL)


def git_fetch(repos: List[Dict]):
    """获取远程更新（不合并）"""
    for repo in repos:
        print_color(f"\n🌐 正在获取: {repo['name']}", Colors.BOLD + Colors.OKGREEN)
        rc, out, err = run_cmd(["git", "fetch", "--all"], repo["path"])
        if out:
            print(out)
        if err:
            print_color(err, Colors.WARNING)
        if rc == 0:
            print_color("   ✅ 获取完成", Colors.OKGREEN)
        else:
            print_color("   ❌ 获取失败", Colors.FAIL)


def git_add(repos: List[Dict]):
    """暂存文件"""
    for repo in repos:
        print_color(f"\n📦 暂存: {repo['name']}", Colors.BOLD + Colors.OKCYAN)

        # 先显示状态
        rc, out, _ = run_cmd(["git", "status", "--short"], repo["path"])
        if out:
            print("当前变更:")
            print(out)

        print("\n选项: [1] 全部暂存(add -A)  [2] 交互式暂存(add -p)  [3] 指定文件  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            rc, out, err = run_cmd(["git", "add", "-A"], repo["path"])
            if rc == 0:
                print_color("   ✅ 全部暂存完成", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            # 交互式 - 用系统git命令直接交互
            print_color("   进入交互式暂存模式 (按提示操作，q退出)...", Colors.WARNING)
            os.system(f'cd "{repo["path"]}" && git add -p')
        elif choice == "3":
            files = input("输入文件路径（相对仓库根目录，多个用空格分隔）: ").strip()
            if files:
                rc, out, err = run_cmd(["git", "add"] + files.split(), repo["path"])
                if rc == 0:
                    print_color("   ✅ 指定文件暂存完成", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)


def git_commit(repos: List[Dict]):
    """提交变更"""
    for repo in repos:
        print_color(f"\n💾 提交: {repo['name']}", Colors.BOLD + Colors.OKCYAN)

        # 检查是否有暂存内容
        rc, out, _ = run_cmd(["git", "diff", "--cached", "--quiet"], repo["path"])
        if rc == 0:
            print_color("   ⚠️ 没有暂存的变更，跳过", Colors.WARNING)
            continue

        print("\n选项: [1] 输入提交信息  [2] 使用默认信息  [3] 修改上次提交(amend)  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            msg = input("输入提交信息: ").strip()
            if msg:
                rc, out, err = run_cmd(["git", "commit", "-m", msg], repo["path"])
                if rc == 0:
                    print_color("   ✅ 提交成功", Colors.OKGREEN)
                    print(out)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            msg = "update: batch commit via git-manager"
            rc, out, err = run_cmd(["git", "commit", "-m", msg], repo["path"])
            if rc == 0:
                print_color("   ✅ 提交成功", Colors.OKGREEN)
                print(out)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "3":
            rc, out, err = run_cmd(["git", "commit", "--amend", "--no-edit"], repo["path"])
            if rc == 0:
                print_color("   ✅ 修改提交成功", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)


def git_push(repos: List[Dict]):
    """推送到远程"""
    for repo in repos:
        print_color(f"\n🚀 推送: {repo['name']}", Colors.BOLD + Colors.OKGREEN)

        # 获取当前分支
        rc, branch, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], repo["path"])
        branch = branch.strip()

        print(f"当前分支: {branch}")
        print("选项: [1] 推送到当前分支  [2] 强制推送(--force)  [3] 推送到指定分支  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            rc, out, err = run_cmd(["git", "push", "origin", branch], repo["path"])
            if rc == 0:
                print_color("   ✅ 推送成功", Colors.OKGREEN)
                if out:
                    print(out)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            confirm = input("⚠️ 强制推送可能覆盖远程历史，确认? (y/n): ").strip().lower()
            if confirm == "y":
                rc, out, err = run_cmd(["git", "push", "--force", "origin", branch], repo["path"])
                if rc == 0:
                    print_color("   ✅ 强制推送成功", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "3":
            target = input("输入目标分支名: ").strip()
            if target:
                rc, out, err = run_cmd(["git", "push", "origin", f"{branch}:{target}"], repo["path"])
                if rc == 0:
                    print_color("   ✅ 推送成功", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)


def git_branch(repos: List[Dict]):
    """分支管理"""
    for repo in repos:
        print_color(f"\n🌿 分支: {repo['name']}", Colors.BOLD + Colors.OKCYAN)
        rc, out, _ = run_cmd(["git", "branch", "-a"], repo["path"])
        if out:
            print(out)

        print("\n选项: [1] 新建分支  [2] 切换分支  [3] 删除分支  [4] 合并分支  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            name = input("新分支名: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "checkout", "-b", name], repo["path"])
                if rc == 0:
                    print_color(f"   ✅ 已创建并切换到 {name}", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            name = input("要切换的分支名: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "checkout", name], repo["path"])
                if rc == 0:
                    print_color(f"   ✅ 已切换到 {name}", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "3":
            name = input("要删除的分支名: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "branch", "-d", name], repo["path"])
                if rc != 0:
                    force = input("分支未合并，强制删除? (y/n): ").strip().lower()
                    if force == "y":
                        rc, out, err = run_cmd(["git", "branch", "-D", name], repo["path"])
                if rc == 0:
                    print_color(f"   ✅ 已删除 {name}", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "4":
            name = input("要合并的分支名: ").strip()
            if name:
                rc, out, err = run_cmd(["git", "merge", name], repo["path"])
                if rc == 0:
                    print_color(f"   ✅ 合并 {name} 成功", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 合并冲突或失败: {err}", Colors.FAIL)


def git_log(repos: List[Dict]):
    """查看日志"""
    for repo in repos:
        print_color(f"\n📜 日志: {repo['name']}", Colors.BOLD + Colors.OKCYAN)
        rc, out, _ = run_cmd(
            ["git", "log", "--oneline", "--graph", "--decorate", "-15"],
            repo["path"]
        )
        if out:
            print(out)


def git_stash(repos: List[Dict]):
    """暂存管理"""
    for repo in repos:
        print_color(f"\n📂 Stash: {repo['name']}", Colors.BOLD + Colors.OKCYAN)

        rc, out, _ = run_cmd(["git", "stash", "list"], repo["path"])
        if out:
            print("当前stash列表:")
            print(out)
        else:
            print("暂无stash")

        print("\n选项: [1] 保存stash  [2] 弹出最新stash  [3] 查看stash内容  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            msg = input("stash备注信息(可选): ").strip()
            cmd = ["git", "stash", "push"]
            if msg:
                cmd += ["-m", msg]
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                print_color("   ✅ stash保存成功", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            rc, out, err = run_cmd(["git", "stash", "pop"], repo["path"])
            if rc == 0:
                print_color("   ✅ stash弹出成功", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "3":
            idx = input("查看第几个stash (0=最新, 默认0): ").strip() or "0"
            rc, out, _ = run_cmd(["git", "stash", "show", "-p", f"stash@{{{idx}}}"], repo["path"])
            if out:
                print(out)


def git_reset(repos: List[Dict]):
    """重置操作"""
    for repo in repos:
        print_color(f"\n↩️ 重置: {repo['name']}", Colors.BOLD + Colors.WARNING)
        print("⚠️ 警告: 重置可能丢失变更！")
        print("选项: [1] 软重置(保留工作区)  [2] 混合重置(保留文件，取消暂存)  [3] 硬重置(丢弃所有变更)  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            rc, out, err = run_cmd(["git", "reset", "--soft", "HEAD~1"], repo["path"])
            if rc == 0:
                print_color("   ✅ 软重置完成", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "2":
            rc, out, err = run_cmd(["git", "reset", "--mixed", "HEAD"], repo["path"])
            if rc == 0:
                print_color("   ✅ 混合重置完成（已取消暂存）", Colors.OKGREEN)
            else:
                print_color(f"   ❌ 失败: {err}", Colors.FAIL)
        elif choice == "3":
            confirm = input("⚠️ 这将丢弃所有未提交的变更，确认? (y/n): ").strip().lower()
            if confirm == "y":
                rc, out, err = run_cmd(["git", "reset", "--hard", "HEAD"], repo["path"])
                if rc == 0:
                    print_color("   ✅ 硬重置完成", Colors.OKGREEN)
                else:
                    print_color(f"   ❌ 失败: {err}", Colors.FAIL)


def git_remote_url(repos: List[Dict]):
    """查看所有远程URL"""
    print_color("\n🔗 远程URL列表", Colors.BOLD + Colors.HEADER)
    for repo in repos:
        color = Colors.OKCYAN if repo["is_submodule"] else Colors.OKGREEN
        print(f"  {color}{repo['name']}{Colors.ENDC}")
        print(f"     URL: {Colors.WARNING}{repo['remote_url']}{Colors.ENDC}")
        # 也显示所有remote
        rc, out, _ = run_cmd(["git", "remote", "-v"], repo["path"])
        if out:
            for line in out.strip().splitlines():
                print(f"     {line}")


def git_submodule_update(repos: List[Dict]):
    """更新子模块"""
    # 只对主仓库执行
    main_repos = [r for r in repos if not r["is_submodule"] or r["rel_path"] == "."]
    for repo in main_repos:
        print_color(f"\n🔄 更新子模块: {repo['name']}", Colors.BOLD + Colors.OKGREEN)
        print("选项: [1] 初始化并更新  [2] 仅更新  [3] 递归更新  [回车] 跳过")
        choice = input(">>> ").strip()

        if choice == "1":
            os.system(f'cd "{repo["path"]}" && git submodule update --init')
        elif choice == "2":
            os.system(f'cd "{repo["path"]}" && git submodule update')
        elif choice == "3":
            os.system(f'cd "{repo["path"]}" && git submodule update --init --recursive')


def quick_sync(repos: List[Dict]):
    """快速同步：暂存 -> 提交 -> 拉取 -> 推送"""
    for repo in repos:
        print_color(f"\n⚡ 快速同步: {repo['name']}", Colors.BOLD + Colors.OKGREEN)

        # 暂存
        run_cmd(["git", "add", "-A"], repo["path"])

        # 检查是否有变更要提交
        rc, _, _ = run_cmd(["git", "diff", "--cached", "--quiet"], repo["path"])
        if rc != 0:
            msg = input(f"  [{repo['name']}] 提交信息 (回车跳过提交): ").strip()
            if msg:
                run_cmd(["git", "commit", "-m", msg], repo["path"])

        # 拉取
        print("  正在拉取...")
        rc, out, err = run_cmd(["git", "pull"], repo["path"])
        if rc != 0:
            print_color(f"  拉取冲突: {err}", Colors.WARNING)
            continue

        # 推送
        print("  正在推送...")
        rc, branch, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], repo["path"])
        branch = branch.strip()
        rc, out, err = run_cmd(["git", "push", "origin", branch], repo["path"])
        if rc == 0:
            print_color("  ✅ 同步完成", Colors.OKGREEN)
        else:
            print_color(f"  ❌ 推送失败: {err}", Colors.FAIL)


def main():
    parser = argparse.ArgumentParser(description="Git 仓库/子模块批量管理工具")
    parser.add_argument("--path", "-p", default=".", help="工程根路径 (默认当前目录)")
    parser.add_argument("--depth", "-d", type=int, default=5, help="扫描深度 (默认5)")
    parser.add_argument("--no-color", action="store_true", help="禁用颜色输出")
    args = parser.parse_args()

    if args.no_color:
        global Colors
        for attr in dir(Colors):
            if not attr.startswith("_"):
                setattr(Colors, attr, "")

    root_path = os.path.abspath(args.path)
    if not os.path.exists(root_path):
        print_color(f"路径不存在: {root_path}", Colors.FAIL)
        sys.exit(1)

    print_color("🔍 正在扫描Git仓库...", Colors.OKCYAN)
    repos = find_git_repos(root_path, args.depth)

    if not repos:
        print_color("未找到任何Git仓库", Colors.FAIL)
        sys.exit(1)

    show_repo_list(repos)

    while True:
        print_color("\n" + "=" * 50, Colors.HEADER)
        print_color("  Git 批量管理菜单", Colors.HEADER + Colors.BOLD)
        print_color("=" * 50, Colors.HEADER)
        print("""
  [1]  📊 查看状态 (status)
  [2]  📥 拉取代码 (pull)
  [3]  🌐 获取远程 (fetch)
  [4]  📦 暂存文件 (add/stage)
  [5]  💾 提交变更 (commit)
  [6]  🚀 推送代码 (push)
  [7]  🌿 分支管理 (branch)
  [8]  📜 查看日志 (log)
  [9]  📂 Stash管理
  [10] ↩️  重置操作 (reset)
  [11] 🔗 查看所有远程URL
  [12] 🔄 子模块更新
  [13] ⚡ 快速同步 (add+commit+pull+push)
  [14] 📝 重新扫描仓库
  [0]  ❌ 退出
        """)

        choice = input("请选择操作 >>> ").strip()

        if choice == "0":
            print_color("👋 再见！", Colors.OKGREEN)
            break

        if choice == "14":
            print_color("🔍 重新扫描...", Colors.OKCYAN)
            repos = find_git_repos(root_path, args.depth)
            show_repo_list(repos)
            continue

        # 需要选择仓库的操作
        if choice in ("11",):
            # URL查看不需要选择，直接显示全部
            selected = repos
        elif choice in ("12",):
            selected = repos
        else:
            selected = select_repos(repos, "选择要操作的仓库")

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
        elif choice == "13":
            quick_sync(selected)
        else:
            print_color("无效选项", Colors.FAIL)

        input("\n按回车继续...")


if __name__ == "__main__":
    main()
