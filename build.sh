#!/bin/bash
# ChargeControl – build.sh
# Compiles the charge_control binary, validates module files, and creates
# the installable Magisk/KernelSU ZIP.

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

# ── Compile charge_control binary ────────────────────────
# Set SKIP_COMPILE=1 to skip (e.g. when CI has already cross-compiled the binary)
if [ "${SKIP_COMPILE}" = "1" ]; then
    info "SKIP_COMPILE=1 – skipping compilation, using pre-built binary."
    if [ ! -f "charge_control" ]; then
        err "SKIP_COMPILE=1 but charge_control binary not found! Did the cross-compile step succeed?"
    fi
    ok "Pre-built charge_control found"
else
    command -v make >/dev/null 2>&1 || err "make is not installed (needed to compile charge_control)"
ok "make found"
    info "Compiling charge_control binary (make)..."
    if ! make; then
        err "Compilation failed. Fix the errors above, then re-run build.sh."
    fi
    if [ ! -f "charge_control" ]; then
        err "make succeeded but charge_control binary not found. Check the Makefile TARGET."
    fi
fi
chmod 0755 charge_control
ok "charge_control compiled and marked executable"

# ── Required module files ─────────────────────────────────
info "Validating module structure..."
REQUIRED=(module.prop service.sh post-fs-data.sh uninstall.sh \ 
          config.json Makefile \ 
          src/main.c src/charge_control.c src/charge_control.h \ 
          src/stats.c src/stats.h \ 
          src/snapshot_daemon.c src/snapshot_daemon.h \ 
          src/config.c src/config.h \ 
          src/cJSON.c src/cJSON.h \ 
          webroot/index.html webroot/styles.css webroot/script.js)

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
chmod +x service.sh post-fs-data.sh uninstall.sh build.sh 2>/dev/null || true
ok "Permissions set"

# ── Package module ────────────────────────────────────────
info "Creating ZIP: $ZIP_PATH"

INCLUDE=(
    module.prop
    service.sh
    post-fs-data.sh
    uninstall.sh
    common.prop
    config.json
    Makefile
    README.md
)

for f in "${INCLUDE[@]}"; do
    if [ -f "$f" ]; then
        zip -q "$ZIP_PATH" "$f"
        ok "Added $f"
    fi
done

# Include the compiled charge_control binary (required by service.sh)
zip -q "$ZIP_PATH" charge_control
ok "Added charge_control"

# Include C source files
if [ -d "src" ]; then
    zip -qr "$ZIP_PATH" src/
    ok "Added src/"
fi

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

# ── Verify ZIP contains the binary ───────────────────────
info "Verifying ZIP contents..."
if ! unzip -l "$ZIP_PATH" | grep -q 'charge_control$'; then
    err "charge_control is missing from $ZIP_PATH – packaging error."
fi
ok "charge_control present in ZIP"

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