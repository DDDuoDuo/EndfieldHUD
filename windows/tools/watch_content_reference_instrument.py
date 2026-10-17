#!/usr/bin/env python3
"""Guarded hooks on a pinned source build copy; no production source edits."""
import hashlib
import importlib.util
import json
import pathlib
import sys


def main(root, output):
    root, output = pathlib.Path(root), pathlib.Path(output)
    existing = root / 'windows/tools/desktop_chrome_reference_instrument.py'
    spec = importlib.util.spec_from_file_location('source_chrome_instrument', existing)
    chrome = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(chrome)
    chrome.run(root, output)
    copied = output / 'HUDSourceWatchView.swift'
    hooks = root / 'windows/tools/watch_content_reference_hooks.swift'
    copied.write_text(copied.read_text() + '\n' + hooks.read_text())
    source = (root / 'Sources/HUDSourceWatchView.swift').read_text()
    recipes = {name: chrome.block(source, name) for name in (
        'private func desktopCaptionSize(', 'private func updateDesktopSelection(',
        'private func bindDesktopButtons(', 'private func updateDesktopBindings(')}
    report = json.loads((output / 'instrumentation.json').read_text())
    report.update({'contentHooksSHA256': hashlib.sha256(hooks.read_bytes()).hexdigest(),
                   'generatedWatchSHA256': hashlib.sha256(copied.read_bytes()).hexdigest(),
                   'originalContentRecipes': recipes,
                   'contentHookScope': 'Calls unchanged source binding/font fitting/artwork/projection on a detached desktop-mode view.'})
    (output / 'instrumentation.json').write_text(json.dumps(report, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main(*sys.argv[1:])
