#!/usr/bin/env python3
"""Fetch official pinned TLS source; --verify-only performs no network access."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import tarfile

ROOT = Path(__file__).resolve().parents[1]
PIN = json.loads((ROOT / "cmake/lan-dependencies.json").read_text(encoding="utf-8-sig"))
CACHE = ROOT / ".toolchains"
SOURCE = CACHE / ("mbedtls-" + PIN["version"])
ARCHIVE = CACHE / PIN["archive"]
INPUT_SUFFIXES = (".h", ".c", ".cmake", ".txt", ".py", ".json", ".s", ".S")


def archive_valid():
    return ARCHIVE.is_file() and hashlib.sha256(ARCHIVE.read_bytes()).hexdigest() == PIN["sha256"]


def build_input(name):
    return name.endswith(INPUT_SUFFIXES) or "/LICENSE" in name


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--verify-only", action="store_true")
    args = parser.parse_args()
    if not archive_valid():
        if args.verify_only:
            raise SystemExit("Missing/corrupt TLS archive; run python tools/fetch-lan-deps.py")
        CACHE.mkdir(exist_ok=True)
        # A complete wrong archive cannot be resumed. Partial downloads are retained.
        if ARCHIVE.exists() and ARCHIVE.stat().st_size >= PIN["bytes"]:
            ARCHIVE.unlink()
        curl = ["curl.exe" if os.name == "nt" else "curl"]
        if os.name == "nt":
            curl += ["--ssl-revoke-best-effort"]
        # Every connection and transfer is bounded; rerunning resumes partial data.
        for _ in range(30):
            result = subprocess.run(curl + [
                "-4", "--fail", "-L", "-C", "-", "--connect-timeout", "10",
                "--max-time", "20", "--speed-limit", "1024", "--speed-time", "10",
                "-o", str(ARCHIVE), PIN["url"],
            ])
            if archive_valid():
                break
            if result.returncode == 0:
                raise SystemExit("TLS archive SHA256 mismatch; remove corrupt archive and retry")
        else:
            raise SystemExit("TLS download incomplete; rerun to resume")
    if not SOURCE.exists():
        if args.verify_only:
            raise SystemExit("Missing TLS source; run python tools/fetch-lan-deps.py")
        with tarfile.open(ARCHIVE) as archive:
            archive.extractall(CACHE, filter="data")
    # Compare every build input with the authenticated archive before offline CMake.
    expected = set()
    with tarfile.open(ARCHIVE) as archive:
        for entry in archive.getmembers():
            if entry.isfile() and build_input(entry.name):
                path = CACHE / entry.name
                expected.add(path.resolve())
                if not path.is_file() or path.read_bytes() != archive.extractfile(entry).read():
                    raise SystemExit("TLS source differs from pinned archive: " + entry.name)
    for path in SOURCE.rglob("*"):
        if path.is_file() and build_input(path.as_posix()) and path.resolve() not in expected:
            raise SystemExit("Unexpected TLS build input: " + str(path))
    print("Verified Mbed TLS " + PIN["version"] + " archive and source")


if __name__ == "__main__":
    main()
