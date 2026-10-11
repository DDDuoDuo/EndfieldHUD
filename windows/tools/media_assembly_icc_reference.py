#!/usr/bin/env python3
"""Colour-managed stills through the unchanged original Media Assembly path.

Writes windows/tests/fixtures/media-assembly-icc-source.json. Synthetic,
owned PNG files carry synthetic ICC profiles (iCCP chunk) that this tool
builds from public colorimetry only (primary chromaticities, the D65 and D50
white points, the Bradford adaptation and the ICC.1 matrix/TRC and grey TRC
tag types); no system or vendor profile is read or copied. The oracle
compiles the pinned Engine.apply (shared with
media_assembly_processor_reference.py) and decodes each PNG with the same
public calls as the pinned source:

  export   CGImageSourceCreateImageAtIndex(kCGImageSourceShouldCacheImmediately)
           -> CIImage(cgImage:).oriented(forExifOrientation:) -> Engine.apply
           -> CIContext(working sRGB).createCGImage(.RGBA8, sRGB)
  preview  CGImageSourceCreateThumbnailAtIndex(FromImageAlways, WithTransform,
           MaxPixelSize 1024, ShouldCacheImmediately) -> CIImage(cgImage:)
           -> Engine.apply -> the same createCGImage (identity only)

Each image also records its identity export rendered as unclamped RGBAf in
the working space. Those floats are the black-box evidence for the Windows
source conversion (modules/media_assembly_processor.hpp,
MediaAssemblySourceColour) and mark the pixels a wide-gamut profile places
outside sRGB (Core Image keeps them in its extended working range until the
final RGBA8 store, so edits can bring them back).

Profiles (8-bit PNG; every row of the 256x4 image holds each code value once
per channel: r=x, g=(7x+64y+3)%256, b=(13x+32y+101)%256; grey v=(x+64y)%256):
  p3-gamma22    Display P3 primaries (SMPTE EG 432-1), D65, 'curv' gamma 563/256
  p3-gamma18    the same primaries, 'curv' gamma 461/256
  bt2020-gamma26  ITU-R BT.2020 primaries, D65, 'curv' gamma 666/256
  p3-table22    P3 primaries, a 1024-entry 'curv' table of x^2.2
  adobe-table   Adobe RGB (1998) primaries, a 1024-entry table of the sRGB transfer
  display-p3-para  ICC v4: P3 primaries, 'para' type 3 with the sRGB transfer
                constants, 'mluc' text and a 'chad' tag
  srgb-matrix   sRGB primaries with the sRGB table (a profile describing sRGB)
  grey-gamma22  a grey PNG with a 'GRAY' profile ('kTRC' gamma 563/256)
  untagged      the RGB pixels without a profile (sRGB by definition)
Probes (identity only, one 256x1 row with every code per channel, P3
primaries) separate the curve handling: u8Fixed8 gammas next to 1.2, 1.7,
2.3 and around 1.8 and 2.2 (up to 7/256 off), and 'para' curves just off
the sRGB transfer (threshold 0.04 instead of 0.04045; exponent 2.42).
"""
from pathlib import Path
import base64, importlib.util, json, struct, subprocess, sys, tempfile, zlib

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('media_assembly_processor_reference', here / 'media_assembly_processor_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

WIDTH, HEIGHT = 256, 4
D65 = (0.3127, 0.3290)
D50_XYZ = (0.9642, 1.0, 0.8249)          # ICC PCS illuminant
BRADFORD = [[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367], [0.0389, -0.0685, 1.0296]]
P3 = ((0.680, 0.320), (0.265, 0.690), (0.150, 0.060))
SRGB = ((0.640, 0.330), (0.300, 0.600), (0.150, 0.060))
ADOBE = ((0.640, 0.330), (0.210, 0.710), (0.150, 0.060))
BT2020 = ((0.708, 0.292), (0.170, 0.797), (0.131, 0.046))
PROFILES = [
    ('p3-gamma22', 'rgb', P3, ('gamma', 563)),
    ('p3-gamma18', 'rgb', P3, ('gamma', 461)),
    ('bt2020-gamma26', 'rgb', BT2020, ('gamma', 666)),
    ('p3-table22', 'rgb', P3, ('table', 'power22')),
    ('adobe-table', 'rgb', ADOBE, ('table', 'srgb')),
    ('display-p3-para', 'rgb4', P3, ('para', 'srgb')),
    ('srgb-matrix', 'rgb', SRGB, ('table', 'srgb')),
    ('grey-gamma22', 'grey', None, ('gamma', 563)),
    ('untagged', 'none', None, None),
]
PROBES = [
    ('gamma-307', 'rgb', P3, ('gamma', 307)),
    ('gamma-435', 'rgb', P3, ('gamma', 435)),
    ('gamma-564', 'rgb', P3, ('gamma', 564)),
    ('gamma-589', 'rgb', P3, ('gamma', 589)),
    ('gamma-561', 'rgb', P3, ('gamma', 561)),
    ('gamma-566', 'rgb', P3, ('gamma', 566)),
    ('gamma-567', 'rgb', P3, ('gamma', 567)),
    ('gamma-570', 'rgb', P3, ('gamma', 570)),
    ('gamma-458', 'rgb', P3, ('gamma', 458)),
    ('gamma-463', 'rgb', P3, ('gamma', 463)),
    ('gamma-464', 'rgb', P3, ('gamma', 464)),
    ('para-srgb-threshold', 'rgb4', P3, ('para', (2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04))),
    ('para-exponent-2.42', 'rgb4', P3, ('para', (2.42, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045))),
]
# Edits that move colours: identity, two that bring wide-gamut values back
# towards the sRGB cube, and one with a LUT (clamped lookup input).
EDITS = [
    ('identity', {}),
    ('saturation0.5', {'saturation': 0.5}),
    ('exposure-1', {'exposure': -1.0}),
    ('filter3', {'filter': 'filter_3'}),
]
SRGB_PARA = (2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045)


def inverse(m):
    a, b, c = m[0]; d, e, f = m[1]; g, h, i = m[2]
    det = a * (e * i - f * h) - b * (d * i - f * g) + c * (d * h - e * g)
    return [[(e * i - f * h) / det, (c * h - b * i) / det, (b * f - c * e) / det],
            [(f * g - d * i) / det, (a * i - c * g) / det, (c * d - a * f) / det],
            [(d * h - e * g) / det, (b * g - a * h) / det, (a * e - b * d) / det]]


def multiply(a, b):
    return [[sum(a[r][k] * b[k][c] for k in range(3)) for c in range(3)] for r in range(3)]


def apply(m, v):
    return [sum(m[r][k] * v[k] for k in range(3)) for r in range(3)]


def xyz(xy):
    x, y = xy
    return [x / y, 1.0, (1 - x - y) / y]


def adaptation():
    """Bradford D65 -> D50 (the 'chad' matrix)."""
    source, target = apply(BRADFORD, xyz(D65)), apply(BRADFORD, list(D50_XYZ))
    return multiply(inverse(BRADFORD), multiply([[target[0] / source[0], 0, 0], [0, target[1] / source[1], 0], [0, 0, target[2] / source[2]]], BRADFORD))


def colorants(primaries):
    """RGB -> XYZ (D65) from chromaticities, Bradford-adapted to the D50 PCS:
    the columns are the rXYZ, gXYZ and bXYZ tags."""
    p = [[xyz(c)[r] for c in primaries] for r in range(3)]
    scale = apply(inverse(p), xyz(D65))
    return multiply(adaptation(), [[p[r][c] * scale[c] for c in range(3)] for r in range(3)])


def s15(v):
    return struct.pack('>i', int(round(v * 65536)))


def srgb_decode(v):
    return v / 12.92 if v <= 0.04045 else ((v + 0.055) / 1.055) ** 2.4


def tag_xyz(values):
    return b'XYZ ' + b'\0' * 4 + b''.join(s15(v) for v in values)


def tag_curve(curve):
    kind, value = curve
    if kind == 'gamma':
        return b'curv' + b'\0' * 4 + struct.pack('>IH', 1, value)
    if kind == 'para':
        return b'para' + b'\0' * 4 + struct.pack('>HH', 3, 0) + b''.join(s15(v) for v in (SRGB_PARA if value == 'srgb' else value))
    shape = srgb_decode if value == 'srgb' else (lambda v: v ** 2.2)
    entries = [int(round(shape(k / 1023) * 65535)) for k in range(1024)]
    return b'curv' + b'\0' * 4 + struct.pack('>I', len(entries)) + struct.pack('>%dH' % len(entries), *entries)


def tag_desc(text):
    ascii = text.encode('ascii') + b'\0'
    return b'desc' + b'\0' * 4 + struct.pack('>I', len(ascii)) + ascii + struct.pack('>II', 0, 0) + struct.pack('>HB', 0, 0) + b'\0' * 67


def tag_mluc(text):
    utf16 = text.encode('utf-16-be')
    return b'mluc' + b'\0' * 4 + struct.pack('>II', 1, 12) + b'enUS' + struct.pack('>II', len(utf16), 28) + utf16


def assemble(version, space, tags):
    table_size = 4 + 12 * len(tags)
    offset = 128 + table_size
    table, data, shared = b'', b'', {}
    for signature, payload in tags:
        if payload in shared:
            at = shared[payload]
        else:
            while (offset + len(data)) % 4:
                data += b'\0'
            at = offset + len(data)
            shared[payload] = at
            data += payload
        table += signature + struct.pack('>II', at, len(payload))
    while len(data) % 4:
        data += b'\0'
    size = 128 + table_size + len(data)
    header = (struct.pack('>I', size) + b'\0' * 4 + struct.pack('>I', version) + b'mntr' + space + b'XYZ '
              + struct.pack('>6H', 2026, 10, 11, 0, 0, 0) + b'acsp' + b'\0' * 4 + b'\0' * 4 + b'\0' * 4 + b'\0' * 4 + b'\0' * 8
              + struct.pack('>I', 0) + b''.join(s15(v) for v in D50_XYZ) + b'\0' * 4 + b'\0' * 16 + b'\0' * 28)
    assert len(header) == 128
    return header + struct.pack('>I', len(tags)) + table + data


def profile(name, kind, primaries, curve):
    """ICC.1 display profile with deterministic bytes: v2.4 RGB matrix/TRC,
    v4.3 RGB matrix/TRC ('rgb4') or v2.4 grey TRC."""
    trc = tag_curve(curve)
    if kind == 'grey':
        return assemble(0x02400000, b'GRAY', [(b'desc', tag_desc('EndfieldHUD synthetic ' + name)), (b'cprt', b'text' + b'\0' * 4 + b'No copyright, synthetic test profile\0'),
                                             (b'wtpt', tag_xyz(D50_XYZ)), (b'kTRC', trc)]), None
    m = colorants(primaries)
    common = [(b'wtpt', tag_xyz(D50_XYZ)), (b'rXYZ', tag_xyz([m[r][0] for r in range(3)])), (b'gXYZ', tag_xyz([m[r][1] for r in range(3)])),
              (b'bXYZ', tag_xyz([m[r][2] for r in range(3)])), (b'rTRC', trc), (b'gTRC', trc), (b'bTRC', trc)]
    if kind == 'rgb4':
        chad = b'sf32' + b'\0' * 4 + b''.join(s15(v) for row in adaptation() for v in row)
        return assemble(0x04300000, b'RGB ', [(b'desc', tag_mluc('EndfieldHUD synthetic ' + name)), (b'cprt', tag_mluc('No copyright, synthetic test profile')),
                                             (b'chad', chad)] + common), m
    return assemble(0x02400000, b'RGB ', [(b'desc', tag_desc('EndfieldHUD synthetic ' + name)), (b'cprt', b'text' + b'\0' * 4 + b'No copyright, synthetic test profile\0')] + common), m


def chunk(kind, payload):
    return struct.pack('>I', len(payload)) + kind + payload + struct.pack('>I', zlib.crc32(kind + payload) & 0xffffffff)


def png(width, height, samples, channels, icc=None, name=b'synthetic'):
    row = width * channels
    raw = b''.join(b'\0' + bytes(samples[y * row:(y + 1) * row]) for y in range(height))
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2 if channels == 3 else 0, 0, 0, 0))
    if icc is not None:
        out += chunk(b'iCCP', name + b'\0\0' + zlib.compress(icc, 9))
    return out + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


def synthetic_rgb(height=HEIGHT):
    return [v for y in range(height) for x in range(WIDTH) for v in (x, (7 * x + 64 * y + 3) % 256, (13 * x + 32 * y + 101) % 256)]


def synthetic_grey():
    return [(x + 64 * y) % 256 for y in range(HEIGHT) for x in range(WIDTH)]


DRIVER = r'''
let color=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
struct Edit:Decodable{let name:String;let adjustments:MediaAssemblyAdjustments}
struct Request:Decodable{let files:[String];let probes:Int;let edits:[Edit]}
let request=try JSONDecoder().decode(Request.self,from:Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[2])))
func bytes(_ image:CIImage)->String{
 let cg=context.createCGImage(image,from:image.extent,format:.RGBA8,colorSpace:color)!
 let data=cg.dataProvider!.data! as Data;var packed=Data()
 for y in 0..<cg.height{packed.append(data.subdata(in:y*cg.bytesPerRow..<(y*cg.bytesPerRow+cg.width*4)))}
 return packed.base64EncodedString()
}
var images:[[String:Any]]=[]
for (index,file) in request.files.enumerated(){
 let probe=index>=request.files.count-request.probes
 let url=URL(fileURLWithPath:file)
 // Export decode (pinned MediaAssemblyEngine.export, still branch).
 let source=CGImageSourceCreateWithURL(url as CFURL,[kCGImageSourceShouldCache:false] as CFDictionary)!
 let decoded=CGImageSourceCreateImageAtIndex(source,0,[kCGImageSourceShouldCacheImmediately:true] as CFDictionary)!
 let properties=CGImageSourceCopyPropertiesAtIndex(source,0,nil) as? [CFString:Any]
 let orientation=(properties?[kCGImagePropertyOrientation] as? NSNumber)?.int32Value ?? 1
 let exported=CIImage(cgImage:decoded).oriented(forExifOrientation:orientation)
 // Preview decode (pinned MediaAssemblyEngine.preview, still branch).
 let thumbnail=CGImageSourceCreateThumbnailAtIndex(source,0,[kCGImageSourceCreateThumbnailFromImageAlways:true,
   kCGImageSourceCreateThumbnailWithTransform:true,kCGImageSourceThumbnailMaxPixelSize:1024,
   kCGImageSourceShouldCacheImmediately:true] as CFDictionary)!
 let previewed=CIImage(cgImage:thumbnail)
 var cases:[[String:Any]]=[]
 for edit in request.edits{
  let e=try Original.apply(edit.adjustments,to:exported,includeStickers:true)
  var record:[String:Any]=["name":edit.name,"export":bytes(e),"width":Int(e.extent.width),"height":Int(e.extent.height)]
  if edit.name=="identity"{record["preview"]=bytes(try Original.apply(edit.adjustments,to:previewed,includeStickers:false))}
  cases.append(record)
  if probe{break}
 }
 // Identity export as unclamped working-space floats (conversion evidence).
 let w=Int(exported.extent.width),h=Int(exported.extent.height)
 var floats=[Float](repeating:0,count:w*h*4)
 floats.withUnsafeMutableBytes{context.render(exported,toBitmap:$0.baseAddress!,rowBytes:w*16,bounds:exported.extent,format:.RGBAf,colorSpace:color)}
 images.append(["file":url.lastPathComponent,"decodedColorSpace":(decoded.colorSpace?.name as String?) ?? "",
   "decodedModel":Int(decoded.colorSpace?.model.rawValue ?? -1),"cases":cases,
   "identityFloat":floats.withUnsafeBytes{Data($0)}.base64EncodedString()])
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["images":images],options:[.sortedKeys]))
'''


def main():
    output = Path(sys.argv[1])
    images = []
    with tempfile.TemporaryDirectory(prefix='endfield-media-icc-') as tmp:
        directory = Path(tmp)
        files = []
        for name, kind, primaries, curve in PROFILES + PROBES:
            probe = (name, kind, primaries, curve) in PROBES
            height = 1 if probe else HEIGHT
            record = {'name': name, 'kind': kind, 'height': height, 'probe': probe}
            if kind == 'none':
                data = png(WIDTH, height, synthetic_rgb(height), 3)
            else:
                icc, matrix = profile(name, kind, primaries, curve)
                data = png(WIDTH, height, synthetic_grey() if kind == 'grey' else synthetic_rgb(height), 1 if kind == 'grey' else 3, icc, name.encode())
                record.update({'profile': base64.b64encode(icc).decode(), 'curve': [curve[0], list(curve[1]) if isinstance(curve[1], tuple) else curve[1]]})
                if matrix:
                    record.update({'primaries': [list(p) for p in primaries], 'white': list(D65), 'colorants': [[float('%.9g' % v) for v in row] for row in matrix]})
            path = directory / (name + '.png')
            path.write_bytes(data)
            files.append(str(path))
            record['png'] = base64.b64encode(data).decode()
            images.append(record)
        edits = []
        for name, values in EDITS:
            adjustments = {'crop': {'x': 0, 'y': 0, 'width': 1, 'height': 1}, 'rotationQuarterTurns': 0, 'mirrored': False,
                           'brightness': 0.0, 'contrast': 1.0, 'saturation': 1.0, 'temperature': 6500.0, 'tint': 0.0,
                           'highlights': 1.0, 'shadows': 0.0, 'exposure': 0.0, 'curve': [0, 0.25, 0.5, 0.75, 1],
                           'levelsBlack': 0.0, 'levelsWhite': 1.0, 'levelsGamma': 1.0, 'trimStart': 0.0, 'filter': 'none', 'stickers': []}
            adjustments.update(values)
            edits.append({'name': name, 'adjustments': adjustments})
        (directory / 'request.json').write_text(json.dumps({'files': files, 'probes': len(PROBES), 'edits': edits}))
        sources = reference.pinned_sources()
        oracle = reference.build_oracle(sources, DRIVER, directory)
        result = json.loads(subprocess.check_output([str(oracle), str(reference.assets), str(directory / 'request.json')]))
    for image, rendered in zip(images, result['images']):
        image.update({k: rendered[k] for k in ('decodedColorSpace', 'decodedModel', 'cases', 'identityFloat')})
    fixture = {'sourceCommit': reference.commit, 'sourcePins': reference.source_pins(sources), 'usesAppOrWindow': False,
               'width': WIDTH, 'height': HEIGHT, 'edits': [{'name': n, 'adjustments': v} for n, v in EDITS], 'images': images,
               'synthetic': 'RGB r=x, g=(7x+64y+3)%256, b=(13x+32y+101)%256; grey v=(x+64y)%256; 8-bit PNG, top-left rows',
               'scope': 'Synthetic ICC matrix/TRC and grey profiles built from public colorimetry, embedded in owned PNG files, decoded and edited '
                        'with the same public calls as the pinned still preview and export. No system or vendor profile, user data or app.'}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(fixture, sort_keys=True, separators=(',', ':')) + '\n')
    print(output, len(images), output.stat().st_size)


if __name__ == '__main__':
    main()
