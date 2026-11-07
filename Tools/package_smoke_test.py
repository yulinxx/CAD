#!/usr/bin/env python3
# -*- coding: utf-8 -*-
r"""
SanYiCAD 安装包冒烟测试。

覆盖三条路径：
  1. staged：用 `cmake --install` 安装到临时前缀，验证文件布局、运行 --version、
     重复安装（升级/覆盖）、按 install_manifest 卸载；
  2. nsis：Windows 下真正调用安装包静默安装、运行、再次安装（升级路径）、
     调用 uninstall.exe 静默卸载；
  3. dmg / deb：挂载/解包后验证文件布局，模拟升级（覆盖）和卸载（删除目录）。
     这两种模式不调用系统级安装器，避免 CI 权限和系统污染；真正的 OS 级卸载
     仍由人工/专项测试覆盖。

设计目标：
  - 只依赖 Python 标准库；
  - 每一步失败都打印足够上下文，便于定位是“包缺文件”还是“程序起不来”；
  - 不碰用户真实安装目录，所有操作都在临时工作目录内完成。

用法：
    # 1) 对已有构建树做 staged 冒烟
    python3 Tools/package_smoke_test.py --build-dir build

    # 2) 对 Windows NSIS 安装包做真实安装/升级/卸载
    python3 Tools/package_smoke_test.py --installer build/SanYiCAD-1.0.0-win64.exe

    # 3) 用上一版安装包验证跨版本升级
    python3 Tools/package_smoke_test.py \
        --installer build-new/SanYiCAD-1.0.1-win64.exe \
        --previous-installer build-old/SanYiCAD-1.0.0-win64.exe \
        --expected-version 1.0.1 --previous-version 1.0.0

    # 4) macOS 构建树 staged 测试：把 Qt 框架路径显式传给程序
    python3 Tools/package_smoke_test.py --build-dir build --qt-dir "$HOME/Qt/6.11.1/macos"
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Callable, Dict, List, Optional, Sequence


APP_NAME = "SanYiCAD"


class SmokeError(RuntimeError):
    pass


@dataclass
class Context:
    work_dir: Path
    timeout: int
    no_run: bool
    keep: bool
    qt_dir: Optional[Path]
    verbose: bool


def log(message: str) -> None:
    print(f"[smoke] {message}", flush=True)


def run(
    cmd: Sequence[object],
    *,
    cwd: Optional[Path] = None,
    env: Optional[Dict[str, str]] = None,
    timeout: int = 120,
    check: bool = False,
    quiet: bool = False,
) -> subprocess.CompletedProcess:
    cmd_str = " ".join(str(part) for part in cmd)
    if not quiet:
        log(f"$ {cmd_str}")
    try:
        completed = subprocess.run(
            [str(part) for part in cmd],
            cwd=str(cwd) if cwd else None,
            env=env,
            stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=timeout,
        )
    except FileNotFoundError as exc:
        raise SmokeError(f"命令不存在: {cmd[0]}") from exc
    except subprocess.TimeoutExpired as exc:
        output = exc.stdout or ""
        if isinstance(output, bytes):
            output = output.decode("utf-8", errors="replace")
        raise SmokeError(f"命令超时（{timeout}s）: {cmd_str}\n{output}") from exc

    if completed.stdout and not quiet:
        print(completed.stdout.rstrip())

    if check and completed.returncode != 0:
        raise SmokeError(
            f"命令失败（exit={completed.returncode}）: {cmd_str}\n{completed.stdout or ''}"
        )
    return completed


def wait_for(predicate: Callable[[], bool], timeout: int, interval: float = 0.5) -> bool:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if predicate():
            return True
        time.sleep(interval)
    return predicate()


def is_windows() -> bool:
    return sys.platform.startswith("win")


def is_macos() -> bool:
    return sys.platform == "darwin"


def is_linux() -> bool:
    return sys.platform.startswith("linux")


def find_executable(install_dir: Path) -> Optional[Path]:
    candidates: List[Path] = []
    if is_windows():
        candidates.extend(
            [
                install_dir / "bin" / f"{APP_NAME}.exe",
                install_dir / f"{APP_NAME}.exe",
            ]
        )
    elif is_macos():
        candidates.extend(
            [
                install_dir / f"{APP_NAME}.app" / "Contents" / "MacOS" / APP_NAME,
                install_dir / "Applications" / f"{APP_NAME}.app" / "Contents" / "MacOS" / APP_NAME,
            ]
        )
    else:
        candidates.extend(
            [
                install_dir / "bin" / APP_NAME,
                install_dir / "opt" / APP_NAME / "bin" / APP_NAME,
                install_dir / "usr" / "bin" / APP_NAME,
            ]
        )

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def find_app_bundle(install_dir: Path) -> Optional[Path]:
    direct = install_dir / f"{APP_NAME}.app"
    if direct.is_dir():
        return direct
    matches = sorted(install_dir.glob(f"**/{APP_NAME}.app"))
    return matches[0] if matches else None


def check_layout(install_dir: Path, mode: str) -> Path:
    log(f"检查安装布局: {install_dir}")
    if not install_dir.exists():
        raise SmokeError(f"安装目录不存在: {install_dir}")

    exe = find_executable(install_dir)
    if exe is None:
        raise SmokeError(
            f"找不到主程序 {APP_NAME}（mode={mode}）。目录内容：\n"
            + "\n".join(str(path) for path in sorted(install_dir.iterdir())[:50])
        )

    if is_macos():
        app = find_app_bundle(install_dir)
        if app is None:
            raise SmokeError("找不到 SanYiCAD.app")
        plist = app / "Contents" / "Info.plist"
        if not plist.is_file():
            raise SmokeError(f"macOS bundle 缺少 Info.plist: {plist}")
        lib_dir = install_dir / "bin"
    else:
        lib_dir = exe.parent

    if is_windows():
        dlls = sorted(path.name for path in lib_dir.glob("*.dll"))
        if len(dlls) < 3:
            raise SmokeError(f"Windows 安装目录 DLL 数量异常（{len(dlls)} < 3）: {lib_dir}")
        if not any(name.lower() == "qt6core.dll" for name in dlls):
            raise SmokeError(f"未找到 Qt6Core.dll，windeployqt 可能没有执行: {lib_dir}")
        platforms = lib_dir / "platforms"
        if not platforms.is_dir():
            raise SmokeError(f"未找到 Qt platforms 插件目录: {platforms}")
    elif is_linux():
        libs = sorted(lib_dir.glob("*.so*"))
        if len(libs) < 3:
            raise SmokeError(f"Linux 安装目录共享库数量异常（{len(libs)} < 3）: {lib_dir}")
    elif is_macos():
        libs = sorted(lib_dir.glob("*.dylib"))
        if len(libs) < 3:
            raise SmokeError(f"macOS 安装目录 dylib 数量异常（{len(libs)} < 3）: {lib_dir}")

    for optional in ("translations", "resources"):
        candidate = lib_dir / optional
        if not candidate.exists():
            log(f"提示: 可选目录不存在（不阻塞）: {candidate}")

    log(f"主程序: {exe}")
    return exe


def build_runtime_env(exe: Path, qt_dir: Optional[Path]) -> Dict[str, str]:
    env = os.environ.copy()
    bin_dir = str(exe.parent)

    if is_windows():
        path_parts = [bin_dir]
        if qt_dir:
            path_parts.append(str(qt_dir / "bin"))
        env["PATH"] = os.pathsep.join(path_parts + [env.get("PATH", "")])
        return env

    lib_parts = [bin_dir]
    if qt_dir:
        lib_parts.append(str(qt_dir / "lib"))

    if is_macos():
        env["DYLD_LIBRARY_PATH"] = os.pathsep.join(lib_parts)
        env["DYLD_FALLBACK_LIBRARY_PATH"] = os.pathsep.join(lib_parts)
        if qt_dir:
            env["DYLD_FRAMEWORK_PATH"] = str(qt_dir / "lib")
            plugin_dir = qt_dir / "plugins"
            if plugin_dir.is_dir():
                env["QT_PLUGIN_PATH"] = str(plugin_dir)
    else:
        env["LD_LIBRARY_PATH"] = os.pathsep.join(lib_parts)
        if qt_dir:
            plugin_dir = qt_dir / "plugins"
            if plugin_dir.is_dir():
                env["QT_PLUGIN_PATH"] = str(plugin_dir)

    return env


def run_version(exe: Path, expected: Optional[str], context: Context) -> str:
    if context.no_run:
        log("跳过运行 --version（--no-run）")
        return ""

    env = build_runtime_env(exe, context.qt_dir)
    completed = run([exe, "--version"], env=env, timeout=30)
    output = (completed.stdout or "").strip()
    if completed.returncode != 0:
        hint = ""
        if is_macos():
            hint = (
                "\n提示: macOS staged 安装不会自动把 Qt 框架打进 .app；"
                "可用 --qt-dir 指向 Qt 安装目录，或先补 macdeployqt。"
            )
        raise SmokeError(
            f"安装后的程序无法运行（exit={completed.returncode}）: {exe}\n{output}{hint}"
        )

    match = re.search(rf"{re.escape(APP_NAME)}\s+(\d+\.\d+\.\d+)", output)
    if not match:
        raise SmokeError(f"--version 输出无法识别:\n{output}")
    actual = match.group(1)
    if expected and actual != expected:
        raise SmokeError(f"版本不匹配: 期望 {expected}，实际 {actual}")
    log(f"运行正常，版本 {actual}")
    return actual


def detect_project_version(build_dir: Optional[Path]) -> Optional[str]:
    if build_dir:
        cache = build_dir / "CMakeCache.txt"
        if cache.is_file():
            text = cache.read_text(encoding="utf-8", errors="replace")
            match = re.search(
                r"^(?:CMAKE_PROJECT_VERSION|PROJECT_VERSION):[^=]*=(\d+\.\d+\.\d+)",
                text,
                re.MULTILINE,
            )
            if match:
                return match.group(1)

    root_cmake = Path(__file__).resolve().parent.parent / "CMakeLists.txt"
    if root_cmake.is_file():
        text = root_cmake.read_text(encoding="utf-8", errors="replace")
        match = re.search(r"project\s*\(\s*\w+[\s\S]*?VERSION\s+(\d+\.\d+\.\d+)", text)
        if match:
            return match.group(1)
    return None


def install_with_cmake(build_dir: Path, prefix: Path, config: Optional[str], timeout: int) -> None:
    if prefix.exists():
        shutil.rmtree(prefix)
    cmd: List[object] = ["cmake", "--install", str(build_dir), "--prefix", str(prefix)]
    if config:
        cmd.extend(["--config", config])
    run(cmd, timeout=timeout, check=True)


def uninstall_staged(prefix: Path, build_dir: Path, exe: Path) -> None:
    manifest = build_dir / "install_manifest.txt"
    if manifest.is_file():
        log(f"按 install_manifest.txt 卸载: {manifest}")
        for line in manifest.read_text(encoding="utf-8", errors="replace").splitlines():
            path = Path(line.strip())
            if not path:
                continue
            try:
                path.resolve().relative_to(prefix.resolve())
            except ValueError:
                continue
            if path.is_file() or path.is_symlink():
                path.unlink(missing_ok=True)
            elif path.is_dir():
                shutil.rmtree(path, ignore_errors=True)

    # 清掉 manifest 没有覆盖的空目录；staged 模式的“卸载”等价于删除临时前缀。
    shutil.rmtree(prefix, ignore_errors=True)
    if exe.exists():
        raise SmokeError(f"卸载后主程序仍存在: {exe}")
    log("staged 卸载完成")


def mode_staged(args: argparse.Namespace, context: Context, expected: Optional[str]) -> None:
    if not args.build_dir:
        raise SmokeError("staged 模式需要 --build-dir")
    build_dir = args.build_dir.resolve()
    if not (build_dir / "CMakeCache.txt").is_file():
        raise SmokeError(f"不是有效的 CMake 构建目录（缺 CMakeCache.txt）: {build_dir}")

    prefix = context.work_dir / "install"
    install_with_cmake(build_dir, prefix, args.config, context.timeout)
    exe = check_layout(prefix, "staged")
    run_version(exe, expected, context)

    if args.previous_build_dir:
        previous_build = args.previous_build_dir.resolve()
        log("== 安装上一版本（升级前置） ==")
        install_with_cmake(previous_build, prefix, args.config, context.timeout)
        check_layout(prefix, "staged")
        run_version(exe, args.previous_version, context)

    log("== 覆盖安装（升级/幂等路径） ==")
    install_with_cmake(build_dir, prefix, args.config, context.timeout)
    exe = check_layout(prefix, "staged")
    run_version(exe, expected, context)

    log("== 卸载 ==")
    uninstall_staged(prefix, build_dir, exe)


def find_uninstaller(install_dir: Path) -> Optional[Path]:
    for candidate in install_dir.glob("*ninstall*.exe"):
        if candidate.is_file():
            return candidate
    for candidate in install_dir.glob("*ninstall*.exe"):
        if candidate.is_file():
            return candidate
    return None


def mode_nsis(args: argparse.Namespace, context: Context, expected: Optional[str]) -> None:
    if not is_windows():
        raise SmokeError("nsis 模式只能在 Windows 上运行")
    installer = args.installer.resolve()
    if not installer.is_file():
        raise SmokeError(f"安装包不存在: {installer}")

    install_dir = context.work_dir / "install"
    if install_dir.exists():
        shutil.rmtree(install_dir)

    def install(package: Path) -> None:
        log(f"静默安装: {package}")
        run([package, "/S", f"/D={install_dir}"], timeout=context.timeout, check=True)
        if not wait_for(lambda: find_executable(install_dir) is not None, context.timeout):
            raise SmokeError(f"安装后未找到 {APP_NAME}.exe: {install_dir}")

    def uninstall() -> None:
        uninstaller = find_uninstaller(install_dir)
        if uninstaller is None:
            raise SmokeError(f"找不到 uninstall.exe: {install_dir}")
        log(f"静默卸载: {uninstaller}")
        run([uninstaller, "/S"], timeout=context.timeout, check=True)
        if not wait_for(
            lambda: find_executable(install_dir) is None or not install_dir.exists(),
            context.timeout,
        ):
            raise SmokeError(f"卸载后主程序仍存在: {install_dir}")

    install(installer)
    exe = check_layout(install_dir, "nsis")
    run_version(exe, expected, context)

    if args.previous_installer:
        previous = args.previous_installer.resolve()
        if not previous.is_file():
            raise SmokeError(f"上一版安装包不存在: {previous}")
        log("== 安装上一版本 ==")
        install(previous)
        exe = check_layout(install_dir, "nsis")
        run_version(exe, args.previous_version, context)

    log("== 覆盖安装（升级路径） ==")
    install(installer)
    exe = check_layout(install_dir, "nsis")
    run_version(exe, expected, context)

    log("== 卸载 ==")
    uninstall()

    if install_dir.exists():
        leftovers = sorted(path.name for path in install_dir.iterdir())
        if leftovers:
            log(f"提示: 卸载后目录仍有残留文件（不一定是错误）: {leftovers[:20]}")


def copy_app_from_dmg(installer: Path, install_dir: Path, timeout: int) -> None:
    mount = install_dir.parent / "dmg"
    mount.mkdir(parents=True, exist_ok=True)
    run(["hdiutil", "attach", str(installer), "-nobrowse", "-readonly", "-mountpoint", str(mount)], timeout=timeout, check=True)
    try:
        apps = sorted(mount.glob("*.app"))
        if not apps:
            raise SmokeError(f"DMG 中找不到 .app: {installer}")
        if install_dir.exists():
            shutil.rmtree(install_dir)
        install_dir.mkdir(parents=True, exist_ok=True)
        shutil.copytree(apps[0], install_dir / apps[0].name, symlinks=True)
        # 与 .app 同级的 bin/ 一起复制，保持“可执行文件 + 动态库”的完整布局。
        sibling_bin = mount / "bin"
        if sibling_bin.is_dir():
            shutil.copytree(sibling_bin, install_dir / "bin", symlinks=True)
    finally:
        run(["hdiutil", "detach", str(mount), "-force"], timeout=timeout, check=False, quiet=True)


def mode_dmg(args: argparse.Namespace, context: Context, expected: Optional[str]) -> None:
    if not is_macos():
        raise SmokeError("dmg 模式只能在 macOS 上运行")
    installer = args.installer.resolve()
    if not installer.is_file():
        raise SmokeError(f"安装包不存在: {installer}")

    install_dir = context.work_dir / "install"
    copy_app_from_dmg(installer, install_dir, context.timeout)
    exe = check_layout(install_dir, "dmg")
    run_version(exe, expected, context)

    if args.previous_installer:
        previous = args.previous_installer.resolve()
        log("== 覆盖上一版本（升级路径） ==")
        copy_app_from_dmg(previous, install_dir, context.timeout)
        check_layout(install_dir, "dmg")
        run_version(exe, args.previous_version, context)
        copy_app_from_dmg(installer, install_dir, context.timeout)
        exe = check_layout(install_dir, "dmg")
        run_version(exe, expected, context)

    log("== 卸载（删除 .app，模拟） ==")
    shutil.rmtree(install_dir, ignore_errors=True)
    if exe.exists():
        raise SmokeError(f"卸载后主程序仍存在: {exe}")


def mode_deb(args: argparse.Namespace, context: Context, expected: Optional[str]) -> None:
    if not is_linux():
        raise SmokeError("deb 模式只能在 Linux 上运行")
    installer = args.installer.resolve()
    if not installer.is_file():
        raise SmokeError(f"安装包不存在: {installer}")

    install_dir = context.work_dir / "install"
    if install_dir.exists():
        shutil.rmtree(install_dir)
    install_dir.mkdir(parents=True)

    def extract(package: Path) -> None:
        log(f"解包: {package}")
        run(["dpkg-deb", "-x", str(package), str(install_dir)], timeout=context.timeout, check=True)

    extract(installer)
    exe = check_layout(install_dir, "deb")
    run_version(exe, expected, context)

    if args.previous_installer:
        previous = args.previous_installer.resolve()
        log("== 覆盖上一版本（升级路径） ==")
        extract(previous)
        check_layout(install_dir, "deb")
        run_version(exe, args.previous_version, context)
        extract(installer)
        exe = check_layout(install_dir, "deb")
        run_version(exe, expected, context)

    log("== 卸载（删除解包目录，模拟） ==")
    shutil.rmtree(install_dir, ignore_errors=True)
    if exe.exists():
        raise SmokeError(f"卸载后主程序仍存在: {exe}")


def detect_mode(args: argparse.Namespace) -> str:
    if args.mode != "auto":
        return args.mode
    if args.installer:
        suffix = args.installer.suffix.lower()
        if suffix == ".exe":
            return "nsis"
        if suffix == ".dmg":
            return "dmg"
        if suffix == ".deb":
            return "deb"
        raise SmokeError(f"无法根据扩展名判断安装包类型: {args.installer}")
    if args.build_dir:
        return "staged"
    raise SmokeError("请指定 --installer 或 --build-dir")


def parse_args(argv: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="SanYiCAD 安装/卸载/升级冒烟测试")
    parser.add_argument("--mode", choices=["auto", "staged", "nsis", "dmg", "deb"], default="auto")
    parser.add_argument("--build-dir", type=Path, help="CMake 构建目录（staged 模式）")
    parser.add_argument("--previous-build-dir", type=Path, help="上一版 CMake 构建目录（跨版本升级）")
    parser.add_argument("--installer", type=Path, help="安装包路径（nsis/dmg/deb）")
    parser.add_argument("--previous-installer", type=Path, help="上一版安装包路径（跨版本升级）")
    parser.add_argument("--config", default="Release", help="cmake --install 的配置（默认 Release）")
    parser.add_argument("--expected-version", help="期望版本号；默认从 CMakeCache/CMakeLists 读取")
    parser.add_argument("--previous-version", help="上一版期望版本号")
    parser.add_argument("--qt-dir", type=Path, help="Qt 安装目录，用于 staged 模式运行时查找 Qt 库")
    parser.add_argument("--timeout", type=int, default=180, help="单步超时秒数（默认 180）")
    parser.add_argument("--work-dir", type=Path, help="临时工作目录（默认系统临时目录）")
    parser.add_argument("--keep", action="store_true", help="保留临时工作目录，便于排查")
    parser.add_argument("--no-run", action="store_true", help="只检查文件布局，不运行 --version")
    parser.add_argument("--verbose", action="store_true", help="输出更详细的命令")
    return parser.parse_args(argv)


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = parse_args(argv)
    expected = args.expected_version or detect_project_version(args.build_dir)

    if args.work_dir:
        work_dir = args.work_dir.resolve()
        work_dir.mkdir(parents=True, exist_ok=True)
        cleanup = False
    else:
        work_dir = Path(tempfile.mkdtemp(prefix="sanyi-package-smoke-"))
        cleanup = not args.keep

    context = Context(
        work_dir=work_dir,
        timeout=args.timeout,
        no_run=args.no_run,
        keep=args.keep,
        qt_dir=args.qt_dir.resolve() if args.qt_dir else None,
        verbose=args.verbose,
    )

    mode = detect_mode(args)
    log(f"模式: {mode}")
    log(f"工作目录: {work_dir}")
    if expected:
        log(f"期望版本: {expected}")

    try:
        if mode == "staged":
            mode_staged(args, context, expected)
        elif mode == "nsis":
            mode_nsis(args, context, expected)
        elif mode == "dmg":
            mode_dmg(args, context, expected)
        elif mode == "deb":
            mode_deb(args, context, expected)
        else:
            raise SmokeError(f"未知模式: {mode}")
    except SmokeError as exc:
        print(f"\n[smoke] FAILED: {exc}", file=sys.stderr)
        return 1
    finally:
        if cleanup:
            shutil.rmtree(work_dir, ignore_errors=True)
        else:
            log(f"保留工作目录: {work_dir}")

    log("PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
