#!/bin/bash
# Regenerates windows/tests/fixtures/display-center-logo-source.json from the
# unchanged Mac HUDCenterLogoPresentation.swift and HUDSourceProfileArtwork.swift.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: display_center_logo_reference.sh NEW_OUTPUT_DIR EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$ROOT/Sources/HUDCenterLogoPresentation.swift" "$ROOT/Sources/HUDSourceProfileArtwork.swift" \
 "$ROOT/windows/tools/display_center_logo_reference.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$ROOT/Resources" "$OUTPUT/display-center-logo-source.json"
python3 - "$ROOT" "$OUTPUT/display-center-logo-source.json" <<'PY'
from pathlib import Path
import hashlib,json,sys
root=Path(sys.argv[1]);out=Path(sys.argv[2]);value=json.loads(out.read_text())
value['sourceCode']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['Sources/HUDCenterLogoPresentation.swift','Sources/HUDSourceProfileArtwork.swift','windows/tools/display_center_logo_reference.swift']}
out.write_text(json.dumps(value,ensure_ascii=False,indent=1,sort_keys=True)+'\n')
PY
