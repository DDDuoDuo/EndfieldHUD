#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: hud_clock_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import pathlib,hashlib,json,sys
root,out=map(pathlib.Path,sys.argv[1:]);p=root/'Sources/HUDClock.swift'
(out/'provenance.json').write_text(json.dumps({'source':'Sources/HUDClock.swift','sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'modifications':[]},indent=2)+'\n')
PY
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$ROOT/Sources/HUDClock.swift" "$ROOT/windows/tools/hud_clock_reference.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$OUTPUT/reference.json"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import pathlib,hashlib,json,sys
root,out=map(pathlib.Path,sys.argv[1:]);p=json.loads((out/'provenance.json').read_text());assert hashlib.sha256((root/p['source']).read_bytes()).hexdigest()==p['sha256']
v=json.loads((out/'reference.json').read_text());v['provenance']=p;(out/'reference.json').write_text(json.dumps(v,ensure_ascii=False,separators=(',',':'))+'\n')
PY
