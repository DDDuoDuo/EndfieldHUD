#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 1 ]] || { echo 'Usage: archive_text_reference.sh [new-output-directory]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-archive-text-reference}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/archive-text-reference.json" ]] || { echo 'Refusing to replace existing text oracle' >&2; exit 1; }
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-archive-text.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
python3 - "$ROOT" "$OUTPUT" "$BUILD" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out,build=map(pathlib.Path,sys.argv[1:])
pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources'],cwd=root).returncode==0
source=(root/'Sources/ArchiveStore.swift').read_bytes()
block=source[source.index(b'struct ArchiveCategory:'):source.index(b'struct ArchiveEntry:')]
# Legacy's template type is an explicit two-case fixture shim; the complete
# original category declaration, including nameKey, remains byte-for-byte.
shim=b'import Foundation\nenum ArchiveTemplate { case journal, research }\n'
(build/'ArchiveCategory.swift').write_bytes(shim+block)
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourcePath':'Sources/ArchiveStore.swift','sourceSHA256':hashlib.sha256(source).hexdigest(),'sourceBlockSHA256':hashlib.sha256(block).hexdigest(),'canvasSHA256':hashlib.sha256((root/'Sources/ArchiveCanvas.swift').read_bytes()).hexdigest(),'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/archive_text_reference.swift','windows/tools/archive_text_reference.sh']},'windowsCreated':False,'input':'synthetic text; prohibited activation; temporary CFFIXED_USER_HOME'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" \
    -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" \
    -framework Cocoa "$BUILD/ArchiveCategory.swift" "$ROOT/windows/tools/archive_text_reference.swift" \
    -o "$BUILD/archive-text-reference"
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/archive-text-reference" "$OUTPUT/archive-text-reference.json"
