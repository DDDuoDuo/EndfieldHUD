#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: settings_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:]);paths=sorted(p for folder in ('Sources','Resources') for p in (root/folder).rglob('*') if p.is_file())
(out/'provenance.json').write_text(json.dumps({'originals':{str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in paths},'modifications':[]},indent=2)+'\n')
PY
SOURCES=()
while IFS= read -r path; do SOURCES+=("$ROOT/$path"); done < <(cd "$ROOT"; rg --files Sources -g '*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldSettingsReference -D HUD_WATCH_MOTION_PREVIEW \
 -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement \
 -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
 "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/settings_reference.swift" -o "$OUTPUT/settings-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-settings-oracle-') as home:subprocess.run([str(out/'settings-reference'),'--ui-test',str(out)],cwd=root,env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in json.loads((out/'provenance.json').read_text())['originals'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source/resource changed'
PY
