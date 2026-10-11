#!/bin/bash
set -euo pipefail
# Build-only reference for pasting foreign rich text into a Notes text field:
# unchanged Sources/NotesRichText.swift plus the HUD's own editor setup and
# finish capture statements (extracted byte for byte), driven through a
# detached NSTextView and private uniquely named pasteboards. No window, no
# general clipboard, no user store. Writes the fixture JSON to stdout's file.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -eq 1 ]] || { echo 'Usage: notes_paste_reference.sh <new-output.json>' >&2; exit 1; }
OUTPUT="$1"
[[ ! -e "$OUTPUT" ]] || { echo 'Refusing to overwrite an existing oracle' >&2; exit 1; }
WORK="$(mktemp -d "${TMPDIR:-/tmp}/endfield-notes-paste.XXXXXX")"
trap 'rm -rf "$WORK"' EXIT
mkdir -p "$WORK/home" "$WORK/tmp" "$WORK/cache"
python3 -I - "$ROOT" "$WORK" <<'PY'
import pathlib,sys
root,work=map(pathlib.Path,sys.argv[1:])
def block(text,marker):
    start=text.index(marker); at=text.index('{',start); depth=0; quoted=False; escaped=False
    for n in range(at,len(text)):
        c=text[n]
        if quoted:
            if escaped: escaped=False
            elif c=='\\': escaped=True
            elif c=='"': quoted=False
            continue
        if c=='"': quoted=True
        elif c=='{': depth+=1
        elif c=='}':
            depth-=1
            if depth==0:return text[start:n+1]
    raise ValueError('Unclosed source block: '+marker)
canvas=(root/'Sources/NotesCanvas.swift').read_text()
interaction=(root/'Sources/HUDNotesInteraction.swift').read_text()
begin=block(interaction,'private func beginEditing(_ next: NotesEditRequest) {')
setup='        let initialFont ='+begin.split('        let initialFont =',1)[1].split('        text.delegate = self',1)[0]
finish=block(interaction,'func finishEditing() {')
capture='        var rich: NotesRichText?'+finish.split('        var rich: NotesRichText?',1)[1].split('        editor.delegate = nil',1)[0]
assert 'text.typingAttributes = [.font: initialFont' in setup and 'NotesRichText.capture(storage' in capture
generated='import AppKit\n\n'+'\n\n'.join([block(canvas,'enum NotesTextMetrics {'),block(canvas,'struct NotesEditRequest {'),block(canvas,'enum NotesCoordinateSpace {')])
generated+='''
// The HUD's text note editor (HUDNotesInteraction.beginEditing) for a new,
// empty text note on the dark theme, without its window/host wiring.
private let request = NotesEditRequest(noteID: UUID(uuidString: "00000000-0000-4000-8000-0000000000AA")!, itemID: nil, text: "",
    rect: CGRect(x: 0, y: 0, width: 240, height: 120), fontSize: 12, multiline: true)
func prepare() -> FixtureEditor {
    let next = request
    let text = FixtureEditor(frame: CGRect(origin: .zero, size: next.rect.size))
    text.isRichText = next.itemID == nil; text.importsGraphics = false; text.allowsUndo = true
    text.isAutomaticQuoteSubstitutionEnabled = false; text.isAutomaticDashSubstitutionEnabled = false
    let isDark: (() -> Bool)? = { true }; let scale: CGFloat = 1; let rect = next.rect
'''+setup+'''
    return text
}
// HUDNotesInteraction.finishEditing: the stored NotesRichText (nil = plain).
func finish(_ editor: FixtureEditor) -> NotesRichText? {
    let isDark: (() -> Bool)? = { true }; let scale: CGFloat = 1
'''+capture+'''
    return rich
}
'''
generated+='\n'+(root/'windows/tools/notes_paste_reference.swift').read_text()
(work/'fixture.swift').write_text(generated)
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
xcrun swiftc -swift-version 5 -O -parse-as-library -module-name EndfieldNotesPasteReference -sdk "$SDK" -module-cache-path "$WORK/cache" \
    -framework Cocoa "$ROOT/Sources/NotesRichText.swift" "$WORK/fixture.swift" -o "$WORK/reference" >"$WORK/compile.log" 2>&1 || { tail -60 "$WORK/compile.log" >&2; exit 1; }
CFFIXED_USER_HOME="$WORK/home" TMPDIR="$WORK/tmp/" "$WORK/reference" >"$WORK/out.json"
python3 -I - "$WORK/out.json" "$OUTPUT" "$ROOT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
out=json.loads(pathlib.Path(sys.argv[1]).read_text());root=pathlib.Path(sys.argv[3])
assert out['readablePasteboardTypes'][0]=='NeXT Rich Text Format v1.0 pasteboard type' and 'Apple HTML pasteboard type' in out['readablePasteboardTypes']
for c in out['cases']:
    n=len(c['text'].encode('utf-16-le'))//2;assert n==c['utf16Length']
    end=0
    for r in c['captured']['runs']:assert r['location']==end and r['length']>0;end+=r['length']
    assert end==n,c['name']
out['provenance']={'sources':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['Sources/NotesRichText.swift','Sources/NotesCanvas.swift','Sources/HUDNotesInteraction.swift']},
    'tool':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/notes_paste_reference.sh','windows/tools/notes_paste_reference.swift']},
    'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True).strip().splitlines()[0]}
pathlib.Path(sys.argv[2]).write_text(json.dumps(out,indent=1,sort_keys=True,ensure_ascii=False)+'\n')
print('Recorded',len(out['cases']),'paste cases')
PY
