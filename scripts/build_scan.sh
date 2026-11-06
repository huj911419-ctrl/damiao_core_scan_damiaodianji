#!/usr/bin/env bash
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
"$ROOT/scripts/prepare_project.sh"
make -C "$ROOT/workspace" -f Makefile.j4310-scan -j"$(nproc)" all
printf '\nFirmware SHA256:\n'
sha256sum "$ROOT/workspace/build/j4310-v11-scan/J4310-v11-id-scan.hex"
