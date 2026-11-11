#!/usr/bin/env bash
# ============================================================================
# April CAD Linux 打包脚本
# ============================================================================
# 职责：
#   1. 构建 Release（可跳过）
#   2. 收拢可执行文件、工程 .so、Qt 插件与运行期资源
#   3. 按可用工具产出 AppImage（linuxdeploy）或 tar.gz（patchelf 手工部署）
#   4. 校验解压 / 运行所需依赖是否齐全
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
PACKAGE_FORMAT="auto"      # auto | appimage | targz | both
JOBS=""
ARCH_OVERRIDE=""
TRIPLET=""
ICON_PATH=""

usage() {
    cat <<EOF
Usage: $(basename "$0") [options]

Build and package April for Linux.

Options:
  -b, --build-dir <path>     CMake build directory (default: auto-detect)
  -o, --out-dir <path>       Output directory (default: <repo>/dist)
      --version <ver>        Package version (default: project VERSION)
      --qt-dir <path>        Qt installation prefix (default: auto-detect)
      --skip-build           Do not run cmake/ninja; use existing artifacts
      --format <kind>        auto | appimage | targz | both (default: auto)
  -j, --jobs <n>             Parallel build jobs (default: CMake decides)
      --arch <name>          Override artifact architecture name (e.g. x86_64)
      --triplet <name>       vcpkg triplet, for logging only (e.g. x64-linux)
      --icon <path>          PNG icon for AppImage / desktop entry (optional)
  -h, --help                 Show this help

Examples:
  $(basename "$0")
  $(basename "$0") --skip-build --format targz
  $(basename "$0") --format appimage --qt-dir /opt/Qt/6.11.1/gcc_64
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        -b|--build-dir)  BUILD_DIR_ARG="${2:-}"; shift 2 ;;
        -o|--out-dir)    OUT_DIR_ARG="${2:-}"; shift 2 ;;
        --version)       VERSION_ARG="${2:-}"; shift 2 ;;
        --qt-dir)        QT_DIR_ARG="${2:-}"; shift 2 ;;
        --skip-build)    SKIP_BUILD=true; shift ;;
        --format)        PACKAGE_FORMAT="${2:-}"; shift 2 ;;
        -j|--jobs)       JOBS="${2:-}"; shift 2 ;;
        --arch)          ARCH_OVERRIDE="${2:-}"; shift 2 ;;
        --triplet)       TRIPLET="${2:-}"; shift 2 ;;
        --icon)          ICON_PATH="${2:-}"; shift 2 ;;
        -h|--help)       usage; exit 0 ;;
        *)               log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
done

case "$PACKAGE_FORMAT" in
    auto|appimage|targz|both) ;;
    *) die "Invalid --format: $PACKAGE_FORMAT (expected auto|appimage|targz|both)" ;;
esac

[[ "$(uname -s)" == "Linux" ]] || die "This script must run on Linux."

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

if [[ -n "$ARCH_OVERRIDE" ]]; then
    ARCH="$ARCH_OVERRIDE"
else
    ARCH="$(uname -m)"
fi
BASE_NAME="$APP_NAME-$VERSION-linux-$ARCH"

# ----------------------------------------------------------------------------
# 1. 构建
# ----------------------------------------------------------------------------
if [[ "$SKIP_BUILD" == true ]]; then
    log_info "Skipping build (--skip-build)"
else
    require_cmd cmake "Install CMake 4.3+ (sudo apt install cmake)"
    log_step "Building Release target: $APP_NAME"
    if [[ ! -f "$BUILD_DIR/CMakeCache.txt" ]]; then
        require_cmd ninja "Install Ninja (sudo apt install ninja-build)"
        cmake -S "$REPO_ROOT" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
    fi
    build_args=(--build "$BUILD_DIR" --config Release --target "$EXE_NAME")
    if [[ -n "$JOBS" ]]; then
        build_args+=(--parallel "$JOBS")
    fi
    if ! cmake "${build_args[@]}"; then
        log_error "Build failed."
        log_error "Ensure Qt 6, vcpkg dependencies and all enabled submodules are available."
        die "Cannot package without a successful build (use --skip-build to reuse artifacts)."
    fi
fi

BIN_DIR="$(find_bin_dir "$BUILD_DIR")" || die "No bin_Qt* output directory under $BUILD_DIR"
APP_BIN="$BIN_DIR/$EXE_NAME"
[[ -x "$APP_BIN" ]] || die "Application executable not found: $APP_BIN"

QT_DIR="$(resolve_qt_dir "$QT_DIR_ARG")" || die \
    "Qt installation not found. Pass --qt-dir, set QT_DIR, or configure CMake first."
export QTDIR="$QT_DIR"
export PATH="$QT_DIR/bin:$PATH"

WORK_DIR="$OUT_DIR/linux"
STAGE_DIR="$WORK_DIR/$APP_NAME"
APPIMAGE_STAGE="$WORK_DIR/AppDir"

log_info "Repository : $REPO_ROOT"
log_info "Build dir  : $BUILD_DIR"
log_info "Binary dir : $BIN_DIR"
log_info "Qt prefix  : $QT_DIR"
log_info "Version    : $VERSION"
log_info "Arch       : $ARCH"
if [[ -n "$TRIPLET" ]]; then
    log_info "Triplet    : $TRIPLET"
fi
log_info "Output     : $OUT_DIR"

# ----------------------------------------------------------------------------
# 2. 复制运行产物到 portable staging
# ----------------------------------------------------------------------------
copy_runtime_files() {
    local dest="$1"
    local lib

    mkdir -p "$dest"
    cp -a "$APP_BIN" "$dest/"

    for lib in "$BIN_DIR"/lib*.so*; do
        [[ -e "$lib" ]] || continue
        cp -a "$lib" "$dest/"
    done

    for extra in plugins resources translations Python tools; do
        if [[ -d "$BIN_DIR/$extra" ]]; then
            rm -rf "$dest/$extra"
            cp -a "$BIN_DIR/$extra" "$dest/$extra"
        fi
    done
}

log_step "Staging portable application directory"
safe_remove_dir "$WORK_DIR" "$OUT_DIR"
mkdir -p "$WORK_DIR"
copy_runtime_files "$STAGE_DIR"
log_info "Staged: $STAGE_DIR"

# ----------------------------------------------------------------------------
# 3. Qt 插件 / 手工依赖部署（tar.gz 方案）
# ----------------------------------------------------------------------------
# bundle_shared_libs <stage_dir>
# 用 ldd 递归收集非系统共享库到 <stage>/lib，再用 patchelf 写 $ORIGIN rpath。
bundle_shared_libs() {
    local stage="$1"
    local lib_dir="$stage/lib"
    mkdir -p "$lib_dir"

    # 工程 .so 先归位到 lib/，可执行文件 rpath 指向 $ORIGIN/lib
    local lib
    for lib in "$stage"/lib*.so*; do
        [[ -e "$lib" ]] || continue
        mv "$lib" "$lib_dir/"
    done

    # Qt 插件目录（Qt 通过 qt.conf 定位）
    local plugin_dest="$stage/plugins"
    mkdir -p "$plugin_dest"
    local plugin_sub
    for plugin_sub in platforms imageformats iconengines styles tls networkinformation \
                      wayland-shell-integration wayland-decoration-client; do
        if [[ -d "$QT_DIR/plugins/$plugin_sub" ]]; then
            cp -a "$QT_DIR/plugins/$plugin_sub" "$plugin_dest/"
        fi
    done

    local changed=1
    local files file dep base
    while [[ "$changed" -eq 1 ]]; do
        changed=0
        files="$(find "$stage" -type f \( -perm -u+x -o -name '*.so*' \) 2>/dev/null || true)"
        while IFS= read -r file; do
            [[ -z "$file" || -d "$file" ]] && continue
            while IFS= read -r dep; do
                [[ -z "$dep" ]] && continue
                case "$dep" in
                    /lib/*|/lib64/*|/lib32/*|/usr/lib/*|/usr/lib64/*|/usr/lib32/*) continue ;;
                    linux-vdso*|ld-linux*|*/ld-linux*) continue ;;
                esac
                [[ -f "$dep" ]] || continue
                base="$(basename "$dep")"
                if [[ ! -e "$lib_dir/$base" ]]; then
                    cp -a "$dep" "$lib_dir/$base"
                    changed=1
                fi
            done < <(ldd "$file" 2>/dev/null | awk '/=> \// {print $3}')
        done <<< "$files"
    done

    # 校验：不应再有 not found
    local missing=""
    if command -v ldd >/dev/null 2>&1; then
        missing="$(ldd "$stage/$EXE_NAME" 2>/dev/null | awk '/not found/ {print $1}' || true)"
    fi
    if [[ -n "$missing" ]]; then
        log_error "Unresolved shared libraries:"
        printf '%s\n' "$missing" >&2
        die "Automatic dependency bundling failed. Install the missing packages or use AppImage packaging."
    fi

    # rpath：可执行文件到 lib/，库之间同目录
    patchelf --set-rpath '$ORIGIN/lib' "$stage/$EXE_NAME"
    local lib
    for lib in "$lib_dir"/*.so*; do
        [[ -e "$lib" ]] || continue
        patchelf --set-rpath '$ORIGIN' "$lib"
    done

    # 插件位于 plugins/<subdir>/，按层级回指到 lib/
    local plugin
    while IFS= read -r plugin; do
        [[ -n "$plugin" ]] || continue
        patchelf --set-rpath '$ORIGIN/../../lib' "$plugin"
    done < <(find "$plugin_dest" -type f -name "*.so" 2>/dev/null || true)

    cat > "$stage/qt.conf" <<'EOF'
[Paths]
Prefix = .
Libraries = lib
Plugins = plugins
EOF
}

# prepare_portable_targz <stage_dir>
prepare_portable_targz() {
    local stage="$1"
    require_cmd patchelf "Install patchelf (sudo apt install patchelf) or use --format appimage"
    log_step "Bundling shared libraries with ldd + patchelf"
    bundle_shared_libs "$stage"

    local archive="$OUT_DIR/$BASE_NAME.tar.gz"
    make_tar_gz "$WORK_DIR" "$archive" "$APP_NAME"
    TARGZ_PATH="$archive"
    log_info "Created: $archive"
}

# ----------------------------------------------------------------------------
# 4. AppImage（linuxdeploy 方案）
# ----------------------------------------------------------------------------
build_appimage() {
    local linuxdeploy
    linuxdeploy="$(command -v linuxdeploy || true)"
    if [[ -z "$linuxdeploy" && -x "$QT_DIR/bin/linuxdeploy" ]]; then
        linuxdeploy="$QT_DIR/bin/linuxdeploy"
    fi
    if [[ -z "$linuxdeploy" ]]; then
        return 1
    fi
    if ! command -v linuxdeploy-plugin-qt >/dev/null 2>&1 \
       && [[ ! -x "$QT_DIR/bin/linuxdeploy-plugin-qt" ]]; then
        log_warn "linuxdeploy-plugin-qt not found; cannot build an AppImage"
        return 1
    fi

    log_step "Building AppImage with linuxdeploy"
    safe_remove_dir "$APPIMAGE_STAGE" "$OUT_DIR"
    mkdir -p "$APPIMAGE_STAGE/usr/bin" \
             "$APPIMAGE_STAGE/usr/lib" \
             "$APPIMAGE_STAGE/usr/share/applications" \
             "$APPIMAGE_STAGE/usr/share/icons/hicolor/256x256/apps"

    cp -a "$APP_BIN" "$APPIMAGE_STAGE/usr/bin/"
    local lib
    for lib in "$BIN_DIR"/lib*.so*; do
        [[ -e "$lib" ]] || continue
        cp -a "$lib" "$APPIMAGE_STAGE/usr/lib/"
    done
    # 应用按 applicationDirPath 读资源，必须与可执行文件同目录
    for extra in plugins resources translations Python tools; do
        if [[ -d "$BIN_DIR/$extra" ]]; then
            cp -a "$BIN_DIR/$extra" "$APPIMAGE_STAGE/usr/bin/"
        fi
    done

    local desktop="$APPIMAGE_STAGE/usr/share/applications/$APP_NAME.desktop"
    {
        echo "[Desktop Entry]"
        echo "Type=Application"
        echo "Name=$APP_NAME"
        echo "Exec=$EXE_NAME"
        echo "Terminal=false"
        echo "Categories=Graphics;Engineering;"
        if [[ -n "$ICON_PATH" ]]; then
            echo "Icon=$APP_NAME"
        fi
    } > "$desktop"

    local icon_arg=()
    if [[ -n "$ICON_PATH" ]]; then
        [[ -f "$ICON_PATH" ]] || die "Icon file not found: $ICON_PATH"
        cp "$ICON_PATH" "$APPIMAGE_STAGE/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png"
        icon_arg=(--icon-file "$APPIMAGE_STAGE/usr/share/icons/hicolor/256x256/apps/$APP_NAME.png")
    fi

    (
        cd "$WORK_DIR"
        "$linuxdeploy" \
            --appdir "$APPIMAGE_STAGE" \
            --executable "$APPIMAGE_STAGE/usr/bin/$EXE_NAME" \
            --desktop-file "$desktop" \
            "${icon_arg[@]}" \
            --plugin qt \
            --output appimage
    ) || return 1

    local produced
    produced="$(find "$WORK_DIR" -maxdepth 1 -name '*.AppImage' -type f | head -n 1)"
    [[ -n "$produced" ]] || return 1
    local target="$OUT_DIR/$BASE_NAME.AppImage"
    mv "$produced" "$target"
    chmod +x "$target"
    APPIMAGE_PATH="$target"
    log_info "Created: $target"
    return 0
}

# ----------------------------------------------------------------------------
# 5. 按 format 产出
# ----------------------------------------------------------------------------
ARCHIVES=()

case "$PACKAGE_FORMAT" in
    auto)
        if build_appimage; then
            ARCHIVES+=("$APPIMAGE_PATH")
        else
            log_warn "linuxdeploy / linuxdeploy-plugin-qt unavailable; falling back to tar.gz"
            prepare_portable_targz "$STAGE_DIR"
            ARCHIVES+=("$TARGZ_PATH")
        fi
        ;;
    appimage)
        if ! build_appimage; then
            log_error "AppImage toolchain is missing."
            log_error "Install linuxdeploy and linuxdeploy-plugin-qt:"
            log_error "  https://github.com/linuxdeploy/linuxdeploy/releases"
            log_error "  https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases"
            die "Cannot build AppImage in this environment."
        fi
        ARCHIVES+=("$APPIMAGE_PATH")
        ;;
    targz)
        prepare_portable_targz "$STAGE_DIR"
        ARCHIVES+=("$TARGZ_PATH")
        ;;
    both)
        prepare_portable_targz "$STAGE_DIR"
        ARCHIVES+=("$TARGZ_PATH")
        if ! build_appimage; then
            log_error "AppImage toolchain is missing; only tar.gz was produced."
            log_error "Install linuxdeploy + linuxdeploy-plugin-qt to enable AppImage output."
            exit 1
        fi
        ARCHIVES+=("$APPIMAGE_PATH")
        ;;
esac

# -----------------------------------------------------------------------------
# 6. 结果
# -----------------------------------------------------------------------------
echo
log_info "Linux package completed"
print_artifact "Version:" "$VERSION"
print_artifact "Qt:" "$QT_DIR"
for archive in "${ARCHIVES[@]}"; do
    print_artifact "Archive:" "$archive"
done
log_info "Portable staging directory kept for inspection: $STAGE_DIR"
log_warn "Linux packaging was authored and syntax-checked, but NOT executed on a Linux host."
