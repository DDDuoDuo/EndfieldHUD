#!/usr/bin/env python3
"""Export the original Mac Map accessibility contract in five languages.

Reads the unchanged Mac sources at the pinned authority:
  * Sources/WorldMapCanvas.swift          accessibilityStatus, toolbarActions,
                                          accessibleActions (pin labels), deleteRect
  * Sources/HUDWorldMapInteraction.swift  layoutAccessibility help text and the
                                          requestAccessibilityLayout cadence
  * Sources/WorldMapPinArtwork.swift      pin style titles
  * Sources/LocalizationCatalog.swift     Entry rows (zh-Hant, ja) and the Korean map
and writes every caption as Mac L10n.text renders it in English, Simplified
Chinese, Traditional Chinese, Japanese and Korean (Korean falls back to
English like LocalizationCatalog.Entry.korean), with the control geometry.

No app, defaults, window, network or user data is touched.
Usage: map_accessibility_reference.py NEW-output.json
"""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
CANVAS, INTERACTION, ARTWORK, CATALOG = ("Sources/WorldMapCanvas.swift", "Sources/HUDWorldMapInteraction.swift",
                                        "Sources/WorldMapPinArtwork.swift", "Sources/LocalizationCatalog.swift")
PINS = [CANVAS, INTERACTION, ARTWORK, CATALOG]
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
    canvas, interaction, artwork, catalog = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

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

    status = re.findall(r'var accessibilityStatus: String \{\s*message \?\? ' + PAIR, canvas)
    toolbar_section = section(canvas, "private var toolbarActions", "\n    }\n")
    toolbar = re.findall(r'\("(\w+)", ' + PAIR + r', (\d+), ', toolbar_section)
    if 'WorldMapAction(id: "map:" + $0.0, label: $0.1,\n            rect: CGRect(x: 26, y: $0.2, width: 22, height: 22), enabled: $0.3)' not in toolbar_section:
        raise SystemExit("Map toolbar geometry changed")
    add = re.findall(r'WorldMapAction\(id: "map:addPin", label: ' + PAIR + r',\s*rect: CGRect\(x: (\d+), y: (\d+), width: (\d+), height: (\d+)\)', toolbar_section)
    delete = re.findall(r'WorldMapAction\(id: "map:deletePin", label: ' + PAIR + r', rect: Self\.deleteRect', toolbar_section)
    delete_rect = re.findall(r'static let deleteRect = CGRect\(x: (\d+), y: (\d+), width: (\d+), height: (\d+)\)', canvas)
    pin_label = 'WorldMapPinArtwork.title(for: pin.style) + " " + String(index + 1) + ", "\n                    + WorldMapGeometry.coordinateDescription(x: pin.x, y: pin.y)'
    help_text = re.findall(r'let help = label \+ ' + PAIR, section(interaction, "func layoutAccessibility()", "if status.isHidden"))
    cadence = re.findall(r'let remaining = 1\.0 / (\d+) - \(timeSource\(\) - lastAccessibilityUpdate\)', interaction)
    titles = dict((name, (e, z)) for name, e, z in re.findall(r'case \.(\w+): return ' + PAIR, section(artwork, "static func title(for style", "\n    }\n")))
    if not (len(status) == 1 and [t[0] for t in toolbar] == ["zoomIn", "zoomOut", "reset"] and len(add) == 1 and len(delete) == 1 and
            len(delete_rect) == 1 and pin_label in canvas and len(help_text) == 1 and cadence == ["12"] and list(titles) == ["yellow", "green", "player"]):
        raise SystemExit("Map accessibility contract changed")
    result = {
        "source": {"authority": AUTHORITY, "files": PINS},
        "status": languages(*status[0]),
        "help": languages(*help_text[0]),
        "toolbar": [dict(languages(e, z), id="map:" + name, rect=[26, int(y), 22, 22]) for name, e, z, y in toolbar],
        "addPin": dict(languages(add[0][0], add[0][1]), id="map:addPin", rect=[int(v) for v in add[0][2:]]),
        "deletePin": dict(languages(*delete[0]), id="map:deletePin", rect=[int(v) for v in delete_rect[0]]),
        "pinTitles": {name: languages(*pair) for name, pair in titles.items()},
        "pinLabel": "title + ' ' + (1-based index) + ', ' + coordinateDescription(x, y)",
        "cadenceHz": int(cadence[0]),
    }
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
