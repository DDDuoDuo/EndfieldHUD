#!/bin/bash
# Actual-source oracle of Work Mode and Calendar on the redesign (Mac 1.3.0 WorkModeCanvas, HUDCalendarCanvas and
# their controllers over the design library): compiles the unchanged Mac Sources with
# tools/module_reference_layers.swift, tools/hud_design_reference_support.swift and
# tools/work_mode_calendar_reference.swift, runs it under a temporary CFFIXED_USER_HOME (no window, no input, no
# notification centre, synthetic Calendar events in a temporary directory) with English as the preferred system
# language, and writes NEW_OUTPUT/work-mode-calendar-source.json (+ provenance.json). Copy the JSON to
# windows/tests/fixtures/work-mode-calendar-source.json after reviewing it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: work_mode_calendar_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
python3 - "$ROOT" "$OUTPUT" "$SDK" <<'PY'
import hashlib,json,pathlib,platform,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]);paths=sorted(p for folder in ('Sources','Resources') for p in (root/folder).rglob('*') if p.is_file())
tools=['windows/tools/module_reference_layers.swift','windows/tools/hud_design_reference_support.swift','windows/tools/work_mode_calendar_reference.swift','windows/tools/work_mode_calendar_reference.sh']
(out/'provenance.json').write_text(json.dumps({'originals':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},
 'oracle':{t:hashlib.sha256((root/t).read_bytes()).hexdigest() for t in tools},'modifications':[],
 'gitHead':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
 'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True,stderr=subprocess.STDOUT).strip(),'sdk':sys.argv[3],
 'macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),'architecture':platform.machine()},indent=2,sort_keys=True)+'\n')
PY
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -g -whole-module-optimization -parse-as-library -module-name EndfieldWorkModeCalendarReference -D HUD_WATCH_MOTION_PREVIEW \
 -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
 -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
 "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/hud_design_reference_support.swift" \
 "$ROOT/windows/tools/work_mode_calendar_reference.swift" -o "$OUTPUT/work-mode-calendar-reference" >"$OUTPUT/compile.log" 2>&1; then
    grep -E 'error' "$OUTPUT/compile.log" | head -60 >&2; exit 1
fi
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-work-calendar-oracle-') as home:
    subprocess.run([str(out/'work-mode-calendar-reference'),'--ui-test',str(out),'-AppleLanguages','(en-US)','-AppleLocale','en_US'],cwd=root,
                   env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in json.loads((out/'provenance.json').read_text())['originals'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source/resource changed'
data=json.loads((out/'work-mode-calendar-source.json').read_text()); data['provenance']=json.loads((out/'provenance.json').read_text())
keep=('Sources/HUD','Sources/WorkMode','Sources/Localization','Resources/Fonts')
data['provenance']['originals']=dict(sorted((k,v) for k,v in data['provenance']['originals'].items() if k.startswith(keep)))
(out/'work-mode-calendar-source.json').write_text(json.dumps(data,sort_keys=True,ensure_ascii=False,separators=(',',':'))+'\n')
PY
rm -f "$OUTPUT/work-mode-calendar-reference"
