#!/usr/bin/env bash
# Assemble the complete, reproducible read-only CAN ID scanning project.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
UPSTREAM="$ROOT/upstream"
WORKSPACE="$ROOT/workspace"
PINNED_UPSTREAM=a0ebb9596b9cf5a827140eb4209af1628ea67525

if [[ ! -f "$UPSTREAM/Makefile" ]]; then
    echo '[1/3] Fetch pinned upstream sources and dependencies...'
    git -C "$ROOT" submodule update --init --recursive upstream
fi
ACTUAL="$(git -C "$UPSTREAM" rev-parse HEAD)"
if [[ "$ACTUAL" != "$PINNED_UPSTREAM" ]]; then
    echo "ERROR: upstream HEAD=$ACTUAL, expected $PINNED_UPSTREAM. Refusing a mixed-version build." >&2
    exit 1
fi
mkdir -p "$WORKSPACE"
if [[ -e "$WORKSPACE/.git" ]]; then
    echo 'ERROR: workspace contains Git metadata; refusing to overwrite repository.' >&2
    exit 1
fi

echo '[2/3] Export full pinned upstream source tree...'
git -C "$UPSTREAM" archive --format=tar "$PINNED_UPSTREAM" | tar -xf - -C "$WORKSPACE"
echo '[3/3] Overlay tested read-only J4310 scan sources...'
for path in Core/Src/main.c Core/Src/gpio.c Makefile Makefile.j4310-scan \
    application/j4310_scan/j4310_scan.c application/j4310_scan/j4310_scan.h \
    docs/j4310-v1.1/ID_SCANNING.md docs/j4310-v1.1/ID_RESULT_2026-11-05.md; do
    mkdir -p "$WORKSPACE/$(dirname "$path")"
    cp "$ROOT/$path" "$WORKSPACE/$path"
done
printf 'READY: %s\n' "$WORKSPACE"
printf 'Build with: %s/scripts/build_scan.sh\n' "$ROOT"
