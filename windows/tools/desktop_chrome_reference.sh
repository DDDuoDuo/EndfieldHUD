#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo "macOS is required for authoritative chrome export." >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-desktop-chrome-reference}"
[[ $# -le 1 ]] || { echo "Usage: desktop_chrome_reference.sh [output-dir]" >&2; exit 1; }
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD/module-cache"
python3 "$ROOT/windows/tools/desktop_chrome_reference_instrument.py" "$ROOT" "$BUILD"
cp "$BUILD/instrumentation.json" "$OUTPUT/instrumentation.json"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import pathlib,hashlib,json,sys
root,out=map(pathlib.Path,sys.argv[1:])
paths=sorted(p for d in ('Sources','Resources') for p in (root/d).rglob('*') if p.is_file())
exporters=sorted((root/'windows/tools').glob('desktop_chrome_reference*'))+[root/'windows/tools/module_reference_layers.swift']
report={'sourceAndResourceSHA256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'exporterSHA256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in exporters},'SystemHUDViewConstructed':False}
(out/'provenance.json').write_text(json.dumps(report,indent=2)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT" && rg --files Sources -g '*.swift' | sed '/\/main.swift$/d;/\/HUDSourceWatchView.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -D HUD_WATCH_MOTION_PREVIEW -D HUD_CHROME_REFERENCE -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$BUILD/HUDSourceWatchView.swift" "$BUILD/ChromeSourceFixture.swift" \
    "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/desktop_chrome_reference.swift" \
    -o "$BUILD/chrome-reference" > "$BUILD/compile.log" 2>&1; then
    tail -80 "$BUILD/compile.log" >&2; exit 1
fi
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-chrome-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/chrome-reference" --ui-test --output "$OUTPUT"
python3 "$ROOT/windows/tests/test_desktop_chrome_reference.py" "$OUTPUT" --source-root "$ROOT"
