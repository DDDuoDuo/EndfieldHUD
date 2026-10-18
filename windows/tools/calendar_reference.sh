#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 ]] || { echo 'Usage: calendar_reference.sh NEW-output-directory' >&2; exit 1; }
[[ ! -e "$1/calendar-reference.json" ]] || { echo 'Refusing to overwrite original source oracle' >&2; exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:]);pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0
shims={
'HUDCalendarController':'''\nextension HUDCalendarController {func calendarReferenceConfigure(_ events:[HUDCalendarEvent],busy:Bool=false,permission:HUDCalendarPermission = .authorized,error:String?=nil){self.events=events;self.loaded=true;self.busy=busy;self.permission=permission;self.error=error}}\n''',
'HUDCalendarCanvas':'''\nextension HUDCalendarCanvas {func calendarReferenceConfigure(selected:HUDCalendarDay,month:HUDCalendarDay,first:Int=0){self.selectedDay=selected;self.month=month;self.firstEvent=first;self.visible=true;self.artworkPrepared=true}}\n''',
'HUDCalendarInteraction':'''\nfunc calendarReferenceMenu(editing:Bool,dark:Bool,deleting:Bool,busy:Bool,error:String?)->NotesRetainedMenu {let menu=CalendarEventMenu(editing:editing,dark:dark);if deleting{menu.perform("delete")};menu.update(busy:busy,error:error);return menu}\n'''}
for stem,shim in shims.items():
 b=(root/'Sources'/f'{stem}.swift').read_bytes();(out/'.compiler'/f'{stem}.swift').write_bytes(b+shim.encode());assert (out/'.compiler'/f'{stem}.swift').read_bytes()[:len(b)]==b
paths=['HUDCalendarStore','HUDCalendarController','HUDCalendarCanvas','HUDCalendarInteraction','HUDControlHighlightLayer','NotesFormattingControls']
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{f'Sources/{p}.swift':hashlib.sha256((root/'Sources'/f'{p}.swift').read_bytes()).hexdigest() for p in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/calendar_reference.swift','windows/tools/calendar_reference.sh','windows/tools/module_reference_layers.swift','windows/tools/prepare_calendar_reference.py']},'privateFixtureShimSHA256':{p:hashlib.sha256(s.encode()).hexdigest() for p,s in shims.items()},'isolation':'detached Calendar canvas and actual private menu factories; injected fixed clock/zone/noop scheduler; no created window, notification permission, personal data, activation or polling'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path;do SOURCES+=("$ROOT/$path");done < <(cd "$ROOT";rg --files Sources -g '*.swift' | sed '/\/main.swift$/d; /\/HUDCalendarController.swift$/d; /\/HUDCalendarCanvas.swift$/d; /\/HUDCalendarInteraction.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldCalendarReference -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz "${SOURCES[@]}" "$OUT/.compiler/HUDCalendarController.swift" "$OUT/.compiler/HUDCalendarCanvas.swift" "$OUT/.compiler/HUDCalendarInteraction.swift" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/calendar_reference.swift" -o "$OUT/.compiler/calendar-reference" >"$OUT/.compiler/compile.log" 2>&1;then tail -80 "$OUT/.compiler/compile.log" >&2;exit 1;fi
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/endfield-calendar-home.XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT
CFFIXED_USER_HOME="$FIXTURE" "$OUT/.compiler/calendar-reference" "$OUT"

python3 "$ROOT/windows/tools/prepare_calendar_reference.py" "$OUT" "$OUT/prepared"
