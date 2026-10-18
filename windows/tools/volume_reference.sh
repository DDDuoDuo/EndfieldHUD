#!/bin/bash
set -euo pipefail
# Compile untouched Mac source with a detached synthetic Volume fixture. No
# visible windows, app activation, real audio backend, user files or settings.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 1 ]] || { echo 'Usage: volume_reference.sh [new-output-directory]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-volume-reference}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/volume-reference.json" ]] || { echo 'Refusing to overwrite an existing oracle' >&2; exit 1; }
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD" "$ROOT/build/windows-module-reference/.compiler/module-cache"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-volume-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:])
pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0,'Authoritative Mac source changed'
paths=['Sources/VolumeCanvas.swift','Sources/HUDVolumeInteraction.swift','Sources/AudioDeviceController.swift','Sources/PerAppAudioController.swift','Sources/HUDControlHighlightLayer.swift','Sources/HUDModuleContent.swift','Sources/HUDModule.swift']
tools=['windows/tools/volume_reference.swift','windows/tools/volume_reference.sh','windows/tools/module_reference_layers.swift']
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in tools},'input':'synthetic fixtures only; CFFIXED_USER_HOME injected','windowsCreated':False,'audioBackend':'AudioDeviceController(snapshot:) + PerAppAudioController.fixture()'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(rg --files Sources -g '*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldVolumeReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/volume_reference.swift" \
    -o "$BUILD/volume-reference" >"$BUILD/compile.log" 2>&1; then
    tail -100 "$BUILD/compile.log" >&2; exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/volume-reference" --output "$OUTPUT"
