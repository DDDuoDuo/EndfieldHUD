#!/bin/bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "The authoritative shell exporter requires macOS and Metal." >&2
    exit 1
fi
OUTPUT="${1:-$ROOT/build/windows-shell-packet}"
if [[ $# -gt 0 ]]; then shift; fi
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD/module-cache"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 "$ROOT/windows/tools/export_shell_packet_instrument.py" "$ROOT" "$BUILD"
cp "$BUILD/instrumentation.json" "$OUTPUT/instrumentation.json"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, output = map(pathlib.Path, sys.argv[1:])
paths = sorted(p for folder in ('Sources', 'Resources') for p in (root/folder).rglob('*') if p.is_file())
exporters = sorted((root/'windows/tools').glob('export_shell_packet*')) + [
    root/'windows/tools/module_reference_layers.swift', root/'windows/tools/export_macos_reference.swift']
value = {'sourceAndResourceSHA256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
         'exporterSHA256': {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in exporters},
         'baselineRequested': 'ca04f142185c7de40acd8523bdb563195d90a1d1',
         'gitHead': subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'], text=True).strip(),
         'compiler': subprocess.check_output(['xcrun','swiftc','--version'], text=True).strip()}
(output/'provenance.json').write_text(json.dumps(value, indent=2, sort_keys=True)+'\n')
PY
SOURCES=()
while IFS= read -r source; do SOURCES+=("$source"); done < <(find "$ROOT/Sources" -maxdepth 1 -name '*.swift' ! -name main.swift ! -name HUDSourceMetalRenderer.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -D HUD_WATCH_MOTION_PREVIEW -D HUD_SOURCE_RENDER_PREVIEW -D HUD_SHELL_PACKET_EXPORT \
    -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$BUILD/HUDSourceMetalRenderer.swift" "$BUILD/MacOSReferenceHelpers.swift" \
    "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/export_shell_packet.swift" \
    -o "$BUILD/export-shell-packet" >"$BUILD/compile.log" 2>&1; then
    tail -80 "$BUILD/compile.log" >&2
    exit 1
fi
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-shell-packet.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/export-shell-packet" --ui-test --export-shell-packet --output "$OUTPUT" "$@"
python3 "$ROOT/windows/tests/test_shell_packet_export.py" "$OUTPUT" --source-root "$ROOT"
