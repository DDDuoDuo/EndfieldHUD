#!/bin/bash
# Actual-source oracle of Storage, Activity Monitor and Battery (tools/telemetry_reference.swift): compiles the
# unchanged Mac Sources with tools/module_reference_layers.swift, tools/hud_design_reference_support.swift and the harness, runs it under a temporary CFFIXED_USER_HOME (no window, no input, no
# store; every disk, process and battery reader injected) with English as the preferred system language, and
# writes NEW_OUTPUT/telemetry-source.json (+ provenance). Copy the JSON to
# windows/tests/fixtures/telemetry-source.json after reviewing it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: telemetry_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldTelemetryReference -D HUD_WATCH_MOTION_PREVIEW \
 -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
 -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
 "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/hud_design_reference_support.swift" "$ROOT/windows/tools/telemetry_reference.swift" -o "$OUTPUT/telemetry-reference" >"$OUTPUT/compile.log" 2>&1; then
    grep -E 'error' "$OUTPUT/compile.log" | head -60 >&2; exit 1
fi
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
sources=['Sources/TelemetryCanvases.swift','Sources/ActivityMonitorCanvas.swift','Sources/BatteryCanvas.swift','Sources/StorageController.swift',
         'Sources/BatteryCapacity.swift','Sources/BatteryMonitor.swift','Sources/AppActivityMonitor.swift','Sources/SystemActivityMonitor.swift',
         'Sources/HUDChargeMetric.swift','Sources/LocalizationCatalog.swift','Sources/LocalizationCatalogRedesign.swift']
before={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources}
with tempfile.TemporaryDirectory(prefix='ehud-telemetry-oracle-') as home:
    subprocess.run([str(out/'telemetry-reference'),str(out/'run'),'-AppleLanguages','(en-US)','-AppleLocale','en_US'],cwd=root,
                   env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in before.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source changed'
data=json.loads((out/'run'/'telemetry-source.json').read_text())
data['provenance']={'originals':before,'oracle':{t:hashlib.sha256((root/t).read_bytes()).hexdigest() for t in ['windows/tools/module_reference_layers.swift','windows/tools/hud_design_reference_support.swift','windows/tools/telemetry_reference.swift','windows/tools/telemetry_reference.sh']},
    'gitHead':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()}
(out/'telemetry-source.json').write_text(json.dumps(data,sort_keys=True,ensure_ascii=False,separators=(',',':'))+'\n')
print(out/'telemetry-source.json')
PY
