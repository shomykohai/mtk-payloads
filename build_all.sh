#!/usr/bin/env bash

DIRS=(
    da_x
    da_xml
    extloader
    brom_defuse
    hakujoudai
    sla_xml
)

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"
PAYLOADS_DIR="$ROOT_DIR/payloads"

RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[0;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
NC='\033[0m'

log()     { echo -e "${BLUE}[INFO]${NC} $*"; }
step()    { echo -e "${CYAN}[STEP]${NC} $*"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $*"; }
warn()    { echo -e "${YELLOW}[WARNING]${NC} $*"; }
error()   { echo -e "${RED}[ERROR]${NC} $*" >&2; }

log "Starting build process"
step "Cleaning environment"

rm -rf "$PAYLOADS_DIR"
mkdir -p "$PAYLOADS_DIR"

for dir in "${DIRS[@]}"; do
    BUILD_DIR="$ROOT_DIR/$dir"

    [[ -d "$BUILD_DIR" ]] || { warn "Missing $dir, skipping"; continue; }

    step "Building $dir"
    pushd "$BUILD_DIR" >/dev/null

    STATUS=0

    if [[ -f Makefile ]]; then
        make clean >/dev/null 2>&1 || true

        if [[ -x build.sh ]]; then
            ./build.sh >/dev/null
            STATUS=$?
        else
            make >/dev/null
            STATUS=$?
        fi
    else
        warn "No Makefile in $dir"
        STATUS=1
    fi

    if [[ $STATUS -ne 0 ]]; then
        warn "$dir build failed (continuing)"
    else
        success "$dir built"
    fi

    popd >/dev/null
done


success "Build process completed successfully"
