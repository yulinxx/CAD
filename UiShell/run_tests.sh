#!/usr/bin/env bash
# ---------------------------------------------------------------------------
# run_tests.sh — 一键跑三套客户的 configure → build → ctest
#
# 用法:
#   ./run_tests.sh                  # 跑全部三套客户
#   ./run_tests.sh MantiSoft        # 只跑指定客户
#   ./run_tests.sh GoodLaser TyvokStudio  # 跑多个
#
# 依赖: cmake ≥ 3.22、CMakePresets.json（已在项目根）
# ---------------------------------------------------------------------------
set -euo pipefail

CUSTOMERS=("${@}")
if [ ${#CUSTOMERS[@]} -eq 0 ]; then
    CUSTOMERS=("MantiSoft" "TyvokStudio" "GoodLaser")
fi

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

PASS_COUNT=0
FAIL_COUNT=0
FAILED_LIST=()

echo "============================================"
echo "  UI Shell Test Runner"
echo "  Customers: ${CUSTOMERS[*]}"
echo "  $(date '+%Y-%m-%d %H:%M:%S')"
echo "============================================"
echo ""

for customer in "${CUSTOMERS[@]}"; do
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"
    echo "  ${customer}"
    echo "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━"

    # 1. Configure
    echo "  [1/3] configure ..."
    if ! cmake --preset "${customer}" >/dev/null 2>&1; then
        echo -e "  ${RED}✗ configure FAILED${NC}"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_LIST+=("${customer}: configure")
        continue
    fi

    # 2. Build
    echo "  [2/3] build ..."
    if ! cmake --build --preset "build-${customer}" -j"$(sysctl -n hw.ncpu 2>/dev/null || echo 4)" >/dev/null 2>&1; then
        echo -e "  ${RED}✗ build FAILED${NC}"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_LIST+=("${customer}: build")
        continue
    fi

    # 3. Test（用 test preset，自动带 --output-on-failure）
    echo "  [3/3] test ..."
    if ctest --preset "test-${customer}" 2>&1 | tee "/tmp/ctest_${customer}.log"; then
        echo -e "  ${GREEN}✓ all tests passed${NC}"
        PASS_COUNT=$((PASS_COUNT + 1))
    else
        echo -e "  ${YELLOW}⚠ some tests FAILED — see /tmp/ctest_${customer}.log${NC}"
        FAIL_COUNT=$((FAIL_COUNT + 1))
        FAILED_LIST+=("${customer}: test")
    fi
    echo ""
done

echo "============================================"
echo -e "  Summary: ${GREEN}${PASS_COUNT} pass${NC}  ${RED}${FAIL_COUNT} fail${NC}"
echo "============================================"
if [ ${#FAILED_LIST[@]} -gt 0 ]; then
    echo "  Failures:"
    for item in "${FAILED_LIST[@]}"; do echo "    - ${item}"; done
    exit 1
fi
exit 0
