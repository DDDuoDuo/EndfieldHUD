#!/bin/bash
# The Mac Calendar model oracle (tools/calendar_reference.swift) from the unchanged Sources: compiles every Mac
# source with the exporter, runs it under a temporary CFFIXED_USER_HOME and writes
# NEW-output-directory/calendar-source.json (+ provenance). Copy it to windows/tests/fixtures/calendar-source.json
# after reviewing it. The canvas oracle is tools/work_mode_calendar_reference.sh.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 2 ]] || { echo 'Usage: calendar_reference.sh NEW-output-directory EXISTING_MODULE_CACHE' >&2; exit 1; }
[[ ! -e "$1" && -d "$2" ]] || { echo 'Refusing to overwrite an existing output directory' >&2; exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)";CACHE="$(cd "$2" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldCalendarReference -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" \
 -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement -framework Metal \
 -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz "${SOURCES[@]}" \
 "$ROOT/windows/tools/calendar_reference.swift" -o "$OUT/.compiler/calendar-reference" >"$OUT/.compiler/compile.log" 2>&1; then tail -80 "$OUT/.compiler/compile.log" >&2; exit 1; fi
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/endfield-calendar-home.XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT
CFFIXED_USER_HOME="$FIXTURE" "$OUT/.compiler/calendar-reference" "$OUT"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:])
data=json.loads((out/'calendar-reference.json').read_text())
assert not data.pop('windowCreated'),'Calendar reference created a window'
data['provenance']={'gitHead':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'sourceSHA256':{f'Sources/{p}.swift':hashlib.sha256((root/'Sources'/f'{p}.swift').read_bytes()).hexdigest() for p in ['HUDCalendarStore','HUDCalendarController']},
 'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/calendar_reference.swift','windows/tools/calendar_reference.sh']},
 'modifications':[],'isolation':'unchanged model; injected fixed clock and zones; temporary store; no window, notification permission, personal data or activation'}
(out/'calendar-source.json').write_text(json.dumps(data,separators=(',',':'),ensure_ascii=False,sort_keys=True)+'\n')
PY
rm -rf "$OUT/.compiler"
echo "PASS Calendar model oracle: $OUT/calendar-source.json"
