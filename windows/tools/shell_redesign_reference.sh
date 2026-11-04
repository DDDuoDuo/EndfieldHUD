#!/bin/bash
# Regenerates windows/tests/fixtures/shell-redesign-source.json from the
# unchanged Mac 1.3.0 Sources: HUDClockStyleArtwork (five clock styles, the
# status row), HUDAccountGauge (Work Mode link, recovery menu layout) and
# HUDSourceDesktopIconLayout (side button glyph sizing). Detached classes only:
# no SystemHUDView, window, timer, account or user data.
# Usage (macOS only): windows/tools/shell_redesign_reference.sh NEW-scratch-directory [OUTPUT.json]
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && ( $# -eq 1 || $# -eq 2 ) ]] || { echo 'Usage: shell_redesign_reference.sh NEW-scratch-directory [OUTPUT.json]' >&2; exit 1; }
[[ ! -e "$1" ]] || { echo 'Scratch directory must be new' >&2; exit 1; }
mkdir -p "$1/module-cache"
BUILD="$(cd "$1" && pwd)"
OUTPUT="${2:-$ROOT/windows/tests/fixtures/shell-redesign-source.json}"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT" && git ls-files 'Sources/*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldShellReference \
    -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$BUILD/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
    -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/shell_redesign_reference.swift" \
    -o "$BUILD/shell-reference" >"$BUILD/compile.log" 2>&1; then
  tail -60 "$BUILD/compile.log" >&2; exit 1
fi
FIXTURE_HOME="$(mktemp -d "${TMPDIR:-/tmp}/endfield-shell-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_HOME"' EXIT
CFFIXED_USER_HOME="$FIXTURE_HOME" TZ=UTC "$BUILD/shell-reference" "$BUILD/raw.json"
python3 -I - "$ROOT" "$BUILD/raw.json" "$OUTPUT" <<'PY'
import hashlib, json, pathlib, sys
root, raw, output = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
value = json.loads(raw.read_text())
sources = ['Sources/HUDClockStyle.swift', 'Sources/HUDAccountGauge.swift', 'Sources/HUDSourceDesktopNavigationLayout.swift',
           'Sources/HUDDesignText.swift', 'Sources/HUDDesignFonts.swift', 'Sources/HUDSourceWatchView.swift']
value['provenance'] = {'harness': 'windows/tools/shell_redesign_reference.swift',
    'harnessSHA256': hashlib.sha256((root / 'windows/tools/shell_redesign_reference.swift').read_bytes()).hexdigest(),
    'sources': {s: hashlib.sha256((root / s).read_bytes()).hexdigest() for s in sources}, 'modifications': []}
output.write_text(json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n')
print('Wrote', output, output.stat().st_size, 'bytes')
PY
