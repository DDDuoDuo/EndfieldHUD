#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
OUTPUT="${1:-$ROOT/build/storage-files-reference}"
mkdir -p "$OUTPUT"
[[ ! -e "$OUTPUT/storage-files.json" ]] || { echo 'Oracle already exists' >&2; exit 1; }
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
CACHE="$ROOT/build/windows-module-reference/.compiler/module-cache"
[[ -d "$CACHE" ]] || { echo 'Shared existing module cache required; no duplicate cache will be made' >&2; exit 1; }
# Mac 1.3.0's StorageController.swift names the startup disk through L10n
# (Models.swift AppLanguage), so the whole unchanged Sources tree is compiled.
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
  -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
  -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
  "${SOURCES[@]}" "$ROOT/windows/tools/storage_files_reference.swift" -o "$OUTPUT/reference"
"$OUTPUT/reference" "$OUTPUT/storage-files.json"
shasum -a 256 "$ROOT/Sources/StorageController.swift" "$ROOT/windows/tools/storage_files_reference.swift" > "$OUTPUT/source-sha256.txt"
