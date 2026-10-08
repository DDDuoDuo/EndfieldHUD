#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
if [[ $# -ne 2 || "$(uname -s)" != Darwin ]]; then
    echo "Usage on macOS: subsection_transform_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE" >&2
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
import hashlib,json,pathlib,platform,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]); source=root/'Sources/HUDSubsectionTransition.swift'; data=source.read_text()
anchors=['static let duration: TimeInterval = 0.26',
         'CAMediaTimingFunction(controlPoints: 0.16, 0.78, 0.25, 1)',
         'reveal.keyTimes = [0, 0.3, 0.68, 1]',
         'reveal.values = [CGFloat(0), 0.38, 0.72, 1].map',
         'CAMediaTimingFunction(name: .easeOut), count: 3']
if any(data.count(anchor)!=1 for anchor in anchors):
    raise SystemExit('Original subsection transition anchors changed')
(out/'provenance.json').write_text(json.dumps({'source':'Sources/HUDSubsectionTransition.swift',
 'sourceSHA256':hashlib.sha256(source.read_bytes()).hexdigest(),
 'oracleSHA256':hashlib.sha256((root/'windows/tools/subsection_transform_reference.swift').read_bytes()).hexdigest(),
 'scope':'Unmodified source reveal on own paused synthetic layers; no HUD/services/data/capture; handoff not instantiated',
 'macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),
 'osBuild':subprocess.check_output(['sw_vers','-buildVersion'],text=True).strip(),
 'architecture':platform.machine(),'sdk':sys.argv[3]},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
    -framework AppKit -framework QuartzCore "$ROOT/Sources/HUDSubsectionTransition.swift" \
    "$ROOT/windows/tools/subsection_transform_reference.swift" -o "$OUTPUT/subsection-transform-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$OUTPUT" <<'PY'
import os,pathlib,subprocess,sys,tempfile
out=pathlib.Path(sys.argv[1])
with tempfile.TemporaryDirectory(prefix='endfield-subsection-transform-') as fixture:
    subprocess.run([str(out/'subsection-transform-reference'),str(out/'samples.json')],
                   env=dict(os.environ,CFFIXED_USER_HOME=fixture),check=True)
PY
