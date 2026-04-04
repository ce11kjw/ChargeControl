#!/bin/bash
# ChargeControl – build.sh
# Validates module files and creates the installable KernelSU ZIP.

set -e

# ── Colors ────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'; BLUE='\033[0;34m'; NC='\033[0m'
ok()   { echo -e "${GREEN}  ✓ $*${NC}"; }
warn() { echo -e "${YELLOW}  ⚠ $*${NC}"; }
err()  { echo -e "${RED}  ✗ $*${NC}"; exit 1; }
info() { echo -e "${BLUE}  → $*${NC}"; }

echo ""
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo -e "${BLUE}   ChargeControl Module Builder        ${NC}"
echo -e "${BLUE}═══════════════════════════════════════${NC}"
echo ""

# ── Read version from module.prop ─────────────────────────
VERSION=$(grep '^version=' module.prop | cut -d= -f2)
VERSION_CODE=$(grep '^versionCode=' module.prop | cut -d= -f2)
info "Version: $VERSION (code: $VERSION_CODE)"

# ── Check required tools ──────────────────────────────────
info "Checking tools..."
command -v zip  >/dev/null 2>&1 || err "zip is not installed"
ok "zip found"

# ── Required module files ─────────────────────────────────
info "Validating module structure..."
REQUIRED=(module.prop service.sh post-fs-data.sh uninstall.sh \
          server.py charge_control.py stats.py config.json \
          webroot/index.html webroot/styles.css webroot/script.js start_server.sh)

for f in "${REQUIRED[@]}"; do
    if [ -f "$f" ]; then
        ok "$f"
    else
        warn "Missing: $f"
    fi
done

# ── Create output directory ───────────────────────────────
OUTPUT_DIR="out"
mkdir -p "$OUTPUT_DIR"

ZIP_NAME="ChargeControl_${VERSION}.zip"
ZIP_PATH="$OUTPUT_DIR/$ZIP_NAME"

# ── Remove old build ──────────────────────────────────────
[ -f "$ZIP_PATH" ] && rm -f "$ZIP_PATH"

# ── Set executable permissions ────────────────────────────
info "Setting permissions..."
chmod +x service.sh post-fs-data.sh uninstall.sh start_server.sh build.sh 2>/dev/null || true
ok "Permissions set"

# ── Package module ────────────────────────────────────────
info "Creating ZIP: $ZIP_PATH"

INCLUDE=(
    module.prop
    service.sh
    post-fs-data.sh
    uninstall.sh
    common.prop
    server.py
    charge_control.py
    stats.py
    config.json
    start_server.sh
    README.md
)

for f in "${INCLUDE[@]}"; do
    if [ -f "$f" ]; then
        zip -q "$ZIP_PATH" "$f"
        ok "Added $f"
    fi
done

# Also include webroot/ if present
if [ -d "webroot" ]; then
    zip -qr "$ZIP_PATH" webroot/
    ok "Added webroot/"
fi

# Also include docs/ if present
if [ -d "docs" ]; then
    zip -qr "$ZIP_PATH" docs/
    ok "Added docs/"
fi

echo ""
echo -e "${GREEN}═══════════════════════════════════════${NC}"
echo -e "${GREEN}   Build complete!                     ${NC}"
echo -e "${GREEN}   Output: $ZIP_PATH                   ${NC}"
if command -v du >/dev/null 2>&1; then
    SIZE=$(du -sh "$ZIP_PATH" | cut -f1)
    echo -e "${GREEN}   Size:   $SIZE                       ${NC}"
fi
echo -e "${GREEN}═══════════════════════════════════════${NC}"
echo ""
