#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 1 && "$(uname -s)" == Darwin ]] || { echo 'Usage: archive_date_reference.sh NEW_OUTPUT' >&2; exit 1; }
OUTPUT="$1"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)"
git -C "$ROOT" diff --quiet ca04f142185c7de40acd8523bdb563195d90a1d1 -- Sources Resources
python3 - "$ROOT" "$OUTPUT" <<'PY'
from pathlib import Path
import hashlib,json,sys
root,out=map(Path,sys.argv[1:]);src=(root/'Sources/ArchiveCanvas.swift').read_bytes()
start=src.index(b'    static func dateString(_ date: Date) -> String {')
end=src.index(b'\n}\n\n/// Static posters',start)
block=src[start:end]
(out/'ArchiveCanvas.swift').write_bytes(b'import Foundation\nfinal class ArchiveCanvas {\n'+block+b'\n}\n')
(out/'provenance.json').write_text(json.dumps({'sourceCommit':'ca04f142185c7de40acd8523bdb563195d90a1d1','sourcePath':'Sources/ArchiveCanvas.swift','sourceSHA256':hashlib.sha256(src).hexdigest(),'methodsSHA256':hashlib.sha256(block).hexdigest(),'isolation':'Foundation only, synthetic dates, process-local time zones; no app data, window or system setting changes'},indent=2)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" \
 -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" \
 "$OUTPUT/ArchiveCanvas.swift" "$ROOT/windows/tools/archive_date_reference.swift" -o "$OUTPUT/reference"
"$OUTPUT/reference" "$OUTPUT/reference.json"
