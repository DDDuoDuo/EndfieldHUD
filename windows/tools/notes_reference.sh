#!/bin/bash
# Actual-source oracle of the redesigned Notes centre (NotesCanvas, NotesFormatBar): compiles the unchanged Mac Sources with
# tools/module_reference_layers.swift, tools/hud_design_reference_support.swift and tools/notes_reference.swift,
# runs it under a temporary CFFIXED_USER_HOME in UTC (the synthetic stores live there; no window, no input)
# with English as the preferred language, and writes NEW_OUTPUT/notes-canvas-source.json with its provenance. Copy it
# to windows/tests/fixtures/notes-canvas-source.json after reviewing it.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: notes_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
TOOLS=(windows/tools/module_reference_layers.swift windows/tools/hud_design_reference_support.swift windows/tools/notes_reference.swift)
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; find Sources -type f -name '*.swift' ! -name main.swift | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldNotesReference \
 -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
 -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit \
 -framework JavaScriptCore -lsqlite3 -lz "${SOURCES[@]}" "${TOOLS[@]/#/$ROOT/}" -o "$OUTPUT/notes-reference" >"$OUTPUT/compile.log" 2>&1; then
    grep -E 'error' "$OUTPUT/compile.log" | head -60 >&2; exit 1
fi
python3 -I - "$ROOT" "$OUTPUT" "$SDK" "${TOOLS[@]}" <<'PY'
import hashlib,json,os,pathlib,platform,subprocess,sys,tempfile
root,out,sdk,tools=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]),sys.argv[3],sys.argv[4:]
names=['NotesCanvas','NotesFormattingControls','NotesRichText','ArchiveCanvas','NotesStore']
originals={f'Sources/{n}.swift':hashlib.sha256((root/'Sources'/f'{n}.swift').read_bytes()).hexdigest() for n in names}
originals.update({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'Sources').glob('HUDDesign*.swift'))})
originals.update({str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted((root/'Sources').glob('Localization*.swift'))})
with tempfile.TemporaryDirectory(prefix='ehud-notes-oracle-') as home:
    subprocess.run([str(out/'notes-reference'),str(out),'-AppleLanguages','(en-US)','-AppleLocale','en_US'],cwd=root,
                   env=dict(os.environ,CFFIXED_USER_HOME=home,TZ='UTC'),check=True)
for name,digest in originals.items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source changed during export'
data=json.loads((out/'notes-canvas-source.json').read_text())
data['provenance']={'originals':originals,'oracle':{t:hashlib.sha256((root/t).read_bytes()).hexdigest() for t in tools+['windows/tools/notes_reference.sh']},
    'modifications':[],'gitHead':subprocess.check_output(['git','-C',str(root),'rev-parse','HEAD'],text=True).strip(),
    'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True,stderr=subprocess.STDOUT).strip(),'sdk':sdk,
    'macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),'architecture':platform.machine(),
    'isolation':{'files':'temporary CFFIXED_USER_HOME (synthetic stores)','window':False,'inputPosted':False,'timeZone':'UTC'}}
(out/'notes-canvas-source.json').write_text(json.dumps(data,sort_keys=True,ensure_ascii=False,separators=(',',':'))+'\n')
print('Wrote',out/'notes-canvas-source.json',(out/'notes-canvas-source.json').stat().st_size,'bytes')
PY
