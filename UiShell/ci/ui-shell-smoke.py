#!/usr/bin/env python3
"""AprilShell headless smoke test.

Verify only that the shell can assemble a profile and reach the event loop.
No CenterView is created, so no native Metal/OpenGL surface is required
(MetalSurface cannot be created with the offscreen platform plugin).

An explicit --profile also exercises the command-line contract in README.
"""
import json
import os
import pathlib
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[2]  # UiShell/ci -> repo root
BUILD_DIR = pathlib.Path(os.environ.get("BUILD_DIR", ROOT / "build"))
BUILD_TYPE = os.environ.get("BUILD_TYPE", "Release")
EXE = BUILD_DIR / "bin_Qt6" / "shell" / BUILD_TYPE / "AprilShell"

PROFILE = {
    "version": 2,
    "profileId": "april-smoke",
    "titleBar": {"title": "AprilShell Smoke"},
    "window": {"minWidth": 640, "minHeight": 480},
    "regions": [
        {"id": "titlebar", "type": "TitleBar", "order": 1},
        {"id": "menubar", "type": "MenuBar", "order": 5},
        {"id": "bottom", "type": "StatusBar", "order": 50,
         "areas": [{"id": "bottom.main", "order": 10}]},
    ],
    "panels": [],
    "menus": [],
}


def main() -> int:
    if not EXE.is_file():
        print(f"missing executable: {EXE}", file=sys.stderr)
        return 1

    with tempfile.TemporaryDirectory() as tmp:
        profile = pathlib.Path(tmp) / "ui.json"
        profile.write_text(json.dumps(PROFILE, ensure_ascii=False), encoding="utf-8")
        env = dict(os.environ, QT_QPA_PLATFORM="offscreen")
        proc = subprocess.Popen(
            [str(EXE), "--lang", "zh_CN", "--profile=" + str(profile)],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            env=env, text=True,
        )
        try:
            out = proc.communicate(timeout=6)[0]
        except subprocess.TimeoutExpired:
            proc.kill()
            out = proc.communicate()[0]
            print("AprilShell smoke OK (event loop reached)")
            return 0

    print(f"AprilShell exited early with {proc.returncode}\n{out}", file=sys.stderr)
    return 1


if __name__ == "__main__":
    sys.exit(main())
