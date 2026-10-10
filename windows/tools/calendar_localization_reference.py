#!/usr/bin/env python3
"""Export the original Mac Calendar error/permission captions in five languages.

Reads the unchanged Mac sources at the pinned authority:
  * Sources/HUDCalendarStore.swift      HUDCalendarError.errorDescription pairs
  * Sources/HUDCalendarController.swift reminderStatus (denied/unavailable) pairs
  * Sources/HUDCalendarInteraction.swift layoutAccessibility, CalendarEventMenu
                                        items and the editor field labels
  * Sources/NotesFormattingControls.swift NotesRetainedMenu "Close" fallback
  * Sources/LocalizationCatalog.swift   Entry rows (zh-Hant, ja) and the Korean map
and writes, for every source pair, the text Mac L10n.text renders in English,
Simplified Chinese, Traditional Chinese, Japanese and Korean (the catalog row;
Korean falls back to English exactly like LocalizationCatalog.Entry.korean).

Self-checks: the three files equal the authority commit, every HUDCalendarError
case has exactly one pair, every pair has exactly one catalog Entry, and the
Swift string literals contain only the escapes this parser decodes.

No app, defaults, notification center, window, network or user data is touched.
Usage: calendar_localization_reference.py NEW-output.json
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
STORE, CONTROLLER, CATALOG = "Sources/HUDCalendarStore.swift", "Sources/HUDCalendarController.swift", "Sources/LocalizationCatalog.swift"
INTERACTION, MENU = "Sources/HUDCalendarInteraction.swift", "Sources/NotesFormattingControls.swift"
PINS = [STORE, CONTROLLER, CATALOG, INTERACTION, MENU]
CASES = ["invalidData", "changedOnDisk", "capacity", "upcomingCapacity", "invalidDate", "missing", "notifications"]
LITERAL = r'"((?:[^"\\]|\\.)*)"'


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
    store, controller, catalog, interaction, menu = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

    errors = section(store, "enum HUDCalendarError", "\n}\n")
    pairs = {}
    for name, english, simplified in re.findall(r'case \.(\w+): return L10n\.text\(' + LITERAL + r', ' + LITERAL + r'\)', errors):
        if name in pairs:
            raise SystemExit(f"Duplicate HUDCalendarError case {name}")
        pairs[name] = (decode(english), decode(simplified))
    if list(pairs) != CASES:
        raise SystemExit(f"HUDCalendarError cases changed: {list(pairs)}")

    status = section(controller, "var reminderStatus: String", "\n    }\n")
    permission = {name: (decode(e), decode(s)) for name, e, s in
                  re.findall(r'case \.(\w+): return L10n\.text\(' + LITERAL + r', ' + LITERAL + r'\)', status)}
    if set(permission) != {"authorized", "denied", "unknown", "unavailable"}:
        raise SystemExit(f"reminderStatus cases changed: {sorted(permission)}")

    rows = {}
    for english, simplified, traditional, japanese in re.findall(
            r'Entry\(' + LITERAL + r', ' + LITERAL + r', ' + LITERAL + r', ' + LITERAL + r'\)', catalog):
        key = (decode(english), decode(simplified))
        rows.setdefault(key, []).append((decode(traditional), decode(japanese)))
    korean_section = section(catalog, "static let koreanTranslations", "\n    ]\n")
    korean = {}
    for key, value in re.findall(LITERAL + r': ' + LITERAL, korean_section):
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

    pair = LITERAL + r', ' + LITERAL
    layout = section(interaction, "func layoutAccessibility()", "button.setAccessibilityLabel(label)")
    canvas = {name: (decode(e), decode(z)) for name, e, z in re.findall(r'case "(\w+)": label = L10n\.text\(' + pair + r'\)', layout)}
    if list(canvas) != ["new", "previousMonth", "nextMonth", "reminders"]:
        raise SystemExit(f"Calendar canvas accessibility names changed: {list(canvas)}")
    if 'default: label = action.id.hasPrefix("day:") ? String(action.id.dropFirst(4)) : action.label' not in layout:
        raise SystemExit("Calendar canvas default accessibility rule changed")
    event_menu = section(interaction, "private final class CalendarEventMenu", "override func paintContent")
    save = re.search(r'Item\(id: "save", title: "✓", rect: [^)]*\), enabled: !busy, accessibilityTitle: L10n\.text\(' + pair + r'\)\)', event_menu)
    delete = re.search(r'Item\(id: deleting \? "cancelDelete" : "delete", title: "×", rect: [^)]*\), enabled: !busy,\s*accessibilityTitle: deleting \? L10n\.text\(' + pair + r'\) : L10n\.text\(' + pair + r'\)\)', event_menu)
    confirm = re.search(r'Item\(id: "confirmDelete", title: "✓", rect: [^)]*\), enabled: !busy, accessibilityTitle: L10n\.text\(' + pair + r'\)\)', event_menu)
    close = re.search(r'item\.accessibilityTitle \?\? \(item\.id == "close" \? L10n\.text\(' + pair + r'\) : item\.title\)', menu)
    fields = re.search(r'let label = field == "title" \? L10n\.text\(' + pair + r'\) : field == "date" \? L10n\.text\(' + pair + r'\) : L10n\.text\(' + pair + r'\)', interaction)
    if not (save and delete and confirm and close and fields) or 'Item(id: "close", title: "×"' not in event_menu:
        raise SystemExit("Calendar menu accessibility contract changed")
    p = lambda m, i: (decode(m.group(i)), decode(m.group(i + 1)))
    accessibility = {
        "canvas": {name: languages(value) for name, value in canvas.items()},
        "canvasDefault": "day:YYYY-MM-DD reads YYYY-MM-DD; any other action reads its own label",
        "menu": {"close": languages(p(close, 1)), "save": languages(p(save, 1)), "cancelDelete": languages(p(delete, 1)),
                 "delete": languages(p(delete, 3)), "confirmDelete": languages(p(confirm, 1))},
        "fields": {"title": languages(p(fields, 1)), "date": languages(p(fields, 3)), "details": languages(p(fields, 5))},
    }
    result = {
        "authority": AUTHORITY,
        "sourcePins": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PINS},
        "errors": [dict(case=name, **languages(pairs[name])) for name in CASES],
        "permission": {name: languages(permission[name]) for name in ("denied", "unavailable", "unknown", "authorized")},
        "accessibility": accessibility,
        "contract": "errors: HUDCalendarError.errorDescription in Mac case order; permission: HUDCalendarController.reminderStatus. "
                    "accessibility: HUDCalendarInteraction AX names (canvas buttons, CalendarEventMenu items, editor fields). "
                    "Each row is the text Mac L10n renders per language from LocalizationCatalog (Korean falls back to English).",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(output, hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
