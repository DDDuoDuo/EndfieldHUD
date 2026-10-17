#!/bin/bash
set -euo pipefail
# Untouched Mac Archive with isolated store/executors. A byte-for-byte compiler
# copy exposes private menus only; no copied expected drawing algorithm.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 1 ]] || { echo 'Usage: archive_reference.sh [new-output-directory]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/windows-archive-reference}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/archive-reference.json" ]] || { echo 'Refusing to overwrite an existing oracle' >&2; exit 1; }
BUILD="$OUTPUT/.compiler"
mkdir -p "$BUILD" "$ROOT/build/windows-module-reference/.compiler/module-cache"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-archive-reference.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
cd "$ROOT"
python3 - "$ROOT" "$OUTPUT" "$BUILD" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out,build=map(pathlib.Path,sys.argv[1:])
pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0,'Authoritative Mac source changed'
paths=['Sources/ArchiveCanvas.swift','Sources/ArchiveStore.swift','Sources/HUDArchiveInteraction.swift','Sources/NotesRichText.swift','Sources/NotesMedia.swift','Sources/NotesRetainedMenu.swift','Sources/HUDControlHighlightLayer.swift','Sources/EndfieldGameIcon.swift']
paths=[p for p in paths if (root/p).exists()]
tools=['windows/tools/archive_reference.swift','windows/tools/archive_reference.sh','windows/tools/module_reference_layers.swift']
data=(root/'Sources/HUDArchiveInteraction.swift').read_bytes()
shim=b'''\n// Build-only access to original private classes; original prefix unchanged.\nfunc endfieldArchiveReferenceCategoryMenu(_ categories: [ArchiveCategory], dark: Bool) -> NotesRetainedMenu { ArchiveCategoryMenu(categories: categories, dark: dark) }\nfunc endfieldArchiveReferenceChoiceMenu(category: Bool, dark: Bool) -> NotesRetainedMenu { ArchiveChoiceMenu(title: category ? L10n.text("Remove this category?", "\xe5\x88\xa0\xe9\x99\xa4\xe6\xad\xa4\xe5\x88\x86\xe7\xb1\xbb\xef\xbc\x9f") : L10n.text("Delete this document?", "\xe5\x88\xa0\xe9\x99\xa4\xe6\xad\xa4\xe6\xa1\xa3\xe6\xa1\x88\xef\xbc\x9f"), detail: category ? L10n.text("Documents will move to Uncategorized.", "\xe6\xa1\xa3\xe6\xa1\x88\xe5\xb0\x86\xe7\xa7\xbb\xe8\x87\xb3\xe6\x9c\xaa\xe5\x88\x86\xe7\xb1\xbb\xe3\x80\x82") : nil, choices: [("cancel", L10n.text("Cancel", "\xe5\x8f\x96\xe6\xb6\x88")), ("delete", L10n.text("Delete", "\xe5\x88\xa0\xe9\x99\xa4"))], dark: dark) }\nfunc endfieldArchiveReferenceScroll(_ menu: NotesRetainedMenu, delta: CGFloat) { (menu as? ArchiveCategoryMenu)?.scroll(delta: delta) }\nfunc endfieldArchiveReferenceScrollOffset(_ menu: NotesRetainedMenu) -> CGFloat { (menu as? ArchiveCategoryMenu)?.scrollOffset ?? 0 }\n'''
(build/'HUDArchiveInteraction.swift').write_bytes(data+shim)
assert (build/'HUDArchiveInteraction.swift').read_bytes()[:len(data)]==data
(out/'provenance.json').write_text(json.dumps({'sourceAuthority':pin,'sourceSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'exporterSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in tools},'privateAccessShimSHA256':hashlib.sha256(shim).hexdigest(),'input':'synthetic fixture SQLite only; temporary CFFIXED_USER_HOME','windowsCreated':False,'mediaOpened':False},indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
SOURCES=()
while IFS= read -r path; do
    if [[ "$path" == Sources/HUDArchiveInteraction.swift ]]; then SOURCES+=("$BUILD/HUDArchiveInteraction.swift"); else SOURCES+=("$ROOT/$path"); fi
done < <(rg --files Sources -g '*.swift' | sed '/\/main.swift$/d' | LC_ALL=C sort)
if ! xcrun swiftc -swift-version 5 -Onone -whole-module-optimization -parse-as-library \
    -module-name EndfieldArchiveReference -D HUD_WATCH_MOTION_PREVIEW \
    -sdk "$SDK" -module-cache-path "$ROOT/build/windows-module-reference/.compiler/module-cache" \
    -framework Cocoa -framework IOKit -framework CoreAudio -framework Quartz \
    -framework Carbon -framework ServiceManagement -framework Metal -framework MetalKit \
    -framework WebKit -framework Security -framework PDFKit -framework JavaScriptCore -lsqlite3 -lz \
    "${SOURCES[@]}" "$ROOT/windows/tools/module_reference_layers.swift" "$ROOT/windows/tools/archive_reference.swift" \
    -o "$BUILD/archive-reference" >"$BUILD/compile.log" 2>&1; then
    tail -100 "$BUILD/compile.log" >&2; exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" "$BUILD/archive-reference" --output "$OUTPUT"
