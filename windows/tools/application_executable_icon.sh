#!/bin/bash
# Regenerates windows/resources/app/EndfieldHUD.ico and its provenance from the
# unchanged Mac icon renderer. Usage: application_executable_icon.sh NEW_OUTPUT EXISTING_MODULE_CACHE
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: application_executable_icon.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
git -C "$ROOT" diff --quiet ca04f142185c7de40acd8523bdb563195d90a1d1 -- Sources/HUDApplicationIcon.swift Sources/EndfieldGameIcon.swift
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$ROOT/Sources/HUDApplicationIcon.swift" "$ROOT/Sources/EndfieldGameIcon.swift" \
 "$ROOT/windows/tools/application_executable_icon.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$ROOT/Resources" "$OUTPUT/icons"
python3 -I - "$ROOT" "$OUTPUT/icons" <<'PY'
# Pack the rendered PNG frames into one ICO (PNG-compressed entries, the
# Windows Vista+ format; width/height byte 0 means 256).
from pathlib import Path
import hashlib, json, struct, sys
root, icons = Path(sys.argv[1]), Path(sys.argv[2])
provenance = json.loads((icons / 'provenance.json').read_text())
frames = [(entry['size'], (icons / entry['file']).read_bytes()) for entry in provenance['sizes']]
header = struct.pack('<HHH', 0, 1, len(frames))
offset = 6 + 16 * len(frames)
directory, payload = b'', b''
for size, data in frames:
    directory += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset + len(payload))
    payload += data
ico = header + directory + payload
(icons / 'EndfieldHUD.ico').write_bytes(ico)
provenance['ico'] = {'file': 'EndfieldHUD.ico', 'sha256': hashlib.sha256(ico).hexdigest(), 'bytes': len(ico)}
provenance['sourceCode'] = {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in ['Sources/HUDApplicationIcon.swift', 'Sources/EndfieldGameIcon.swift']}
(icons / 'provenance.json').write_text(json.dumps(provenance, ensure_ascii=False, indent=2, sort_keys=True) + '\n')
print('EndfieldHUD.ico', provenance['ico'])
PY
