#!/usr/bin/env python3
"""Export the original Mac Reader captions and errors in five languages.

Reads the unchanged Mac sources at the pinned authority:
  * Sources/ReaderZIP.swift               ReaderError.errorDescription pairs
  * Sources/ReaderCanvas.swift            toolbar actions, title/loading/empty text
  * Sources/ReaderInteraction.swift       Library/Bookmarks/Shelf menu titles and
                                          the Reading settings menu items
  * Sources/NotesFormattingControls.swift the Open chooser ("Choose in Finder",
                                          "Choose from Shelf") Reader presents
  * Sources/LocalizationCatalog.swift     Entry rows (zh-Hant, ja) and the Korean map
and writes, for every source pair, the text Mac L10n.text renders in English,
Simplified Chinese, Traditional Chinese, Japanese and Korean (the catalog row;
Korean falls back to English exactly like LocalizationCatalog.Entry.korean).

Self-checks: the files equal the authority commit, every ReaderError case has
exactly one pair, every caption is found exactly where the source draws it and
every pair has exactly one catalog Entry.

No app, defaults, window, network or user data is touched.
Usage: reader_localization_reference.py NEW-output.json
"""
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
ZIP, CANVAS, INTERACTION = "Sources/ReaderZIP.swift", "Sources/ReaderCanvas.swift", "Sources/ReaderInteraction.swift"
CHOOSER, CATALOG = "Sources/NotesFormattingControls.swift", "Sources/LocalizationCatalog.swift"
PINS = [ZIP, CANVAS, INTERACTION, CHOOSER, CATALOG]
CASES = ["unsupported", "invalidArchive", "tooLarge", "invalidBook", "unavailable", "encrypted", "changedOnDisk"]
LITERAL = r'"((?:[^"\\]|\\.)*)"'
PAIR = r'L10n\.text\(' + LITERAL + r', ' + LITERAL + r'\)'


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


def one(text, prefix, name):
    """The single L10n pair that follows `prefix` (a literal source fragment)."""
    found = re.findall(re.escape(prefix) + PAIR, text)
    if len(found) != 1:
        raise SystemExit(f"Reader caption {name} changed: {len(found)} matches for {prefix!r}")
    return decode(found[0][0]), decode(found[0][1])


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    if output.exists():
        raise SystemExit("Choose a new output path; source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    zipped, canvas, interaction, chooser, catalog = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

    errors = section(zipped, "enum ReaderError", "\n}\n")
    pairs = {}
    for name, english, simplified in re.findall(r'case \.(\w+): return ' + PAIR, errors):
        if name in pairs:
            raise SystemExit(f"Duplicate ReaderError case {name}")
        pairs[name] = (decode(english), decode(simplified))
    if list(pairs) != CASES:
        raise SystemExit(f"ReaderError cases changed: {list(pairs)}")

    # Where the source draws each ReaderStrings caption.
    settings_menu = section(interaction, "final class ReaderSettingsMenu", "\n}\n")
    captions = {
        "open": one(canvas, 'Action(id: "open", label: ', "open"),
        "library": one(canvas, 'Action(id: "library", label: ', "library"),
        "reading": one(canvas, 'Action(id: "settings", label: ', "reading"),
        "bookmarks": one(canvas, 'Action(id: "bookmarks", label: ', "bookmarks"),
        "title": one(canvas, "controller.book?.title ?? ", "title"),
        "loading": one(canvas, "controller.loading ? ", "loading"),
        "empty": one(canvas, "var accessibilityText: String { controller.error ?? controller.current?.summary ?? ", "empty"),
        "chooseFile": one(chooser, 'Item(id: "finder", title: ', "chooseFile"),
        "chooseShelf": one(chooser, 'Item(id: "shelf", title: ', "chooseShelf"),
        "shelfTitle": one(section(interaction, "private func showShelf()", "\n    }\n"), "ReaderListMenu(choices: choices, title: ", "shelfTitle"),
        "font": one(settings_menu, 'Item(id: "font", title: ', "font"),
        "fontSize": one(settings_menu, '("size", ', "fontSize"),
        "lineSpacing": one(settings_menu, '("spacing", ', "lineSpacing"),
        "margin": one(settings_menu, '("margin", ', "margin"),
        "horizontal": one(settings_menu, 'Item(id: "horizontal", title: ', "horizontal"),
        "vertical": one(settings_menu, 'Item(id: "vertical", title: ', "vertical"),
    }
    library_menu = re.findall(r'ReaderListMenu\(choices: choices, title: ' + PAIR + r', allowsDelete: true\)', interaction)
    bookmark_menu = re.findall(r'title: ' + PAIR + r', width: 240\)', interaction)
    if [tuple(map(decode, x)) for x in library_menu] != [captions["library"]] or [tuple(map(decode, x)) for x in bookmark_menu] != [captions["bookmarks"]]:
        raise SystemExit("Reader Library/Bookmarks menu titles no longer equal their toolbar captions")
    if captions["loading"][0] != "Loading book…" or captions["chooseFile"][0] != "Choose in Finder":
        raise SystemExit("Reader loading or Finder chooser caption changed")

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

    def languages(pair):
        found = rows.get(pair, [])
        if len(found) != 1:
            raise SystemExit(f"Expected exactly one catalog Entry for {pair}, found {len(found)}")
        traditional, japanese = found[0]
        return {"english": pair[0], "simplifiedChinese": pair[1], "traditionalChinese": traditional,
                "japanese": japanese, "korean": korean.get(pair, pair[0])}

    result = {
        "source": {"authority": AUTHORITY, "files": PINS},
        "errors": {name: languages(pairs[name]) for name in CASES},
        "captions": {name: languages(pair) for name, pair in captions.items()},
        "windowsReplacement": {"chooseFile": "Choose in Finder names a Mac app; Windows shows the File Explorer chooser row"},
    }
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
