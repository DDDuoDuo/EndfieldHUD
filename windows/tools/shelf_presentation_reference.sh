#!/bin/bash
set -euo pipefail
# This build-only tool never edits Sources/Resources or opens a window.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 1 ]] || { echo 'Usage: shelf_presentation_reference.sh [new-output-directory]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/shelf-presentation-reference}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/shelf.json" ]] || { echo 'Refusing to overwrite existing oracle' >&2; exit 1; }
mkdir -p "$OUTPUT/.compiler"
# Reuse the retained source exporter cache rather than duplicating another SDK cache.
CACHE="$ROOT/build/windows-shell-packet-live-closure/.compiler/module-cache"
mkdir -p "$CACHE"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-shelf-presentation.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
mkdir -p "$FIXTURE_ROOT/tmp"
cd "$ROOT"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:])
paths=sorted(p for p in (root/'Sources').rglob('*.swift') if p.name!='main.swift')
paths += [root/'windows/tools/module_reference_layers.swift',root/'windows/tools/shelf_presentation_reference.swift']
value={'sourceBaseline':'ca04f142185c7de40acd8523bdb563195d90a1d1',
       'sourcesUnmodified':subprocess.run(['git','diff','--quiet','ca04f142185c7de40acd8523bdb563195d90a1d1','--','Sources','Resources']).returncode==0,
       'compiledSHA256':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
       'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True).strip(),
       'instrumentation':'none; unchanged original Sources compiled with separate @main'}
assert value['sourcesUnmodified'], 'Authoritative source pin changed'
(out/'provenance.json').write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(rg --files Sources -g '*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldShelfPresentationReference -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$CACHE" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
    -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/shelf_presentation_reference.swift" \
    -o "$OUTPUT/.compiler/reference" >"$OUTPUT/.compiler/compile.log" 2>&1; then
    tail -80 "$OUTPUT/.compiler/compile.log" >&2; exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" TMPDIR="$FIXTURE_ROOT/tmp/" "$OUTPUT/.compiler/reference" --ui-test --output "$OUTPUT"
