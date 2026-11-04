#!/usr/bin/env python3
"""Export the Mac 1.3.0 Reader errors and captions in five languages.

Reads the unchanged Mac sources at the pinned authority (Mac 1.3.0):
  * Sources/ReaderZIP.swift                  ReaderError.errorDescription pairs
  * Sources/ReaderCanvas.swift               every pair the redesigned E-Reader
                                             shows: L10n.text (tabs, buttons, menus,
                                             confirm bar, settings pop-over,
                                             accessibility labels), the result
                                             toasts and the module title
  * Sources/LocalizationCatalog.swift        the core Entry rows and Korean map
  * Sources/LocalizationCatalogRedesign.swift the redesign R(...) rows
and writes, for every pair, the text Mac L10n.text renders in English,
Simplified Chinese, Traditional Chinese, Japanese and Korean: the first
catalog Entry of the pair (core rows first, then the redesign rows, as
LocalizationCatalog.entries), the Korean map merged core-first, Korean falling
back to English like LocalizationCatalog.Entry.korean. Interpolated Swift
strings ("Bookmark \\(n)") are templates ("Bookmark {0}"); the export renders
them with the argument "Q".

Self-checks: the files equal the authority commit, every ReaderError case has
exactly one pair, ReaderCanvas has the expected number of distinct pairs, and
every pair has a catalog Entry.

No app, defaults, window, network or user data is touched.
Usage: reader_localization_reference.py NEW-output.json
"""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "4fda15a03ba50675e0b12d69a83efdb6526c19f6"
ZIP, CANVAS = "Sources/ReaderZIP.swift", "Sources/ReaderCanvas.swift"
CATALOG, REDESIGN = "Sources/LocalizationCatalog.swift", "Sources/LocalizationCatalogRedesign.swift"
PINS = [ZIP, CANVAS, CATALOG, REDESIGN]
CASES = ["unsupported", "invalidArchive", "tooLarge", "invalidBook", "unavailable", "encrypted", "changedOnDisk"]
LITERAL = r'"((?:[^"\\]|\\.)*)"'
PAIR = r'L10n\.text\(' + LITERAL + r', ' + LITERAL + r'\)'
ARGUMENT = "Q"
EXPECTED = 36  # distinct pairs: L10n.text, the result toasts and the module title


def decode(literal):
    """A Swift string literal; interpolations become {0}, {1}, ... in order."""
    out, index, count = [], 0, 0
    while index < len(literal):
        c = literal[index]
        if c != "\\":
            out.append(c); index += 1; continue
        nxt = literal[index + 1]
        if nxt == "(":
            depth, end = 1, index + 2
            while depth:
                depth += {"(": 1, ")": -1}.get(literal[end], 0); end += 1
            out.append("{%d}" % count); count += 1; index = end; continue
        if nxt == "u":
            end = literal.index("}", index)
            out.append(chr(int(literal[index + 3:end], 16))); index = end + 1; continue
        simple = {'"': '"', "\\": "\\", "n": "\n", "t": "\t", "0": "\0"}
        if nxt not in simple:
            raise SystemExit(f"Unsupported Swift escape \\{nxt}")
        out.append(simple[nxt]); index += 2
    return "".join(out)


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def render(template):
    return re.sub(r"\{\d+\}", ARGUMENT, template)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    if output.exists():
        raise SystemExit("Choose a new output path; source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    zipped, canvas, catalog, redesign = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

    errors = section(zipped, "enum ReaderError", "\n}\n")
    pairs = {}
    for name, english, simplified in re.findall(r'case \.(\w+): return ' + PAIR, errors):
        if name in pairs:
            raise SystemExit(f"Duplicate ReaderError case {name}")
        pairs[name] = (decode(english), decode(simplified))
    if list(pairs) != CASES:
        raise SystemExit(f"ReaderError cases changed: {list(pairs)}")

    captions = []
    sources = (PAIR, r'toast\(icon: \.\w+, ' + LITERAL + r', ' + LITERAL, r'HUDModuleTitle\(' + LITERAL + r', ' + LITERAL + r'\)')
    for pattern in sources:
        for english, simplified in re.findall(pattern, canvas):
            pair = (decode(english), decode(simplified))
            if pair not in captions:
                captions.append(pair)
    if len(captions) != EXPECTED:
        raise SystemExit(f"ReaderCanvas pairs changed: {len(captions)}")

    # LocalizationCatalog.entries = coreEntries + redesignEntries (first match wins).
    rows = {}
    for english, simplified, traditional, japanese in re.findall(
            r'Entry\(' + LITERAL + r',\s*' + LITERAL + r',\s*' + LITERAL + r',\s*' + LITERAL + r'\)', catalog):
        rows.setdefault((decode(english), decode(simplified)), (decode(traditional), decode(japanese)))
    korean = {}
    for key, value in re.findall(LITERAL + r': ' + LITERAL, section(catalog, "private static let coreKoreanTranslations", "\n    ]\n")):
        english, separator, simplified = decode(key).partition("\x1f")
        if not separator:
            raise SystemExit("Korean catalog key lacks the U+001F separator")
        korean.setdefault((english, simplified), decode(value))
    for english, simplified, traditional, japanese, ko in re.findall(
            r'R\(' + LITERAL + r',\s*' + LITERAL + r',\s*' + LITERAL + r',\s*' + LITERAL + r',\s*' + LITERAL + r'\)', redesign):
        pair = (decode(english), decode(simplified))
        rows.setdefault(pair, (decode(traditional), decode(japanese)))
        korean.setdefault(pair, decode(ko))

    def languages(pair):
        if pair not in rows:
            raise SystemExit(f"No catalog Entry for {pair}")
        traditional, japanese = rows[pair]
        return {"english": render(pair[0]), "simplifiedChinese": render(pair[1]), "traditionalChinese": render(traditional),
                "japanese": render(japanese), "korean": render(korean.get(pair, pair[0]))}

    result = {
        "source": {"authority": AUTHORITY, "files": PINS, "argument": ARGUMENT},
        "errors": {name: languages(pairs[name]) for name in CASES},
        "captions": [dict(languages(pair), template=pair[0], templateSimplifiedChinese=pair[1]) for pair in captions],
    }
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
