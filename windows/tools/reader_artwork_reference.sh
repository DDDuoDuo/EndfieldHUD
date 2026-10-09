#!/bin/bash
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin && $# -eq 1 ]] || { echo 'Usage: reader_artwork_reference.sh NEW-output-directory' >&2;exit 1; }
[[ ! -e "$1/reader-artwork-reference.json" ]] || { echo 'Refusing to overwrite source oracle' >&2;exit 1; }
mkdir -p "$1/.compiler"
OUT="$(cd "$1" && pwd)"
python3 - "$ROOT" "$OUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:]);pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0
shims={'ReaderController':'''\nextension ReaderController {
 func readerArtworkConfigure(kind:String,dark:Bool,loading:Bool,error:String?) {
  active=true;self.dark=dark;self.size=CGSize(width:376,height:334)
  self.loading=loading;self.error=error
  current=kind == "none" ? nil : readerArtworkPage(3,illustration:kind == "image")
  pages=current.map{[$0,readerArtworkPage(4,illustration:kind == "image"),readerArtworkPage(2,illustration:kind == "image")]} ?? []
 }
 func readerArtworkDrain(){queue.sync {};writer.sync {}}
}
''','ReaderInteraction':'''\nfunc readerArtworkList(choices:[(String,String)],title:String,allowsDelete:Bool,width:CGFloat,scroll:CGFloat=0,deletion:String?=nil)->NotesRetainedMenu {
 let menu=ReaderListMenu(choices:choices,title:title,allowsDelete:allowsDelete,width:width)
 if scroll != 0 {menu.scroll(delta:scroll)}
 if let deletion {menu.perform("delete:"+deletion)}
 return menu
}
func readerArtworkSettings(_ preferences:ReaderPreferences)->NotesRetainedMenu {ReaderSettingsMenu(preferences:preferences)}
'''}
for stem,shim in shims.items():
 b=(root/'Sources'/f'{stem}.swift').read_bytes();(out/'.compiler'/f'{stem}.swift').write_bytes(b+shim.encode())
 assert (out/'.compiler'/f'{stem}.swift').read_bytes()[:len(b)]==b
paths=['ReaderCanvas','ReaderController','ReaderInteraction','ReaderStore','ReaderDocument','ReaderEPUB','ReaderZIP','HUDControlHighlightLayer','NotesFormattingControls']
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{f'Sources/{p}.swift':hashlib.sha256((root/'Sources'/f'{p}.swift').read_bytes()).hexdigest() for p in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/reader_artwork_reference.swift','windows/tools/reader_artwork_reference.sh','windows/tools/module_reference_layers.swift']},'privateFixtureShimSHA256':{p:hashlib.sha256(s.encode()).hexdigest() for p,s in shims.items()},'isolation':'detached synthetic controller and source menus; temporary reference-only store; no activated application/windows/real book/providers'},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path;do SOURCES+=("$ROOT/$path");done < <(cd "$ROOT";rg --files Sources -g '*.swift' | sed '/\/main.swift$/d; /\/ReaderController.swift$/d; /\/ReaderInteraction.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library -module-name EndfieldReaderArtworkReference -D HUD_WATCH_MOTION_PREVIEW -sdk "$SDK" -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz "${SOURCES[@]}" "$OUT/.compiler/ReaderController.swift" "$OUT/.compiler/ReaderInteraction.swift" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/reader_artwork_reference.swift" -o "$OUT/.compiler/reader-artwork-reference" >"$OUT/.compiler/compile.log" 2>&1;then tail -80 "$OUT/.compiler/compile.log" >&2;exit 1;fi
FIXTURE="$(mktemp -d "${TMPDIR:-/tmp}/endfield-reader-artwork-home.XXXXXX")"
trap 'rm -rf "$FIXTURE"' EXIT
CFFIXED_USER_HOME="$FIXTURE" "$OUT/.compiler/reader-artwork-reference" "$OUT"
