#!/usr/bin/env python3
"""Guarded hooks on a build copy of HUDSourceWatchView; no production source edits.

Mac 1.3.0: the content export no longer needs the extracted SystemHUDView
chrome fixture (its header/footer rows were dropped; the shell redesign oracle
covers the clock): only the source watch view, with the content hooks appended.
"""
import hashlib
import importlib.util
import json
import pathlib
import sys


def main(root, output):
    root, output = pathlib.Path(root), pathlib.Path(output)
    output.mkdir(parents=True, exist_ok=True)
    existing = root / 'windows/tools/desktop_chrome_reference_instrument.py'
    spec = importlib.util.spec_from_file_location('source_chrome_instrument', existing)
    chrome = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(chrome)
    hooks = root / 'windows/tools/watch_content_reference_hooks.swift'
    source = (root / 'Sources/HUDSourceWatchView.swift').read_text()
    copied = output / 'HUDSourceWatchView.swift'
    copied.write_text(source + '\n' + hooks.read_text())
    recipes = {name: chrome.block(source, name) for name in (
        'private func desktopCaptionSize(', 'private func updateDesktopSelection(',
        'private func bindDesktopButtons(', 'private func updateDesktopBindings(')}
    report = {'sourceSHA256': {'Sources/HUDSourceWatchView.swift': hashlib.sha256(source.encode()).hexdigest()},
              'productionSourceModified': False, 'SystemHUDViewConstructed': False,
              'contentHooksSHA256': hashlib.sha256(hooks.read_bytes()).hexdigest(),
              'generatedWatchSHA256': hashlib.sha256(copied.read_bytes()).hexdigest(),
              'originalContentRecipes': recipes,
              'contentHookScope': 'Calls unchanged source binding/font fitting/artwork/projection on a detached desktop-mode view.'}
    (output / 'instrumentation.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main(*sys.argv[1:])
