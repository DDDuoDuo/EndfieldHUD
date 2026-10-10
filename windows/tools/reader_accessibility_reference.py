#!/usr/bin/env python3
"""Export the original Mac Reader accessibility contract in five languages.

Reads the unchanged Mac sources at the pinned authority:
  * Sources/ReaderInteraction.swift       layoutAccessibility canvas labels
  * Sources/ReaderCanvas.swift            accessibilityText (button help)
  * Sources/NotesFormattingControls.swift NotesRetainedMenu accessibility labels
  * Sources/LocalizationCatalog.swift     Entry rows (zh-Hant, ja) and the Korean map
and writes every caption as Mac L10n.text renders it in English, Simplified
Chinese, Traditional Chinese, Japanese and Korean (Korean falls back to
English like LocalizationCatalog.Entry.korean).

No app, defaults, window, network or user data is touched.
Usage: reader_accessibility_reference.py NEW-output.json
"""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
INTERACTION, CANVAS, MENU, CATALOG = ("Sources/ReaderInteraction.swift", "Sources/ReaderCanvas.swift",
                                      "Sources/NotesFormattingControls.swift", "Sources/LocalizationCatalog.swift")
PINS = [INTERACTION, CANVAS, MENU, CATALOG]
LITERAL = r'"((?:[^"\\]|\\.)*)"'
PAIR = r'L10n\.text\(\s*' + LITERAL + r',\s*' + LITERAL + r'\s*\)'


def decode(literal):
    def escape(match):
        body = match.group(1)
        if body.startswith("u{"):
            return chr(int(body[2:-1], 16))
        simple = {'"': '"', "\\": "\\", "n": "\n", "t": "\t", "0": "\0"}
        if body not in simple:
            raise SystemExit(f"Unsupported Swift escape \\{body}")
        return simple[body]
    return re.sub(r'\\(u\{[0-9A-Fa-f]+\}|.)', escape, literal)


def section(text, start, end):
    begin = text.index(start)
    return text[begin:text.index(end, begin)]


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    if output.exists():
        raise SystemExit("Choose a new output path; source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    interaction, canvas, menu, catalog = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

    rows = {}
    for english, simplified, traditional, japanese in re.findall(
            r'Entry\(' + LITERAL + r', ' + LITERAL + r', ' + LITERAL + r', ' + LITERAL + r'\)', catalog):
        rows.setdefault((decode(english), decode(simplified)), []).append((decode(traditional), decode(japanese)))
    korean = {}
    for key, value in re.findall(LITERAL + r': ' + LITERAL, section(catalog, "static let koreanTranslations", "\n    ]\n")):
        english, separator, simplified = decode(key).partition("\x1f")
        if not separator:
            raise SystemExit("Korean catalog key lacks the U+001F separator")
        korean[(english, simplified)] = decode(value)

    def languages(english, simplified):
        pair = (decode(english), decode(simplified))
        found = rows.get(pair, [])
        if len(found) != 1:
            raise SystemExit(f"Expected exactly one catalog Entry for {pair}, found {len(found)}")
        traditional, japanese = found[0]
        return {"english": pair[0], "simplifiedChinese": pair[1], "traditionalChinese": traditional,
                "japanese": japanese, "korean": korean.get(pair, pair[0])}

    layout = section(interaction, "func layoutAccessibility()", "button.setAccessibilityLabel(label)")
    named = re.findall(r'case "(\w+)": label = ' + PAIR, layout)
    text = re.findall(r'var accessibilityText: String \{ controller\.error \?\? controller\.current\?\.summary \?\? ' + PAIR + r' \}', canvas)
    close = re.findall(r'item\.accessibilityTitle \?\? \(item\.id == "close" \? ' + PAIR + r' : item\.title\)', menu)
    if not ([n[0] for n in named] == ["bookmark", "zoomIn", "zoomOut", "zoomReset"] and "default: label = action.label" in layout and
            "button.setAccessibilityHelp(canvas.accessibilityText)" in interaction and len(text) == 1 and len(close) == 1):
        raise SystemExit("Reader accessibility contract changed")
    result = {
        "source": {"authority": AUTHORITY, "files": PINS},
        "canvas": {name: languages(e, z) for name, e, z in named},
        "canvasDefault": "every other canvas action reads its own label",
        "help": "error ?? current page summary ?? empty caption",
        "emptyCaption": languages(*text[0]),
        "menuClose": languages(*close[0]),
        "menuDefault": "every other menu item reads its title (accessibilityTitle is unset for Reader menus)",
    }
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
