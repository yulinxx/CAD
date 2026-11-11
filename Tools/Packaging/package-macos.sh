#!/usr/bin/env bash
# ============================================================================
# April CAD macOS 打包脚本
# ============================================================================
# 职责：
#   1. 构建 Release（可跳过）
#   2. 把 .app 复制到输出目录，并收拢工程 dylib
#   3. 用 macdeployqt 部署 Qt Framework / 插件
#   4. 清理构建机专属 rpath、按需签名
#   5. 产出 .dmg 和/或 .zip
#
# 约定：用户可见输出与错误信息使用英文，注释使用中文。
# 幂等：只清理输出目录内的 staging 目录，绝不删除构建目录或源码。
# ============================================================================

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"
load_package_conf

# ----------------------------------------------------------------------------
# 参数
# ----------------------------------------------------------------------------
BUILD_DIR_ARG=""
OUT_DIR_ARG=""
VERSION_ARG=""
QT_DIR_ARG=""
SKIP_BUILD=false
NO_SIGN=false
SIGN_IDENTITY=""
PACKAGE_FORMAT="auto"      # auto | dmg | zip | both
JOBS=""
VERBOSE_LEVEL=1

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Build and package April for macOS.

Options:
  -b, --build-dir <path>     CMake build directory (default: auto-detect)
  -o, --out-dir <path>       Output directory (default: <repo>/dist)
      --version <ver>        Package version (default: project VERSION)
      --qt-dir <path>        Qt installation prefix (default: auto-detect)
      --skip-build           Do not run cmake/ninja; use existing artifacts
      --sign <identity>      Developer ID identity; enables hardened runtime
                             and a secure timestamp. Default: ad-hoc signing.
      --no-sign              Skip codesign entirely
      --format <kind>        auto | dmg | zip | both (default: auto)
  -j, --jobs <n>             Parallel build jobs (default: CMake decides)
      --verbose <0-3>        macdeployqt verbosity (default: 1)
  -h, --help                 Show this help

Examples:
  $(basename "$0")
  $(basename "$0") --skip-build --format both
  $(basename "$0") --sign "Developer ID Application: Example Corp (TEAMID)"
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build-dir)  BUILD_DIR_ARG="${2:-}"; shift 2 ;;
        -o|--out-dir)    OUT_DIR_ARG="${2:-}"; shift 2 ;;
        --version)       VERSION_ARG="${2:-}"; shift 2 ;;
        --qt-dir)        QT_DIR_ARG="${2:-}"; shift 2 ;;
        --skip-build)    SKIP_BUILD=true; shift ;;
        --sign)          SIGN_IDENTITY="${2:-}"; shift 2 ;;
        --no-sign)       NO_SIGN=true; shift ;;
        --format)        PACKAGE_FORMAT="${2:-}"; shift 2 ;;
        -j|--jobs)       JOBS="${2:-}"; shift 2 ;;
        --verbose)       VERBOSE_LEVEL="${2:-}"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *)               log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
done

case "$PACKAGE_FORMAT" in
    auto|dmg|zip|both) ;;
    *) die "Invalid --format: $PACKAGE_FORMAT (expected auto|dmg|zip|both)" ;;
esac

[[ "$(uname -s)" == "Darwin" ]] || die "This script must run on macOS."

# Developer ID 也可以只通过环境变量提供（CI 场景），命令行 --sign 优先级更高。
if [[ -z "$SIGN_IDENTITY" && -n "${APPLE_SIGN_IDENTITY:-}" ]]; then
    SIGN_IDENTITY="$APPLE_SIGN_IDENTITY"
fi

# ----------------------------------------------------------------------------
# 解析构建目录 / 版本 / Qt
# ----------------------------------------------------------------------------
if [[ -n "$BUILD_DIR_ARG" ]]; then
    BUILD_DIR="$(resolve_abs "$BUILD_DIR_ARG")"
else
    BUILD_DIR="$(detect_build_dir)" || die "No CMake build directory found. Pass --build-dir <path>."
fi
[[ -d "$BUILD_DIR" ]] || die "Build directory does not exist: $BUILD_DIR"

if [[ -n "$VERSION_ARG" ]]; then
    VERSION="$VERSION_ARG"
else
    VERSION="$(repo_version)"
    [[ -n "$VERSION" ]] || VERSION="$DEFAULT_VERSION"
fi

if [[ -n "$OUT_DIR_ARG" ]]; then
    OUT_DIR="$(resolve_abs "$OUT_DIR_ARG")"
else
    OUT_DIR="$REPO_ROOT/dist"
fi
mkdir -p "$OUT_DIR"

# ----------------------------------------------------------------------------
# 1. 构建
# ----------------------------------------------------------------------------
if [[ "$SKIP_BUILD" == true ]]; then
    log_info "Skipping build (--skip-build)"
else
    require_cmd cmake "Install CMake 4.3+ (brew install cmake)"
    log_step "Building Release target: $APP_NAME"
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        require_cmd ninja "Install Ninja (brew install ninja)"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    build_args=(--build "$BUILD_DIR" --config Release --target "$EXE_NAME")
    if [[ -n "$JOBS" ]]; then
        build_args+=(--parallel "$JOBS")
    fi
    if ! cmake "${build_args[@]}"; then
        log_error "Build failed."
        log_error "Known macOS environment prerequisites for this project:"
        log_error "  - Xcode command line tools and the Metal toolchain must be installed"
        log_error "  - CAM / Python / Raster submodules must be initialized for those features"
        die "Cannot package without a successful build (use --skip-build to reuse artifacts)."
    fi
fi

BIN_DIR="$(find_bin_dir "$BUILD_DIR")" || die "No bin_Qt* output directory under $BUILD_DIR"
APP_SRC="$BIN_DIR/$APP_NAME.app"
[[ -d "$APP_SRC" ]] || die "Application bundle not found: $APP_SRC"

QT_DIR="$(resolve_qt_dir "$QT_DIR_ARG")" || die \
    "Qt installation not found. Pass --qt-dir, set QT_DIR, or configure CMake first."
MACDEPLOYQT="$QT_DIR/bin/macdeployqt"
if [[ ! -x "$MACDEPLOYQT" ]]; then
    MACDEPLOYQT="$(command -v macdeployqt || true)"
fi
[[ -n "$MACDEPLOYQT" && -x "$MACDEPLOYQT" ]] || die \
    "macdeployqt not found. Install the official Qt 6 SDK or pass --qt-dir <Qt prefix>."

ARCH="$(uname -m)"
BASE_NAME="$APP_NAME-$VERSION-macos-$ARCH"
WORK_DIR="$OUT_DIR/macos"
APP_DST="$WORK_DIR/$APP_NAME.app"

log_info "Repository : $REPO_ROOT"
log_info "Build dir  : $BUILD_DIR"
log_info "Binary dir : $BIN_DIR"
log_info "Qt prefix  : $QT_DIR"
log_info "Version    : $VERSION"
log_info "Arch       : $ARCH"
log_info "Output     : $OUT_DIR"

# ----------------------------------------------------------------------------
# 2. 准备 staging（只清理输出目录内部）
# ----------------------------------------------------------------------------
log_step "Staging application bundle"
safe_remove_dir "$WORK_DIR" "$OUT_DIR"
mkdir -p "$WORK_DIR"
ditto "$APP_SRC" "$APP_DST"

APP_BIN="$APP_DST/Contents/MacOS/$EXE_NAME"
[[ -x "$APP_BIN" ]] || die "Bundle executable missing: $APP_BIN"
FRAMEWORKS="$APP_DST/Contents/Frameworks"
mkdir -p "$FRAMEWORKS"

# 工程自身 dylib（vcpkg 依赖在本工程为静态链接，不单独成 dylib）
dylib_count=0
for lib in "$BIN_DIR"/lib*.dylib; do
    [[ -e "$lib" ]] || continue
    cp -a "$lib" "$FRAMEWORKS/"
    dylib_count=$((dylib_count + 1))
done
if [[ "$dylib_count" -eq 0 ]]; then
    log_warn "No project dylibs found in $BIN_DIR (lib*.dylib)"
else
    log_info "Staged $dylib_count project dylib entries"
fi

# 运行期资源目录（与构建输出保持一致；不存在则跳过）
for extra in Python resources plugins tools translations; do
    if [[ -d "$BIN_DIR/$extra" ]]; then
        log_info "Staging runtime directory: $extra"
        rm -rf "$APP_DST/Contents/MacOS/$extra"
        ditto "$BIN_DIR/$extra" "$APP_DST/Contents/MacOS/$extra"
    fi
done

# 确保 bundle 内可解析 @rpath 引用
if ! otool -l "$APP_BIN" | grep -q "@executable_path/../Frameworks"; then
    install_name_tool -add_rpath "@executable_path/../Frameworks" "$APP_BIN"
fi

# ----------------------------------------------------------------------------
# 3. 部署 Qt
# ----------------------------------------------------------------------------
log_step "Deploying Qt frameworks and plugins (macdeployqt)"
"$MACDEPLOYQT" "$APP_DST" \
    -libpath="$BIN_DIR" \
    -always-overwrite \
    -no-codesign \
    -verbose="$VERBOSE_LEVEL"

# ----------------------------------------------------------------------------
# 4. 规范化 install name / 清理构建机专属 rpath
# ----------------------------------------------------------------------------
strip_rpath() {
    local file="$1"
    local rpath="$2"
    local current
    while IFS= read -r current; do
        [[ -n "$current" ]] || continue
        if [[ "$current" == "$rpath" ]]; then
            install_name_tool -delete_rpath "$rpath" "$file" 2>/dev/null || true
        fi
    done < <(otool -l "$file" 2>/dev/null | grep -A2 "cmd LC_RPATH" | awk '/^ *path / {print $2}')
}

normalize_binary() {
    local file="$1"
    local current_id base desired

    current_id="$(otool -D "$file" 2>/dev/null | tail -n +2 | head -n 1 || true)"
    if [[ -n "$current_id" ]]; then
        base="$(basename "$current_id")"
        desired="@rpath/$base"
        if [[ "$current_id" != "$desired" ]]; then
            install_name_tool -id "$desired" "$file"
        fi
    fi

    if ! otool -l "$file" | grep -q "@loader_path"; then
        install_name_tool -add_rpath "@loader_path" "$file" 2>/dev/null || true
    fi

    strip_rpath "$file" "$BUILD_DIR"
    strip_rpath "$file" "$QT_DIR"
}

log_step "Normalizing install names and rpaths"
normalize_binary "$APP_BIN"
while IFS= read -r -d '' file; do
    normalize_binary "$file"
done < <(find "$FRAMEWORKS" -type f -name "*.dylib" -print0)

# ----------------------------------------------------------------------------
# 5. 签名
# ----------------------------------------------------------------------------
if [[ "$NO_SIGN" == true ]]; then
    log_warn "Code signing skipped (--no-sign); the .app will only run on this Mac."
elif [[ -n "$SIGN_IDENTITY" ]]; then
    # Developer ID 路径统一交给 sign-macos.sh：它负责嵌套组件签名、公证与
    # staple。公证凭据缺失时脚本会明确告警并跳过，不会让打包失败。
    log_step "Signing with Developer ID: $SIGN_IDENTITY"
    "$PACKAGING_DIR/sign-macos.sh" --app "$APP_DST" --identity "$SIGN_IDENTITY"
else
    log_step "Ad-hoc signing (no Developer ID supplied)"
    log_warn "APPLE_SIGN_IDENTITY is not set; notarization is skipped."
    sign_args=(--force --sign -)

    # Qt frameworks 必须以 bundle 为单位签名
    for fw in "$FRAMEWORKS"/*.framework; do
        [[ -d "$fw" ]] || continue
        codesign "${sign_args[@]}" "$fw"
    done

    # 工程 dylib
    while IFS= read -r -d '' file; do
        codesign "${sign_args[@]}" "$file"
    done < <(find "$FRAMEWORKS" -type f -name "*.dylib" -print0)

    # Bundle 内自带的外部可执行文件（例如 poppler / ghostscript）
    for helper in "$APP_DST/Contents/Resources/bin"/*; do
        [[ -f "$helper" && -x "$helper" ]] || continue
        codesign "${sign_args[@]}" "$helper"
    done

    # 必须带 --deep：本工程的着色器 / 字体 / 翻译文件位于 Contents/MacOS
    # （运行期按 applicationDirPath 读取），codesign 不带 --deep 时会把这些
    # 非 Mach-O 资源当成「未签名的子组件」而直接拒绝签名。
    codesign "${sign_args[@]}" --deep "$APP_DST"
fi

# ----------------------------------------------------------------------------
# 6. 验证（失败即停，避免产出不可用的包）
# ----------------------------------------------------------------------------
log_step "Verifying bundle"

if [[ "$NO_SIGN" != true ]]; then
    # 不能使用 --deep：Bundle 的 Contents/MacOS 里还有着色器等非代码资源，
    # --deep --strict 会把它们当成未签名的子组件而误报失败。
    codesign --verify --strict "$APP_DST" || die "codesign verification failed (app bundle)"
    for fw in "$FRAMEWORKS"/*.framework; do
        [[ -d "$fw" ]] || continue
        codesign --verify --strict "$fw" || die "codesign verification failed: $(basename "$fw")"
    done
    while IFS= read -r -d '' file; do
        codesign --verify --strict "$file" || die "codesign verification failed: $file"
    done < <(find "$FRAMEWORKS" -type f -name "*.dylib" -print0)
fi

# 不允许残留构建机绝对路径依赖（否则换机后 dyld 找不到库）
check_no_machine_paths() {
    local file="$1"
    local bad
    bad="$(otool -L "$file" 2>/dev/null | tail -n +2 | awk '{print $1}' \
        | grep -E "^($BUILD_DIR|$QT_DIR)/" || true)"
    if [[ -n "$bad" ]]; then
        log_error "Machine-specific dependency found in $file:"
        printf '%s\n' "$bad" >&2
        die "Bundle still references build-machine paths."
    fi
}

check_no_machine_paths "$APP_BIN"
while IFS= read -r -d '' file; do
    check_no_machine_paths "$file"
done < <(find "$APP_DST/Contents" -type f \( -name "*.dylib" -o -path "*.framework/Versions/*" \) -print0)

# 检查可执行文件的 @rpath 依赖在 Frameworks 内可解析
missing="$(otool -L "$APP_BIN" | tail -n +2 | awk '{print $1}' \
    | grep "^@rpath/" | sed 's|^@rpath/||' \
    | while IFS= read -r rel; do
        [[ -e "$FRAMEWORKS/$rel" ]] || printf '%s\n' "$rel"
      done || true)"
if [[ -n "$missing" ]]; then
    log_error "Unresolved @rpath dependencies:"
    printf '%s\n' "$missing" >&2
    die "Bundle is missing runtime libraries."
fi

# ----------------------------------------------------------------------------
# 7. 归档
# ----------------------------------------------------------------------------
log_step "Creating distribution archives"
ARCHIVES=()

if [[ "$PACKAGE_FORMAT" == "auto" ]]; then
    if command -v hdiutil >/dev/null 2>&1; then
        PACKAGE_FORMAT="dmg"
    else
        PACKAGE_FORMAT="zip"
    fi
fi

if [[ "$PACKAGE_FORMAT" == "dmg" || "$PACKAGE_FORMAT" == "both" ]]; then
    require_cmd hdiutil "hdiutil ships with macOS; this environment looks incomplete."
    DMG_ROOT="$WORK_DIR/dmgroot"
    rm -rf "$DMG_ROOT"
    mkdir -p "$DMG_ROOT"
    ditto "$APP_DST" "$DMG_ROOT/$APP_NAME.app"
    ln -s /Applications "$DMG_ROOT/Applications"
    DMG_PATH="$OUT_DIR/$BASE_NAME.dmg"
    hdiutil create -volname "$APP_NAME $VERSION" -srcfolder "$DMG_ROOT" \
        -ov -format UDZO "$DMG_PATH" >/dev/null
    ARCHIVES+=("$DMG_PATH")
fi

if [[ "$PACKAGE_FORMAT" == "zip" || "$PACKAGE_FORMAT" == "both" ]]; then
    ZIP_PATH="$OUT_DIR/$BASE_NAME.zip"
    ditto -c -k --sequesterRsrc --keepParent "$APP_DST" "$ZIP_PATH"
    ARCHIVES+=("$ZIP_PATH")
fi

# ----------------------------------------------------------------------------
# 7.1 DMG 补签名 + 公证 + staple
# ----------------------------------------------------------------------------
# zip 内的 .app 在步骤 5 已经公证并 staple，不再对 zip 单独提交，避免重复排队。
if [[ "$NO_SIGN" != true && -n "$SIGN_IDENTITY" ]]; then
    dmg_artifacts=()
    for archive in "${ARCHIVES[@]}"; do
        [[ "$archive" == *.dmg ]] && dmg_artifacts+=("$archive")
    done
    if [[ ${#dmg_artifacts[@]} -gt 0 ]]; then
        artifact_args=()
        for archive in "${dmg_artifacts[@]}"; do
            artifact_args+=(--artifact "$archive")
        done
        "$PACKAGING_DIR/sign-macos.sh" "${artifact_args[@]}" --identity "$SIGN_IDENTITY"
    fi
fi

# -----------------------------------------------------------------------------
# 8. 结果
# -----------------------------------------------------------------------------
log_info "Staged bundle kept for inspection: $APP_DST"

echo
log_info "macOS package completed"
print_artifact "App bundle:" "$APP_DST"
for archive in "${ARCHIVES[@]}"; do
    print_artifact "Archive:" "$archive"
done
print_artifact "Version:" "$VERSION"
print_artifact "Qt:" "$QT_DIR"
if [[ "$NO_SIGN" == true ]]; then
    print_artifact "Signing:" "skipped"
elif [[ -n "$SIGN_IDENTITY" ]]; then
    if [[ -n "${NOTARYTOOL_PROFILE:-}" || ( -n "${APPLE_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ) ]]; then
        print_artifact "Signing:" "Developer ID + notarized"
    else
        print_artifact "Signing:" "Developer ID (notarization skipped)"
    fi
else
    print_artifact "Signing:" "ad-hoc"
fi
