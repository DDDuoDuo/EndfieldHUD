#!/usr/bin/env python3
"""Compile the pinned Mac catalog to immutable C++ literals; no UI/app data."""
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
from check_source_authority import verify

ROOT = Path(__file__).resolve().parents[2]

def generate():
    authority = verify(ROOT)
    source = (ROOT / 'Sources/LocalizationCatalog.swift').read_text()
    with tempfile.TemporaryDirectory(prefix='ehud-language-') as work:
        swift = Path(work) / 'main.swift'
        swift.write_text(source + '''
let result = LocalizationCatalog.entries.map { [$0.english, $0.simplifiedChinese, $0.traditionalChinese, $0.japanese, $0.korean] }
let data = try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys])
FileHandle.standardOutput.write(data)
''')
        args = ['swift']
        # Match the compiler bundled on this migration machine; callers with
        # another toolchain can select their SDK through SDKROOT normally.
        sdk = Path('/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk')
        if sdk.exists(): args += ['-sdk', str(sdk)]
        result = json.loads(subprocess.check_output(args + [str(swift)]))
    result.sort(key=lambda x: (x[0].encode(), x[1].encode()))
    keys = [(x[0], x[1]) for x in result]
    if len(set(keys)) != len(keys): raise ValueError('Duplicate source language key')
    def literal(s):
        # Fixed octal avoids C++ hex-escape consumption by adjacent digits.
        return '"' + ''.join('\\%03o' % ord(c) if ord(c) < 32 else '\\"' if c == '"' else '\\\\' if c == '\\' else c for c in s) + '"'
    text = '// Generated from authoritative macOS LocalizationCatalog.swift; do not edit translations here.\n'
    text += '// Commit: ' + authority['commit'] + '\n'
    text += '// SHA256: ' + hashlib.sha256(source.encode()).hexdigest() + '\n'
    text += '\n'.join('    {' + ','.join(map(literal, row)) + '},' for row in result) + '\n'
    target = ROOT / 'windows/core/localization_catalog.inc'
    target.write_text(text)
    print(f'Generated {len(result)} original five-language entries ({len(text.encode())} source bytes)')

if __name__ == '__main__': generate()
