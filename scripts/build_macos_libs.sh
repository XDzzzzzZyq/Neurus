#!/usr/bin/env bash
set -euo pipefail

# ============================================================================
# build_macos_libs.sh - Build shaderc & qtadvanceddocking from source on macOS
#
# Builds precompiled libraries for Apple Silicon (arm64) and packages them
# into the lib/macos/ layout that setup_dependencies.py expects.
#
# Usage:
#   ./scripts/build_macos_libs.sh [--clean]
#
# Run chmod +x first:
#   chmod +x scripts/build_macos_libs.sh
# ============================================================================

# ---------------------------------------------------------------------------
# Colors
# ---------------------------------------------------------------------------
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
BOLD='\033[1m'
NC='\033[0m' # No Color

step()  { echo -e "\n${CYAN}${BOLD}>>> $1${NC}"; }
ok()    { echo -e "${GREEN}  OK: $1${NC}"; }
warn()  { echo -e "${YELLOW}  WARN: $1${NC}"; }
fail()  { echo -e "${RED}${BOLD}  FAIL: $1${NC}" >&2; exit 1; }

# ---------------------------------------------------------------------------
# Project root detection
# ---------------------------------------------------------------------------
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "${SCRIPT_DIR}/.." && pwd)"

if [[ ! -f "${PROJECT_ROOT}/CMakeLists.txt" ]]; then
    fail "Cannot find CMakeLists.txt in ${PROJECT_ROOT} — is scripts/ inside the project root?"
fi

echo -e "${BOLD}=== Neurus macOS arm64 Library Builder ===${NC}"
echo "  Project root: ${PROJECT_ROOT}"

# ---------------------------------------------------------------------------
# Parse arguments
# ---------------------------------------------------------------------------
CLEAN=0
for arg in "$@"; do
    case "${arg}" in
        --clean) CLEAN=1 ;;
        -h|--help)
            echo "Usage: $0 [--clean]"
            echo ""
            echo "  --clean    Remove previous build artifacts before building"
            exit 0
            ;;
        *)
            fail "Unknown argument: ${arg}"
            ;;
    esac
done

# ---------------------------------------------------------------------------
# Clean previous builds if requested
# ---------------------------------------------------------------------------
if [[ ${CLEAN} -eq 1 ]]; then
    step "Cleaning previous build artifacts"
    for dir in \
        "${PROJECT_ROOT}/build/_shaderc_macos" \
        "${PROJECT_ROOT}/build/_ads_macos_release" \
        "${PROJECT_ROOT}/build/_ads_macos_debug" \
        "${PROJECT_ROOT}/lib/macos"; do
        if [[ -d "${dir}" ]]; then
            echo "  Removing ${dir}"
            rm -rf "${dir}"
        fi
    done
    ok "Clean complete"
fi

# ---------------------------------------------------------------------------
# Ensure output directories exist
# ---------------------------------------------------------------------------
SHADERC_OUT="${PROJECT_ROOT}/lib/macos/shaderc/lib"
ADS_OUT="${PROJECT_ROOT}/lib/macos/qtadvanceddocking/lib"

mkdir -p "${SHADERC_OUT}"
mkdir -p "${ADS_OUT}"

# ---------------------------------------------------------------------------
# Helper: require command
# ---------------------------------------------------------------------------
require_cmd() {
    if ! command -v "$1" &>/dev/null; then
        fail "Required command '$1' not found. Please install it first."
    fi
}

# ---------------------------------------------------------------------------
# Preflight checks
# ---------------------------------------------------------------------------
step "Preflight checks"
require_cmd cmake
require_cmd ninja
require_cmd git
require_cmd python3
ok "All required commands found"

# =============================
#  shaderc
# =============================
step "Building shaderc"

# --- Initialize submodule ---
step "shaderc: Initializing submodule"
(
    cd "${PROJECT_ROOT}"
    git submodule update --init dep/shaderc
) || fail "Failed to initialize shaderc submodule"
ok "shaderc submodule initialized"

# --- Sync third-party deps (glslang, spirv-tools, spirv-headers) ---
step "shaderc: Syncing dependencies (glslang, spirv-tools, spirv-headers)"
(
    cd "${PROJECT_ROOT}/dep/shaderc"
    python3 utils/git-sync-deps
) || fail "Failed to sync shaderc dependencies (git-sync-deps)"
ok "shaderc dependencies synced"

# --- CMake Configure ---
step "shaderc: CMake configure"
SHADERC_BUILD="${PROJECT_ROOT}/build/_shaderc_macos"
cmake -S "${PROJECT_ROOT}/dep/shaderc" -B "${SHADERC_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DSHADERC_SKIP_TESTS=ON \
    -DSHADERC_ENABLE_EXAMPLES=OFF \
    -DSHADERC_SKIP_INSTALL=OFF \
    -DCMAKE_INSTALL_PREFIX="${SHADERC_BUILD}/install" \
    -DSHADERC_ENABLE_HLSL=OFF \
    -DENABLE_OPT=OFF \
|| fail "shaderc CMake configure failed"
ok "shaderc configured"

# --- Build ---
step "shaderc: Building"
cmake --build "${SHADERC_BUILD}" --config Release \
|| fail "shaderc build failed"
ok "shaderc built"

# --- Install ---
step "shaderc: Installing"
cmake --install "${SHADERC_BUILD}" \
|| fail "shaderc install failed"
ok "shaderc installed"

# --- Copy dylibs ---
step "shaderc: Copying dylibs to lib/macos/shaderc/lib/"
INSTALL_LIB="${SHADERC_BUILD}/install/lib"
if [[ ! -d "${INSTALL_LIB}" ]]; then
    fail "shaderc install lib directory not found at ${INSTALL_LIB}"
fi

# Copy all shared library files (dylibs + symlinks) preserving symlinks
cp -a "${INSTALL_LIB}"/libshaderc_shared*.dylib "${SHADERC_OUT}/" \
|| fail "Failed to copy shaderc dylibs"
ok "shaderc dylibs copied to ${SHADERC_OUT}"

# Verify expected files
if [[ -L "${SHADERC_OUT}/libshaderc_shared.dylib" ]]; then
    ok "libshaderc_shared.dylib symlink present"
else
    warn "libshaderc_shared.dylib symlink not found — creating it"
    # Find the versioned dylib and create the symlink
    VERSIONED=$(find "${SHADERC_OUT}" -name 'libshaderc_shared.?.dylib' -not -type l | head -1)
    if [[ -n "${VERSIONED}" ]]; then
        ln -sf "$(basename "${VERSIONED}")" "${SHADERC_OUT}/libshaderc_shared.dylib"
        ok "Created symlink libshaderc_shared.dylib -> $(basename "${VERSIONED}")"
    else
        warn "No versioned dylib found to create symlink from"
    fi
fi

# =============================
#  qtadvanceddocking (Release)
# =============================
step "Building qtadvanceddocking — Release"

step "qtadvanceddocking: Initializing submodule"
(
    cd "${PROJECT_ROOT}"
    git submodule update --init dep/qtadvanceddocking
) || fail "Failed to initialize qtadvanceddocking submodule"
ok "qtadvanceddocking submodule initialized"

# --- CMake Configure (Release) ---
step "qtadvanceddocking: CMake configure (Release)"
ADS_RELEASE_BUILD="${PROJECT_ROOT}/build/_ads_macos_release"
cmake -S "${PROJECT_ROOT}/dep/qtadvanceddocking" -B "${ADS_RELEASE_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DADS_VERSION=4.5.0 \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_STATIC=ON \
|| fail "qtadvanceddocking Release CMake configure failed"
ok "qtadvanceddocking Release configured"

# --- Build (Release) ---
step "qtadvanceddocking: Building (Release)"
cmake --build "${ADS_RELEASE_BUILD}" --config Release \
|| fail "qtadvanceddocking Release build failed"
ok "qtadvanceddocking Release built"

# --- Copy static lib ---
step "qtadvanceddocking: Copying Release static lib"
ADS_RELEASE_LIB=$(find "${ADS_RELEASE_BUILD}" -name 'libqtadvanceddocking-qt6_static.a' -type f | head -1)
if [[ -z "${ADS_RELEASE_LIB}" ]]; then
    # Try alternate naming patterns
    ADS_RELEASE_LIB=$(find "${ADS_RELEASE_BUILD}" -name 'libqtadvanceddocking*.a' -type f | head -1)
    if [[ -z "${ADS_RELEASE_LIB}" ]]; then
        fail "Could not find qtadvanceddocking Release static library in ${ADS_RELEASE_BUILD}"
    fi
    warn "Expected libqtadvanceddocking-qt6_static.a, found $(basename "${ADS_RELEASE_LIB}")"
fi
cp "${ADS_RELEASE_LIB}" "${ADS_OUT}/libqtadvanceddocking-qt6_static.a" \
|| fail "Failed to copy qtadvanceddocking Release static lib"
ok "qtadvanceddocking Release lib copied to ${ADS_OUT}/libqtadvanceddocking-qt6_static.a"

# =============================
#  qtadvanceddocking (Debug)
# =============================
step "Building qtadvanceddocking — Debug"

# --- CMake Configure (Debug) ---
step "qtadvanceddocking: CMake configure (Debug)"
ADS_DEBUG_BUILD="${PROJECT_ROOT}/build/_ads_macos_debug"
cmake -S "${PROJECT_ROOT}/dep/qtadvanceddocking" -B "${ADS_DEBUG_BUILD}" -G Ninja \
    -DCMAKE_BUILD_TYPE=Debug \
    -DCMAKE_OSX_ARCHITECTURES=arm64 \
    -DADS_VERSION=4.5.0 \
    -DBUILD_EXAMPLES=OFF \
    -DBUILD_STATIC=ON \
|| fail "qtadvanceddocking Debug CMake configure failed"
ok "qtadvanceddocking Debug configured"

# --- Build (Debug) ---
step "qtadvanceddocking: Building (Debug)"
cmake --build "${ADS_DEBUG_BUILD}" --config Debug \
|| fail "qtadvanceddocking Debug build failed"
ok "qtadvanceddocking Debug built"

# --- Copy static lib ---
step "qtadvanceddocking: Copying Debug static lib"
ADS_DEBUG_LIB=$(find "${ADS_DEBUG_BUILD}" -name 'libqtadvanceddocking-qt6d_static.a' -type f | head -1)
if [[ -z "${ADS_DEBUG_LIB}" ]]; then
    # Try alternate naming patterns (debug builds may use different suffix)
    ADS_DEBUG_LIB=$(find "${ADS_DEBUG_BUILD}" -name 'libqtadvanceddocking*d_static.a' -type f | head -1)
    if [[ -z "${ADS_DEBUG_LIB}" ]]; then
        # Broader search — pick any static lib that isn't the release one
        ADS_DEBUG_LIB=$(find "${ADS_DEBUG_BUILD}" -name 'libqtadvanceddocking*.a' -type f | head -1)
        if [[ -z "${ADS_DEBUG_LIB}" ]]; then
            fail "Could not find qtadvanceddocking Debug static library in ${ADS_DEBUG_BUILD}"
        fi
    fi
    warn "Expected libqtadvanceddocking-qt6d_static.a, found $(basename "${ADS_DEBUG_LIB}")"
fi
cp "${ADS_DEBUG_LIB}" "${ADS_OUT}/libqtadvanceddocking-qt6d_static.a" \
|| fail "Failed to copy qtadvanceddocking Debug static lib"
ok "qtadvanceddocking Debug lib copied to ${ADS_OUT}/libqtadvanceddocking-qt6d_static.a"

# =============================
#  Summary
# =============================
echo ""
echo -e "${GREEN}${BOLD}=== Done ===${NC}"
echo ""
echo "Precompiled macOS arm64 libraries built at: lib/macos/"
echo ""
echo "Contents:"
echo "  lib/macos/shaderc/lib/"
ls -la "${SHADERC_OUT}/" 2>/dev/null | tail -n +2 | while read -r line; do
    echo "    ${line}"
done
echo "  lib/macos/qtadvanceddocking/lib/"
ls -la "${ADS_OUT}/" 2>/dev/null | tail -n +2 | while read -r line; do
    echo "    ${line}"
done
echo ""
echo "To create a release archive:"
echo "  cd lib/macos && tar -czf ../../macos-arm64.tar.gz ."
echo ""
echo "Upload macos-arm64.tar.gz to https://github.com/XDzzzzzZyq/Neurus-Lib/releases"
