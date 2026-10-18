#!/bin/bash
# Mac-only oracle: exports ClipboardPayload.matches(_:), the identity
# ClipboardStore.capture uses to move a repeated copy to the front instead of
# adding it (file items by standardizedFileURL, strings by Swift String
# equality), over fixed file URLs and strings to
# NEW_OUTPUT/clipboard_matches.json (copied to
# windows/tests/fixtures/clipboard_matches.json). ClipboardFileReferences and
# ClipboardPayload are compiled from an unchanged copy of their source text;
# only the `fileprivate` keyword is dropped so the reference can call them. No
# pasteboard, app, window, network or user data is touched.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: clipboard_matches_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/scripts/build.sh" --print-sdk)"
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:])
path=root/'Sources/ClipboardStore.swift';source=path.read_text()
start=source.index('fileprivate final class ClipboardFileReferences {')
end=source.index('/// A session-only, main-thread clipboard cache.',start)
block=source[start:end];assert source.count(block)==1
for line in ['        case (.files(let first), .files(let second)):',
             '            return first.urls.map(\\.standardizedFileURL) == second.urls.map(\\.standardizedFileURL)',
             '        case (.text(let first), .text(let second)): return first == second',
             '        case (.url(let first, let text), .url(let second, let otherText)): return first == second && text == otherText']:
    assert line in block,line
# ClipboardStore.capture moves a matching item to the front; read() takes the
# pasteboard's file URLs as given.
for line in ['            if let index = items.firstIndex(where: { $0.payload.matches(item.payload) }) {',
             '           let urls = pasteboard.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL], !urls.isEmpty {',
             '            return make(.files, preview: preview, payload: .files(ClipboardFileReferences(urls)))']:
    assert line in source,line
copy='import AppKit\n'+block.replace('fileprivate ','')
(out/'ClipboardPayloadReference.swift').write_text(copy)
(out/'provenance.json').write_text(json.dumps({'source':'Sources/ClipboardStore.swift','sourceSHA256':hashlib.sha256(path.read_bytes()).hexdigest(),
    'blockSHA256':hashlib.sha256(block.encode()).hexdigest(),'modification':'ClipboardFileReferences and ClipboardPayload: fileprivate visibility only'},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$OUTPUT/ClipboardPayloadReference.swift" "$ROOT/windows/tools/clipboard_matches_reference.swift" -o "$OUTPUT/clipboard-matches-reference" >"$OUTPUT/compile.log" 2>&1
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-clipboard-matches-oracle-') as home:
 subprocess.run([str(out/'clipboard-matches-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
provenance=json.loads((out/'provenance.json').read_text())
assert hashlib.sha256((root/provenance['source']).read_bytes()).hexdigest()==provenance['sourceSHA256'],'Original source changed'
PY
