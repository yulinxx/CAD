#!/usr/bin/env bash
# Cross-platform client packaging script (Linux/macOS)
# Mirrors Tools/PackageClient.ps1 functionality
# Usage: ./package-client.sh -c <clientId> [-b <buildDir>] [-r <clientsRoot>] [-o <outDir>] [--no-zip] [--no-shortcut]

set -euo pipefail

# Defaults
CLIENT_ID=""
BUILD_DIR=""
CLIENTS_ROOT=""
OUT_DIR=""
NO_ZIP=false
NO_SHORTCUT=false
REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $*"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $*"; }
log_error() { echo -e "${RED}[ERROR]${NC} $*" >&2; }
log_step() { echo -e "${CYAN}[STEP]${NC} $*"; }

usage() {
    cat <<EOF
Usage: $0 -c <clientId> [options]

Required:
  -c, --client-id <id>        Client ID (e.g. acme)

Optional:
  -b, --build-dir <path>      Build output directory (default: auto-detect)
  -r, --clients-root <path>   Clients root directory (default: \$REPO_ROOT/Clients)
  -o, --out-dir <path>        Output directory (default: \$REPO_ROOT/dist)
      --no-zip                Skip archive creation
      --no-shortcut           Skip .desktop/.app shortcut creation
  -h, --help                  Show this help

Examples:
  $0 -c acme
  $0 -c acme -b build/bin_qt6/Release -o /tmp/dist
EOF
}

# Parse args
while [[ $# -gt 0 ]]; do
    case $1 in
        -c|--client-id) CLIENT_ID="$2"; shift 2 ;;
        -b|--build-dir) BUILD_DIR="$2"; shift 2 ;;
        -r|--clients-root) CLIENTS_ROOT="$2"; shift 2 ;;
        -o|--out-dir) OUT_DIR="$2"; shift 2 ;;
        --no-zip) NO_ZIP=true; shift ;;
        --no-shortcut) NO_SHORTCUT=true; shift ;;
        -h|--help) usage; exit 0 ;;
        *) log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
done

[[ -z "$CLIENT_ID" ]] && { log_error "Client ID is required (-c)"; usage; exit 1; }

# Paths
CLIENTS_ROOT="${CLIENTS_ROOT:-$REPO_ROOT/Clients}"
OUT_DIR="${OUT_DIR:-$REPO_ROOT/dist}"
CLIENT_DIR="$CLIENTS_ROOT/$CLIENT_ID"
CONFIG_PATH="$CLIENT_DIR/configs/$CLIENT_ID.json"
BRAND_DIR="$CLIENT_DIR/branding/$CLIENT_ID"
BRAND_PATH="$BRAND_DIR/brand.json"

# Validate client exists
[[ -f "$CONFIG_PATH" ]] || { log_error "Config not found: $CONFIG_PATH"; exit 1; }
[[ -f "$BRAND_PATH" ]] || { log_error "Brand not found: $BRAND_PATH"; exit 1; }

# Read JSON with jq
if ! command -v jq &>/dev/null; then
    log_error "jq is required but not installed"
    exit 1
fi

CONFIG_JSON=$(cat "$CONFIG_PATH")
BRAND_JSON=$(cat "$BRAND_PATH")

CONFIG_CLIENT_ID=$(echo "$CONFIG_JSON" | jq -r '.meta.clientId // empty')
[[ -n "$CONFIG_CLIENT_ID" && "$CONFIG_CLIENT_ID" != "$CLIENT_ID" ]] && {
    log_error "Config meta.clientId ($CONFIG_CLIENT_ID) != $CLIENT_ID"; exit 1; }

BRAND_CLIENT_ID=$(echo "$BRAND_JSON" | jq -r '.clientId // empty')
[[ -n "$BRAND_CLIENT_ID" && "$BRAND_CLIENT_ID" != "$CLIENT_ID" ]] && {
    log_error "brand.json clientId ($BRAND_CLIENT_ID) != $CLIENT_ID"; exit 1; }

PRODUCT_NAME=$(echo "$BRAND_JSON" | jq -r '.productName // empty')
[[ -z "$PRODUCT_NAME" ]] && { log_error "brand.json productName is required"; exit 1; }

# Auto-detect build dir
if [[ -z "$BUILD_DIR" ]]; then
    for candidate in \
        "$REPO_ROOT/build/bin_qt6/Release" \
        "$REPO_ROOT/build/bin_qt6/Debug" \
        "$REPO_ROOT/build/bin_qt6"; do
        if [[ -d "$candidate" ]]; then
            BUILD_DIR="$candidate"
            break
        fi
    done
fi
[[ -z "$BUILD_DIR" || ! -d "$BUILD_DIR" ]] && {
    log_error "Build directory not found. Pass -b <path>"; exit 1; }

# Find executable (non-Qt helper)
EXE=$(find "$BUILD_DIR" -maxdepth 1 -type f -executable ! -name "Qt*" ! -name "*.so*" ! -name "*.dylib*" | head -1)
[[ -z "$EXE" ]] && { log_error "No application executable found in $BUILD_DIR"; exit 1; }
EXE_NAME=$(basename "$EXE")

log_step "Packaging client: $CLIENT_ID ($PRODUCT_NAME)"
log_info "Build dir: $BUILD_DIR"
log_info "Executable: $EXE_NAME"

# Prepare dist
DIST_DIR="$OUT_DIR/$CLIENT_ID"
rm -rf "$DIST_DIR"
mkdir -p "$DIST_DIR"

# Copy entire build output
cp -r "$BUILD_DIR"/* "$DIST_DIR/"

# Configs
mkdir -p "$DIST_DIR/configs"
cp "$CONFIG_PATH" "$DIST_DIR/configs/"
cp "$REPO_ROOT/Main/Src/UI/ClientConfig/configs/base.json" "$DIST_DIR/configs/" 2>/dev/null || true

# Branding
mkdir -p "$DIST_DIR/branding/$CLIENT_ID"
cp -r "$BRAND_DIR"/* "$DIST_DIR/branding/$CLIENT_ID/"

# Plugins
PLUGIN_SRC="$BUILD_DIR/plugins/clients/$CLIENT_ID"
PLUGIN_DST="$DIST_DIR/plugins/clients/$CLIENT_ID"
if [[ -d "$PLUGIN_SRC" ]]; then
    mkdir -p "$(dirname "$PLUGIN_DST")"
    cp -r "$PLUGIN_SRC" "$PLUGIN_DST"
else
    log_warn "Plugin output not found: $PLUGIN_SRC (client will run without plugin panels)"
fi

# client.id
echo "$CLIENT_ID" > "$DIST_DIR/client.id"

# Platform-specific shortcuts / integration
if [[ "$NO_SHORTCUT" != true ]]; then
    case "$(uname -s)" in
        Linux)
            # .desktop file
            DESKTOP_FILE="$DIST_DIR/${PRODUCT_NAME// /_}.desktop"
            ICON_PATH=""
            APP_ICON=$(echo "$BRAND_JSON" | jq -r '.appIcon // empty')
            if [[ -n "$APP_ICON" ]]; then
                CANDIDATE="$BRAND_DIR/$APP_ICON"
                [[ -f "$CANDIDATE" ]] && ICON_PATH="$CANDIDATE"
            fi
            [[ -z "$ICON_PATH" ]] && ICON_PATH="$EXE"

            cat > "$DESKTOP_FILE" <<EOF
[Desktop Entry]
Type=Application
Name=$PRODUCT_NAME
Exec=$EXE_NAME
Icon=$ICON_PATH
Terminal=false
Categories=Graphics;Engineering;
Comment=$(echo "$BRAND_JSON" | jq -r '.tagline // ""')
EOF
            chmod +x "$DESKTOP_FILE"
            log_info "Created .desktop: $DESKTOP_FILE"
            ;;
        Darwin)
            # macOS: create .app bundle wrapper (simplified)
            APP_DIR="$DIST_DIR/${PRODUCT_NAME// /_}.app"
            mkdir -p "$APP_DIR/Contents/MacOS"
            mkdir -p "$APP_DIR/Contents/Resources"
            # Copy executable as main binary
            cp "$EXE" "$APP_DIR/Contents/MacOS/"
            # Info.plist
            cat > "$APP_DIR/Contents/Info.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key><string>$EXE_NAME</string>
    <key>CFBundleIdentifier</key><string>com.$(echo "$CLIENT_ID" | tr '[:upper:]' '[:lower:]').cad</string>
    <key>CFBundleName</key><string>$PRODUCT_NAME</string>
    <key>CFBundleVersion</key><string>1.0.0</string>
    <key>CFBundleShortVersionString</key><string>1.0.0</string>
    <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
EOF
            # Icon
            ICON_PATH=""
            APP_ICON=$(echo "$BRAND_JSON" | jq -r '.appIcon // empty')
            if [[ -n "$APP_ICON" ]]; then
                CANDIDATE="$BRAND_DIR/$APP_ICON"
                [[ -f "$CANDIDATE" ]] && ICON_PATH="$CANDIDATE"
            fi
            if [[ -n "$ICON_PATH" && "$ICON_PATH" == *.icns ]]; then
                cp "$ICON_PATH" "$APP_DIR/Contents/Resources/app.icns"
            elif [[ -n "$ICON_PATH" ]]; then
                # Convert with sips if PNG
                sips -s format icns "$ICON_PATH" --out "$APP_DIR/Contents/Resources/app.icns" 2>/dev/null || true
            fi
            log_info "Created .app bundle: $APP_DIR"
            ;;
    esac
fi

# Archive
if [[ "$NO_ZIP" != true ]]; then
    DATE_TAG=$(date +%Y%m%d)
    case "$(uname -s)" in
        Linux)
            ARCHIVE="$OUT_DIR/${CLIENT_ID}-${DATE_TAG}.tar.gz"
            tar -czf "$ARCHIVE" -C "$OUT_DIR" "$CLIENT_ID"
            ;;
        Darwin)
            ARCHIVE="$OUT_DIR/${CLIENT_ID}-${DATE_TAG}.tar.gz"
            tar -czf "$ARCHIVE" -C "$OUT_DIR" "$CLIENT_ID"
            # Could also create .dmg with hdiutil if needed
            ;;
    esac
    log_info "Archive: $ARCHIVE"
fi

log_info "Package ready: $DIST_DIR"
echo "  product : $PRODUCT_NAME"
echo "  client  : $CLIENT_ID"
echo "  exe     : $EXE_NAME"