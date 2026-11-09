#!/bin/bash
# Regenerates windows/tests/fixtures/now-playing-artwork-source.json from the
# unchanged Mac NowPlayingArtwork.swift, HUDPortraitArtwork.swift and
# HUDSourceProfileArtwork.swift (which the portrait file names): cover
# layout rows, letterbox trims and the small-cover decode (foreground and
# backdrop samples) over synthetic pictures, and HUDPortraitArtwork.panningImage
# rows (whole premultiplied outputs) for the Personal ID portrait.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: now_playing_artwork_reference.sh NEW_OUTPUT_DIR EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1";CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$ROOT/Sources/NowPlayingArtwork.swift" "$ROOT/Sources/HUDPortraitArtwork.swift" "$ROOT/Sources/HUDSourceProfileArtwork.swift" \
 "$ROOT/windows/tools/now_playing_artwork_reference.swift" -o "$OUTPUT/reference" >"$OUTPUT/compile.log" 2>&1
"$OUTPUT/reference" "$OUTPUT/now-playing-artwork-source.json"
python3 - "$ROOT" "$OUTPUT/now-playing-artwork-source.json" <<'PY'
from pathlib import Path
import hashlib,json,subprocess,sys
root=Path(sys.argv[1]);out=Path(sys.argv[2]);value=json.loads(out.read_text())
value['sourceCode']={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['Sources/NowPlayingArtwork.swift','Sources/HUDPortraitArtwork.swift','Sources/HUDSourceProfileArtwork.swift','windows/tools/now_playing_artwork_reference.swift']}
out.write_text(json.dumps(value,ensure_ascii=False,indent=1,sort_keys=True)+'\n')
PY
