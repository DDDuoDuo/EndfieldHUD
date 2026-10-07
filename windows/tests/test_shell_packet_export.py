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
    for desc in report['frames']:
        frame = json.loads(blob(desc))
        assert desc['name'] not in names; names.add(desc['name'])
        nodes = {n['id'] for n in frame['nodes']}
        assert len(nodes) == len(frame['nodes']) and nodes
        assert [b['index'] for b in frame['batches']] == list(range(len(frame['batches'])))
        assert frame['gpuCamera']['sceneColorMode'] == 'directLDR'
        for batch in frame['batches']:
            assert batch['mesh'] in meshes and batch['material'] in materials
            assert batch['sourceMesh'] == meshes[batch['mesh']]['sourceMesh']
            assert len(batch['gpuVertexColor']) == 4 and all(math.isfinite(x) for x in batch['gpuVertexColor'])
            assert batch['sourceNodeID'] is None or batch['sourceNodeID'] in nodes
            if batch['indexRange'] is not None:
                a,b = batch['indexRange']; assert 0 <= a < b <= meshes[batch['mesh']]['indexCount']
            material = materials[batch['material']]
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
        if frame['nativeOverlayStateIncluded']:
            assert frame['nativeLayers']['children']
            assert 'Endministrator' in frame['nativeProfileCaptions'] and 'UID: 1000000000' in frame['nativeProfileCaptions']
            visible.update(n['target'] for n in frame['nativeNavigation'] if n['verifiedHitPoint'] is not None)
    assert visible == {m['id'] for m in report['modules']} and len(visible) == 24
    assert any('-opening-' in n for n in names) and any('-closing-' in n for n in names)
    animation = json.loads(blob(report['animation']))
    assert animation['library']['clips'] and len(animation['scene']['nodes']) > 0
    assert animation['playback']['finiteEase'] == 'OutQuad'
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
