#!/usr/bin/env bash
# UiShell 迁移的固定 Step 0 基线：configure -> build -> 分层/QtTest -> 外壳冒烟。
#
# 用法：
#   BUILD_DIR=build bash UiShell/ci/ui-shell-baseline.sh
#
# 关键参数不可省略：
#   -DUI_CUSTOMER=April   UiShell 默认是 MantiSoft，不指定就不会生成 AprilShell / UiApril
#   -DBUILD_TESTS=ON      UiLayerCheck 与 UiTst* 才会注册进 ctest
#
# 每个迁移里程碑提交前都必须跑通本脚本。SampleFrame/ 是只读参考，既不参与
# 构建也不应被改动；本地用 SHA-256 快照核对（见 UiShell/MIGRATION.md §0.5）。
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
BUILD_DIR="${BUILD_DIR:-${ROOT}/build}"
BUILD_TYPE="${BUILD_TYPE:-Release}"

cmake -S "${ROOT}" -B "${BUILD_DIR}" -G Ninja \
    -DBUILD_UI_SHELL=ON \
    -DUI_CUSTOMER=April \
    -DBUILD_TESTS=ON \
    -DCMAKE_BUILD_TYPE="${BUILD_TYPE}"

# 新外壳 + 旧应用必须同时可构建；顺序即交付边界。
cmake --build "${BUILD_DIR}" --target AprilShell UiApril AprilUi AprilCore April

# 测试目标也要重编：切换 UI_CUSTOMER 后若只重编主目标，
# 集成测试仍链接旧宿主，会误报 buildFingerprintsMatchHost 失败。
cmake --build "${BUILD_DIR}"

ctest --test-dir "${BUILD_DIR}" -R "UiTst|UiLayerCheck" --output-on-failure

# 外壳冒烟：最小 profile（无 CenterView）下 AprilShell 必须进入事件循环。
# 完整 April profile 会创建 Metal 视口，offscreen 平台没有原生窗口，不能用冒烟判定。
python3 "${ROOT}/UiShell/ci/ui-shell-smoke.py"

echo "UiShell Step 0 baseline OK (customer=April)."
