#!/usr/bin/env python3
"""Validate exported real Mac desktop references; no app/system data is read.

Usage: python3 windows/tests/test_reference_export.py EXPORT_DIR [--source-root REPO]
       ... EXPORT_DIR --compare SECOND_EXPORT_DIR
Comparisons require exact model JSON and same-host pixel hashes, not merely PNG
existence. Rendering parity on Windows is a separate future test.
"""
import argparse
import hashlib
import json
import pathlib
import struct


def load(path):
    return json.loads(path.read_text())


def verify(root, source_root=None):
    report = load(root / 'reference.json')
    assert report['desktopMode'] is True
    assert report['schemaVersion'] == 1
    assert report['isolation'] == {
        'windowCreated': False, 'persistentStoresCreated': False, 'userDefaultsAccessed': False,
        'clipboardAccessed': False, 'nativeCursorSetCount': 0, 'displayTimersAfterCleanup': 0}
    assert len(report['modules']) == 24
    assert len({m['id'] for m in report['modules']}) == 24
    expected = {m['id'] for m in report['modules']}
    names = set()
    visible_by_size = {}
    stable_by_size = {}
    for item in report['frames']:
        assert item['name'] not in names
        names.add(item['name'])
        frame = load(root / item['frame'])
        assert all(x.startswith('Unbound source curve: ') for x in frame['diagnostics'])
        assert frame['rendererDiagnostics'] == [] and frame['nativeDesktopHitQueries'] is True
        assert len(frame['batches']) == item['batchCount'] > 0
        assert len(frame['hits']) == item['hitCount'] > 0
        assert len(frame['nodes']) == item['nodeCount'] > 0
        assert set(n['target'] for n in frame['nativeNavigation']) == expected
        assert 'Endministrator' in frame['nativeProfileCaptions']
        assert 'UID: 1000000000' in frame['nativeProfileCaptions']
        assert all('#' not in x for x in frame['nativeProfileCaptions'])
        node_ids = {n['id'] for n in frame['nodes']}
        assert all(h['buttonID'] in node_ids and h['graphicID'] in node_ids for h in frame['hits'])
        assert all(len(b['worldMatrix']) == 4 and all(len(c) == 4 for c in b['worldMatrix']) for b in frame['batches'])
        assert [b['index'] for b in frame['batches']] == list(range(len(frame['batches'])))
        assert any(n['verifiedHitPoint'] is not None for n in frame['nativeNavigation'])
        size = tuple(frame['viewport'])
        visible_by_size.setdefault(size, set()).update(
            n['target'] for n in frame['nativeNavigation'] if n['verifiedHitPoint'] is not None)
        if item['name'].endswith('-top'):
            stable_by_size[size] = frame
        png = (root / item['png']).read_bytes()
        assert png[:8] == b'\x89PNG\r\n\x1a\n'
        assert list(struct.unpack('>II', png[16:24])) == frame['viewport']
        assert frame['rgbaSHA256'] == item['rgbaSHA256'] and len(item['rgbaSHA256']) == 64
    assert names and len(names) % 2 == 0
    assert all(targets == expected for targets in visible_by_size.values()), 'Scroll endpoints missed a desktop module'
    for path in report['animationTraces']:
        trace = load(root / path)
        assert trace['nativeOverlayPixels'] is False and trace['ambientEnabled'] is False
        assert len(trace['samples']) == 10
        for phase in ('opening', 'closing'):
            samples = [s for s in trace['samples'] if s['phase'] == phase]
            assert len(samples) == 5
            assert [s['elapsedSeconds'] for s in samples] == sorted(s['elapsedSeconds'] for s in samples)
            assert samples[-1]['elapsedSeconds'] == samples[-1]['durationSeconds']
        assert trace['samples'][-1]['concealed'] is True
        endpoint = trace['samples'][4]['frame']
        stable = stable_by_size[tuple(endpoint['viewport'])]
        for field in ('camera', 'nodes', 'batches'):
            assert endpoint[field] == stable[field], f'Opening endpoint differs from actual stable desktop {field}'
        # Native hit lookup is intentionally absent from standalone animation
        # samples; all geometric hit/mask/raycast fields must still agree.
        geometry_hits = lambda frame: [{k: v for k, v in h.items() if k != 'desktopTargetAtCenter'} for h in frame['hits']]
        assert geometry_hits(endpoint) == geometry_hits(stable)
        for sample in trace['samples']:
            if 'frame' in sample:
                assert all(x.startswith('Unbound source curve: ') for x in sample['frame']['diagnostics'])
                assert sample['frame']['nativeDesktopHitQueries'] is False
    if source_root:
        provenance = load(root / 'provenance.json')
        for path, expected_hash in provenance['sourceAndResourceSHA256'].items():
            actual = hashlib.sha256((source_root / path).read_bytes()).hexdigest()
            assert actual == expected_hash, f'Source changed during export: {path}'
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('export', type=pathlib.Path)
    parser.add_argument('--source-root', type=pathlib.Path)
    parser.add_argument('--compare', type=pathlib.Path)
    args = parser.parse_args()
    report = verify(args.export, args.source_root)
    if args.compare:
        other = verify(args.compare, args.source_root)
        assert report == other, 'Reference manifest or same-host pixel hashes differ'
        for path in [x['frame'] for x in report['frames']] + report['animationTraces']:
            assert load(args.export / path) == load(args.compare / path), f'Non-deterministic reference model: {path}'
    print(json.dumps({'result': 'PASS', 'desktopMode': True, 'scope': 'desktop shell only',
                      'frames': len(report['frames']), 'animationTraces': len(report['animationTraces']),
                      'repeatedExportEqual': bool(args.compare)}, sort_keys=True))


if __name__ == '__main__':
    main()
