#!/bin/bash
# Usage: application_quit_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE
# Writes application_quit_source.json from the unchanged HUDQuitConfirmationView.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: application_quit_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUT="$1";CACHE="$2"
[[ ! -e "$OUT" && -d "$CACHE" ]]
mkdir -p "$OUT";OUT="$(cd "$OUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
git -C "$ROOT" diff --quiet ca04f142185c7de40acd8523bdb563195d90a1d1 -- Sources/HUDQuitConfirmationView.swift
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -swift-version 5 -O -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework QuartzCore "$ROOT/Sources/HUDQuitConfirmationView.swift" "$ROOT/windows/tools/application_quit_reference.swift" -o "$OUT/reference"
python3 -I - "$OUT" "$ROOT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
out,root=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2])
with tempfile.TemporaryDirectory(prefix='ehud-quit-') as home:subprocess.run([str(out/'reference'),str(out/'application_quit_source.json')],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
value=json.loads((out/'application_quit_source.json').read_text())
value['provenance']={'commit':'ca04f142185c7de40acd8523bdb563195d90a1d1','sha256':hashlib.sha256((root/'Sources/HUDQuitConfirmationView.swift').read_bytes()).hexdigest(),'modifications':[]}
(out/'application_quit_source.json').write_text(json.dumps(value,ensure_ascii=False,separators=(',',':'),sort_keys=True)+'\n')
PY
