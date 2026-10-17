#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: battery_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"; OUTPUT="$(cd "$OUTPUT" && pwd)"; CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 "$ROOT/windows/tools/battery_reference.py" "$ROOT" "$OUTPUT"
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" -framework AppKit -framework QuartzCore -framework IOKit -framework CryptoKit \
 "$ROOT/Sources/BatteryMonitor.swift" "$ROOT/Sources/BatteryCapacity.swift" "$ROOT/Sources/HUDControlHighlightLayer.swift" \
 "$OUTPUT/BatteryReferenceFixture.swift" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/battery_reference.swift" -o "$OUTPUT/battery-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$OUTPUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='ehud-battery-oracle-') as home:subprocess.run([str(out/'battery-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
PY
python3 "$ROOT/windows/tools/battery_reference.py" --compact "$OUTPUT"
