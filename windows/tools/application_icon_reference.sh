#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: application_icon_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
python3 "$ROOT/windows/tools/check_source_authority.py"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$ROOT/Sources/HUDApplicationIcon.swift" "$ROOT/Sources/EndfieldGameIcon.swift" \
 "$ROOT/windows/tools/application_icon_reference.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$ROOT/Resources" "$OUTPUT/icons"
python3 - "$ROOT" "$OUTPUT/icons/manifest.json" <<'PY'
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]);out=Path(sys.argv[2]);value=json.loads(out.read_text())
value['sourceCode']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['Sources/HUDApplicationIcon.swift','Sources/EndfieldGameIcon.swift']}
out.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n')
PY
