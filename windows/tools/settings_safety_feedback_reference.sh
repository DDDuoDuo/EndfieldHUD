#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: settings_safety_feedback_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUT="$1";CACHE="$2"
[[ ! -e "$OUT" && -d "$CACHE" ]]
mkdir -p "$OUT";OUT="$(cd "$OUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
shasum -a 256 "$ROOT/Sources/HUDQuitConfirmationView.swift" > "$OUT/source.sha256"
xcrun swiftc -swift-version 5 -O -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework QuartzCore "$ROOT/Sources/HUDQuitConfirmationView.swift" "$ROOT/windows/tools/settings_safety_feedback_reference.swift" -o "$OUT/reference"
python3 - "$OUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='ehud-settings-safety-') as home:subprocess.run([str(out/'reference'),str(out/'reference.json')],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
PY
shasum -a 256 -c "$OUT/source.sha256"
