#!/bin/bash
# Mac-only oracle: exports EventLogCanvas.timestamp(_:) strings for fixed and
# DST-boundary instants in nine time zones to NEW_OUTPUT/event_log_timestamps.json
# (copied to windows/tests/fixtures/event_log_timestamps.json). The canvas is
# compiled from an unchanged copy except that timestamp(_:) is made callable.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: event_log_timestamp_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:]);names=['SystemEventLog','EventLogCanvas','HUDModule','HUDControlHighlightLayer','HUDSubsectionTransition','HUDRenderScale','HUDSectionHeading']
pins={f'Sources/{name}.swift':hashlib.sha256((root/'Sources'/f'{name}.swift').read_bytes()).hexdigest() for name in names}
source=(root/'Sources/EventLogCanvas.swift').read_text();old='    private func timestamp(_ date: Date) -> String {';assert source.count(old)==1
copy=source.replace(old,'    func timestamp(_ date: Date) -> String {');(out/'EventLogCanvas.swift').write_text(copy)
(out/'provenance.json').write_text(json.dumps({'sources':pins,'canvasBuildCopySHA256':hashlib.sha256(copy.encode()).hexdigest(),'modification':'timestamp(_:) visibility only'},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" -framework AppKit -framework QuartzCore \
 "$ROOT/Sources/SystemEventLog.swift" "$OUTPUT/EventLogCanvas.swift" "$ROOT/Sources/HUDModule.swift" "$ROOT/Sources/HUDControlHighlightLayer.swift" "$ROOT/Sources/HUDSubsectionTransition.swift" "$ROOT/Sources/HUDRenderScale.swift" "$ROOT/Sources/HUDSectionHeading.swift" \
 "$ROOT/windows/tools/event_log_timestamp_reference.swift" -o "$OUTPUT/event-log-timestamp-reference" >"$OUTPUT/compile.log" 2>&1
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-event-time-oracle-') as home:
 subprocess.run([str(out/'event-log-timestamp-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
for name,digest in json.loads((out/'provenance.json').read_text())['sources'].items():assert hashlib.sha256((root/name).read_bytes()).hexdigest()==digest,'Original source changed'
PY
