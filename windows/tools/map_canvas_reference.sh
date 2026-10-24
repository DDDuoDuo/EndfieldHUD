#!/bin/bash
# Actual-source oracle of the redesigned Map (tools/map_canvas_reference.swift): compiles the unchanged Mac
# Sources with tools/module_reference_layers.swift, tools/hud_design_reference_support.swift and the harness,
# runs it under a temporary CFFIXED_USER_HOME (no window, no input, no geography; the map store in its own
# temporary directory) with English as the preferred system language, and writes
# NEW_OUTPUT/map-canvas-source.json (+ provenance). Copy the JSON to
# windows/tests/fixtures/map-canvas-source.json after reviewing it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: map_canvas_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldMapCanvasReference -D HUD_WATCH_MOTION_PREVIEW \
 -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
 -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
 "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/hud_design_reference_support.swift" "$ROOT/windows/tools/map_canvas_reference.swift" -o "$OUTPUT/map-canvas-reference" >"$OUTPUT/compile.log" 2>&1; then
    grep -E 'error' "$OUTPUT/compile.log" | head -60 >&2; exit 1
fi
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
sources=['Sources/WorldMapCanvas.swift','Sources/WorldMapSurface.swift','Sources/WorldMapGeometry.swift','Sources/WorldMapStore.swift',
         'Sources/WorldMapPinArtwork.swift','Sources/HUDDesignDrawing.swift','Sources/LocalizationCatalog.swift','Sources/LocalizationCatalogRedesign.swift']
before={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources}
with tempfile.TemporaryDirectory(prefix='ehud-map-canvas-oracle-') as home:
    subprocess.run([str(out/'map-canvas-reference'),str(out/'run'),'-AppleLanguages','(en-US)','-AppleLocale','en_US'],cwd=root,
                   env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in before.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source changed'
data=json.loads((out/'run'/'map-canvas-source.json').read_text())
data['provenance']={'originals':before,'oracle':{t:hashlib.sha256((root/t).read_bytes()).hexdigest() for t in ['windows/tools/module_reference_layers.swift','windows/tools/hud_design_reference_support.swift','windows/tools/map_canvas_reference.swift','windows/tools/map_canvas_reference.sh']},
    'gitHead':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip()}
(out/'map-canvas-source.json').write_text(json.dumps(data,sort_keys=True,ensure_ascii=False,separators=(',',':'))+'\n')
print(out/'map-canvas-source.json')
PY
