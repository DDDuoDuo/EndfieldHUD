#!/bin/bash
set -euo pipefail

# Compile the Mac implementation itself, never a Windows approximation or raw
# game preview. Usage: export_macos_reference.sh [output-directory] [--size WxH]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ "$(uname -s)" != Darwin ]]; then
    echo "This reference exporter requires macOS and Metal." >&2
    exit 1
fi
OUTPUT="${1:-$ROOT/build/windows-macos-reference}"
if [[ $# -gt 0 ]]; then shift; fi
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD/module-cache"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$path"); done < <(find "$ROOT/Sources" -maxdepth 1 -name '*.swift' ! -name main.swift | LC_ALL=C sort)

# A fresh preference root provides a second containment boundary. The fixture
# does not instantiate UserDefaults or any persistent store in the first place.
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-macos-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, output = map(pathlib.Path, sys.argv[1:])
files = sorted(p for folder in ('Sources', 'Resources') for p in (root / folder).rglob('*') if p.is_file())
manifest = {str(p.relative_to(root)): hashlib.sha256(p.read_bytes()).hexdigest() for p in files}
provenance = {'baselineRequested': 'ca04f14',
              'gitHead': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
              'sourceAndResourceSHA256': manifest,
              'exporterSHA256': {p.name: hashlib.sha256(p.read_bytes()).hexdigest() for p in
                                (root/'windows/tools').glob('export_macos_reference.*')},
              'compiler': subprocess.check_output(['xcrun', 'swiftc', '--version'], text=True).strip()}
(output/'provenance.json').write_text(json.dumps(provenance, indent=2, sort_keys=True) + '\n')
PY
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/export_macos_reference.swift" -o "$BUILD/export-macos-reference" >"$BUILD/compile.log" 2>&1; then
    tail -80 "$BUILD/compile.log" >&2
    exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/export-macos-reference" \
    --ui-test --render-macos-reference --output "$OUTPUT" "$@"
python3 "$ROOT/windows/tests/test_reference_export.py" "$OUTPUT" --source-root "$ROOT"
