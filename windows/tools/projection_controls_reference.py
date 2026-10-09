#!/usr/bin/env python3
"""Extract a bounded controls contract from original Swift source, without compiling it.

Writes JSON to stdout. This is a geometry/string source comparison, not a native
glyph-raster or screenshot oracle. No application, user data, or cache is opened.
"""
import hashlib
import json
from pathlib import Path
import re
import sys


def one(pattern, text):
    match = re.search(pattern, text, re.S)
    if not match:
        raise ValueError(f"Source contract changed: {pattern}")
    return match


def rect(text):
    return list(map(float, one(r'CGRect\(x: ([-\d.]+), y: ([-\d.]+), width: ([-\d.]+), height: ([-\d.]+)\)', text).groups()))


def title(text):
    value = one(r'title: (L10n\.text\("[^"]*", "[^"]*"\)|String\(Int\(model\.brushWidth\)\)|"[^"]*")', text)[1]
    if value.startswith('L10n'):
        return list(one(r'L10n\.text\("([^"]*)", "([^"]*)"\)', value).groups())
    if value.startswith('String'):
        return ['@brush.truncated', '@brush.truncated']
    return [value[1:-1]] * 2


def items(text):
    result = []
    for chunk in re.split(r'\bItem\(id: ', text)[1:]:
        item = {'id': one(r'^"([^"]+)"', chunk)[1], 'rect': rect(chunk), 'title': title(chunk)}
        ax = re.search(r'accessibilityTitle: L10n\.text\("([^"]*)", "([^"]*)"\)', chunk)
        item['label'] = list(ax.groups()) if ax else item['title']
        result.append(item)
    return result


def main(root):
    source_bytes = (root / 'Sources/ProjectionControls.swift').read_bytes()
    common_bytes = (root / 'Sources/NotesFormattingControls.swift').read_bytes()
    source = source_bytes.decode('utf-8')
    common = common_bytes.decode('utf-8')
    toolbar, confirmation, adjustment = re.split(r'final class Projection(?:Toolbar|ClearConfirmation|AdjustmentMenu): NotesRetainedMenu \{', source)[1:]
    toolbar_items = one(r'items = \[(.*?)\]; paint\(\)', toolbar)[1]
    confirmation_items = one(r'items = \[(.*?)\]\s*paint\(\)', confirmation)[1]
    rail = one(r'func rail.*?CGRect\(x: ([\d.]+), y: ([\d.]+) \+ CGFloat\(index\) \* ([\d.]+), width: ([\d.]+), height: ([\d.]+)\)', adjustment)
    question = one(r'text\(L10n\.text\("([^"]*)", "([^"]*)"\), rect: (CGRect\([^)]*\)), size: ([\d.]+)', confirmation)
    result = {
        'kind': 'original-swift-source-contract', 'nativeGlyphComparison': False,
        'sourcePins': {'Sources/ProjectionControls.swift': hashlib.sha256(source_bytes).hexdigest(),
                       'Sources/NotesFormattingControls.swift': hashlib.sha256(common_bytes).hexdigest()},
        'toolbar': {'bounds': [0, 0, *map(float, one(r'super\.init\(size: CGSize\(width: ([\d.]+), height: ([\d.]+)\)', toolbar).groups())],
                    'items': items(toolbar_items),
                    'surfaceRadius': float(one(r'surface\.cornerRadius = ([\d.]+)', toolbar)[1]),
                    'itemRadius': float(one(r'child\.cornerRadius = ([\d.]+)', toolbar)[1]),
                    'fontSize': float(one(r'systemFont\(ofSize: ([\d.]+)', toolbar)[1]),
                    'scale': float(one(r'let scale: CGFloat = ([\d.]+)', toolbar)[1])},
        'clear': {'bounds': [0, 0, *map(float, one(r'super\.init\(size: CGSize\(width: ([\d.]+), height: ([\d.]+)\)', confirmation).groups())],
                  'items': items(confirmation_items), 'question': list(question.groups()[:2]), 'questionRect': rect(question[3]), 'fontSize': float(question[4])},
        'adjustment': {'rail': list(map(float, rail.groups())),
                       'height': list(map(float, one(r'height: ([\d.]+) \+ CGFloat\(values.count\) \* ([\d.]+)', adjustment).groups())),
                       'close': rect(one(r'Item\(id: "close".*?\)\)', adjustment)[0]),
                       'stepY': float(one(r'let y = ([\d.]+) \+ CGFloat\(index\)', adjustment)[1]),
                       'labelY': float(one(r'rect: CGRect\(x: 10, y: ([\d.]+) \+ CGFloat\(index\)', adjustment)[1])},
        'palette': {'ink': float(one(r'var ink: NSColor.*?dark \? ([\d.]+)', common)[1]),
                    'backAlpha': float(one(r'back.backgroundColor = NSColor.black.withAlphaComponent\(([\d.]+)\)', common)[1]),
                    'face': list(map(float, one(r'face.backgroundColor = NSColor\(white: dark \? ([\d.]+) : [\d.]+, alpha: ([\d.]+)\)', common).groups())),
                    'borderWidth': float(one(r'face.borderWidth = ([\d.]+)', common)[1]),
                    'borderAlpha': float(one(r'face.borderColor = HUDRuntimeAppearance.accent.withAlphaComponent\(([\d.]+)\)', common)[1])}}
    print(json.dumps(result, ensure_ascii=False, separators=(',', ':')))


if __name__ == '__main__':
    if len(sys.argv) != 2:
        sys.exit('Usage: projection_controls_reference.py SOURCE_REPOSITORY')
    main(Path(sys.argv[1]).resolve())
