#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 1 && "$(uname -s)" == Darwin ]] || { echo 'Usage: notes_drawing_reference.sh NEW_OUTPUT' >&2; exit 1; }
OUTPUT="$1"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)"
git -C "$ROOT" diff --quiet ca04f142185c7de40acd8523bdb563195d90a1d1 -- Sources Resources
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" \
 -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" \
 "$ROOT/Sources/NotesDrawing.swift" "$ROOT/Sources/NotesRichText.swift" \
 "$ROOT/windows/tools/notes_drawing_reference.swift" -o "$OUTPUT/reference"
"$OUTPUT/reference" "$OUTPUT/reference.json"
python3 - "$ROOT" "$OUTPUT" <<'PY'
from pathlib import Path
import hashlib,json,sys
root,out=map(Path,sys.argv[1:]);p=out/'reference.json';v=json.loads(p.read_text())
v['provenance']={'sourceCommit':'ca04f142185c7de40acd8523bdb563195d90a1d1','sources':{n:hashlib.sha256((root/n).read_bytes()).hexdigest() for n in ['Sources/NotesDrawing.swift','Sources/NotesRichText.swift']},'isolation':'Synthetic geometry. No windows, preference access, or real data.'}
p.write_text(json.dumps(v,ensure_ascii=False,separators=(',',':'))+'\n')
PY
