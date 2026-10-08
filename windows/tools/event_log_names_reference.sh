#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: event_log_names_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import pathlib,hashlib,json,sys
root,out=map(pathlib.Path,sys.argv[1:]);p=root/'Sources/SystemEventLog.swift';s=p.read_text();old='private static func compact(_ value: String) -> String {';assert s.count(old)==1
(out/'SystemEventLog.swift').write_text(s.replace(old,'static func compact(_ value: String) -> String {'))
(out/'provenance.json').write_text(json.dumps({'source':'Sources/SystemEventLog.swift','sha256':hashlib.sha256(p.read_bytes()).hexdigest(),'change':'build-copy access modifier only; private compact method body unchanged'},indent=2)+'\n')
PY
xcrun swiftc -O -swift-version 5 -parse-as-library -framework CoreGraphics -sdk "$SDK" -module-cache-path "$CACHE" \
 "$OUTPUT/SystemEventLog.swift" "$ROOT/Sources/HUDModule.swift" "$ROOT/windows/tools/event_log_names_reference.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$OUTPUT/reference.json"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import pathlib,hashlib,json,sys
root,out=map(pathlib.Path,sys.argv[1:]);p=json.loads((out/'provenance.json').read_text());assert hashlib.sha256((root/p['source']).read_bytes()).hexdigest()==p['sha256']
v=json.loads((out/'reference.json').read_text());v['provenance']=p;(out/'reference.json').write_text(json.dumps(v,ensure_ascii=False,separators=(',',':'))+'\n')
lines=['// Generated from the unmodified SystemEventLog.compact Foundation sets.',
       '// Source SHA256: '+p['sha256'],
       '// Regenerate with windows/tools/event_log_names_reference.sh; do not substitute ICU categories.',
       'struct ScalarRange {std::uint32_t first,last;};']
for name,key in [('sourceControls','control'),('sourceWhitespace','whitespace'),('sourceSpaceOrControl','spaceOrControl')]:
    lines.append('constexpr std::array '+name+'{')
    lines.extend('    ScalarRange{'+hex(a)+','+hex(b)+'},' for a,b in v['sets'][key])
    lines.append('};')
(out/'event_log_name_sets.inc').write_text('\n'.join(lines)+'\n')
PY
