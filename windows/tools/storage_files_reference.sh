#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT="${1:-$ROOT/build/storage-files-reference}"
mkdir -p "$OUTPUT"
[[ ! -e "$OUTPUT/storage-files.json" ]] || { echo 'Oracle already exists' >&2; exit 1; }
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
CACHE="$ROOT/build/windows-module-reference/.compiler/module-cache"
[[ -d "$CACHE" ]] || { echo 'Shared existing module cache required; no duplicate cache will be made' >&2; exit 1; }
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
  "$ROOT/Sources/StorageController.swift" "$ROOT/windows/tools/storage_files_reference.swift" -o "$OUTPUT/reference"
"$OUTPUT/reference" "$OUTPUT/storage-files.json"
shasum -a 256 "$ROOT/Sources/StorageController.swift" "$ROOT/windows/tools/storage_files_reference.swift" > "$OUTPUT/source-sha256.txt"
