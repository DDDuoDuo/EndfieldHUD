#!/usr/bin/env python3
"""Validate compact source variants, provenance and exact matrix coverage."""
import argparse
import hashlib
import json
import pathlib

LANGUAGES = {'english', 'simplifiedChinese', 'traditionalChinese', 'japanese', 'korean'}
PRESETS = {'original', 'bolt', 'star', 'terminal', 'globe', 'folder', 'music', 'play', 'brush', 'code', 'game', 'camera', 'grid', 'textBubble'}


def validate(root, source, shell_packet=None):
    root = pathlib.Path(root)
    data = json.loads((root / 'watch-content.json').read_text())
    assert data['schemaVersion'] == 1 and data['desktopMode'] is True
    assert all(value in (False, 0) for value in data['safety'].values())
    assert data['unsupported'] == [], data['unsupported']
    slots = {slot['buttonID']: slot for slot in data['slots']}
    right = {key for key, slot in slots.items() if slot['group'] == 'right'}
    assert len(slots) == 24 and len(right) == 18
    bindings = data['initialBindings']
    assert set(bindings) == set(slots) and len(set(bindings.values())) == 24
    trees = data['contentTrees']
    assert len(trees) < 20000
    total = 0
    for digest, record in trees.items():
        assert record['file'] == 'content/' + digest + '.json'
        raw = (root / record['file']).read_bytes()
        assert record['bytes'] == len(raw) and record['sha256'] == digest == hashlib.sha256(raw).hexdigest()
        tree = json.loads(raw)
        assert tree['id'] == 'content' and tree['position'] == [0, 0] and tree['anchorPoint'] == [0, 0]
        assert tree['transform'] == [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
        assert tree['bounds'][2] > 0 and tree['bounds'][3] > 0
        total += len(raw)
    observed = set()
    for row in data['variants']:
        key = tuple(row[x] for x in ('language', 'theme', 'selected', 'target', 'buttonID'))
        assert key not in observed
        observed.add(key)
        assert row['caption'] in trees and row['icon'] in trees
    pairs = {(target, slot) for slot, target in bindings.items() if target != 'addApp'}
    pairs |= {('addApp', slot) for slot in right}
    if data['matrix'] == 'all-right-slots':
        targets = {bindings[slot] for slot in right}
        pairs |= {(target, slot) for target in targets for slot in right}
    else:
        assert data['matrix'] == 'source-navigation-minimal'
        assert len(pairs) == 41
    expected = {(language, theme, selected, target, slot) for language in LANGUAGES
                for theme in ('dark', 'light') for selected in (False, True) for target, slot in pairs}
    assert observed == expected, (len(observed), len(expected), expected - observed)
    custom = {(r['language'], r['titleIndex'], r['buttonID']) for r in data['customCaptions']}
    assert custom == {(language, title, slot) for language in LANGUAGES for title in range(len(data['customTitles'])) for slot in right}
    assert len(custom) == len(data['customCaptions'])
    for row in data['customCaptions']:
        assert row['caption'] in trees
        caption = json.loads((root / trees[row['caption']]['file']).read_text())
        assert caption['text']['string'] == data['customTitles'][row['titleIndex']]
        assert caption['text']['truncation'] == 'end' and caption['text']['wrapped'] is False
    icons = {(row['preset'], row['buttonID']) for row in data['shortcutIcons']}
    assert len(icons) == len(data['shortcutIcons']) and icons == {(preset, slot) for preset in PRESETS for slot in right}
    for row in data['shortcutIcons']:
        assert row['icon'] in trees
    # Prove the source icon-local convention is identical across all 18 slots,
    # rather than assuming this to omit slot observations from the artifact.
    for preset in PRESETS:
        assert len({row['icon'] for row in data['shortcutIcons'] if row['preset'] == preset}) == 1
    assert {(r['language'], r['theme']) for r in data['chrome']} == {(language, theme) for language in LANGUAGES for theme in ('dark', 'light')}
    for row in data['chrome']:
        assert row['header'] in trees and row['footer'] in trees and len(row['moduleTitles']) == 24
        assert row['workActive'] == 'WORK MODE / ACTIVE' and row['workPaused'] == 'WORK MODE / PAUSED'
        header = json.loads((root / trees[row['header']]['file']).read_text())
        strings = [child.get('text', {}).get('string') for child in header['children']]
        assert 'ENDFIELDHUD' in strings and 'SYSTEM INTERFACE' in strings
    for asset in data['rasterAssets']:
        path = pathlib.PurePosixPath(asset['path'])
        assert not path.is_absolute() and '..' not in path.parts
        raw = (root / path).read_bytes()
        assert hashlib.sha256(raw).hexdigest() == asset['sha256']
        total += len(raw)
    provenance = json.loads((root / 'provenance.json').read_text())
    instrumentation = json.loads((root / 'instrumentation.json').read_text())
    assert instrumentation['productionSourceModified'] is False and instrumentation['SystemHUDViewConstructed'] is False
    assert len(instrumentation['originalContentRecipes']) == 4
    if source:
        source = pathlib.Path(source)
        for collection in ('sourceAndResourceSHA256', 'exporterSHA256'):
            for name, digest in provenance[collection].items():
                assert hashlib.sha256((source / name).read_bytes()).hexdigest() == digest, name
    if shell_packet:
        # Independent, previously frozen Mac exporter: compare every default
        # local plane, retaining all descendant visual data and ignoring only
        # instance IDs and the outer projective placement owned by the host.
        top = json.loads((pathlib.Path(shell_packet) / 'frame/desktop-shell-1280x800-top.json').read_text())
        by_name = {node['name']: node['children'][0] for node in top['nativeLayers']['children']}

        def local(node, outer=False):
            node = dict(node)
            node.pop('id', None)
            if outer:
                for key in ('frame', 'position', 'anchorPoint', 'transform', 'anchorPointZ', 'zPosition'):
                    node.pop(key, None)
            node['children'] = [local(child) for child in node['children']]
            if node.get('mask'):
                node['mask'] = local(node['mask'])
            return node

        variants = {(row['buttonID'], row['target']): row for row in data['variants']
                    if row['language'] == 'english' and row['theme'] == 'dark' and row['selected'] == (row['target'] == 'power')}
        for slot, descriptor in slots.items():
            row = variants[(slot, bindings[slot])]
            for kind, key, prefix in (('caption', 'captionID', 'desktop.watch.label.'), ('icon', 'iconID', 'desktop.watch.icon.')):
                actual = json.loads((root / trees[row[kind]]['file']).read_text())
                assert local(actual, True) == local(by_name[prefix + descriptor[key]], True), (slot, kind)
        print('PASS all 48 default local planes match the independent frozen Mac shell export')
    print(f"PASS {len(observed)} original module variants, {len(custom)} explicit custom captions, {len(icons)} preset/slot observations; {len(trees)} trees, {total:,} content bytes")


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('root')
    parser.add_argument('--source-root')
    parser.add_argument('--shell-packet')
    args = parser.parse_args()
    validate(args.root, args.source_root, args.shell_packet)
