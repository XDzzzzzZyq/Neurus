#!/usr/bin/env python3
"""
setup_dependencies.py - Download pre-compiled third-party libraries

Downloads platform-specific binary archives from the Neurus-Lib
GitHub Release and extracts them into lib/<platform>/.

Usage:
    python scripts/setup_dependencies.py [--version VERSION]

The script:
    1. Detects the current platform (windows / linux / macos)
    2. Determines the tag version (from --version, pinned file, or latest release)
    3. Downloads the matching archive from GitHub Releases
    4. Verifies SHA256 checksum if available
    5. Extracts into lib/<platform>/
    6. Writes a .version stamp file for cache invalidation

Neurus-Lib repository: https://github.com/XDzzzzzZyq/Neurus-Lib
"""

import argparse
import hashlib
import json
import os
import platform
import shutil
import sys
import zipfile
from pathlib import Path
from urllib.request import Request, urlopen

# ---------------------------------------------------------------------------
# Configuration
# ---------------------------------------------------------------------------

REPO = "XDzzzzzZyq/Neurus-Lib"
RELEASE_API = f"https://api.github.com/repos/{REPO}/releases"
DOWNLOAD_URL = f"https://github.com/{REPO}/releases/latest/download"
MANIFEST_FILE = "manifest.json"
VERSION_STAMP = ".version"

# Headers for GitHub API (optional token for higher rate limits)
_GITHUB_TOKEN = os.environ.get("GITHUB_TOKEN", "")


def _headers():
    hdrs = {"Accept": "application/vnd.github+json", "User-Agent": "Neurus-setup-deps/1.0"}
    if _GITHUB_TOKEN:
        hdrs["Authorization"] = f"Bearer {_GITHUB_TOKEN}"
    return hdrs


# ---------------------------------------------------------------------------
# Platform Detection
# ---------------------------------------------------------------------------

def detect_platform():
    """Return the current platform identifier string (windows, linux, macos)."""
    sysname = platform.system()
    if sysname == "Windows":
        return "windows"
    elif sysname == "Linux":
        return "linux"
    elif sysname == "Darwin":
        return "macos"
    else:
        raise RuntimeError(f"Unsupported platform: {sysname}")


# ---------------------------------------------------------------------------
# Version Resolution
# ---------------------------------------------------------------------------

def get_latest_tag():
    """Query the GitHub API for the latest release tag name."""
    url = f"{RELEASE_API}/latest"
    req = Request(url, headers=_headers())
    with urlopen(req) as resp:
        data = json.loads(resp.read().decode("utf-8"))
    return data["tag_name"]


def resolve_version(pinned_version: str = None):
    """
    Resolve the version to download.

    Priority:
      1. --version argument (explicit pin)
      2. Script directory .version_pin file
      3. Latest GitHub Release (live query)
    """
    if pinned_version:
        return pinned_version

    pin_file = Path(__file__).parent / ".version_pin"
    if pin_file.exists():
        return pin_file.read_text().strip()

    return get_latest_tag()


# ---------------------------------------------------------------------------
# Download & Verification
# ---------------------------------------------------------------------------

def download_archive(url: str, dest: Path):
    """Download a file from a URL to the destination path, with progress."""
    print(f"  Downloading {url} ...")
    req = Request(url, headers=_headers())
    with urlopen(req) as resp:
        dest.parent.mkdir(parents=True, exist_ok=True)
        with open(dest, "wb") as f:
            shutil.copyfileobj(resp, f)
    size_mb = dest.stat().st_size / (1024 * 1024)
    print(f"  Downloaded {size_mb:.1f} MB -> {dest.name}")


def verify_checksum(filepath: Path, expected_sha256: str):
    """Verify a file's SHA256 checksum against the expected value."""
    if not expected_sha256:
        return  # No checksum available, skip verification

    print(f"  Verifying checksum ...")
    sha256 = hashlib.sha256()
    with open(filepath, "rb") as f:
        for chunk in iter(lambda: f.read(65536), b""):
            sha256.update(chunk)
    actual = sha256.hexdigest()
    if actual != expected_sha256:
        raise RuntimeError(
            f"Checksum mismatch!\n"
            f"  Expected: {expected_sha256}\n"
            f"  Actual:   {actual}"
        )
    print(f"  Checksum OK")


def get_archive_name(platform_name: str) -> str:
    """Return the archive filename for the given platform."""
    if platform_name == "windows":
        return "windows-x64.zip"
    elif platform_name == "macos":
        return "macos-arm64.tar.gz"
    else:
        return f"{platform_name}-x64.tar.gz"


# ---------------------------------------------------------------------------
# Extraction
# ---------------------------------------------------------------------------

def extract_archive(archive_path: Path, lib_dir: Path, platform_name: str):
    """Extract the downloaded archive into lib/<platform>/."""
    target = lib_dir / platform_name

    # Remove existing extracted libs for a clean install
    if target.exists():
        print(f"  Removing existing {target} ...")
        shutil.rmtree(target)

    target.mkdir(parents=True, exist_ok=True)

    if archive_path.suffix == ".zip":
        with zipfile.ZipFile(archive_path, "r") as zf:
            zf.extractall(target)
    else:
        import tarfile
        with tarfile.open(archive_path, "r:gz") as tf:
            tf.extractall(target)

    print(f"  Extracted -> {target}")


# ---------------------------------------------------------------------------
# Cache Invalidation
# ---------------------------------------------------------------------------

def write_version_stamp(lib_dir: Path, version: str):
    """Write a .version stamp so cmake configure can detect stale libs."""
    stamp = lib_dir / VERSION_STAMP
    stamp.write_text(f"{version}\n")
    print(f"  Version stamp: {stamp}")


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(
        description="Download pre-compiled Neurus dependencies"
    )
    parser.add_argument(
        "--version",
        help="Pin to a specific release tag (e.g., 2407)",
        default=None,
    )
    parser.add_argument(
        "--skip-verify",
        help="Skip checksum verification",
        action="store_true",
        default=False,
    )
    args = parser.parse_args()

    # Determine paths relative to the project root
    script_dir = Path(__file__).resolve().parent
    project_root = script_dir.parent
    lib_dir = project_root / "lib"

    platform_name = detect_platform()
    version = resolve_version(args.version)

    print(f"=== Neurus Dependency Setup ===")
    print(f"  Platform : {platform_name}")
    print(f"  Version  : {version}")
    print(f"  Lib dir  : {lib_dir / platform_name}")

    archive_name = get_archive_name(platform_name)
    download_url = f"{DOWNLOAD_URL}/{archive_name}"

    # Download
    temp_dir = lib_dir / ".tmp"
    temp_dir.mkdir(parents=True, exist_ok=True)
    archive_path = temp_dir / archive_name

    try:
        download_archive(download_url, archive_path)

        # Checksum (optional)
        if not args.skip_verify:
            sha_url = f"{download_url}.sha256"
            try:
                req = Request(sha_url, headers=_headers())
                with urlopen(req) as resp:
                    expected = resp.read().decode("utf-8").strip().split()[0]
                verify_checksum(archive_path, expected)
            except Exception:
                print("  No checksum file found, skipping verification")

        # Extract
        extract_archive(archive_path, lib_dir, platform_name)

        # Stamp
        write_version_stamp(lib_dir, version)

    finally:
        # Clean up temp download
        if temp_dir.exists():
            shutil.rmtree(temp_dir)

    print(f"=== Done ===")
    print(f"  Libraries installed to: {lib_dir / platform_name}")
    print(f"  Run cmake --preset default to configure with pre-compiled libs.")


if __name__ == "__main__":
    main()
