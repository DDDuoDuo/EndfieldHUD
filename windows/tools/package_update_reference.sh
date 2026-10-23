#!/bin/bash
# Usage: package_update_reference.sh NEW_OUTPUT
# Writes update-release-source.json (windows/packaging/tests/fixtures) from the
# UNCHANGED Sources/HUDGitHubRelease.swift: HUDReleaseVersion acceptance and
# precedence, validatedURL, Foundation's control-character set and
# HUDGitHubReleaseClient decisions over synthetic GitHub responses.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 1 && "$(uname -s)" == Darwin ]] || { echo 'Usage: package_update_reference.sh NEW_OUTPUT' >&2; exit 1; }
OUT="$1"
[[ ! -e "$OUT" && ! -L "$OUT" ]]
mkdir -p "$OUT";OUT="$(cd "$OUT" && pwd)"
git -C "$ROOT" diff --quiet ca04f142185c7de40acd8523bdb563195d90a1d1 -- Sources/HUDGitHubRelease.swift
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
mkdir -p "$OUT/module-cache"
xcrun swiftc -O -swift-version 5 -parse-as-library -sdk "$SDK" -module-cache-path "$OUT/module-cache" \
 "$ROOT/Sources/HUDGitHubRelease.swift" "$ROOT/windows/tools/package_update_reference.swift" -o "$OUT/reference"
"$OUT/reference" "$OUT/raw.json"
python3 -I - "$OUT" "$ROOT" <<'PY'
import hashlib,json,pathlib,sys
out,root=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2])
value=json.loads((out/'raw.json').read_text(encoding='utf-8'))
source=(root/'Sources/HUDGitHubRelease.swift').read_bytes()
value['provenance']={'commit':'ca04f142185c7de40acd8523bdb563195d90a1d1','source':'Sources/HUDGitHubRelease.swift',
  'sha256':hashlib.sha256(source).hexdigest(),'modifications':[],
  'isolation':'Foundation only; injected synchronous transport, fixed clock, synthetic GitHub metadata; no network'}
(out/'update-release-source.json').write_text(json.dumps(value,ensure_ascii=False,separators=(',',':'),sort_keys=True)+'\n',encoding='utf-8')
PY
echo "Wrote $OUT/update-release-source.json"
