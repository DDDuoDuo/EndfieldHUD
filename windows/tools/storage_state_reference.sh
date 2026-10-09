#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT="${1:-$ROOT/build/storage-state-reference}"
mkdir -p "$OUTPUT"
[[ ! -e "$OUTPUT/storage-state.json" ]] || { echo 'Oracle already exists' >&2; exit 1; }
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
# This tiny standalone compile uses the retained shared module cache.
CACHE="$ROOT/build/windows-shell-packet-live-closure/.compiler/module-cache"
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
  "$ROOT/Sources/StorageController.swift" "$ROOT/windows/tools/storage_state_reference.swift" -o "$OUTPUT/reference"
"$OUTPUT/reference" "$OUTPUT/storage-state.json"
shasum -a 256 "$ROOT/Sources/StorageController.swift" "$ROOT/windows/tools/storage_state_reference.swift" > "$OUTPUT/source-sha256.txt"
