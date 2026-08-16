
#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
Git 仓库/子模块批量管理脚本 v2.0
人性化设计：默认全选、多远程支持、批量自动处理、减少交互

Mac使用:
cd /Users/ms/Documents/CAD
find . \( -name "*.cpp" -o -name "*.h" -o -name "*.hpp" \) -exec clang-format -i {} +

"""

import os
import sys
import subprocess
import argparse
import json
from datetime import datetime
from pathlib import Path
from typing import List, Dict, Tuple, Optional


def enable_windows_color():
    """启用 Windows 控制台 ANSI 转义支持（VT100）"""
    if sys.platform != "win32":
        return
    try:
        import ctypes
        kernel32 = ctypes.windll.kernel32
        # STD_OUTPUT_HANDLE = -11, ENABLE_VIRTUAL_TERMINAL_PROCESSING = 0x0004
        handle = kernel32.GetStdHandle(-11)
        mode = ctypes.c_uint32()
        if kernel32.GetConsoleMode(handle, ctypes.byref(mode)):
            kernel32.SetConsoleMode(handle, mode.value | 0x0004)
    except Exception:
        pass


enable_windows_color()


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


def cmd_str(cmd: List[str]) -> str:
    """将命令列表转成可读的命令字符串"""
    return " ".join(cmd)


def show_cmd(cmd: List[str]):
    """在执行 git 命令前打印完整命令，让用户知道 git 在操作什么"""
    print(c(f"  $ {cmd_str(cmd)}", Colors.DIM))


def is_exit_key(choice: str) -> bool:
    """判断输入是否为退出/取消"""
    return choice.strip().lower() in ("0", "q", "quit", "exit", "e", "x", "退出", "取消")


def prompt(text: str, default: str = None, allow_exit: bool = True,
           exit_hint: str = None, to_lower: bool = False) -> Optional[str]:
    """统一的输入提示。默认提示可退出，输入 0/q/取消 等退出键返回 None。

    Args:
        text:      提示文本（例如 "  统一提交信息"）
        default:   回车时的默认值（None 表示无默认，空字符串则代表默认空文本）
        allow_exit: 是否允许用退出键取消当前操作（默认 True）
        exit_hint: 自定义退出提示文本（默认 "[0/q=取消]"）
        to_lower:  非 None 时结果转小写（便于 y/n 判断）
    """
    suffix = ""
    if allow_exit:
        hint = exit_hint if exit_hint is not None else "0/q=取消"
        if default is not None:
            suffix = f" (回车={default!r}, {hint})"
        else:
            suffix = f" ({hint})"
    elif default is not None:
        suffix = f" (回车={default!r})"

    value = input(text + suffix + ": ").strip()
    if allow_exit and is_exit_key(value):
        print(c("  → 已取消", Colors.DIM))
        return None
    if not value and default is not None:
        value = default
    if to_lower and value is not None:
        value = value.lower()
    return value


def confirm(text: str, default_yes: bool = True, allow_exit: bool = False) -> Optional[bool]:
    """统一的 y/n 确认。默认提示含 0/q 退出（allow_exit=True 时）。
    返回 True=是、False=否、None=用户选择退出（仅 allow_exit 时会出现）。"""
    default_label = "y" if default_yes else "n"
    exit_label = " 0/q=取消" if allow_exit else ""
    full = f"{text} (y/n, 默认{default_label}{exit_label})"
    v = prompt(full, default=default_label, allow_exit=allow_exit, to_lower=True)
    if v is None:
        return None
    return v in ("y", "yes", "是")


def autostash_msg(action: str) -> str:
    """生成自动 stash 的带时间戳备注，例如 autostash before pull 2026-08-14 14:30:00"""
    ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
    return f"autostash {action} {ts}"


def run_cmd(cmd: List[str], cwd: str, check: bool = False) -> Tuple[int, str, str]:
    try:
        result = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True,
                                encoding="utf-8", errors="replace")
        return result.returncode, result.stdout, result.stderr
    except FileNotFoundError:
        # 命令本身不存在（如未安装 git）
        return 127, "", f"命令不存在: {cmd[0]}"
    except Exception as e:
        return 1, "", str(e)


def run_cmd_interactive(cmd: List[str], cwd: str) -> int:
    """运行需要用户交互的命令（如 git add -p），继承当前 stdio"""
    try:
        return subprocess.run(cmd, cwd=cwd).returncode
    except FileNotFoundError:
        print(c(f"  ❌ 命令不存在: {cmd[0]}", Colors.FAIL))
        return 127
    except Exception as e:
        print(c(f"  ❌ {e}", Colors.FAIL))
        return 1


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
    选择仓库。默认全选（回车/空格直接选全部）
    输入: 1,3,5 或 1-3 或 all；0/q=退出当前操作
    """
    print(f"{prompt} [回车/空格=全选, 0/q=退出当前操作]")
    print("  输入编号 (如: 1,3,5 或 1-3), 'all'=全选, '0'或'q'=退出当前操作")
    choice = input(">>> ").strip()

    if not choice:
        if default_all:
            print(c(f"  → 默认全选 {len(repos)} 个仓库", Colors.DIM))
            return repos
        return []

    if is_exit_key(choice):
        print(c("  → 已退出当前操作", Colors.DIM))
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

    if not unique:
        print(c("  → 未选择有效编号", Colors.DIM))
    return unique


def select_remotes(repo: Dict, prompt: str = "选择远程") -> List[str]:
    """选择远程仓库，默认全选；0/q=退出跳过该仓库"""
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

    print("  选择远程 [回车/空格=全选, 0/q=退出, 跳过此仓库]:")
    choice = input("  >>> ").strip()

    if is_exit_key(choice):
        print(c("  → 已退出，跳过此仓库", Colors.DIM))
        return []

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
        show_cmd(["git", "status", "-sb"])
        rc, out, _ = run_cmd(["git", "status", "-sb"], repo["path"])
        if out:
            print(out)

        rc2, out2, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        if out2:
            print(c("  $ git status --porcelain", Colors.DIM))
            # porcelain 格式为 "XY path"，X 为暂存区状态，Y 为工作区状态
            # " M" = 工作区修改未暂存；"M " = 已暂存；"MM" = 暂存后又有修改；"??" = 未跟踪
            staged_cnt = 0
            modified_cnt = 0
            untracked_cnt = 0
            for line in out2.splitlines():
                if len(line) < 2:
                    continue
                x, y = line[0], line[1]
                if x == "?" and y == "?":
                    untracked_cnt += 1
                    continue
                if x not in (" ", "?"):
                    staged_cnt += 1
                if y not in (" ", "?"):
                    modified_cnt += 1
            print(f"  已暂存: {staged_cnt} | 已修改: {modified_cnt} | 未跟踪: {untracked_cnt}")
        else:
            print(c("  工作区干净", Colors.DIM))


def try_pull_with_stash(repo: Dict, remote: str, branch: str, pop: bool = True) -> Tuple[bool, List[Tuple[str, bool, str]]]:
    """拉取冲突时的救援: stash → pull → (可选 pop)，返回 (最终是否成功, 各步骤结果)"""
    steps = []
    cmd = ["git", "stash", "push", "-m", autostash_msg("before pull")]
    show_cmd(cmd)
    rc, _, err = run_cmd(cmd, repo["path"])
    steps.append(("stash", rc == 0, err if rc != 0 else ""))
    if rc != 0:
        return False, steps

    cmd = ["git", "pull", remote, branch]
    show_cmd(cmd)
    rc, _, err = run_cmd(cmd, repo["path"])
    pull_ok = rc == 0
    steps.append((f"pull {remote}/{branch}", pull_ok, err if rc != 0 else ""))

    if pop:
        cmd = ["git", "stash", "pop"]
        show_cmd(cmd)
        rc2, _, err2 = run_cmd(cmd, repo["path"])
        steps.append(("stash pop", rc2 == 0, err2 if rc2 != 0 else ""))
        return pull_ok and rc2 == 0, steps

    return pull_ok, steps


def git_pull(repos: List[Dict]):
    """拉取：支持多远程，批量自动处理"""
    for repo in repos:
        print(c(f"📥 {repo['name']}", Colors.BOLD + Colors.OKGREEN))

        remotes = select_remotes(repo, "选择要拉取的远程")
        if not remotes:
            continue

        branch = repo["current_branch"]
        for remote in remotes:
            cmd = ["git", "pull", remote, branch]
            show_cmd(cmd)
            print(f"  → 从 {c(remote, Colors.OKBLUE)} 拉取 {branch}...", end=" ")
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                print(c("✅ 成功", Colors.OKGREEN))
                if "Already up to date" in out or "已经是最新" in out:
                    print(c("     (已是最新)", Colors.DIM))
                elif out.strip():
                    for line in out.strip().splitlines()[:5]:
                        print(f"     {line}")
            elif err and "local changes" in err.lower():
                print(c("⚠️ 本地变更冲突", Colors.WARNING))
                print("    [1] stash→pull→pop(还原改动)  [2] stash→pull(改动留在stash)  [0] 跳过  [回车/空格=1]")
                sub = input("    >>> ").strip()
                if is_exit_key(sub):
                    print(c("    → 已跳过", Colors.DIM))
                else:
                    do_pop = (not sub or sub == "1")
                    label = " → pop" if do_pop else " (不pop)"
                    print(c(f"    自动 stash → pull{label} ...", Colors.DIM))
                    ok, steps = try_pull_with_stash(repo, remote, branch, pop=do_pop)
                    for step, sok, serr in steps:
                        if sok:
                            print(c(f"    ✅ {step}", Colors.OKGREEN))
                        else:
                            print(c(f"    ❌ {step}: {serr}", Colors.FAIL))
                    if not do_pop and ok:
                        print(c("    💡 本地改动仍在 stash 中 (git stash list 可查看/恢复)", Colors.DIM))
                    print(c(f"    结果: {'✅ 成功' if ok else '❌ 失败'}", Colors.OKGREEN if ok else Colors.FAIL))
            elif err and "unmerged files" in err.lower():
                print(c("❌ 失败", Colors.FAIL))
                print(c("     ⚠️ 仓库存在未解决的冲突，git 拒绝拉取。", Colors.WARNING))
                print(c("     请先解决冲突 (git status 查看) 或还原该仓库后再试。", Colors.WARNING))
                if err:
                    print(c(f"     {err}", Colors.FAIL))
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
            cmd = ["git", "fetch", remote]
            show_cmd(cmd)
            print(f"  → 获取 {c(remote, Colors.OKBLUE)}...", end=" ")
            rc, out, err = run_cmd(cmd, repo["path"])
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

        print("  [1] 全部暂存(add -A)  [2] 交互式暂存(-p)  [3] 指定文件  [0] 退出  [回车/空格=1]")
        choice = input("  >>> ").strip()

        if not choice or choice == "1":
            cmd = ["git", "add", "-A"]
            show_cmd(cmd)
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                print(c("  ✅ 全部暂存完成", Colors.OKGREEN))
            else:
                print(c(f"  ❌ 失败: {err}", Colors.FAIL))
        elif is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue
        elif choice == "2":
            print(c("  进入交互式暂存模式 (按提示操作，q退出)...", Colors.WARNING))
            run_cmd_interactive(["git", "add", "-p"], repo["path"])
        elif choice == "3":
            files = input("  文件路径(相对仓库根目录，空格分隔): ").strip()
            if files:
                cmd = ["git", "add"] + files.split()
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
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
            print("  [1] 输入提交信息  [2] 默认信息  [3] amend  [0] 退出  [回车/空格=2]")
            choice = input("  >>> ").strip()

            if is_exit_key(choice):
                print(c("  → 已退出", Colors.DIM))
                continue

            if not choice or choice == "2":
                msg = "update: batch commit"
            elif choice == "1":
                msg = input("  提交信息: ").strip()
                if not msg:
                    continue
            elif choice == "3":
                cmd = ["git", "commit", "--amend", "--no-edit"]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                if rc == 0:
                    print(c("  ✅ amend 完成", Colors.OKGREEN))
                else:
                    print(c(f"  ❌ 失败: {err}", Colors.FAIL))
                continue
            else:
                continue

        cmd = ["git", "commit", "-m", msg]
        show_cmd(cmd)
        rc, out, err = run_cmd(cmd, repo["path"])
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
            cmd = ["git", "push"]
            if force:
                cmd.append("--force")
            cmd += [remote, branch]
            show_cmd(cmd)
            print(f"  → 推送到 {c(remote, Colors.OKBLUE)}/{branch}...", end=" ")

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

        print("  [1] 新建分支  [2] 切换分支  [3] 删除分支  [4] 合并分支  [0] 退出  [回车/空格=0]")
        choice = input("  >>> ").strip()

        if not choice or is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue

        if choice == "1":
            name = input("  新分支名: ").strip()
            if name:
                cmd = ["git", "checkout", "-b", name]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已创建" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            name = input("  切换至: ").strip()
            if name:
                cmd = ["git", "checkout", name]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已切换" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            name = input("  删除分支: ").strip()
            if name:
                cmd = ["git", "branch", "-d", name]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                if rc != 0:
                    if input("  未合并，强制删除? (y/n): ").strip().lower() == "y":
                        cmd = ["git", "branch", "-D", name]
                        show_cmd(cmd)
                        rc, out, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已删除" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "4":
            name = input("  合并分支: ").strip()
            if name:
                cmd = ["git", "merge", name]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 合并成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))


def git_log(repos: List[Dict]):
    for repo in repos:
        print(c(f"📜 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        cmd = ["git", "log", "--oneline", "--graph", "--decorate", "-15"]
        show_cmd(cmd)
        rc, out, _ = run_cmd(cmd, repo["path"])
        if out:
            print(out)


def git_stash(repos: List[Dict]):
    for repo in repos:
        print(c(f"📂 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        rc, out, _ = run_cmd(["git", "stash", "list"], repo["path"])
        stash_lines = out.strip().splitlines() if out.strip() else []
        if stash_lines:
            print("当前 stash (仅名称):")
            for i, line in enumerate(stash_lines):
                print(f"    [{i}] {line}")
        else:
            print(c("  暂无 stash", Colors.DIM))

        print("  [1] 保存stash  [2] 弹出最新  [3] 查看内容  [4] 清空本仓库  [5] 清空所有仓库  [0] 退出  [m] 返回主菜单  [回车/空格=1]")
        choice = input("  >>> ").strip()

        if choice.lower() == "m":
            print(c("  → 已返回主菜单", Colors.DIM))
            return

        if is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue

        if not choice or choice == "1":
            msg = input("  备注(可选): ").strip()
            cmd = ["git", "stash", "push"]
            if msg:
                cmd += ["-m", msg]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
            print(c("  ✅ 保存成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            cmd = ["git", "stash", "pop"]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
            print(c("  ✅ 弹出成功" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            print("  可选 stash (仅名称):")
            if not stash_lines:
                print(c("  (无 stash)", Colors.DIM))
                continue
            for i, line in enumerate(stash_lines):
                print(f"    [{i}] {line}")
            idx = input("  查看第几个 (0=最新, 默认0, m=返回主菜单): ").strip()
            if idx.lower() == "m":
                print(c("  → 已返回主菜单", Colors.DIM))
                return
            if not idx:
                idx = "0"
            if not idx.lstrip("-").isdigit():
                print(c("  ❌ 请输入数字", Colors.FAIL))
                continue
            cmd = ["git", "stash", "show", "-p", f"stash@{{{idx}}}"]
            show_cmd(cmd)
            rc, out, _ = run_cmd(cmd, repo["path"])
            if out:
                print(out)
            elif rc == 0:
                print(c("  (无差异内容)", Colors.DIM))
        elif choice == "4":
            # 清空当前仓库所有 stash 是不可逆操作，需二次确认
            if input("  ⚠️ 将删除该仓库全部 stash 且不可恢复，确认? (y/n): ").strip().lower() == "y":
                cmd = ["git", "stash", "clear"]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已清空" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "5":
            # 清空所有选中仓库的 stash
            print(c(f"  ⚠️ 将清空 {len(repos)} 个仓库的全部 stash 且不可恢复！", Colors.FAIL + Colors.BOLD))
            if input("  确认清空所有仓库? (y/n): ").strip().lower() != "y":
                print(c("  → 已取消", Colors.DIM))
                continue
            for target in repos:
                cmd = ["git", "stash", "clear"]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, target["path"])
                if rc == 0:
                    print(c(f"  ✅ {target['name']} 已清空", Colors.OKGREEN))
                else:
                    print(c(f"  ❌ {target['name']}: {err}", Colors.FAIL))
            return


def git_reset(repos: List[Dict]):
    for repo in repos:
        print(c(f"↩️  {repo['name']}", Colors.BOLD + Colors.WARNING))
        print(c("  ⚠️ 重置可能丢失变更！", Colors.WARNING))
        print("  [1] 软重置(保留工作区)  [2] 混合重置(取消暂存)  [3] 硬重置(丢弃变更)  [0] 退出  [回车/空格=0]")
        choice = input("  >>> ").strip()

        if not choice or is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue

        if choice == "1":
            cmd = ["git", "reset", "--soft", "HEAD~1"]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
            print(c("  ✅ 完成" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            cmd = ["git", "reset", "--mixed", "HEAD"]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
            print(c("  ✅ 完成" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            if input("  ⚠️ 丢弃所有未提交变更，确认? (y/n): ").strip().lower() == "y":
                cmd = ["git", "reset", "--hard", "HEAD"]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
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


def git_remote_manage(repos: List[Dict]):
    """远程仓库管理：add / remove / set-url / rename"""
    for repo in repos:
        print(c(f"🔗 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
        if repo["remotes"]:
            print("  当前远程:")
            for remote, url in repo["remote_urls"].items():
                print(f"    {remote}: {c(url, Colors.DIM)}")
        else:
            print(c("  暂无远程", Colors.DIM))

        print("  [1] 添加远程  [2] 删除远程  [3] 修改URL  [4] 重命名  [0] 退出  [回车/空格=0]")
        choice = input("  >>> ").strip()

        if not choice or is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue

        if choice == "1":
            name = input("  远程名称 (如 origin): ").strip()
            url = input("  URL: ").strip()
            if name and url:
                cmd = ["git", "remote", "add", name, url]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已添加" if rc == 0 else f"  ❌ {err}",
                        Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            name = input("  要删除的远程名称: ").strip()
            if name:
                # 删除远程前确认
                if input(f"  确认删除 {name}? (y/n): ").strip().lower() == "y":
                    cmd = ["git", "remote", "remove", name]
                    show_cmd(cmd)
                    rc, _, err = run_cmd(cmd, repo["path"])
                    print(c("  ✅ 已删除" if rc == 0 else f"  ❌ {err}",
                            Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "3":
            name = input("  远程名称: ").strip()
            url = input("  新 URL: ").strip()
            if name and url:
                cmd = ["git", "remote", "set-url", name, url]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已修改" if rc == 0 else f"  ❌ {err}",
                        Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "4":
            old = input("  旧名称: ").strip()
            new = input("  新名称: ").strip()
            if old and new:
                cmd = ["git", "remote", "rename", old, new]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已重命名" if rc == 0 else f"  ❌ {err}",
                        Colors.OKGREEN if rc == 0 else Colors.FAIL))


def git_discard_files(repos: List[Dict]):
    """丢弃指定文件的工作区变更（不影响未跟踪文件，避免误删）"""
    for repo in repos:
        print(c(f"↩️  {repo['name']}", Colors.BOLD + Colors.WARNING))
        rc, out, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        if not out.strip():
            print(c("  没有变更", Colors.DIM))
            continue

        # 仅列出已跟踪文件的修改（XY 不为 "??"）
        tracked = []
        for line in out.splitlines():
            if len(line) < 3:
                continue
            status, path = line[:2], line[3:]
            if status == "??":
                continue
            tracked.append((status.strip(), path))

        if not tracked:
            print(c("  没有已跟踪文件的变更", Colors.DIM))
            continue

        print("  已修改的已跟踪文件:")
        for i, (st, f) in enumerate(tracked, 1):
            print(f"    [{i:2d}] {c(st, Colors.WARNING)} {f}")

        print("  [1] 丢弃全部已跟踪文件变更  [2] 指定文件  [0] 退出  [回车/空格=0]")
        choice = input("  >>> ").strip()

        if not choice or is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue

        target_files = []
        if choice == "1":
            if input("  ⚠️ 将丢弃所有已跟踪文件的工作区变更，确认? (y/n): ").strip().lower() == "y":
                cmd = ["git", "checkout", "--", "."]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已丢弃" if rc == 0 else f"  ❌ {err}",
                        Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "2":
            idx_input = input("  输入编号 (如 1,3,5 或 1-3): ").strip()
            for part in idx_input.split(","):
                part = part.strip()
                if "-" in part:
                    try:
                        s, e = map(int, part.split("-"))
                        for i in range(s, e + 1):
                            if 1 <= i <= len(tracked):
                                target_files.append(tracked[i - 1][1])
                    except ValueError:
                        pass
                else:
                    try:
                        i = int(part)
                        if 1 <= i <= len(tracked):
                            target_files.append(tracked[i - 1][1])
                    except ValueError:
                        pass

            if not target_files:
                print(c("  未选择文件", Colors.DIM))
                continue

            print("  将丢弃以下文件的变更:")
            for f in target_files:
                print(f"    {c(f, Colors.WARNING)}")
            if input("  确认? (y/n): ").strip().lower() == "y":
                cmd = ["git", "checkout", "--"] + target_files
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 已丢弃" if rc == 0 else f"  ❌ {err}",
                        Colors.OKGREEN if rc == 0 else Colors.FAIL))


def check_consistency(repos: List[Dict]):
    """跨仓库一致性检查：分支、远程、工作区、与远程的领先/落后"""
    print(c("🔍 跨仓库一致性检查", Colors.BOLD + Colors.HEADER))

    if len(repos) < 2:
        print(c("  仅一个仓库，无需检查", Colors.DIM))
        return

    # 1. 分支一致性
    print(c("\n📋 分支检查", Colors.BOLD + Colors.OKCYAN))
    branches = {}
    for repo in repos:
        branches.setdefault(repo["current_branch"], []).append(repo["name"])

    if len(branches) == 1:
        b = list(branches.keys())[0]
        print(c(f"  ✅ 所有仓库分支一致: {b}", Colors.OKGREEN))
    else:
        print(c("  ⚠️ 分支不一致:", Colors.WARNING))
        for b, names in branches.items():
            print(f"    {c(b, Colors.OKBLUE)} ({len(names)} 个):")
            for n in names:
                print(f"      - {n}")

    # 2. 远程名称一致性
    print(c("\n🔗 远程名称检查", Colors.BOLD + Colors.OKCYAN))
    remote_sets = {}
    for repo in repos:
        key = tuple(sorted(repo["remotes"]))
        remote_sets.setdefault(key, []).append(repo["name"])

    if len(remote_sets) == 1:
        rs = list(remote_sets.keys())[0]
        label = ", ".join(rs) if rs else "(无远程)"
        print(c(f"  ✅ 所有仓库远程一致: {label}", Colors.OKGREEN))
    else:
        print(c("  ⚠️ 远程配置不一致:", Colors.WARNING))
        for rs, names in remote_sets.items():
            label = ", ".join(rs) if rs else "(无远程)"
            print(f"    {c(label, Colors.OKBLUE)} ({len(names)} 个):")
            for n in names:
                print(f"      - {n}")

    # 3. 未提交变更检查
    print(c("\n● 工作区变更检查", Colors.BOLD + Colors.OKCYAN))
    dirty = [r["name"] for r in repos if r["has_changes"]]
    if dirty:
        print(c(f"  ⚠️ {len(dirty)} 个仓库有未提交变更:", Colors.WARNING))
        for n in dirty:
            print(f"    - {n}")
    else:
        print(c("  ✅ 所有仓库工作区干净", Colors.OKGREEN))

    # 4. 与远程的领先/落后状态
    print(c("\n📊 与远程同步状态", Colors.BOLD + Colors.OKCYAN))
    for repo in repos:
        if not repo["remotes"]:
            continue
        print(f"  {repo['name']}:")
        branch = repo["current_branch"]
        if branch == "HEAD":
            print(c("    (游离 HEAD，跳过)", Colors.DIM))
            continue
        for remote in repo["remotes"]:
            # rev-list --left-right --count A...B 输出 "<behind>\t<ahead>"
            rc, out, _ = run_cmd(
                ["git", "rev-list", "--left-right", "--count", f"{remote}/{branch}...{branch}"],
                repo["path"])
            if rc != 0 or not out.strip():
                print(c(f"    {remote}/{branch}: (无法比较，可能远程无此分支)",
                        Colors.DIM))
                continue
            parts = out.strip().split()
            if len(parts) != 2:
                continue
            behind, ahead = parts
            if behind == "0" and ahead == "0":
                print(f"    {remote}/{branch}: {c('同步', Colors.OKGREEN)}")
            else:
                info = []
                if int(ahead) > 0:
                    info.append(f"领先 {ahead}")
                if int(behind) > 0:
                    info.append(f"落后 {behind}")
                print(f"    {remote}/{branch}: {c(' / '.join(info), Colors.WARNING)}")




def get_default_branch(repo_path: str, remotes: List[str] = None) -> Tuple[str, Optional[str]]:
    """自动检测默认分支。返回 (分支名, 推荐远程名)；检测不到时远程名为 None。
    优先级：
    1. 遍历所有远程的 HEAD symbolic-ref（子模块多远程场景，不再只看 origin）
    2. 本地分支：main 优先于 master
    3. 各远程的 main/master：main 优先，远程按传入顺序
    4. 回退到当前分支（非 HEAD 游离态）
    """
    _, branch, _ = run_cmd(["git", "rev-parse", "--abbrev-ref", "HEAD"], repo_path)
    current = branch.strip()

    remote_list = remotes or []
    if not remote_list:
        _, ro, _ = run_cmd(["git", "remote"], repo_path)
        remote_list = [r.strip() for r in ro.strip().splitlines() if r.strip()]

    # 1. 各远程的 HEAD symbolic-ref（最权威）
    for remote in remote_list:
        _, out, _ = run_cmd(["git", "symbolic-ref", f"refs/remotes/{remote}/HEAD"], repo_path)
        if out.strip() and f"refs/remotes/{remote}/" in out:
            return out.strip().split("/")[-1], remote

    # 2. 本地分支（main 优先）
    _, out, _ = run_cmd(["git", "branch", "--list", "main", "master"], repo_path)
    branches = [b.strip().lstrip("* ") for b in out.strip().splitlines() if b.strip()]
    if "main" in branches:
        return "main", None
    if "master" in branches:
        return "master", None

    # 3. 远程分支（遍历所有远程，main 优先）
    _, out, _ = run_cmd(["git", "branch", "-r"], repo_path)
    remote_branches = set()
    for line in out.strip().splitlines():
        s = line.strip()
        if "->" in s:
            s = s.split("->", 1)[0].strip()
        if s:
            remote_branches.add(s)

    for remote in remote_list:
        if f"{remote}/main" in remote_branches:
            return "main", remote
    for remote in remote_list:
        if f"{remote}/master" in remote_branches:
            return "master", remote

    # 4. 回退
    return (current if current != "HEAD" else "main"), None


def checkout_latest(repos: List[Dict]):
    """
    将所有仓库切换到最前端（默认分支最新提交）
    流程: 保存当前变更(stash) → 切默认分支 → 拉取最新
    多远程歧义时自动用 git checkout --track <remote>/<branch> 消除
    """
    print(c("\n🎯 切换到最前端模式", Colors.BOLD + Colors.OKGREEN))
    print("  流程: 检测默认分支(main优先) → 切换 → 拉取最新")
    print("  如果工作区有未提交变更，会先自动 stash")
    print(c("  分支名匹配多个远程时，自动用 --track <remote>/<branch> 消除歧义",
            Colors.DIM))
    print(c("  任意参数录入阶段输入 0/q 都可取消当前操作\n", Colors.DIM))

    # 是否自动 stash
    auto_stash = confirm("  有未提交变更时自动 stash", default_yes=True, allow_exit=True)
    if auto_stash is None:
        return

    # 是否自动拉取
    auto_pull = confirm("  切换后自动拉取最新", default_yes=True, allow_exit=True)
    if auto_pull is None:
        return

    ok = confirm("  开始执行", default_yes=True, allow_exit=True)
    if ok is None or not ok:
        print(c("  → 已取消", Colors.DIM))
        return

    results = []
    for repo in repos:
        print(c(f"\n  ── {repo['name']} ──", Colors.OKCYAN))
        result = {"name": repo["name"], "steps": [], "status": "ok"}

        # 1. 检测默认分支（传入 remotes 以支持多远程）
        default_branch, preferred_remote = get_default_branch(repo["path"], repo["remotes"])
        print(f"    默认分支: {c(default_branch, Colors.OKBLUE)}"
              + (f"  (推荐远程: {preferred_remote})" if preferred_remote else ""))

        # 2. 检查是否有未提交变更
        _, diff_out, _ = run_cmd(["git", "status", "--porcelain"], repo["path"])
        has_changes = bool(diff_out.strip())

        if has_changes:
            if auto_stash:
                print(f"    检测到未提交变更，自动 stash...", end=" ")
                cmd = ["git", "stash", "push", "-m", autostash_msg("before checkout-latest")]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                if rc == 0:
                    print(c("✅", Colors.OKGREEN))
                    result["steps"].append(("stash", True))
                else:
                    print(c(f"❌ {err}", Colors.FAIL))
                    result["steps"].append(("stash", False))
                    result["status"] = "failed"
                    results.append(result)
                    continue
            else:
                print(c("    ⚠️ 有未提交变更，跳过 (未启用自动stash)", Colors.WARNING))
                result["steps"].append(("stash", None))
                result["status"] = "skipped_dirty"
                results.append(result)
                continue

        # 3. 切换分支（歧义失败时带 --track 用推荐远程重试）
        print(f"    切换到 {default_branch}...", end=" ")
        cmd = ["git", "checkout", default_branch]
        show_cmd(cmd)
        rc, _, err = run_cmd(cmd, repo["path"])
        if rc == 0:
            print(c("✅", Colors.OKGREEN))
            result["steps"].append((f"checkout {default_branch}", True))
        else:
            ambiguous = err and ("matched multiple" in err or "ambiguous" in err)
            if ambiguous and preferred_remote:
                print(c("⚠️ 分支名多义，重试...", Colors.WARNING))
                track_cmd = ["git", "checkout", "--track", f"{preferred_remote}/{default_branch}"]
                show_cmd(track_cmd)
                print(f"    用 {preferred_remote}/{default_branch} 重试...", end=" ")
                rc2, _, err2 = run_cmd(track_cmd, repo["path"])
                if rc2 == 0:
                    print(c("✅", Colors.OKGREEN))
                    result["steps"].append((f"checkout --track {preferred_remote}/{default_branch}", True))
                else:
                    print(c(f"❌ {err2}", Colors.FAIL))
                    result["steps"].append((f"checkout {default_branch}", False))
                    result["status"] = "failed"
                    results.append(result)
                    continue
            else:
                print(c(f"❌ {err}", Colors.FAIL))
                if ambiguous and not preferred_remote:
                    print(c("      💡 多个远程有同名分支且无法判定默认，建议用 [14] 远程管理确认后重试",
                            Colors.WARNING))
                result["steps"].append((f"checkout {default_branch}", False))
                result["status"] = "failed"
                results.append(result)
                continue

        # 4. 拉取最新（切分支后工作区已干净，直接拉；遇本地变更冲突走 stash 救援）
        if auto_pull:
            pull_any_failed = False
            for remote in repo["remotes"]:
                print(f"    从 {remote} 拉取 {default_branch}...", end=" ")
                cmd = ["git", "pull", remote, default_branch]
                show_cmd(cmd)
                rc, out, err = run_cmd(cmd, repo["path"])
                if rc == 0:
                    if "Already up to date" in out or "已经是最新" in out or "Already up-to-date" in out:
                        print(c("✅ 最新", Colors.OKGREEN))
                        result["steps"].append((f"pull({remote})", "uptodate"))
                    else:
                        print(c("✅ 更新", Colors.OKGREEN))
                        result["steps"].append((f"pull({remote})", "updated"))
                elif err and "local changes" in err.lower():
                    print(c("⚠️ 本地变更冲突，自动 stash→pull→pop", Colors.WARNING))
                    ok, steps = try_pull_with_stash(repo, remote, default_branch, pop=True)
                    for step, sok, serr in steps:
                        print(c(f"    {'✅' if sok else '❌'} {step}" + (f": {serr}" if not sok else ""),
                                Colors.OKGREEN if sok else Colors.FAIL))
                    result["steps"].append((f"pull({remote})", ok))
                    if not ok:
                        pull_any_failed = True
                else:
                    print(c("❌", Colors.FAIL))
                    if err:
                        print(c(f"      {err}", Colors.FAIL))
                    result["steps"].append((f"pull({remote})", False))
                    pull_any_failed = True
            if pull_any_failed:
                result["status"] = "pull_failed"

        results.append(result)

    # 详细汇总（四类：成功 / 切分支成功但拉取失败 / 切换失败 / 跳过）
    print(c("\n" + "═" * 60, Colors.HEADER))
    print(c("  切换到最前端结果汇总", Colors.HEADER + Colors.BOLD))
    print(c("═" * 60, Colors.HEADER))

    ok = [r["name"] for r in results if r["status"] == "ok"]
    pull_failed = [r["name"] for r in results if r["status"] == "pull_failed"]
    failed = [r["name"] for r in results if r["status"] == "failed"]
    skipped = [r["name"] for r in results if r["status"] == "skipped_dirty"]

    if ok:
        print(c(f"\n  ✅ 成功 ({len(ok)} 个):", Colors.OKGREEN))
        for n in ok:
            print(f"    - {n}")
    if pull_failed:
        print(c(f"\n  ⚠️ 切分支成功，但拉取部分失败 ({len(pull_failed)} 个):", Colors.WARNING))
        for n in pull_failed:
            print(f"    - {n}")
    if failed:
        print(c(f"\n  ❌ 切换失败 ({len(failed)} 个):", Colors.FAIL))
        for n in failed:
            print(f"    - {n}")
        print(c("  💡 常见原因: 分支名多义 / 本地有冲突修改 / 远程无此分支", Colors.WARNING))
    if skipped:
        print(c(f"\n  ⏭️  跳过(有未提交变更且未启用自动stash) ({len(skipped)} 个):", Colors.DIM))
        for n in skipped:
            print(f"    - {n}")

    total = len(results)
    print(c(f"\n  共 {total} 个仓库 | 成功 {len(ok)} | 拉取失败 {len(pull_failed)} | "
            f"切换失败 {len(failed)} | 跳过 {len(skipped)}", Colors.BOLD))


def git_submodule_update(repos: List[Dict]):
    main_repos = [r for r in repos if not r["is_submodule"] or r["rel_path"] == "."]
    for repo in main_repos:
        print(c(f"🔄 {repo['name']}", Colors.BOLD + Colors.OKGREEN))
        print("  [1] 初始化并更新  [2] 仅更新  [3] 递归更新  [0] 退出  [回车/空格=1]")
        choice = input("  >>> ").strip()

        if is_exit_key(choice):
            print(c("  → 已退出", Colors.DIM))
            continue
        if not choice:
            choice = "1"

        # 子模块更新输出实时滚动，使用交互模式直接继承 stdio
        cmd = None
        if choice == "1":
            cmd = ["git", "submodule", "update", "--init"]
        elif choice == "2":
            cmd = ["git", "submodule", "update"]
        elif choice == "3":
            cmd = ["git", "submodule", "update", "--init", "--recursive"]

        if cmd:
            show_cmd(cmd)
            rc = run_cmd_interactive(cmd, repo["path"])
            print(c("  ✅ 完成" if rc == 0 else f"  ❌ 失败 (code={rc})",
                    Colors.OKGREEN if rc == 0 else Colors.FAIL))


def quick_sync(repos: List[Dict]):
    """快速同步：暂存 -> 提交 -> 拉取 -> 推送，批量自动"""
    print(c("⚡ 快速同步模式", Colors.BOLD + Colors.OKGREEN))
    print("  流程: 暂存 → 提交 → 拉取 → 推送")
    print("  所有仓库将自动处理，无需逐个确认")
    print(c("  任意参数录入阶段输入 0/q 都可取消当前操作\n", Colors.DIM))

    # 提交信息：回车用默认；输入 0/q → 退出
    msg = prompt("  统一提交信息", default="update: batch sync")
    if msg is None:
        return

    # 是否强制推送
    force = confirm("  是否强制推送", default_yes=False, allow_exit=True)
    if force is None:
        return

    ok = confirm("  开始执行", default_yes=True, allow_exit=True)
    if ok is None or not ok:
        print(c("  → 已取消", Colors.DIM))
        return

    results = []
    for repo in repos:
        print(c(f"  ── {repo['name']} ──", Colors.OKCYAN))
        result = {"name": repo["name"], "steps": []}

        # 1. 暂存
        cmd = ["git", "add", "-A"]
        show_cmd(cmd)
        rc, _, _ = run_cmd(cmd, repo["path"])
        result["steps"].append(("暂存", rc == 0))

        # 2. 提交
        rc, _, _ = run_cmd(["git", "diff", "--cached", "--quiet"], repo["path"])
        if rc != 0:
            cmd = ["git", "commit", "-m", msg]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
            result["steps"].append(("提交", rc == 0))
        else:
            result["steps"].append(("提交", None))  # None = 无变更

        # 3. 拉取（所有远程）
        branch = repo["current_branch"]
        pull_ok = True
        for remote in repo["remotes"]:
            cmd = ["git", "pull", remote, branch]
            show_cmd(cmd)
            rc, _, err = run_cmd(cmd, repo["path"])
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
            show_cmd(cmd)
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
    print_batch_stats(results)


def print_batch_stats(results: List[Dict]):
    """输出批量操作的统计信息（成功/失败/跳过计数），并列出失败项名称"""
    failed = sum(1 for r in results for _, ok in r["steps"] if ok is False)
    succeeded = sum(1 for r in results for _, ok in r["steps"] if ok is True)
    skipped = sum(1 for r in results for _, ok in r["steps"] if ok is None)
    if failed:
        print(c(f"\n  ⚠️ 失败 {failed} 项 | 成功 {succeeded} 项 | 跳过 {skipped} 项",
                Colors.WARNING))
        fail_items = [f"{r['name']} · {step}" for r in results for step, ok in r["steps"] if ok is False]
        for i, item in enumerate(fail_items, 1):
            print(c(f"     失败项 {i}: {item}", Colors.FAIL))
    else:
        print(c(f"\n  ✅ 全部成功 ({succeeded} 项，跳过 {skipped} 项)", Colors.OKGREEN))


def batch_pull(repos: List[Dict]):
    """批量拉取所有远程，三态分类(有更新/已是最新/失败) + 冲突感知"""
    print(c("📥 批量拉取模式", Colors.BOLD + Colors.OKGREEN))
    print("  将自动从所有远程拉取当前分支")
    print(c("  命令格式: git pull <remote> <branch>", Colors.DIM))
    print("  遇本地变更冲突时: [1] stash→pull→pop  [2] stash→pull(不pop)  [3] 跳过  [0] 退出  [回车/空格=1]")
    choice = input("  >>> ").strip()

    if is_exit_key(choice):
        print(c("  → 已退出", Colors.DIM))
        return
    if not choice or choice == "1":
        strategy = "pop"
    elif choice == "2":
        strategy = "nopop"
    else:
        strategy = "skip"

    results = []
    for repo in repos:
        print(c(f"\n  ── {repo['name']} ──", Colors.OKCYAN))
        branch = repo["current_branch"]
        result = {"name": repo["name"], "steps": [], "status": "skip"}

        if not repo["remotes"]:
            print(c("    无远程仓库，跳过", Colors.DIM))
            result["status"] = "no_remote"
            results.append(result)
            continue

        any_failed = False
        any_updated = False
        for remote in repo["remotes"]:
            cmd = ["git", "pull", remote, branch]
            show_cmd(cmd)
            print(f"    → {remote}/{branch}...", end=" ")
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                if "Already up to date" in out or "已经是最新" in out or "Already up-to-date" in out:
                    print(c("✅ 已是最新", Colors.OKGREEN))
                    result["steps"].append((f"{remote}/{branch}", "uptodate"))
                else:
                    print(c("✅ 有更新", Colors.OKGREEN))
                    result["steps"].append((f"{remote}/{branch}", "updated"))
                    any_updated = True
            elif err and "local changes" in err.lower() and strategy != "skip":
                do_pop = (strategy == "pop")
                label = " → pop" if do_pop else " (不pop)"
                print(c(f"⚠️ 本地变更冲突，自动 stash → pull{label}", Colors.WARNING))
                ok, steps = try_pull_with_stash(repo, remote, branch, pop=do_pop)
                for step, sok, serr in steps:
                    if sok:
                        print(c(f"    ✅ {step}", Colors.OKGREEN))
                    else:
                        print(c(f"    ❌ {step}: {serr}", Colors.FAIL))
                if not do_pop and ok:
                    print(c("    💡 本地改动仍在 stash 中 (git stash list 可查看/恢复)", Colors.DIM))
                print(c(f"    结果: {'✅ 成功' if ok else '❌ 失败'}", Colors.OKGREEN if ok else Colors.FAIL))
                result["steps"].append((f"{remote}/{branch}", "updated" if ok else "failed"))
                if ok:
                    any_updated = True
                else:
                    any_failed = True
            else:
                print(c("❌ 失败", Colors.FAIL))
                if err and "unmerged files" in err.lower():
                    print(c("      ⚠️ 存在未解决的冲突，请先解决 (git status) 或还原该仓库。", Colors.WARNING))
                elif err:
                    print(c(f"      {err}", Colors.FAIL))
                result["steps"].append((f"{remote}/{branch}", "failed"))
                any_failed = True

        # 仓库级状态：失败优先，其次有更新，最后全是最新的
        if any_failed:
            result["status"] = "failed"
        elif any_updated:
            result["status"] = "updated"
        else:
            result["status"] = "uptodate"
        results.append(result)

    # 分类汇总
    print(c("\n" + "═" * 60, Colors.HEADER))
    print(c("  批量拉取结果汇总", Colors.HEADER + Colors.BOLD))
    print(c("═" * 60, Colors.HEADER))

    updated = [r["name"] for r in results if r["status"] == "updated"]
    uptodate = [r["name"] for r in results if r["status"] == "uptodate"]
    failed = [r["name"] for r in results if r["status"] == "failed"]
    no_remote = [r["name"] for r in results if r["status"] == "no_remote"]

    if updated:
        print(c(f"\n  ✅ 有更新 ({len(updated)} 个):", Colors.OKGREEN))
        for n in updated:
            print(f"    - {n}")
    if uptodate:
        print(c(f"\n  ⏭️  已是最新，无需拉取 ({len(uptodate)} 个):", Colors.DIM))
        for n in uptodate:
            print(f"    - {n}")
    if failed:
        print(c(f"\n  ❌ 拉取失败 ({len(failed)} 个):", Colors.FAIL))
        for n in failed:
            print(f"    - {n}")
        print(c("  💡 通常因本地有未提交变更或未解决冲突，建议先 commit/stash 或用 [9] Stash 管理",
                Colors.WARNING))
    if no_remote:
        print(c(f"\n  ⚪ 无远程仓库 ({len(no_remote)} 个):", Colors.DIM))
        for n in no_remote:
            print(f"    - {n}")

    total = len(results)
    print(c(f"\n  共 {total} 个仓库 | 更新 {len(updated)} | 最新 {len(uptodate)} | "
            f"失败 {len(failed)} | 无远程 {len(no_remote)}", Colors.BOLD))


def batch_push(repos: List[Dict]):
    """批量推送到所有远程，三态分类(已推送/已是最新/失败)"""
    print(c("🚀 批量推送模式", Colors.BOLD + Colors.OKGREEN))
    print("  将自动推送到所有远程的当前分支")
    print(c("  命令格式: git push [--force] <remote> <branch>", Colors.DIM))
    print(c("  任意参数录入阶段输入 0/q 都可取消当前操作\n", Colors.DIM))

    force = confirm("  强制推送", default_yes=False, allow_exit=True)
    if force is None:
        return

    ok = confirm("  开始执行", default_yes=True, allow_exit=True)
    if ok is None or not ok:
        print(c("  → 已取消", Colors.DIM))
        return

    results = []
    for repo in repos:
        print(c(f"\n  ── {repo['name']} ──", Colors.OKCYAN))
        branch = repo["current_branch"]
        result = {"name": repo["name"], "steps": [], "status": "skip"}

        if not repo["remotes"]:
            print(c("    无远程仓库，跳过", Colors.DIM))
            result["status"] = "no_remote"
            results.append(result)
            continue

        any_failed = False
        any_pushed = False
        any_uptodate = False
        for remote in repo["remotes"]:
            cmd = ["git", "push"]
            if force:
                cmd.append("--force")
            cmd += [remote, branch]
            show_cmd(cmd)
            print(f"    → {remote}/{branch}...", end=" ")
            rc, out, err = run_cmd(cmd, repo["path"])
            if rc == 0:
                if "Everything up-to-date" in (err or "") or "Everything up-to-date" in (out or ""):
                    print(c("✅ 已是最新", Colors.OKGREEN))
                    result["steps"].append((f"{remote}/{branch}", "uptodate"))
                    any_uptodate = True
                else:
                    print(c("✅ 已推送", Colors.OKGREEN))
                    result["steps"].append((f"{remote}/{branch}", "pushed"))
                    any_pushed = True
            else:
                print(c("❌ 失败", Colors.FAIL))
                if err:
                    print(c(f"      {err}", Colors.FAIL))
                result["steps"].append((f"{remote}/{branch}", "failed"))
                any_failed = True

        if any_failed:
            result["status"] = "failed"
        elif any_pushed:
            result["status"] = "pushed"
        else:
            result["status"] = "uptodate"
        results.append(result)

    # 分类汇总
    print(c("\n" + "═" * 60, Colors.HEADER))
    print(c("  批量推送结果汇总", Colors.HEADER + Colors.BOLD))
    print(c("═" * 60, Colors.HEADER))

    pushed = [r["name"] for r in results if r["status"] == "pushed"]
    uptodate = [r["name"] for r in results if r["status"] == "uptodate"]
    failed = [r["name"] for r in results if r["status"] == "failed"]
    no_remote = [r["name"] for r in results if r["status"] == "no_remote"]

    if pushed:
        print(c(f"\n  ✅ 已推送 ({len(pushed)} 个):", Colors.OKGREEN))
        for n in pushed:
            print(f"    - {n}")
    if uptodate:
        print(c(f"\n  ⏭️  已是最新，无需推送 ({len(uptodate)} 个):", Colors.DIM))
        for n in uptodate:
            print(f"    - {n}")
    if failed:
        print(c(f"\n  ❌ 推送失败 ({len(failed)} 个):", Colors.FAIL))
        for n in failed:
            print(f"    - {n}")
    if no_remote:
        print(c(f"\n  ⚪ 无远程仓库 ({len(no_remote)} 个):", Colors.DIM))
        for n in no_remote:
            print(f"    - {n}")

    total = len(results)
    print(c(f"\n  共 {total} 个仓库 | 推送 {len(pushed)} | 最新 {len(uptodate)} | "
            f"失败 {len(failed)} | 无远程 {len(no_remote)}", Colors.BOLD))


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

    try:
        main_loop(repos, root_path, args)
    except KeyboardInterrupt:
        print(c("\n👋 已中断，再见！", Colors.OKGREEN))
        sys.exit(0)


def main_loop(repos, root_path, args):
    """主交互循环，单独抽出便于 KeyboardInterrupt 捕获"""
    while True:
        print(c("" + "═" * 60, Colors.HEADER))
        print(c("  Git 批量管理菜单", Colors.HEADER + Colors.BOLD))
        print(c("═" * 60, Colors.HEADER))
        print("""
  [1]  📊 查看状态 (status)         → git status -sb
  [2]  📥 拉取代码 (pull)           → git pull <remote> <branch> (冲突自动stash/pop)
  [3]  🌐 获取远程 (fetch)          → git fetch <remote>
  [4]  📦 暂存文件 (add)            → git add -A
  [5]  💾 提交变更 (commit)         → git commit -m "msg"
  [6]  🚀 推送代码 (push)           → git push <remote> <branch>
  [7]  🌿 分支管理 (branch)         → git branch / checkout / merge
  [8]  📜 查看日志 (log)            → git log --oneline -15
  [9]  📂 Stash管理                 → git stash push/pop/list/clear
  [10] ↩️  重置操作 (reset)          → git reset --soft/mixed/hard
  [11] 🔗 查看所有远程URL           → git remote -v
  [12] 🔄 子模块更新                → git submodule update
  [13] 🗑️  丢弃文件变更 (discard)   → git checkout -- <file>
  [14] 🔧 远程仓库管理              → git remote add/remove/set-url/rename
  ──────────────────────────────────────────────────
  [21] ⚡ 快速同步                 → git add -A + commit + pull + push
  [22] 📥 批量拉取 (所有远程)       → git pull 全远程 (冲突自动stash/pop)
  [23] 🚀 批量推送 (所有远程)       → git push 全远程
  [24] 📦 批量暂存 (所有仓库)       → git add -A
  [25] 💾 批量提交 (所有仓库)       → git commit -m "msg"
  [26] 🎯 切换到最前端              → git checkout <默认分支> + pull
  [27] 🔍 跨仓库一致性检查          → 分支/远程/同步状态对比
  ──────────────────────────────────────────────────
  [99] 📝 重新扫描仓库
  [0]  ❌ 退出
        """)

        choice = input("请选择操作 [回车=刷新菜单, 0/q=退出] >>> ").strip()

        if is_exit_key(choice):
            print(c("👋 再见！", Colors.OKGREEN))
            break

        if not choice:
            continue

        if choice == "99":
            print(c("🔍 重新扫描...", Colors.OKCYAN))
            repos = find_git_repos(root_path, args.depth)
            show_repo_list(repos)
            continue

        # 需要选择仓库的操作（默认全选）
        if choice in ("11", "12"):
            # 查看类操作直接使用全部仓库
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
        elif choice == "13":
            git_discard_files(selected)
        elif choice == "14":
            git_remote_manage(selected)
        elif choice == "21":
            quick_sync(selected)
        elif choice == "22":
            batch_pull(selected)
        elif choice == "23":
            batch_push(selected)
        elif choice == "24":
            for repo in selected:
                print(c(f"\n📦 {repo['name']}", Colors.BOLD + Colors.OKCYAN))
                cmd = ["git", "add", "-A"]
                show_cmd(cmd)
                rc, _, err = run_cmd(cmd, repo["path"])
                print(c("  ✅ 全部暂存" if rc == 0 else f"  ❌ {err}", Colors.OKGREEN if rc == 0 else Colors.FAIL))
        elif choice == "25":
            msg = prompt("统一提交信息", default="update: batch commit")
            if msg is not None:
                git_commit(selected, batch_msg=msg)
        elif choice == "26":
            checkout_latest(selected)
        elif choice == "27":
            check_consistency(selected)
        else:
            print(c("无效选项", Colors.FAIL))

        input("\n按回车继续...")


if __name__ == "__main__":
    main()
