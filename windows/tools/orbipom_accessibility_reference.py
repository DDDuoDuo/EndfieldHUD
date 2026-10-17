#!/usr/bin/env python3
"""Export the original Mac minigame accessibility names in five languages.

Reads the unchanged Mac sources at the pinned authority:
  * Sources/HUDOrbiPomInteraction.swift   layoutAccessibility label overrides,
                                          OrbiPomRulesMenu label and paragraphs
  * Sources/OrbiPomCanvas.swift           action ids/titles (Start/Play again,
                                          skill titles and suffixes)
  * Sources/OrbiPomRuntime.swift          OrbiPomSkill.energyCost
  * Sources/NotesFormattingControls.swift NotesRetainedMenu "Close" fallback
  * Sources/LocalizationCatalog.swift     Entry rows (zh-Hant, ja), Korean map
Every L10n pair is resolved per language exactly like LocalizationCatalog
(Korean falls back to English). The parser refuses changed source shapes.

No app, window, defaults, game state or user data is touched.
Usage: orbipom_accessibility_reference.py NEW-output.json
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
INTERACTION, CANVAS, RUNTIME = "Sources/HUDOrbiPomInteraction.swift", "Sources/OrbiPomCanvas.swift", "Sources/OrbiPomRuntime.swift"
MENU, CATALOG = "Sources/NotesFormattingControls.swift", "Sources/LocalizationCatalog.swift"
PINS = [INTERACTION, CANVAS, RUNTIME, MENU, CATALOG]
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


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    output = Path(sys.argv[1])
    if output.exists():
        raise SystemExit("Choose a new output path; source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    interaction, canvas, runtime, menu, catalog = ((ROOT / p).read_text(encoding="utf-8") for p in PINS)

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
        return {"english": pair[0], "simplifiedChinese": pair[1], "traditionalChinese": found[0][0],
                "japanese": found[0][1], "korean": korean.get(pair, pair[0])}

    layout = section(interaction, "func layoutAccessibility()", "button.setAccessibilityLabel(title)")
    if "var title = action.title" not in layout:
        raise SystemExit("Minigame accessibility no longer starts from the canvas action title")
    overrides = {}
    for condition, english, simplified in re.findall(r'if (action\.id == "[^{]*) \{ title = ' + PAIR + r' \}', layout):
        ids = re.findall(r'action\.id == "(\w+)"', condition)
        if condition != " || ".join(f'action.id == "{i}"' for i in ids):
            raise SystemExit(f"Unsupported minigame accessibility condition {condition}")
        for identifier in ids:
            overrides[identifier] = languages(english, simplified)
    if sorted(overrides) != sorted(["pause", "restart", "cancelRestart", "cancelSkill", "confirmRestart", "rules"]):
        raise SystemExit(f"Minigame accessibility overrides changed: {sorted(overrides)}")
    if "button.isHidden = secondaryMenu != nil" not in layout or "guard active, let host else { return }" not in layout:
        raise SystemExit("Minigame accessibility visibility rules changed")

    controls = section(canvas, "if restartConfirmation {", "onChange?()")
    start = re.search(r'button\("start",s\.state == "idle" \? ' + PAIR + r' : ' + PAIR + r',', controls)
    titles = re.search(r'let titles = \[' + PAIR + r',' + PAIR + r',' + PAIR + r',' + PAIR + r'\]', controls)
    suffix = 'let suffix = skill == .swap ? " \\(s.swapCharge)/6" : " \\(skill.energyCost)"'
    if not start or not titles or suffix not in controls or 'button(skill.rawValue,titles[i] + suffix,' not in controls:
        raise SystemExit("Minigame action titles changed")
    for glyph in ['button("cancelRestart","×"', 'button("confirmRestart","✓"', 'button("restart","↻"', 'button("cancelSkill","×"', 'button("rules","?"']:
        if glyph not in controls:
            raise SystemExit(f"Minigame control changed: {glyph}")
    cases = re.search(r'enum OrbiPomSkill: String, CaseIterable, Codable \{\s*case clear, wind, shake, swap\s*var energyCost: Int \{ switch self \{ case \.clear: return (\d+); case \.wind: return (\d+); case \.shake: return (\d+); case \.swap: return (\d+) \} \}', runtime)
    if not cases:
        raise SystemExit("OrbiPomSkill cases or costs changed")
    skills = {}
    for index, name in enumerate(["clear", "wind", "shake", "swap"]):
        skills[name] = {"title": languages(titles.group(2 * index + 1), titles.group(2 * index + 2)),
                        "suffix": "swapCharge/6" if name == "swap" else int(cases.group(index + 1))}

    rules = section(interaction, "private final class OrbiPomRulesMenu", "required init?(coder:NSCoder)")
    paragraphs = re.findall(PAIR, section(rules, "paragraphs = [", "]\n"))
    label = re.search(r'setAccessibilityLabel\(' + PAIR + r'\); setAccessibilityValue\(paragraphs\.joined\(separator:"\\n"\)\)', rules)
    close_item = 'items = [Item(id:"close",title:"×",rect:CGRect(x:291,y:8,width:23,height:23))]'
    close = re.search(r'item\.accessibilityTitle \?\? \(item\.id == "close" \? ' + PAIR + r' : item\.title\)', menu)
    if len(paragraphs) != 9 or not label or close_item not in rules or not close:
        raise SystemExit("Minigame rules accessibility changed")

    result = {
        "authority": AUTHORITY,
        "sourcePins": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PINS},
        "overrides": overrides,
        "start": {"idle": languages(start.group(1), start.group(2)), "other": languages(start.group(3), start.group(4))},
        "skills": skills,
        "rules": {"label": languages(label.group(1), label.group(2)), "paragraphs": [languages(e, z) for e, z in paragraphs],
                  "close": languages(close.group(1), close.group(2)), "closeRect": [291, 8, 23, 23]},
        "contract": "HUDOrbiPomInteraction.layoutAccessibility: hidden unless active and while the rules menu is open; "
                    "label = canvas action title with the listed overrides (start: idle ? Start : Play again; skill: "
                    "title + ' ' + energyCost, Swap: title + ' ' + swapCharge + '/6'). Rules menu: label + paragraphs "
                    "joined by newline, plus its Close item. Rows resolve per language like LocalizationCatalog.",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(output, hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
