#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -eq 1 ]] || { echo 'Usage: reader_reference.sh NEW-output-directory' >&2; exit 1; }
[[ ! -e "$1/reader-reference.json" ]] || { echo 'Refusing to overwrite oracle' >&2; exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:]);pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0,'Authoritative Mac source changed'
paths=['ReaderStore','ReaderDocument','ReaderEPUB','ReaderZIP','ReaderController','ReaderCanvas']
shims={'ReaderController':'''\nextension ReaderController {
    func readerReferenceConfigure(_ preferences:ReaderPreferences,bookID:UUID,illustration:Bool) {
        snapshot.preferences=preferences;snapshot.selected=bookID;active=true
        current=readerReferencePage(3,illustration);pages=[current!,readerReferencePage(4,illustration),readerReferencePage(2,illustration)]
    }
    func readerReferenceDrain() {queue.sync {};writer.sync {}}
}
''','ReaderCanvas':'''\nextension ReaderCanvas {
    func readerReferenceFacts()->[String:Any] {
        ["page":controller.current?.location.section ?? -1,"zoom":Double(imageView.zoom),"pan":[Double(imageView.pan.x),Double(imageView.pan.y)],"offset":Double(offset),"pictures":pictures.map{box($0.frame)},"hidden":pictures.map{$0.isHidden},"progressRect":box(progressRect),"turnSequence":pageTurnSequence]
    }
}
'''}
for stem in paths:
    b=(root/'Sources'/f'{stem}.swift').read_bytes();suffix=shims.get(stem,'').encode();(out/'.compiler'/f'{stem}.swift').write_bytes(b+suffix)
    assert (out/'.compiler'/f'{stem}.swift').read_bytes()[:len(b)]==b
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{f'Sources/{s}.swift':hashlib.sha256((root/'Sources'/f'{s}.swift').read_bytes()).hexdigest() for s in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/reader_reference.swift','windows/tools/reader_reference.sh']},'privateAccessShimSHA256':{s:hashlib.sha256(x.encode()).hexdigest() for s,x in shims.items()},'scope':'Original ReaderCanvas/Store state and geometry; detached synthetic controller; dependency shims explicit'},sort_keys=True,indent=2)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
CACHE="$ROOT/build/windows-module-reference/.compiler/module-cache"
mkdir -p "$CACHE"
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldReaderReference -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework PDFKit -lz "$OUT/.compiler/ReaderStore.swift" "$OUT/.compiler/ReaderDocument.swift" "$OUT/.compiler/ReaderEPUB.swift" "$OUT/.compiler/ReaderZIP.swift" "$OUT/.compiler/ReaderController.swift" "$OUT/.compiler/ReaderCanvas.swift" "$ROOT/windows/tools/reader_reference.swift" -o "$OUT/.compiler/reader-reference" >"$OUT/.compiler/compile.log" 2>&1;then tail -80 "$OUT/.compiler/compile.log" >&2;exit 1;fi
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/endfield-reader-home.XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT
CFFIXED_USER_HOME="$FIXTURE" "$OUT/.compiler/reader-reference" --output "$OUT"
