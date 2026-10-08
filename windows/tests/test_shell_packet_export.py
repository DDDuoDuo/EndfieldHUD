#!/usr/bin/env python3
"""Validate retained actual-desktop packets and every referenced byte payload."""
import argparse
import hashlib
import importlib.util
import json
import math
import pathlib
import struct


def load(path):
    def invalid(value):
        raise ValueError('Non-JSON numeric sentinel: ' + value)
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=invalid)


def desktop_texture_dependencies(animation):
    builder, mounted = animation['frameBuilder'], animation['mountedDocument']
    settings = builder['desktopSettings']
    nodes = {n['id']: n for n in animation['scene']['nodes']}
    parents = {child: n['id'] for n in nodes.values() for child in n.get('child_ids', [])}
    hidden = set(settings['hiddenNodes'])
    def excluded(node):
        while node in nodes:
            if node in hidden:
                return True
            node = parents.get(node)
        return False
    ids = {'__white'}
    for node, components in mounted['components'].items():
        if excluded(node):
            continue
        for component in components:
            data, kind = component['data'], component.get('script') or component['type']
            if not data.get('m_Enabled', True):
                continue
            if kind in ('RawImage', 'UIRawImage'):
                ids.add(data.get('m_Texture', {}).get('target_id') or '__white')
            elif kind in ('Image', 'UIImage'):
                if node in settings['images']:
                    ids.add(settings['images'][node]['texture'])
                elif node in settings['sprites']:
                    ids.add(builder['sourceSprites'][settings['sprites'][node]]['textureID'])
                elif component['id'] in builder['sprites']:
                    ids.add(builder['sprites'][component['id']]['textureID'])
        if any((c.get('script') or c['type']) == 'UISoftMask' and c['data'].get('m_Enabled', True) for c in components):
            image = next((c for c in components if (c.get('script') or c['type']) == 'UIImage'), None)
            if image is not None and image['id'] in mounted['spriteByComponent']:
                ids.add(mounted['spriteByComponent'][image['id']]['texture']['id'])
    return ids


def verify(root, source_root=None):
    root = root.resolve()
    report = load(root/'shell-packet.json')
    assert report['schemaVersion'] == 1 and report['desktopMode'] is True
    assert report['unresolvedTextures'] == []
    assert report['isolation'] == {'windowCreated': False, 'persistentStoresCreated': False,
        'userDefaultsAccessed': False, 'clipboardAccessed': False, 'nativeCursorSetCount': 0, 'displayTimersAfterCleanup': 0}
    seen_blobs = {}
    def blob(desc):
        relative = pathlib.PurePosixPath(desc['file'])
        assert not relative.is_absolute() and '..' not in relative.parts and '\\' not in str(relative)
        path = (root/str(relative)).resolve()
        assert path.is_relative_to(root), 'Blob escapes packet root'
        data = path.read_bytes()
        assert len(data) == desc['bytes'] and hashlib.sha256(data).hexdigest() == desc['sha256'], desc['file']
        identity = (desc['bytes'], desc['sha256'])
        assert str(relative) not in seen_blobs or seen_blobs[str(relative)] == identity
        seen_blobs[str(relative)] = identity
        return data
    def unique(records):
        result = {v['id']: v for v in records}
        assert len(result) == len(records) > 0
        return result
    meshes, textures, materials = [unique(report[k]) for k in ('meshes','textures','materials')]
    for mesh in meshes.values():
        vertices, indices, original = [blob(mesh[k]) for k in ('vertices','indices','originalVertexBuffer')]
        assert mesh['vertices']['stride'] == 40 and mesh['vertices']['fields'] == {'position':0,'uv':16,'color':24}
        assert len(vertices) == mesh['vertexCount']*40 and len(indices) == mesh['indexCount']*4
        assert mesh['indices']['format'] == 'uint32-le'
        assert all(i[0] < mesh['vertexCount'] for i in struct.iter_unpack('<I', indices))
        stride, offsets = mesh['originalVertexBuffer']['stride'], mesh['originalVertexBuffer']['offsets']
        assert len(original) == mesh['vertexCount']*stride
        assert set(offsets) == {'position','uv','color','normal','uv1'}
        for i, vertex in enumerate(struct.iter_unpack('<10f', vertices)):
            assert all(math.isfinite(v) for v in vertex)
            assert vertex[:4] == struct.unpack_from('<4f', original, i*stride+offsets['position'])
            assert vertex[4:6] == struct.unpack_from('<2f', original, i*stride+offsets['uv'])
            assert vertex[6:] == struct.unpack_from('<4f', original, i*stride+offsets['color'])
    allowed_formats = {'rgba8Unorm','rgba8Unorm_srgb','bgra8Unorm','bgra8Unorm_srgb','r8Unorm','bc7_rgbaUnorm','bc7_rgbaUnorm_srgb'}
    assert {'desktop.profile.avatar','desktop.profile.background','desktop.profile.hover'} <= textures.keys()
    for tex in textures.values():
        assert tex['pixelFormat'] in allowed_formats and tex['sRGB'] == tex['pixelFormat'].endswith('_srgb')
        assert tex['width'] > 0 and tex['height'] > 0 and tex['mips']
        for key in ('minFilter','magFilter','mipFilter','addressU','addressV','addressW','maxAnisotropy','compareFunction'):
            assert isinstance(tex['sampler'][key], int)
        for level, mip in enumerate(tex['mips']):
            data = blob(mip)
            w, h = max(1,tex['width']>>level), max(1,tex['height']>>level)
            assert (mip['level'],mip['width'],mip['height']) == (level,w,h)
            compressed = tex['pixelFormat'].startswith('bc7')
            assert mip['rowBytes'] == ((w+3)//4)*16 if compressed else mip['rowBytes'] == w*(1 if tex['pixelFormat']=='r8Unorm' else 4)
            assert len(data) == mip['rowBytes']*((h+3)//4 if compressed else h)
    shader_paths = set()
    for asset in report['shaderAssets']:
        blob(asset); assert asset['sourcePath'] not in shader_paths; shader_paths.add(asset['sourcePath'])
    for material in materials.values():
        assert set(material['textures'].values()) <= textures.keys()
        assert material['passes']
        for p in material['passes']:
            assert p['shaderDescriptorFile'] in shader_paths and p['shaderDescriptor']['shader'] == p['shader']
            assert set(p['blend']) == {'enabled','sourceRGB','destinationRGB','sourceAlpha','destinationAlpha','rgbOperation','alphaOperation','writeMask'}
            assert {'depthCompare','depthWrite','front','back'} == p['depthStencil'].keys()
            assert p['vertexAttributes'] and all(a['stride'] == 80 for a in p['vertexAttributes'])
            for stage in p['stages'].values():
                assert stage['file'] in shader_paths
                assert str(pathlib.PurePosixPath(stage['file']).with_suffix('.spv')) in shader_paths
    names, visible = set(), set()
    template_names, template_shapes = [], set()
    def template_shape(batch):
        return json.dumps({'material': materials[batch['material']],
            'geometry': 'source-dynamic-ui' if batch['sourceMesh'].startswith('ui/') else batch['sourceMesh'],
            'uniforms': {key: len(value) for key, value in batch['uniformOverrides'].items()},
            'textures': sorted(batch['textureOverrides']), 'stencil': batch.get('stencil'),
            'colorWriteMask': batch.get('colorWriteMask'), 'indexRange': batch.get('indexRange')}, sort_keys=True)
    builder_inputs = []
    for desc in report['frames']:
        frame = json.loads(blob(desc))
        assert desc['name'] not in names; names.add(desc['name'])
        nodes = {n['id'] for n in frame['nodes']}
        assert len(nodes) == len(frame['nodes']) and nodes
        assert [b['index'] for b in frame['batches']] == list(range(len(frame['batches'])))
        assert frame['gpuCamera']['sceneColorMode'] == 'directLDR'
        template_only = frame.get('templateOnly', False)
        if template_only:
            template_names.append(desc['name'])
            assert frame['nativeOverlayStateIncluded'] is False and 'builderInput' not in frame
            coverage = frame['templateCoverage']
            assert coverage['state'] in ('Normal', 'Highlighted', 'Pressed', 'Disabled')
            assert coverage['retainedTemplateCount'] == len(frame['batches']) > 0
            assert coverage['sourceBatchCount'] >= coverage['retainedTemplateCount']
        baseline_template = desc['name'].startswith('desktop-shell-1280x800-') and desc['name'].endswith(('-top', '-arbitrary-0', '-arbitrary-2'))
        for batch in frame['batches']:
            assert batch['mesh'] in meshes and batch['material'] in materials
            assert batch['sourceMesh'] == meshes[batch['mesh']]['sourceMesh']
            assert len(batch['gpuVertexColor']) == 4 and all(math.isfinite(x) for x in batch['gpuVertexColor'])
            assert batch['sourceNodeID'] is None or batch['sourceNodeID'] in nodes
            if batch['indexRange'] is not None:
                a,b = batch['indexRange']; assert 0 <= a < b <= meshes[batch['mesh']]['indexCount']
            material = materials[batch['material']]
            if template_only or baseline_template:
                shape = template_shape(batch)
                if template_only:
                    assert shape not in template_shapes, 'Redundant desktop template retained'
                template_shapes.add(shape)
            for uniform in batch['uniforms']:
                data = blob(uniform['payload'])
                assert len(data) == uniform['byteCount']
                assert uniform['passID'] in {p['id'] for p in material['passes']}
                assert uniform['stage'] in {'vertex','fragment'} and uniform['bufferName']
                assert all(0 <= f['offset'] < uniform['byteCount'] for f in uniform['fields'])
                # Independent binding check against the exported live draw,
                # rather than merely trusting hashes made by the same writer.
                for field in uniform['fields']:
                    if field['value'] is not None or field['name'] in batch['uniformOverrides']:
                        continue
                    matrix = batch['worldMatrix'] if field['dynamic'] == 'world' else frame['gpuCamera'].get(field['dynamic'])
                    if field['dynamic'] in {'world','viewProjection','projection','inverseView'} and matrix is not None:
                        expected = struct.unpack('<16f',struct.pack('<16f',*(x for column in matrix for x in column)))
                        # JSON serializes negative zero as zero; compare float
                        # values here, while payload SHA retains the exact bits.
                        assert struct.unpack_from('<16f',data,field['offset']) == expected, field['name']
            for p in material['passes']:
                for binding in p['textureBindings']:
                    name = binding['name']
                    texture = batch['textureOverrides'].get(name, material['textures'].get(name,'__white'))
                    assert texture in textures, 'Unresolved texture: '+texture
        if 'builderInput' in frame:
            supplied = frame['builderInput']; builder_inputs.append(supplied)
            assert {'pose','scroll','entryCount','selectableTints','desktopSettings'} <= supplied.keys()
            assert math.isfinite(supplied['scroll']) and supplied['entryCount'] >= 0
            assert set(supplied['pose']['transforms']) <= nodes
            assert all(len(v) == 4 and all(math.isfinite(x) for x in v) for v in supplied['selectableTints'].values())
        if frame['nativeOverlayStateIncluded']:
            assert frame['nativeLayers']['children']
            assert 'Endministrator' in frame['nativeProfileCaptions'] and 'UID: 1000000000' in frame['nativeProfileCaptions']
            visible.update(n['target'] for n in frame['nativeNavigation'] if n['verifiedHitPoint'] is not None)
    assert visible == {m['id'] for m in report['modules']} and len(visible) == 24
    if 'desktopTemplateFrames' in report:
        assert report['desktopTemplateFrames'] == template_names
        assert len(template_names) == len(set(template_names)) <= 64
        assert '__ui_default_clip' in materials, 'Original hover template material was omitted'
    assert any('-opening-' in n for n in names) and any('-closing-' in n for n in names)
    animation = json.loads(blob(report['animation']))
    assert animation['library']['clips'] and len(animation['scene']['nodes']) > 0
    assert animation['playback']['finiteEase'] == 'OutQuad'
    if 'mountedDocument' in animation:
        mounted = animation['mountedDocument']
        node_ids = {node['id'] for node in animation['scene']['nodes']}
        assert set(mounted['components']) <= node_ids
        assert mounted['components'] and mounted['buttons'] and mounted['animators']
        assert all(button['node_id'] in node_ids for button in mounted['buttons'])
        assert all(animator['root_node_id'] in node_ids for animator in mounted['animators'])
    if 'frameBuilder' in animation:
        builder = animation['frameBuilder']
        assert 'includeDomain=false' in builder['scope'] and 'includeSourceText=false' in builder['scope']
        assert {'sprites','sourceSprites','textureSizes','materials','materialVariants','materialPropertyTypes',
                'sourceMeshNames','profileNodeIDs','defaultSelectableTints','desktopSettings'} <= builder.keys()
        assert builder_inputs and any('-arbitrary-' in n for n in names)
        assert set(builder['profileNodeIDs']) <= node_ids
        if 'desktopTextureDependencies' in builder:
            dependencies = builder['desktopTextureDependencies']
            assert dependencies == sorted(desktop_texture_dependencies(animation))
            assert set(dependencies) <= textures.keys(), 'Potential live desktop texture was not exported'
            # This source hover graphic is authored inactive and absent from
            # the original frozen frames, but is revealed by button animation.
            hover = 'CAB-b0b39b6e8de72f174d57a79fadd6af3b:2717747845825510916'
            assert hover in dependencies
        if builder.get('profileHover') is not None:
            profile = builder['profileHover']
            assert profile['rootID'] in profile['nodeIDs']
            assert set(profile['nodeIDs']) == set(builder['profileNodeIDs'])
            assert set(profile['buttonIDs']) <= set(profile['nodeIDs']) and profile['buttonIDs']
            assert len(profile['buttonIDs']) == len(set(profile['buttonIDs']))
        if 'ambientRotationNodes' in builder:
            ambient_ids = set(builder['ambientRotationNodes'])
            assert ambient_ids and ambient_ids <= node_ids
            ambient_inputs = [v for v in builder_inputs if 'ambientPose' in v]
            assert len(ambient_inputs) >= 4
            for supplied in ambient_inputs:
                ambient = supplied['ambientPose']
                assert set(ambient['transforms']) == ambient_ids
                assert ambient['properties'] == {} and ambient['unbound'] == [] and ambient['unregistered'] == []
                assert all(len(value['rotation']) == 4 and value['components'] == {}
                           and all(v is None for k, v in value.items() if k not in ('rotation', 'components'))
                           for value in ambient['transforms'].values())
                assert len(supplied['canvasResolution']) == 2 and all(v > 0 for v in supplied['canvasResolution'])
        for sprite in list(builder['sprites'].values()) + list(builder['sourceSprites'].values()):
            assert len(sprite['size']) == 2 and sprite['pixelsPerUnit'] > 0
            assert all(len(sprite[key]) == 4 for key in ('padding','border','outer','inner'))
            assert sprite['textureID'] in builder['textureSizes']
        for base, variants in builder['materialVariants'].items():
            assert set(variants) <= {'00','01','10','11'}
            assert all(key in builder['materialPropertyTypes'] for key in variants.values())
    for raster in report['nativeRasterAssets']:
        data = blob(raster)
        assert raster['path'] == raster['file'] and data.startswith(b'\x89PNG\r\n\x1a\n')
    assert load(root/'instrumentation.json')['productionSourceModified'] is False
    for oracle in load(root/'verification-oracles.json')['frames']:
        data = blob(oracle)
        assert len(data) == oracle['rowBytes']*oracle['height']
        assert oracle['rowBytes'] >= oracle['width']*4 and oracle['blackMatteApplied'] is False and oracle['desktopCapture'] is False
    if source_root:
        provenance = load(root/'provenance.json')
        for group in ('sourceAndResourceSHA256','exporterSHA256'):
            for path, digest in provenance[group].items():
                assert hashlib.sha256((source_root/path).read_bytes()).hexdigest() == digest, 'Changed during export: '+path
    return report, len(seen_blobs)


def self_test(source_root):
    path = source_root/'windows/tools/export_shell_packet_instrument.py'
    spec = importlib.util.spec_from_file_location('instrument',path); module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
    source = (source_root/'Sources/HUDSourceMetalRenderer.swift').read_text(encoding="utf-8")
    hooks = (source_root/'windows/tools/export_shell_packet_hooks.swift').read_text(encoding="utf-8")
    result = module.instrument(source,hooks)
    assert result.count('shellPacketRecordSampler(') == 5 and result.count('shellPacketRecordDepth(') == 2
    for altered in (source+'\n', source.replace('private struct Vertex {','private struct Vertex2 {',1)):
        try: module.instrument(altered,hooks)
        except ValueError: pass
        else: raise AssertionError('Changed source accepted')
    assert (source_root/'Sources/HUDSourceMetalRenderer.swift').read_text(encoding="utf-8") == source
    print('PASS hash-locked build-only instrumentation checks')


if __name__ == '__main__':
    parser = argparse.ArgumentParser(); parser.add_argument('export',type=pathlib.Path,nargs='?')
    parser.add_argument('--source-root',type=pathlib.Path); parser.add_argument('--self-test',action='store_true')
    args = parser.parse_args()
    if args.self_test: self_test(args.source_root or pathlib.Path(__file__).resolve().parents[2])
    if args.export:
        report,count = verify(args.export,args.source_root)
        print(f"PASS actual desktop shell packet: {len(report['frames'])} frames, {count} checked byte assets")
