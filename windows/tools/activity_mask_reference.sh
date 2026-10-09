#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: activity_mask_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT"; OUTPUT="$(cd "$OUTPUT" && pwd)"; CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" "$SDK" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:3]);source=root/'Sources/HUDSubsectionTransition.swift';raw=source.read_bytes()
assert hashlib.sha256(raw).hexdigest()=='c73bfac1c87385dafb7c9387c8d834c937e37eb51e189ad6425602dd6fcff355','Original subsection source changed'
tool=root/'windows/tools/subsection_mask_reference.swift';text=tool.read_text();old='private let viewport = CGRect(x: 9,y: 40,width: 382,height: 248)';assert text.count(old)==1
text=text.replace(old,'private let viewport = CGRect(x: 0,y: 0,width: 376,height: 226)')
# The Shelf-only feasibility writer originally repeats its fixed header values.
# Update these declarations too; the generic runtime checks the exact viewport.
for old,new in [('[9.0,40.0,382.0,248.0,0.26]','[0.0,0.0,376.0,226.0,0.26]'),('"viewport":[9,40,382,248]','"viewport":[0,0,376,226]')]:
 assert text.count(old)==1
 text=text.replace(old,new)
(out/'activity-mask-reference.swift').write_text(text)
(out/'provenance.json').write_text(json.dumps({'source':'Sources/HUDSubsectionTransition.swift','sourceSHA256':hashlib.sha256(raw).hexdigest(),'canvasSource':'Sources/ActivityMonitorCanvas.swift','canvasSourceSHA256':hashlib.sha256((root/'Sources/ActivityMonitorCanvas.swift').read_bytes()).hexdigest(),'oracleToolSHA256':hashlib.sha256(tool.read_bytes()).hexdigest(),'buildCopySHA256':hashlib.sha256(text.encode()).hexdigest(),'viewport':[0,0,376,226],'scope':'Exact Activity appRows.bounds, actual isolated offscreen Core Animation; no activity/providers/user data; only fixture viewport changed','macOS':subprocess.check_output(['sw_vers','-productVersion'],text=True).strip(),'sdk':sys.argv[3]},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" -framework AppKit -framework QuartzCore \
 "$ROOT/Sources/HUDSubsectionTransition.swift" "$OUTPUT/activity-mask-reference.swift" -o "$OUTPUT/activity-mask-reference" >"$OUTPUT/compile.log" 2>&1
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-activity-mask-') as home:
 subprocess.run([str(out/'activity-mask-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
provenance=json.loads((out/'provenance.json').read_text())
assert hashlib.sha256((root/provenance['source']).read_bytes()).hexdigest()==provenance['sourceSHA256']
binary=(out/'candidate.bin').read_bytes();assert len(binary)<256*1024
import struct
assert struct.unpack_from('<5d',binary,8)==(0.,0.,376.,226.,.26)
report=json.loads((out/'report.json').read_text());report['candidateSHA256']=hashlib.sha256(binary).hexdigest();report['provenance']=provenance
(out/'report.json').write_text(json.dumps(report,indent=2,sort_keys=True)+'\n')
print('Activity candidate bytes',len(binary),'SHA256',report['candidateSHA256'])
PY
