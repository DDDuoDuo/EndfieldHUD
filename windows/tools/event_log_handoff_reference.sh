#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: event_log_handoff_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2";[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]];mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
shasum -a 256 "$ROOT/Sources/HUDSubsectionTransition.swift" >"$OUTPUT/source.sha256"
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" -framework AppKit -framework QuartzCore "$ROOT/Sources/HUDSubsectionTransition.swift" "$ROOT/windows/tools/event_log_handoff_reference.swift" -o "$OUTPUT/handoff-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$OUTPUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='ehud-event-handoff-') as home:subprocess.run([str(out/'handoff-reference'),str(out/'reference.json')],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
PY
shasum -a 256 -c "$OUTPUT/source.sha256"
