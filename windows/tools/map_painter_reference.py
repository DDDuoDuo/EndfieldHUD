#!/usr/bin/env python3
"""Reproduce compact CoreGraphics Map samples using unchanged source files.

Only two synthetic binary archives embedded in the fixture are opened by the
source painter. No app, NSView/window, persistent store or user data is created.
Writes only the caller's build directory; never updates source or the fixture.
"""
from pathlib import Path
import hashlib
import json
import os
import shutil
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
output = Path(sys.argv[1]).resolve() if len(sys.argv) > 1 else root / 'build/map-painter-reference'
fixture_path = root / 'windows/tests/fixtures/map-painter-source.json'
fixture = json.loads(fixture_path.read_text())
for source, expected in fixture['sourcePins'].items():
    if hashlib.sha256((root / source).read_bytes()).hexdigest() != expected:
        raise SystemExit(f'Original source changed: {source}')
output.mkdir(parents=True, exist_ok=True)
for name in ('terrain', 'countries'):
    (output / (name + '.bin')).write_bytes(bytes.fromhex(fixture[name + 'Hex']))
shutil.copyfile(root / 'windows/tools/map_painter_reference.swift', output / 'main.swift')
env = os.environ.copy()
env.pop('SDKROOT', None)
sdk = subprocess.check_output(['bash', str(root / 'scripts/build.sh'), '--print-sdk'], env=env, text=True).strip()
cache = root / 'build/windows-module-reference/.compiler/module-cache'
subprocess.run(['xcrun', 'swiftc', '-sdk', sdk, '-module-cache-path', str(cache),
                *[str(root / source) for source in fixture['sourcePins']],
                str(output / 'main.swift'), '-o', str(output / 'oracle')], check=True, env=env)
subprocess.run([str(output / 'oracle'), str(output)], check=True)
actual = json.loads((output / 'output.json').read_text())
count = 0
def compare(left, right, path):
    global count
    if type(left) in (float, int) and type(right) in (float, int):
        if abs(left - right) > 1e-10:
            raise SystemExit(f'Original sample changed at {path}: {left} vs {right}')
        count += 1
    elif isinstance(left, dict) and isinstance(right, dict):
        if left.keys() != right.keys():
            raise SystemExit(f'Original sample fields changed at {path}')
        for key in left:
            compare(left[key], right[key], path + '/' + key)
    elif isinstance(left, list) and isinstance(right, list):
        if len(left) != len(right):
            raise SystemExit(f'Original sample count changed at {path}')
        for index, (a, b) in enumerate(zip(left, right)):
            compare(a, b, path + '/' + str(index))
    elif left != right:
        raise SystemExit(f'Original sample changed at {path}')
for key in actual:
    compare(actual[key], fixture[key], key)
report = {'authority': fixture['authority'], 'sourcePins': fixture['sourcePins'],
          'numericComparisons': count, 'differences': 0,
          'fixtureSHA256': hashlib.sha256(fixture_path.read_bytes()).hexdigest(),
          'scope': 'Original CoreGraphics sample pixels; Windows antialiasing parity remains unverified.'}
(output / 'comparison.json').write_text(json.dumps(report, indent=2) + '\n')
print(f'Original Map painter: {count} sample values match; no app or real data')
