#!/usr/bin/env bash
# ============================================================================
# April CAD macOS 签名与公证脚本
# ============================================================================
# 职责：
#   1. 对 .app 内的框架 / dylib / 辅助可执行文件与 bundle 本身做 codesign
#      （使用 hardened runtime + secure timestamp）；
#   2. 使用 notarytool 提交公证并 staple 票据；
#   3. 对 .dmg 等分发包补签名、公证与 staple。
#
# 凭据全部来自环境变量，脚本内不保存任何真实密钥：
#   APPLE_SIGN_IDENTITY      Developer ID Application: Example Corp (TEAMID)
#   NOTARYTOOL_PROFILE       notarytool 钥匙串 profile（推荐）
#   或 APPLE_ID + APPLE_APP_PASSWORD + APPLE_TEAM_ID
#
# 行为约定（用户可见输出与错误信息使用英文，注释使用中文）：
#   - 凭据缺失：打印明确告警并跳过签名/公证，退出码 0，不让构建失败；
#   - 凭据存在但签名/公证失败：退出码非 0，避免产出看似已签名的坏包。
#
# 用法：
#   sign-macos.sh --app path/to/April.app
#   sign-macos.sh --app path/to/April.app --artifact dist/April-1.0.0-macos-arm64.dmg
#   sign-macos.sh --artifact dist/April-1.0.0-macos-arm64.dmg   # 仅处理分发包
# ============================================================================

set -euo pipefail

source "$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)/common.sh"

APP_PATH=""
ARTIFACTS=()
IDENTITY="${APPLE_SIGN_IDENTITY:-}"
SKIP_NOTARIZE=false
REQUIRE_CREDENTIALS=false

usage() {
    cat <<'EOF'
Usage: sign-macos.sh [options]

Sign and notarize April macOS artifacts. Credentials are read from the
environment; nothing is stored in the repository.

Options:
  --app <path>             .app bundle to sign, notarize and staple
  --artifact <path>        extra archive to sign/notarize (.dmg is signed and
                           stapled; other archives are notarized only)
  --identity <name>        override APPLE_SIGN_IDENTITY
  --skip-notarize          code signing only
  --require-credentials    fail instead of skipping when credentials are absent
  -h, --help               show this help

Environment:
  APPLE_SIGN_IDENTITY      "Developer ID Application: Example Corp (TEAMID)"
  NOTARYTOOL_PROFILE       notarytool keychain profile (recommended)
  APPLE_ID                 Apple ID for notarytool (alternative to profile)
  APPLE_APP_PASSWORD       app-specific password for the Apple ID
  APPLE_TEAM_ID            Apple Developer Team ID
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --app)                  APP_PATH="${2:-}"; shift 2 ;;
        --artifact)             ARTIFACTS+=("${2:-}"); shift 2 ;;
        --identity)             IDENTITY="${2:-}"; shift 2 ;;
        --skip-notarize)        SKIP_NOTARIZE=true; shift ;;
        --require-credentials)  REQUIRE_CREDENTIALS=true; shift ;;
        -h|--help)              usage; exit 0 ;;
        *)                      log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
done

[[ "$(uname -s)" == "Darwin" ]] || die "This script must run on macOS."
[[ -n "$APP_PATH" || ${#ARTIFACTS[@]} -gt 0 ]] || die "Pass --app and/or --artifact."

if [[ -n "$APP_PATH" ]]; then
    APP_PATH="$(resolve_abs "$APP_PATH")"
    [[ -d "$APP_PATH" ]] || die "Application bundle not found: $APP_PATH"
fi

for artifact in "${ARTIFACTS[@]:-}"; do
    [[ -n "$artifact" ]] || continue
    [[ -f "$artifact" ]] || die "Artifact not found: $artifact"
done

# ----------------------------------------------------------------------------
# 凭据检查：缺失时告警并跳过（默认不失败）
# ----------------------------------------------------------------------------
missing_credentials() {
    local reason="$1"
    if [[ "$REQUIRE_CREDENTIALS" == true ]]; then
        die "$reason"
    fi
    log_warn "$reason"
    log_warn "Signing/notarization skipped. This artifact is for internal testing only."
}

if [[ -z "$IDENTITY" ]]; then
    missing_credentials "APPLE_SIGN_IDENTITY is not set."
    exit 0
fi

require_cmd codesign "codesign ships with the Xcode command line tools."
require_cmd otool "otool ships with the Xcode command line tools."

# ----------------------------------------------------------------------------
# 1. 签名 .app
# ----------------------------------------------------------------------------
sign_nested_code() {
    local app="$1"
    local frameworks="$app/Contents/Frameworks"
    local sign_args=(--force --options runtime --timestamp --sign "$IDENTITY")

    if [[ -d "$frameworks" ]]; then
        local fw
        for fw in "$frameworks"/*.framework; do
            [[ -d "$fw" ]] || continue
            log_info "Signing framework: $(basename "$fw")"
            codesign "${sign_args[@]}" "$fw"
        done

        local file
        while IFS= read -r -d '' file; do
            log_info "Signing dylib: $(basename "$file")"
            codesign "${sign_args[@]}" "$file"
        done < <(find "$frameworks" -type f -name "*.dylib" -print0)
    fi

    local helper_dir="$app/Contents/Resources/bin"
    if [[ -d "$helper_dir" ]]; then
        local helper
        for helper in "$helper_dir"/*; do
            [[ -f "$helper" && -x "$helper" ]] || continue
            log_info "Signing helper: $(basename "$helper")"
            codesign "${sign_args[@]}" "$helper"
        done
    fi

    # 本工程把着色器 / 字体 / 翻译文件放在 Contents/MacOS（运行期按
    # applicationDirPath 读取），必须带 --deep 才能把 bundle 作为整体签完。
    log_info "Signing bundle: $app"
    codesign "${sign_args[@]}" --deep "$app"

    log_step "Verifying signature"
    codesign --verify --strict --verbose=2 "$app"
}

if [[ -n "$APP_PATH" ]]; then
    sign_nested_code "$APP_PATH"
fi

# ----------------------------------------------------------------------------
# 2. 公证凭据
# ----------------------------------------------------------------------------
notary_auth_args=()
notary_configured=false
if [[ -n "${NOTARYTOOL_PROFILE:-}" ]]; then
    notary_auth_args=(--keychain-profile "$NOTARYTOOL_PROFILE")
    notary_configured=true
elif [[ -n "${APPLE_ID:-}" && -n "${APPLE_APP_PASSWORD:-}" && -n "${APPLE_TEAM_ID:-}" ]]; then
    notary_auth_args=(--apple-id "$APPLE_ID" --password "$APPLE_APP_PASSWORD" --team-id "$APPLE_TEAM_ID")
    notary_configured=true
fi

if [[ "$SKIP_NOTARIZE" == true ]]; then
    log_warn "Notarization skipped (--skip-notarize)."
    exit 0
fi

if [[ "$notary_configured" != true ]]; then
    missing_credentials "Notarization credentials are not set (NOTARYTOOL_PROFILE or APPLE_ID/APPLE_APP_PASSWORD/APPLE_TEAM_ID)."
    exit 0
fi

require_cmd xcrun "Install the Xcode command line tools."

# ----------------------------------------------------------------------------
# 3. 提交公证并 staple
# ----------------------------------------------------------------------------
notarize_path() {
    local target="$1"
    local submit_path="$target"
    local tmp_zip=""

    if [[ -d "$target" ]]; then
        tmp_zip="$(mktemp -t april-notarize).zip"
        log_step "Creating temporary archive for notarization"
        ditto -c -k --sequesterRsrc --keepParent "$target" "$tmp_zip"
        submit_path="$tmp_zip"
    fi

    log_step "Submitting for notarization: $submit_path"
    if ! xcrun notarytool submit "$submit_path" "${notary_auth_args[@]}" --wait --timeout 30m; then
        [[ -n "$tmp_zip" ]] && rm -f "$tmp_zip"
        die "notarytool submission failed: $target"
    fi
    [[ -n "$tmp_zip" ]] && rm -f "$tmp_zip"

    if [[ -d "$target" || "$target" == *.dmg ]]; then
        log_step "Stapling notarization ticket: $target"
        xcrun stapler staple "$target"
        xcrun stapler validate "$target"
    fi
}

if [[ -n "$APP_PATH" ]]; then
    notarize_path "$APP_PATH"
    log_step "Assessing Gatekeeper policy"
    if ! spctl --assess --type execute --verbose=2 "$APP_PATH"; then
        log_warn "spctl assessment failed; this can happen on CI runners without a full Gatekeeper database."
    fi
fi

for artifact in "${ARTIFACTS[@]:-}"; do
    [[ -n "$artifact" ]] || continue
    if [[ "$artifact" == *.dmg ]]; then
        log_step "Signing disk image: $artifact"
        codesign --force --timestamp --sign "$IDENTITY" "$artifact"
    fi
    notarize_path "$artifact"
done

log_info "macOS signing and notarization completed."
