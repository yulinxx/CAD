#!/usr/bin/env bash
# ============================================================================
# April CAD 打包脚本公共函数库（macOS / Linux 共用）
# ============================================================================
# 用法：在 package-macos.sh / package-linux.sh 顶部
#   source "$(dirname "$0")/common.sh"
# 引入前调用方应已声明 set -euo pipefail。
#
# 兼容性：macOS 自带 bash 3.2，本文件不使用关联数组、${var,,}、mapfile 等
# bash 4+ 特性；所有 rsync 之外的命令均为系统自带或脚本显式校验存在。
# ============================================================================

PACKAGING_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$PACKAGING_DIR/../.." && pwd)"
PACKAGE_CONF="$PACKAGING_DIR/package.conf"

# ----------------------------------------------------------------------------
# 日志
# ----------------------------------------------------------------------------
if [[ -t 1 && -z "${NO_COLOR:-}" ]]; then
    _C_RED=$'\033[0;31m'
    _C_GREEN=$'\033[0;32m'
    _C_YELLOW=$'\033[1;33m'
    _C_CYAN=$'\033[0;36m'
    _C_NC=$'\033[0m'
else
    _C_RED=''
    _C_GREEN=''
    _C_YELLOW=''
    _C_CYAN=''
    _C_NC=''
fi

log_info() { printf '%b[INFO]%b %s\n' "$_C_GREEN" "$_C_NC" "$*"; }
log_warn() { printf '%b[WARN]%b %s\n' "$_C_YELLOW" "$_C_NC" "$*"; }
log_step() { printf '%b[STEP]%b %s\n' "$_C_CYAN" "$_C_NC" "$*"; }
log_error() { printf '%b[ERROR]%b %s\n' "$_C_RED" "$_C_NC" "$*" >&2; }

# 打印错误并立即退出（面向用户的提示统一使用英文）
die() {
    log_error "$*"
    exit 1
}

# ----------------------------------------------------------------------------
# 前置检查
# ----------------------------------------------------------------------------
# require_cmd <命令> <安装/替代提示>
require_cmd() {
    local cmd="$1"
    local hint="${2:-}"
    if ! command -v "$cmd" >/dev/null 2>&1; then
        log_error "Required tool not found: $cmd"
        if [[ -n "$hint" ]]; then
            log_error "  Hint: $hint"
        fi
        exit 1
    fi
}

# ----------------------------------------------------------------------------
# 共享常量
# ----------------------------------------------------------------------------
# load_package_conf 读取 package.conf，设置 APP_NAME / EXE_NAME 等全局变量。
load_package_conf() {
    [[ -f "$PACKAGE_CONF" ]] || die "Missing shared config: $PACKAGE_CONF"

    APP_NAME=""
    EXE_NAME=""
    DEFAULT_VERSION=""
    ORG_DOMAIN=""
    BUNDLE_ID=""

    local line key value
    while IFS= read -r line || [[ -n "$line" ]]; do
        line="${line%$'\r'}"
        case "$line" in
            ''|'#'*) continue ;;
        esac
        key="${line%%=*}"
        value="${line#*=}"
        # 去掉行尾注释与首尾空白
        value="${value%%#*}"
        value="$(printf '%s' "$value" | sed -e 's/^[[:space:]]*//' -e 's/[[:space:]]*$//')"
        case "$key" in
            APP_NAME|EXE_NAME|DEFAULT_VERSION|ORG_DOMAIN|BUNDLE_ID)
                printf -v "$key" '%s' "$value"
                ;;
        esac
    done < "$PACKAGE_CONF"

    [[ -n "$APP_NAME" ]] || die "APP_NAME is empty in $PACKAGE_CONF"
    [[ -n "$EXE_NAME" ]] || die "EXE_NAME is empty in $PACKAGE_CONF"
}

# ----------------------------------------------------------------------------
# 路径与版本
# ----------------------------------------------------------------------------
# resolve_abs <path>：把相对路径按仓库根目录解析成绝对路径并去掉末尾斜杠。
resolve_abs() {
    local path="$1"
    if [[ "$path" != /* ]]; then
        path="$REPO_ROOT/$path"
    fi
    if [[ -d "$path" ]]; then
        (cd "$path" && pwd -P)
    else
        local parent
        parent="$(cd "$(dirname "$path")" 2>/dev/null && pwd -P)" || die "Cannot resolve path: $path"
        printf '%s/%s\n' "$parent" "$(basename "$path")"
    fi
}

# repo_version 从顶层 CMakeLists.txt 的 project(... VERSION x.y.z) 读取版本号。
repo_version() {
    local version
    version="$(grep -m1 -oE 'VERSION [0-9]+\.[0-9]+\.[0-9]+' "$REPO_ROOT/CMakeLists.txt" 2>/dev/null | awk '{print $2}')"
    printf '%s\n' "${version:-}"
}

# detect_build_dir 在未显式指定时寻找可用的 CMake 构建目录（只读，不创建）。
detect_build_dir() {
    local candidate
    for candidate in "$REPO_ROOT/build" "$REPO_ROOT"/build-*; do
        if [[ -f "$candidate/CMakeCache.txt" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# find_bin_dir <build_dir>：返回实际运行产物目录。
# 多配置生成器（VS/Xcode）输出到 bin_Qt*/Release，单配置生成器直接输出到 bin_Qt*。
find_bin_dir() {
    local build_dir="$1"
    local candidate
    for candidate in "$build_dir"/bin_Qt*/Release "$build_dir"/bin_Qt*; do
        if [[ -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    done
    return 1
}

# read_cache_value <build_dir> <key>：读取 CMakeCache.txt 中的配置值。
read_cache_value() {
    local build_dir="$1"
    local key="$2"
    local cache="$build_dir/CMakeCache.txt"
    [[ -f "$cache" ]] || return 1
    grep -m1 "^${key}:" "$cache" 2>/dev/null | cut -d= -f2-
}

# resolve_qt_dir <explicit>：按 参数 -> 环境变量 -> CMakeCache -> qmake -> Homebrew
# 的顺序定位 Qt 安装前缀，找不到时返回 1。
resolve_qt_dir() {
    local explicit="${1:-}"
    local candidate

    if [[ -n "$explicit" ]]; then
        [[ -d "$explicit" ]] || die "Qt directory does not exist: $explicit"
        (cd "$explicit" && pwd -P)
        return 0
    fi

    for candidate in "${QT_DIR:-}" "${QTDIR:-}" "${Qt6_DIR:-}"; do
        if [[ -n "$candidate" && -d "$candidate" ]]; then
            (cd "$candidate" && pwd -P)
            return 0
        fi
    done

    candidate="$(read_cache_value "${BUILD_DIR:-}" Qt_INSTALL_DIR || true)"
    if [[ -n "$candidate" && -d "$candidate" ]]; then
        (cd "$candidate" && pwd -P)
        return 0
    fi

    local qmake_tool
    for qmake_tool in qmake6 qmake; do
        if command -v "$qmake_tool" >/dev/null 2>&1; then
            candidate="$("$qmake_tool" -query QT_INSTALL_PREFIX 2>/dev/null || true)"
            if [[ -n "$candidate" && -d "$candidate" ]]; then
                printf '%s\n' "$candidate"
                return 0
            fi
        fi
    done

    if command -v brew >/dev/null 2>&1; then
        candidate="$(brew --prefix qt 2>/dev/null || true)"
        if [[ -n "$candidate" && -d "$candidate" ]]; then
            printf '%s\n' "$candidate"
            return 0
        fi
    fi

    return 1
}

# ----------------------------------------------------------------------------
# 安全删除 / 归档
# ----------------------------------------------------------------------------
# safe_remove_dir <target> <allowed_root>
# 只允许删除输出目录内部的路径，避免参数写错时误删源码或构建目录。
safe_remove_dir() {
    local target="$1"
    local allowed_root="$2"
    [[ -n "$target" && -n "$allowed_root" ]] || die "safe_remove_dir: empty argument"
    target="${target%/}"
    allowed_root="${allowed_root%/}"
    if [[ "$target" == "$allowed_root" || "$target" != "$allowed_root"/* ]]; then
        die "Refusing to remove path outside output directory: $target"
    fi
    if [[ -d "$target" ]]; then
        rm -rf -- "$target"
    fi
}

# make_tar_gz <source_dir> <archive_path> <top_level_name>
# 以指定顶层目录名打包，保证解压后目录结构稳定。
make_tar_gz() {
    local source_dir="$1"
    local archive="$2"
    local top_name="$3"
    (cd "$source_dir" && tar -czf "$archive" "$top_name")
}

# print_artifact <label> <path>
print_artifact() {
    local label="$1"
    local path="$2"
    printf '  %-12s %s\n' "$label" "$path"
}
