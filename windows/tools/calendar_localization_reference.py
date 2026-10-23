#!/usr/bin/env python3
"""Export the Mac Calendar error and reminder-status captions in five languages.

Reads the unchanged Mac 1.3.0 sources (the worktree must equal the given commit):
  * Sources/HUDCalendarStore.swift       HUDCalendarError.errorDescription pairs
  * Sources/HUDCalendarController.swift  reminderStatus pairs (the Reminders toast caption, MODS-660)
and renders every pair through the Mac catalog itself (LocalizationCatalog.swift + LocalizationCatalogRedesign.swift
compiled together and dumped through LocalizationCatalog.entries, as tools/build_localization_catalog.py does),
so each language is exactly what L10n.text shows (Korean falls back to English like Entry.korean).
The Calendar canvas's own captions and accessibility names are covered by the canvas oracle
(tools/work_mode_calendar_reference.sh).

Self-checks: the files equal the commit, every HUDCalendarError case has exactly one pair, every pair has a
catalog row, and the Swift string literals contain only the escapes this parser decodes.
No app, defaults, notification center, window, network or user data is touched.
Usage: calendar_localization_reference.py NEW-output.json [--source-commit REV (default origin/main)]
"""
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parent))
from build_localization_catalog import CATALOGS, ROOT, mac_rows  # noqa: E402

STORE, CONTROLLER = "Sources/HUDCalendarStore.swift", "Sources/HUDCalendarController.swift"
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
    args = sys.argv[1:]
    commit_name = "origin/main"
    if "--source-commit" in args:
        at = args.index("--source-commit"); commit_name = args[at + 1]; del args[at:at + 2]
    if len(args) != 1:
        raise SystemExit(__doc__)
    output = Path(args[0])
    if output.exists():
        raise SystemExit("Choose a new output path; source fixtures are immutable")
    commit = subprocess.check_output(["git", "-C", str(ROOT), "rev-parse", "--verify", commit_name + "^{commit}"], text=True).strip()
    pins = [STORE, CONTROLLER] + CATALOGS
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", commit, "--"] + pins, check=True)
    store, controller = ((ROOT / p).read_text(encoding="utf-8") for p in (STORE, CONTROLLER))

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
    for row in mac_rows([(ROOT / path).read_text() for path in CATALOGS]):
        rows.setdefault((row[0], row[1]), row)   # the Mac's lookup takes the first row of a pair

    def languages(pair):
        row = rows.get(pair)
        if row is None:
            raise SystemExit(f"No Mac catalog row for {pair}")
        return {"english": row[0], "simplifiedChinese": row[1], "traditionalChinese": row[2], "japanese": row[3], "korean": row[4]}

    result = {
        "authority": commit,
        "sourcePins": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in pins},
        "errors": [dict(case=name, **languages(pairs[name])) for name in CASES],
        "permission": {name: languages(permission[name]) for name in ("denied", "unavailable", "unknown", "authorized")},
        "contract": "errors: HUDCalendarError.errorDescription in Mac case order; permission: HUDCalendarController.reminderStatus. "
                    "Each row is the text Mac L10n renders per language from LocalizationCatalog.",
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, ensure_ascii=False, indent=1, sort_keys=True) + "\n", encoding="utf-8")
    print(output, hashlib.sha256(output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
