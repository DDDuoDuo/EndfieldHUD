#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ $# -ne 2 || "$(uname -s)" != Darwin ]]; then
    echo "Usage on macOS: module_transform_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE" >&2
    exit 1
fi
OUTPUT="$1"
CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" "$SDK" <<'PY'
import hashlib,json,pathlib,platform,re,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]); source=root/'Sources/HUDModuleContent.swift'; data=source.read_text()
match=re.search(r'    private static func offset\(direction: CGPoint,.*?^    }',data,re.M|re.S)
if not match or data.count('static let transitionDuration: TimeInterval = 0.30')!=1 or data.count('CAMediaTimingFunction(controlPoints: 0.18, 0.72, 0.26, 1)')!=3:
    raise SystemExit('Original module transition extraction anchors changed')
body=match.group(0); copied='import AppKit\nimport QuartzCore\nenum OriginalModuleTransform {\n'+body.replace('private static func','static func',1)+'\n}\n'
(out/'OriginalModuleTransform.swift').write_text(copied)
(out/'provenance.json').write_text(json.dumps({'source':'Sources/HUDModuleContent.swift','sourceSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),'exactOffsetBodySHA256':hashlib.sha256(body.encode()).hexdigest(),'copiedWrapperSHA256':hashlib.sha256(copied.encode()).hexdigest(),'oracleSHA256':hashlib.sha256((root/'windows/tools/module_transform_reference.swift').read_bytes()).hexdigest(),'scope':'Own synthetic paused Core Animation layers; no HUD/provider/store/capture','macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),'osBuild':subprocess.check_output(['sw_vers','-buildVersion'],text=True).strip(),'architecture':platform.machine(),'sdk':sys.argv[3]},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
    -framework AppKit -framework QuartzCore "$OUTPUT/OriginalModuleTransform.swift" \
    "$ROOT/windows/tools/module_transform_reference.swift" -o "$OUTPUT/module-transform-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$OUTPUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='endfield-module-transform-') as fixture:
    subprocess.run([str(out/'module-transform-reference'),str(out/'samples.json')],
                   env=dict(os.environ,CFFIXED_USER_HOME=fixture),check=True)
PY
