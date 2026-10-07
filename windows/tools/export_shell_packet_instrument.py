#!/usr/bin/env python3
"""Add read-only export hooks to a hash-locked build copy, never Sources."""
import hashlib
import json
import pathlib
import sys

EXPECTED_RENDERER_SHA256 = 'b6cb9fdff4d546e480f5bda515ba6c8f4b18d5e02b70a66c8635bb81a1f9fa32'


def instrument(source: str, hooks: str) -> str:
    if hashlib.sha256(source.encode()).hexdigest() != EXPECTED_RENDERER_SHA256:
        raise ValueError('Mac renderer source changed: review and re-anchor instrumentation first')
    def insert(anchor, addition):
        nonlocal source
        if source.count(anchor) != 1:
            raise ValueError(f'Expected exactly one instrumentation anchor: {anchor}')
        source = source.replace(anchor, anchor + '\n    #if HUD_SHELL_PACKET_EXPORT\n' + addition + '\n    #endif')
    insert('        textureAssets[name] = TextureAsset(texture: texture, sampler: state)',
           '        shellPacketRecordSampler(state, sampler)')
    insert('        textureAssets[name] = TextureAsset(texture: texture, sampler: sampler)',
           '        shellPacketRecordSampler(sampler, descriptor)')
    insert('        textureAssets["__white"] = TextureAsset(texture: white, sampler: sampler)',
           '        shellPacketRecordSampler(sampler, MTLSamplerDescriptor())')
    insert('        guard let state = device.makeSamplerState(descriptor: sampler) else { throw Failure.message("Cannot create source sampler") }',
           '        shellPacketRecordSampler(state, sampler)')
    insert('            guard let depthState = device.makeDepthStencilState(descriptor: depth) else { throw Failure.message("Cannot create source depth/stencil state") }',
           '            shellPacketRecordDepth(depthState, depth)')
    anchor = '    private struct Vertex {'
    if source.count(anchor) != 1:
        raise ValueError('Missing hook insertion anchor')
    return source.replace(anchor, hooks + '\n' + anchor)


def main():
    root, output = map(pathlib.Path, sys.argv[1:])
    renderer = root / 'Sources/HUDSourceMetalRenderer.swift'
    hooks = root / 'windows/tools/export_shell_packet_hooks.swift'
    original = renderer.read_text(encoding="utf-8")
    # read_text universal-newline conversion is deliberately detected by hash.
    generated = instrument(original, hooks.read_text(encoding="utf-8"))
    output.mkdir(parents=True, exist_ok=True)
    (output / renderer.name).write_text(generated)
    reference = root / 'windows/tools/export_macos_reference.swift'
    helper = reference.read_text(encoding="utf-8")
    if helper.count('\n@main\n') != 1:
        raise ValueError('Reference helper entry-point anchor changed')
    helper = helper.replace('\n@main\n', '\n', 1)
    (output / 'MacOSReferenceHelpers.swift').write_text(helper)
    (output / 'instrumentation.json').write_text(json.dumps({
        'sourceSHA256': hashlib.sha256(renderer.read_bytes()).hexdigest(),
        'hooksSHA256': hashlib.sha256(hooks.read_bytes()).hexdigest(),
        'instrumenterSHA256': hashlib.sha256(pathlib.Path(__file__).read_bytes()).hexdigest(),
        'instrumentedRendererSHA256': hashlib.sha256(generated.encode()).hexdigest(),
        'referenceHelperSHA256': hashlib.sha256(reference.read_bytes()).hexdigest(),
        'referenceHelperBuildCopySHA256': hashlib.sha256(helper.encode()).hexdigest(),
        'productionSourceModified': False,
        'defines': ['HUD_SOURCE_RENDER_PREVIEW', 'HUD_WATCH_MOTION_PREVIEW', 'HUD_SHELL_PACKET_EXPORT'],
    }, indent=2, sort_keys=True) + '\n')


if __name__ == '__main__':
    main()
