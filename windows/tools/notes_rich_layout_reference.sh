#!/bin/bash
set -euo pipefail
# Build-only detached TextKit/CoreText reference. No visible window/activation,
# user store, native cursor, clipboard, input context or screenshot capture.
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ "$(uname -s)" == Darwin ]] || { echo 'macOS required' >&2; exit 1; }
[[ $# -le 1 ]] || { echo 'Usage: notes_rich_layout_reference.sh [new-output-directory]' >&2; exit 1; }
OUTPUT="${1:-$ROOT/build/notes-rich-layout-reference}"
mkdir -p "$OUTPUT"
OUTPUT="$(cd "$OUTPUT" && pwd)"
[[ ! -e "$OUTPUT/rich-layout.json" ]] || { echo 'Refusing to overwrite an existing oracle' >&2; exit 1; }
mkdir -p "$OUTPUT/.compiler"
CACHE="$ROOT/build/windows-shell-packet-live-closure/.compiler/module-cache"
mkdir -p "$CACHE"
FIXTURE_ROOT="$(mktemp -d "${TMPDIR:-/tmp}/endfield-rich-layout.XXXXXX")"
trap 'rm -rf "$FIXTURE_ROOT"' EXIT
mkdir -p "$FIXTURE_ROOT/tmp"
cd "$ROOT"
python3 - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,subprocess,sys
root,out=map(pathlib.Path,sys.argv[1:])
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
formatting=(root/'Sources/NotesFormattingControls.swift').read_text()
canvas=(root/'Sources/NotesCanvas.swift').read_text()
interaction=(root/'Sources/HUDNotesInteraction.swift').read_text()
projected=(root/'Sources/HUDProjectedTextEditor.swift').read_text()
blocks={
 'NotesFormatChange':block(formatting,'enum NotesFormatChange {'),
 'NotesFormattingEditor':block(formatting,'enum NotesFormattingEditor {'),
 'NotesTextMetrics':block(canvas,'enum NotesTextMetrics {'),
 'NotesWrappedText':block(canvas,'private final class NotesWrappedText {'),
 'NotesEditRequest':block(canvas,'struct NotesEditRequest {'),
 'NotesCoordinateSpace':block(canvas,'enum NotesCoordinateSpace {'),
}
begin=block(interaction,'private func beginEditing(_ next: NotesEditRequest) {')
blocks['editorSetup']=begin.split('        let initialFont =',1)[1].split('        text.delegate = self',1)[0]
blocks['editorSetup']='        let initialFont ='+blocks['editorSetup']
apply=block(interaction,'func applyFormat(_ change: NotesFormatChange) {')
blocks['firstFormat']=apply.split('        let selected = editor.selectedRange()',1)[1].split('        NotesFormattingEditor.apply(change,',1)[0]
blocks['firstFormat']='        let selected = editor.selectedRange()'+blocks['firstFormat']
finish=block(interaction,'func finishEditing() {')
blocks['finishCapture']='        var rich: NotesRichText?'+finish.split('        var rich: NotesRichText?',1)[1].split('        editor.delegate = nil',1)[0]
resize=block(projected,'func resizeDocument() {')
blocks['resizeDocument']=resize[resize.index('{')+1:-1]
assert 'paragraph.minimumLineHeight = NotesTextMetrics.lineHeight(fontSize: next.fontSize) * scale' in blocks['editorSetup']
assert 'paragraph.lineSpacing = editorScale' in blocks['firstFormat']
assert 'undo?.disableUndoRegistration()' in blocks['firstFormat']
assert 'rich = plain && request.richText == nil ? nil : captured' in blocks['finishCapture']
generated='import AppKit\nimport CoreText\nimport QuartzCore\n\n'+ '\n\n'.join(blocks[k] for k in ('NotesFormatChange','NotesFormattingEditor','NotesTextMetrics','NotesWrappedText','NotesEditRequest','NotesCoordinateSpace'))
generated+='''
// Fixture-only facade: execute original private helper statements without its
// NSWindow guard, canvas callbacks, projected raster/capture or action wiring.
private enum FixtureSource {
    static func prepare(_ next: NotesEditRequest, dark: Bool) -> FixtureEditor {
        let text = FixtureEditor(frame: CGRect(origin: .zero, size: next.rect.size))
        text.isRichText = true; text.importsGraphics = false; text.allowsUndo = true
        text.isAutomaticQuoteSubstitutionEnabled = false; text.isAutomaticDashSubstitutionEnabled = false
        let isDark: (() -> Bool)? = { dark }; let scale: CGFloat = 1; let rect = next.rect
'''+blocks['editorSetup']+'''
        text.richEnabled = next.richText != nil
        return text
    }
    static func apply(_ change: NotesFormatChange, to editor: FixtureEditor) {
        guard let storage = editor.textStorage else { preconditionFailure("Missing TextKit storage") }
        var editorHasFormatting = editor.richEnabled; let editorScale: CGFloat = 1
        func resizeEditorDocument() {} // source callback only; measured below
'''+blocks['firstFormat']+'''
        editor.richEnabled = editorHasFormatting
        NotesFormattingEditor.apply(change, to: editor, scale: editorScale)
    }
    static func finish(_ request: NotesEditRequest, editor: FixtureEditor, dark: Bool) -> NotesRichText? {
        let isDark: (() -> Bool)? = { dark }; let scale: CGFloat = 1
'''+blocks['finishCapture']+'''
        return rich
    }
    static func resize(_ textView: FixtureEditor, logicalRect: CGRect, singleLine: Bool = false) {
        func invalidateArtwork() {} // intentionally no bitmap/async capture
'''+blocks['resizeDocument']+'''
    }
}
'''
generated+='\n'+(root/'windows/tools/notes_rich_layout_reference.swift').read_text()
(out/'.compiler/source-fixture.swift').write_text(generated)
pin='ca04f142185c7de40acd8523bdb563195d90a1d1'
sources=['Sources/NotesRichText.swift','Sources/NotesFormattingControls.swift','Sources/NotesCanvas.swift','Sources/HUDNotesInteraction.swift','Sources/HUDProjectedTextEditor.swift']
assert subprocess.run(['git','diff','--quiet',pin,'--','Sources','Resources']).returncode==0,'Authoritative Mac source changed'
value={'sourceBaseline':pin,'sourcesUnmodified':True,'sourceSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in sources},
 'toolSHA256':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in ['windows/tools/notes_rich_layout_reference.sh','windows/tools/notes_rich_layout_reference.swift']},
 'compiler':subprocess.check_output(['xcrun','swiftc','--version'],text=True).strip(),
 'compiledSource':'unchanged NotesRichText.swift plus byte-for-byte source helper extraction in one generated fixture file',
 'generatedFixtureSHA256':hashlib.sha256(generated.encode()).hexdigest(),
 'extractedBlocks':{name:{'sha256':hashlib.sha256(body.encode()).hexdigest(),'source':body} for name,body in blocks.items()},
 'fixtureOverrides':['detached NSTextView instead of private HUDNoteTextView host','caller-owned UndoManager; explicit grouping','resizeEditorDocument/invalidateArtwork host callbacks are no-ops','no NSWindow guard or projected capture executed']}
(out/'provenance.json').write_text(json.dumps(value,indent=2,sort_keys=True)+'\n')
PY
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
if ! xcrun swiftc -swift-version 5 -O -parse-as-library -module-name EndfieldNotesRichLayoutReference \
    -sdk "$SDK" -module-cache-path "$CACHE" -framework Cocoa -framework CoreText -framework QuartzCore \
    "$ROOT/Sources/NotesRichText.swift" "$OUTPUT/.compiler/source-fixture.swift" \
    -o "$OUTPUT/.compiler/reference" >"$OUTPUT/.compiler/compile.log" 2>&1; then
    tail -80 "$OUTPUT/.compiler/compile.log" >&2; exit 1
fi
CFFIXED_USER_HOME="$FIXTURE_ROOT" TMPDIR="$FIXTURE_ROOT/tmp/" "$OUTPUT/.compiler/reference" --ui-test --output "$OUTPUT"
python3 - "$OUTPUT" <<'PY'
import json,math,pathlib,sys
out=pathlib.Path(sys.argv[1]);file=out/'rich-layout.json';assert file.stat().st_size<2*1024*1024
r=json.loads(file.read_text());assert r['isolation']['visibleWindowCreated'] is False and r['isolation']['editorWindowCreated'] is False
assert all(w['class']=='TUINSWindow' and not any(w[k] for k in ('visible','key','main')) for w in r['isolation']['internalHelperWindows'])
rows={c['name']:c for c in r['cases']};assert len(rows)==len(r['cases']) and len(rows)>=18
before,after=rows['before-first-format'],rows['after-first-format']
assert before['defaultParagraph']['minimumLineHeight']>0 and before['defaultParagraph']['maximumLineHeight']>0
assert after['defaultParagraph']['minimumLineHeight']==0 and after['defaultParagraph']['maximumLineHeight']==0 and after['defaultParagraph']['lineSpacing']==1
assert before['text']==after['text'] and before['selectedRange']==after['selectedRange']
assert any(v['style']['fontSize']==48 for v in after['captured']['runs'])
assert rows['collapsed-before']['captured']==rows['collapsed-after']['captured']
assert rows['collapsed-after']['defaultParagraph']['lineSpacing']==1
assert rows['collapsed-after']['typingStyle']['bold'] and rows['collapsed-after']['typingStyle']['fontSize']==48
assert rows['undo-before']['captured']==rows['undo-restored']['captured']
assert rows['undo-formatted']['captured']==rows['redo-restored']['captured']
assert rows['explicit-white']['captured']['runs'][0]['style']['color']=={'red':1,'green':1,'blue':1,'alpha':1}
assert 'color' not in rows['theme-white']['captured']['runs'][0]['style']
assert r['colorIdentity']['whiteEqualsExplicitSRGB'] is False and r['colorIdentity']['blackEqualsExplicitSRGB'] is False
for c in rows.values():
    length=len(c['text'].encode('utf-16-le'))//2;assert length==c['utf16Length'] and length<=1024
    assert c['selectedRange'][0]>=0 and sum(c['selectedRange'])<=length
    for run in c['captured']['runs']:assert 0<=run['location']<length and 0<run['length']<=length-run['location']
    for line in c['settledLines']:assert 0<=line['utf16Range'][0]<=length and sum(line['utf16Range'])<=length and line['height']>0
    for line in c['textKit']['lines']:
        for p in line['insertionPoints']:assert 0<=p['characterIndex']<=length and math.isfinite(p['x'])
print('Validated source rich formatting, undo, paragraph and bounded native layout facts for',len(rows),'cases')
PY
