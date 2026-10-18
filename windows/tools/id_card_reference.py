#!/usr/bin/env python3
"""Reduce the selected source personal card to the facts the ID card binding uses.

Reads the unchanged Resources/WatchSource/Scene/desktop-profile-card.json and the
shell-packet caption export contract, and writes windows/tests/fixtures/
id_card-source.json in the same structure (a strict subset), so portable tests
do not need the Mac resource tree. Without OUTPUT the same bytes are also staged
as the app resource windows/resources/profile/id-card-source.json; update
idCardSourceSHA256 in windows/modules/id_card_binding.hpp when they change.
Usage: id_card_reference.py [OUTPUT]
"""
from pathlib import Path
import hashlib, json, sys

root = Path(__file__).resolve().parents[2]
card_path = root / 'Resources/WatchSource/Scene/desktop-profile-card.json'
output = Path(sys.argv[1]) if len(sys.argv) > 1 else root / 'windows/tests/fixtures/id_card-source.json'
raw = card_path.read_bytes()
card = json.loads(raw)
scene = card['scene']
captions = ['managerName', 'managerNumber', 'managerLevel', 'managerLevelLabel', 'progressTxt']
bound = captions + ['levelSlider', 'headFrameImg', 'playerHead', 'button']
bindings = {key: {'target_node_id': card['bindings'][key]['target_node_id']} for key in bound}
targets = {value['target_node_id'] for value in bindings.values()}
nodes = []
for node in scene['nodes']:
    keep = {'id': node['id'], 'name': node['name'], 'path': node['path'], 'parent_id': node['parent_id']}
    if node['id'] in targets:
        keep['transform'] = {'raw': {'m_SizeDelta': node['transform']['raw']['m_SizeDelta']}}
        keep['components'] = [{'script': c['script'], 'data': {'m_fontSize': c['data']['m_fontSize']}}
                              for c in node['components'] if c.get('script') == 'UIText']
    nodes.append(keep)
sprite = next(s for s in card['sprites']['sprites'] if s['name'] == 'business_card_topic_normal_1')
texture = next(t for t in card['sprites']['source_textures'] if t['id'] == sprite['texture']['id'])
assert texture['texture_format'] == 25 and texture['width'] == 532 and texture['height'] == 204
reduced = {
    'schema_version': card['schema_version'], 'parent_id': card['parent_id'],
    'scene': {'root_node_id': scene['root_node_id'], 'nodes': nodes},
    'bindings': bindings,
    'sprites': {'sprites': [{'id': sprite['id'], 'name': sprite['name'], 'texture': sprite['texture'],
                             'raw_sprite': {'m_Rect': sprite['raw_sprite']['m_Rect']},
                             'effective_render_data': {k: sprite['effective_render_data'][k] for k in ('textureRect', 'textureRectOffset')}}],
                'source_textures': [{'id': texture['id'], 'width': texture['width'], 'height': texture['height'],
                                     'texture_format': texture['texture_format'], 'raw': texture['raw']}]},
    'provenance': {'source': 'Resources/WatchSource/Scene/desktop-profile-card.json', 'sha256': hashlib.sha256(raw).hexdigest(),
                   'reduction': 'node identity/path for every node; UIText m_fontSize and RectTransform m_SizeDelta for bound nodes; selected default background sprite'},
}
encoded = (json.dumps(reduced, ensure_ascii=False, sort_keys=True, separators=(',', ':')) + '\n').encode()
outputs = [output] if len(sys.argv) > 1 else [output, root / 'windows/resources/profile/id-card-source.json']
for path in outputs:
    path.write_bytes(encoded)
    print('Wrote', path, len(nodes), 'nodes')
print('sha256', hashlib.sha256(encoded).hexdigest())
