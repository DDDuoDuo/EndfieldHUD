#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 && ! -e "$1/reader_gb18030_table.hpp" ]] || { echo 'Usage: reader_gb18030_reference.sh NEW-output-directory (macOS)' >&2;exit 1; }
mkdir -p "$1"
OUT="$(cd "$1" && pwd)"
PIN=ca04f142185c7de40acd8523bdb563195d90a1d1
git -C "$ROOT" diff --quiet "$PIN" -- Sources/ReaderDocument.swift
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -O -sdk "$SDK" -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" "$ROOT/windows/tools/reader_gb18030_reference.swift" -o "$OUT/reader-gb18030-reference"
"$OUT/reader-gb18030-reference" "$OUT/reader_gb18030_table.hpp"
python3 - "$ROOT" "$OUT" "$PIN" <<'PY'
import hashlib,json,pathlib,sys
root,out,pin=pathlib.Path(sys.argv[1]),pathlib.Path(sys.argv[2]),sys.argv[3]
paths=['Sources/ReaderDocument.swift','windows/tools/reader_gb18030_reference.swift','windows/tools/reader_gb18030_reference.sh']
value={'sourceAuthority':pin,'sourceSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'tableSHA256':hashlib.sha256((out/'reader_gb18030_table.hpp').read_bytes()).hexdigest(),'scope':'Mac Foundation API output for the exact encoding selected at ReaderDocument.swift59; exhaustive single/pair/four-byte scalar queries; no books/user files/windows/app activation. This is a codec API oracle, not a full ReaderDocument algorithm oracle.'}
(out/'provenance.json').write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
PY
