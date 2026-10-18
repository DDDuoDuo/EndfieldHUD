#!/bin/bash
# Mac oracle for the Windows PDF title reader (windows/modules/reader_pdf_title.hpp):
# writes synthetic PDFs (tools/reader_pdf_title_cases.py), opens each with the
# UNCHANGED Sources/ReaderDocument.swift (PDFKit) and records the library title
# it chooses. Detached temporary files only; no window, user store or network.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 ]] || { echo 'Usage: reader_pdf_title_reference.sh NEW-output-directory (macOS)' >&2; exit 1; }
[[ ! -e "$1/reader-pdf-title-source.json" ]] || { echo 'Refusing to overwrite oracle' >&2; exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
python3 -I "$ROOT/windows/tools/reader_pdf_title_cases.py" "$OUT/.compiler/cases"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib, json, pathlib, subprocess, sys
root, out = map(pathlib.Path, sys.argv[1:])
pin = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
sources = ['ReaderZIP', 'ReaderEPUB', 'ReaderStore', 'ReaderDocument']
assert subprocess.run(['git', '-C', str(root), 'diff', '--quiet', pin, '--'] + [f'Sources/{s}.swift' for s in sources]).returncode == 0
for stem in sources:
    (out / '.compiler' / f'{stem}.swift').write_bytes((root / 'Sources' / f'{stem}.swift').read_bytes())
tools = ['windows/tools/reader_pdf_title_reference.swift', 'windows/tools/reader_pdf_title_reference.sh', 'windows/tools/reader_pdf_title_cases.py']
(out / 'provenance.json').write_text(json.dumps({
    'sourceAuthority': pin,
    'sourceSHA256': {f'Sources/{s}.swift': hashlib.sha256((root / 'Sources' / f'{s}.swift').read_bytes()).hexdigest() for s in sources},
    'exporterSHA256': {p: hashlib.sha256((root / p).read_bytes()).hexdigest() for p in tools},
    'scope': 'Unchanged ReaderDocument PDF title (PDFKit documentAttributes title, else the file name) of synthetic one-page PDFs'},
    indent=2, sort_keys=True) + '\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk 2>/dev/null | tail -1)"
CACHE="$ROOT/build/windows-module-reference/.compiler/module-cache"
xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldReaderPDFTitleReference -sdk "$SDK" -module-cache-path "$CACHE" \
  -framework Cocoa -framework PDFKit -lz "$OUT/.compiler/ReaderStore.swift" "$OUT/.compiler/ReaderDocument.swift" "$OUT/.compiler/ReaderZIP.swift" "$OUT/.compiler/ReaderEPUB.swift" \
  "$ROOT/windows/tools/reader_pdf_title_reference.swift" -o "$OUT/.compiler/reader-pdf-title-reference" >"$OUT/.compiler/compile.log" 2>&1 || { cat "$OUT/.compiler/compile.log" >&2; exit 1; }
"$OUT/.compiler/reader-pdf-title-reference" "$OUT/.compiler/cases" "$OUT"
