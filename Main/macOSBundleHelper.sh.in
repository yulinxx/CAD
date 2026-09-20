#!/bin/bash
# Bundle helper script for macOS app deployment

TARGET_DIR="$1"
PDTOCAIRO="$2"
GHOSTSCRIPT="$3"

mkdir -p "$TARGET_DIR/lib"

# Copy dependencies
for lib in $(otool -L "$PDTOCAIRO" 2>/dev/null | grep -E '^\s+/opt/homebrew|/usr/local' | awk '{print $1}'); do
    cp -n "$lib" "$TARGET_DIR/lib/" 2>/dev/null || true
done

for lib in $(otool -L "$GHOSTSCRIPT" 2>/dev/null | grep -E '^\s+/opt/homebrew|/usr/local' | awk '{print $1}'); do
    cp -n "$lib" "$TARGET_DIR/lib/" 2>/dev/null || true
done

# Copy Ghostscript resources
GS_ROOT=$(dirname $(dirname "$GHOSTSCRIPT"))/share/ghostscript
if [ -d "$GS_ROOT" ]; then
    cp -R "$GS_ROOT"/* "$TARGET_DIR/share/ghostscript/" 2>/dev/null || true
fi

# Fix rpath
install_name_tool -add_rpath @loader_path/../lib "$TARGET_DIR/bin/pdftocairo" 2>/dev/null || true
install_name_tool -add_rpath @loader_path/../lib "$TARGET_DIR/bin/gs" 2>/dev/null || true

# Fix library install names
for f in "$TARGET_DIR/lib"/*.dylib; do
    [ -f "$f" ] || continue
    bn=$(basename "$f")
    install_name_tool -id @rpath/$bn "$f" 2>/dev/null || true
done
