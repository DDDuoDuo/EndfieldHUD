#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 ]] || { echo 'Usage: reader_format_reference.sh NEW-output-directory (macOS)' >&2;exit 1; }
[[ ! -e "$1/reader-format-reference.json" ]] || { echo 'Refusing to overwrite oracle' >&2;exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:]);pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0
shim='''
func readerXMLReference(_ data:Data) throws->[String:Any] {
  let parsed=try ReaderXML.parse(data)
  return ["rootFile":parsed.rootFile as Any? ?? NSNull(),"title":parsed.title,
   "manifest":parsed.manifest.mapValues{["href":$0.href,"type":$0.type]},"spine":parsed.spine,
   "blocks":parsed.blocks.map{block->[String:Any] in switch block {case .text(let t):return ["text":t as String];case .image(let p):return ["image":p]}}]
}
'''
documentShim='''
extension ReaderDocument {
 func readerFormatTextFacts()->[String:Any] {
  ["text":text.map{$0 as String} as Any? ?? NSNull(),"title":title,"sections":sectionCount]
 }
}
'''
for stem in ['ReaderZIP','ReaderEPUB','ReaderStore','ReaderDocument']:
 b=(root/'Sources'/f'{stem}.swift').read_bytes();suffix=shim.encode() if stem=='ReaderEPUB' else documentShim.encode() if stem=='ReaderDocument' else b'';(out/'.compiler'/f'{stem}.swift').write_bytes(b+suffix)
 assert (out/'.compiler'/f'{stem}.swift').read_bytes()[:len(b)]==b
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{f'Sources/{s}.swift':hashlib.sha256((root/'Sources'/f'{s}.swift').read_bytes()).hexdigest() for s in ['ReaderZIP','ReaderEPUB','ReaderStore','ReaderDocument']},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/reader_format_reference.swift','windows/tools/reader_format_reference.sh']},'privateAccessShimSHA256':hashlib.sha256((shim+documentShim).encode()).hexdigest(),'scope':'Unchanged original XML parser and TXT document initialization; detached temporary synthetic files only; no windows, application activation, network resolver or user stores'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
CACHE="$ROOT/build/windows-module-reference/.compiler/module-cache"
xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldReaderFormatsReference -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework PDFKit -lz "$OUT/.compiler/ReaderStore.swift" "$OUT/.compiler/ReaderDocument.swift" "$OUT/.compiler/ReaderZIP.swift" "$OUT/.compiler/ReaderEPUB.swift" "$ROOT/windows/tools/reader_format_reference.swift" -o "$OUT/.compiler/reader-format-reference" >"$OUT/.compiler/compile.log" 2>&1
"$OUT/.compiler/reader-format-reference" "$OUT"
