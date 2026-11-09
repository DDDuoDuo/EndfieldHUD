#!/bin/bash
set -euo pipefail
# Compile untouched Mac source with a detached synthetic audio fixture. No
# visible windows, app activation, real audio backend, user files or settings.
# Writes NEW_OUTPUT/audio-endpoint-source.json and NEW_OUTPUT/provenance.json (refuses to
# overwrite); copy them to windows/tests/fixtures/audio-endpoint-source.json and
# audio-endpoint-source-provenance.json after reviewing them.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 2 ]] || { echo 'Usage: audio_reference.sh [new-output-directory [existing-module-cache]]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-audio-reference}"
CACHE="${2:-$ROOT/build/windows-module-reference/.compiler/module-cache}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/audio-endpoint-source.json" ]] || { echo 'Refusing to overwrite an existing oracle' >&2; exit 1; }
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD" "$CACHE"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-audio-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
# The Mac commit whose Sources and Resources the fixture is exported from (Mac 1.3.1).
PIN=79bc6651959977d3c6aa89c8faeedb155a15f20d
python3 - "$ROOT" "$OUTPUT" "$PIN" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3])
pin=sys.argv[3]
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0,'Authoritative Mac source changed'
paths=['Sources/SystemEventRecorder.swift','Sources/SystemEventLog.swift','Sources/AudioTopologyWatcher.swift','Sources/AudioDeviceController.swift','Sources/PerAppAudioController.swift']
tools=['windows/tools/audio_reference.swift','windows/tools/audio_reference.sh']
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in tools},'input':'synthetic fixtures only; CFFIXED_USER_HOME injected','windowsCreated':False,'audioBackend':'AudioVolumeMath + in-memory SystemEventLog','fixture':'windows/tests/fixtures/audio-endpoint-source.json','regenerate':'bash windows/tools/audio_reference.sh <new-output-directory> [<module-cache>] (macOS)'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(find Sources -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldAudioReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$CACHE" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/audio_reference.swift" \
    -o "$BUILD/audio-reference" >"$BUILD/compile.log" 2>&1; then
    tail -100 "$BUILD/compile.log" >&2; exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/audio-reference" --output "$OUTPUT" --source-authority "$PIN" -AppleLanguages '(en-US)' -AppleLocale en_US
rm -rf "$BUILD"
