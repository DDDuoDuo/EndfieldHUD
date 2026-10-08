#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: work_mode_artwork_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"; OUTPUT="$(cd "$OUTPUT" && pwd)"; CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:]);names=['WorkModeController','WorkModeCanvas','HUDModule','HUDControlHighlightLayer','HUDRenderScale','HUDSectionHeading']
(out/'provenance.json').write_text(json.dumps({'sources':{f'Sources/{name}.swift':hashlib.sha256((root/'Sources'/f'{name}.swift').read_bytes()).hexdigest() for name in names},'modifications':[]},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" -framework AppKit -framework QuartzCore -framework ImageIO -framework CryptoKit \
 "$ROOT/Sources/WorkModeController.swift" "$ROOT/Sources/WorkModeCanvas.swift" "$ROOT/Sources/HUDModule.swift" "$ROOT/Sources/HUDControlHighlightLayer.swift" "$ROOT/Sources/HUDRenderScale.swift" "$ROOT/Sources/HUDSectionHeading.swift" \
 "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/work_mode_artwork_reference.swift" -o "$OUTPUT/work-mode-artwork-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-work-oracle-') as home:subprocess.run([str(out/'work-mode-artwork-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in json.loads((out/'provenance.json').read_text())['sources'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source changed'
PY
