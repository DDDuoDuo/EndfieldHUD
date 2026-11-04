#!/bin/bash
# Mac-only oracle: exports ClipboardStore.parsedURL(_:explicit:) results and
# the ClipboardStore.read string-path link detection for fixed strings to
# NEW_OUTPUT/clipboard_urls.json (copied to windows/tests/fixtures/clipboard_urls.json).
# parsedURL is compiled from an unchanged copy of its source text; only the
# `private` keyword is dropped so the reference can call it. No pasteboard,
# app, window, network or user data is touched.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
[[ $# -eq 2 && "$(uname -s)" == Darwin ]] || { echo 'Usage: clipboard_url_reference.sh NEW_OUTPUT EXISTING_MODULE_CACHE' >&2; exit 1; }
OUTPUT="$1"; CACHE="$2"
[[ ! -e "$OUTPUT" && ! -L "$OUTPUT" && -d "$CACHE" ]]
mkdir -p "$OUTPUT";OUTPUT="$(cd "$OUTPUT" && pwd)";CACHE="$(cd "$CACHE" && pwd)"
SDK="$(bash "$ROOT/windows/tools/mac_sdk.sh")"
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,pathlib,sys
root,out=map(pathlib.Path,sys.argv[1:])
path=root/'Sources/ClipboardStore.swift';source=path.read_text()
start=source.index('    private static func parsedURL(_ text: String, explicit: Bool) -> URL? {')
end=source.index('\n    }\n',start)+len('\n    }\n')
function=source[start:end];assert source.count(function)==1
# The string-path rules the fixture also records (ClipboardStore.read).
for line in ['        if let explicit = pasteboard.string(forType: .URL), let url = Self.parsedURL(explicit, explicit: true) {',
             '            let original = pasteboard.string(forType: .string) ?? explicit',
             '        if text.utf8.count <= 2048, let url = Self.parsedURL(text, explicit: false) {',
             '            return make(.url, preview: text, payload: .url(url, text))',
             'guard writer.setString(url.absoluteString, forType: .URL), writer.setString(original, forType: .string) else { return false }']:
    assert line in source,line
copy='import Foundation\nenum ClipboardStoreReference {\n'+function.replace('    private static func parsedURL','    static func parsedURL',1)+'}\n'
(out/'ClipboardStoreReference.swift').write_text(copy)
(out/'provenance.json').write_text(json.dumps({'source':'Sources/ClipboardStore.swift','sourceSHA256':hashlib.sha256(path.read_bytes()).hexdigest(),
    'functionSHA256':hashlib.sha256(function.encode()).hexdigest(),'modification':'parsedURL(_:explicit:) visibility only'},indent=2)+'\n')
PY
xcrun swiftc -swift-version 5 -O -parse-as-library -sdk "$SDK" -module-cache-path "$CACHE" \
 "$OUTPUT/ClipboardStoreReference.swift" "$ROOT/windows/tools/clipboard_url_reference.swift" -o "$OUTPUT/clipboard-url-reference" >"$OUTPUT/compile.log" 2>&1
python3 -I - "$ROOT" "$OUTPUT" <<'PY'
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root,out=map(pathlib.Path,sys.argv[1:])
with tempfile.TemporaryDirectory(prefix='ehud-clipboard-url-oracle-') as home:
 subprocess.run([str(out/'clipboard-url-reference'),str(out)],env=dict(os.environ,CFFIXED_USER_HOME=home),check=True)
provenance=json.loads((out/'provenance.json').read_text())
assert hashlib.sha256((root/provenance['source']).read_bytes()).hexdigest()==provenance['sourceSHA256'],'Original source changed'
PY
