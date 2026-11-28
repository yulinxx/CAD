#!/usr/bin/env bash
# gen-xcode.sh — 一键生成 Xcode 工程
# 用法: ./gen-xcode.sh [客户名]
#   客户名: MantiSoft / TyvokStudio / GoodLaser  (默认 GoodLaser)
set -euo pipefail

CUSTOMER="${1:-GoodLaser}"
BINARY_DIR="build-xcode/${CUSTOMER}"

case "$CUSTOMER" in
    MantiSoft|TyvokStudio|GoodLaser) ;;
    *) echo "错误: 未知客户 '$CUSTOMER'，可选: MantiSoft TyvokStudio GoodLaser"; exit 1 ;;
esac

echo "=== 生成 Xcode 工程: $CUSTOMER ==="
cmake -S . -B "$BINARY_DIR" -G Xcode --preset "$CUSTOMER"

PROJECT="$BINARY_DIR/MantiSoft.xcodeproj"
if [ -d "$PROJECT" ]; then
    echo ""
    echo "✅ 生成成功，正在打开 Xcode..."
    open "$PROJECT"
    echo "   产物目录: $BINARY_DIR"
else
    echo "❌ 未找到生成的 .xcodeproj，检查上方日志"
    exit 1
fi
