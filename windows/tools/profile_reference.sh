#!/bin/bash
set -euo pipefail
# Actual-source Personal Profile oracle. Compiles the unchanged Mac Sources with
# windows/tools/profile_reference.swift and drives PersonalProfileCanvas and
# UserProfileStore against temporary directories in an isolated process home.
# Usage: profile_reference.sh NEW_BUILD_DIR OUTPUT.json [PRESENTATION_OUTPUT.json [ARTWORK_OUTPUT.json]]
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ ( $# -ge 2 && $# -le 4 ) && "$(uname -s)" == Darwin ]] || { echo 'Usage: profile_reference.sh NEW_BUILD_DIR OUTPUT.json [PRESENTATION_OUTPUT.json [ARTWORK_OUTPUT.json]]' >&2; exit 1; }
BUILD="$1"; OUTPUT="$2"; PRESENTATION="${3:-}"; ARTWORK="${4:-}"
[[ ! -e "$BUILD" && ! -L "$BUILD" ]] || { echo 'Build directory must be new' >&2; exit 1; }
mkdir -p "$BUILD/module-cache"; BUILD="$(cd "$BUILD" && pwd)"
FIXTURE_HOME="$(mktemp -d "${TMPDIR:-/tmp}/endfield-profile-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_HOME"' EXIT
cd "$ROOT"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldProfileReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/profile_reference.swift" \
    -o "$BUILD/profile-reference" >"$BUILD/compile.log" 2>&1; then
    tail -60 "$BUILD/compile.log" >&2
    exit 1
fi
if [[ -n "$PRESENTATION" ]]; then
    mkdir -p "$BUILD/presentation"
    CFFIXED_USER_HOME="$FIXTURE_HOME" "$BUILD/profile-reference" "$BUILD/raw.json" "$BUILD/presentation"
    python3 -I "$ROOT/windows/tools/profile_presentation_reference.py" "$ROOT" "$BUILD/presentation/presentation.json" "$PRESENTATION"
    if [[ -n "$ARTWORK" ]]; then
        python3 -I - "$ROOT" "$BUILD/presentation/artwork.json" "$ARTWORK" <<'PY'
import hashlib, json, pathlib, sys
root, source, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
value = json.loads(source.read_text())
value['provenance'] = {'harness': 'windows/tools/profile_reference.swift', 'modifications': [],
    'sources': {s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in ('Sources/HUDSourceProfileArtwork.swift', 'Sources/HUDPortraitArtwork.swift')},
    'note': 'Synthetic inputs only; the selected business card mip is not copied.'}
output.write_text(json.dumps(value, sort_keys=True, separators=(',', ':')) + '\n')
print('Wrote', output, output.stat().st_size, 'bytes')
PY
    fi
else
    CFFIXED_USER_HOME="$FIXTURE_HOME" "$BUILD/profile-reference" "$BUILD/raw.json"
fi
python3 -I - "$ROOT" "$BUILD/raw.json" "$OUTPUT" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, raw, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
value = json.loads(raw.read_text())
sources = ['Sources/PersonalProfileCanvas.swift', 'Sources/UserProfileStore.swift', 'Sources/HUDSettingsController.swift',
           'Sources/Localization.swift', 'Sources/LocalizationCatalog.swift', 'Sources/HUDPortraitArtwork.swift']
value['provenance'] = {
    'harness': 'windows/tools/profile_reference.swift',
    'harnessSHA256': hashlib.sha256((root/'windows/tools/profile_reference.swift').read_bytes()).hexdigest(),
    'sources': {s: hashlib.sha256((root/s).read_bytes()).hexdigest() for s in sources},
    'modifications': [],
    'gitHead': subprocess.check_output(['git', '-C', str(root), 'rev-parse', 'HEAD'], text=True).strip(),
    'compiler': subprocess.check_output(['xcrun', 'swiftc', '--version'], text=True, stderr=subprocess.STDOUT).strip().splitlines()[0],
}
output.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n')
print('Wrote', output, len(value['scenario']), 'steps')
PY
