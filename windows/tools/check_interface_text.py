#!/usr/bin/env python3
"""Check the translated interface text at its call sites, as the Mac LocalizationTests do.

Scans the production C++ under windows/ (app, core, modules, native, packaging, tools; never tests or third-party code)
for every catalog lookup written with a literal source pair: core::localized("English", "简体中文", ...) and the canvas
and module helpers that forward to it (text, t, tr, L, title, with, value, count, caption, toast(icon, ...)). Each pair
must be a row of the generated catalog windows/core/localization_catalog.inc (otherwise Japanese, Korean and Traditional
Chinese fall back), and neither text may end in a full stop or separate phrases with a middle dot (Mac 0b94c8e, ec418d5;
the C++ tests share the same two rules in tests/interface_text_rules.hpp).

  check_interface_text.py [WINDOWS_DIR]    print each failure; exit 1 on any, or when fewer than 500 pairs were checked

Reads source files only; no app, settings, network or user data.
"""
import pathlib
import re
import sys

PRODUCTION = ('app', 'core', 'modules', 'native', 'packaging', 'tools')
SKIPPED_DIRECTORIES = {'tests', 'third_party', 'fixtures'}
CATALOG = 'core/localization_catalog.inc'
MINIMUM_PAIRS = 500
# A lookup helper's name right before "(", optionally with one leading argument (toast(.warn, ..., tr(*env_, ...).
CALL = re.compile(r'(?:\b(?:core::)?localized|\btext|\bt|\btr|\bL|\btitle|\bwith|\bvalue|\bcount|\b(?:telemetry::)?caption)\s*\(\s*$'
                  r'|\b(?:toast|tr)\s*\(\s*[\w:.*\->]+\s*,\s*$')
STOPS = ('.', '。', '．')
DOTS = ('·', '•', '‧', '･')
NAME_DOT = '・'


def ends_with_full_stop(text):
    """Mac endsWithFullStop: a final ".", "。" or "．" ends a sentence; "..." and a lone stop do not."""
    return len(text) > 1 and text.endswith(STOPS) and not text.endswith('...')


def has_dot_separator(text):
    """Mac hasDotSeparator: a middle dot or bullet anywhere, or U+30FB outside a katakana name."""
    if any(dot in text for dot in DOTS):
        return True
    katakana = lambda c: 0x30A0 <= ord(c) <= 0x30FF
    return any(c == NAME_DOT and not (0 < i < len(text) - 1 and katakana(text[i - 1]) and katakana(text[i + 1]) and text[i + 1] != NAME_DOT)
               for i, c in enumerate(text))


def unescape(body):
    """The value of an ordinary C++ string literal body (UTF-8 source, simple, octal and hex escapes)."""
    out, i = bytearray(), 0
    while i < len(body):
        c = body[i]
        if c != '\\' or i + 1 == len(body):
            out += c.encode(); i += 1; continue
        n = body[i + 1]
        if n in '01234567':
            j = i + 1
            while j < len(body) and j < i + 4 and body[j] in '01234567': j += 1
            out.append(int(body[i + 1:j], 8) & 0xFF); i = j; continue
        if n == 'x':
            j = i + 2
            while j < len(body) and body[j] in '0123456789abcdefABCDEF': j += 1
            out.append(int(body[i + 2:j], 16) & 0xFF); i = j; continue
        if n in 'uU':
            width = 4 if n == 'u' else 8
            out += chr(int(body[i + 2:i + 2 + width], 16)).encode(); i += 2 + width; continue
        out += {'n': '\n', 't': '\t', 'r': '\r', '0': '\0'}.get(n, n).encode(); i += 2
    return out.decode('utf-8', 'replace')


def literals(source):
    """(value, start, end) of each string literal, adjacent literals joined; comments, character and raw literals skipped."""
    found, i, n = [], 0, len(source)
    while i < n:
        if source.startswith('//', i):
            j = source.find('\n', i); i = n if j < 0 else j; continue
        if source.startswith('/*', i):
            j = source.find('*/', i + 2); i = n if j < 0 else j + 2; continue
        c = source[i]
        if c == "'":
            if i > 0 and source[i - 1].isalnum(): i += 1; continue  # digit separator (1'000)
            j = i + 1
            while j < n and source[j] != "'": j += 2 if source[j] == '\\' else 1
            i = j + 1; continue
        raw = re.match(r'(?:u8|u|U|L)?R"([^()\\ ]{0,16})\(', source[i:i + 24]) if c in 'uULR' and (i == 0 or not (source[i - 1].isalnum() or source[i - 1] == '_')) else None
        if raw:
            end = source.find(')' + raw.group(1) + '"', i + raw.end())
            i = end + len(raw.group(1)) + 2; continue
        if c == '"':
            j = i + 1
            while j < n and source[j] != '"': j += 2 if source[j] == '\\' else 1
            value = unescape(source[i + 1:j])
            if found and source[found[-1][2]:i].strip() == '':
                found[-1] = (found[-1][0] + value, found[-1][1], j + 1)
            else:
                found.append((value, i, j + 1))
            i = j + 1; continue
        i += 1
    return found


def catalog_pairs(windows):
    """The (English, Simplified Chinese) pairs of the generated catalog."""
    rows = set()
    for line in (windows / CATALOG).read_text(encoding='utf-8').splitlines():
        if line.startswith('    {"'):
            values = [value for value, _, _ in literals(line)]
            rows.add((values[0], values[1]))
    return rows


def call_pairs(source):
    """(English, Simplified Chinese, line) for each lookup call written with a literal pair."""
    found = literals(source)
    for first, second in zip(found, found[1:]):
        if not re.fullmatch(r'\s*,\s*', source[first[2]:second[1]]):
            continue
        if CALL.search(source[max(0, first[1] - 80):first[1]]):
            yield first[0], second[0], source.count('\n', 0, first[1]) + 1


def check(windows):
    rows = catalog_pairs(windows)
    failures, checked = [], 0
    for directory in PRODUCTION:
        for path in sorted((windows / directory).rglob('*')):
            if path.suffix not in ('.cpp', '.hpp') or path.name == pathlib.Path(CATALOG).name:
                continue
            if SKIPPED_DIRECTORIES.intersection(path.relative_to(windows).parts):
                continue
            for english, simplified, line in call_pairs(path.read_text(encoding='utf-8')):
                checked += 1
                where = f'{path.relative_to(windows)}:{line}: {english}'
                if ends_with_full_stop(english) or ends_with_full_stop(simplified):
                    failures.append('Interface text ends in a full stop: ' + where)
                if has_dot_separator(english) or has_dot_separator(simplified):
                    failures.append('Interface text has a middle dot separator: ' + where)
                if (english, simplified) not in rows:
                    failures.append('Pair missing from the catalog: ' + where)
    return failures, checked


def main():
    windows = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else pathlib.Path(__file__).resolve().parents[1]
    failures, checked = check(windows)
    for failure in failures:
        print(failure)
    if checked < MINIMUM_PAIRS:
        print(f'Only {checked} call-site pairs were found; the scan no longer reaches the interface text')
        return 1
    print(f'{checked} call-site pairs checked, {len(failures)} failures')
    return 1 if failures else 0


if __name__ == '__main__':
    sys.exit(main())
