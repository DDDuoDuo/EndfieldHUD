#!/usr/bin/env python3
"""How the unchanged original Media Assembly decodes JPEG stills.

Writes windows/tests/fixtures/media-assembly-jpeg-source.json. Owned
synthetic JPEG files (a smooth gradient half and a half of saturated 4x4
colour cells crossed by inverted diagonal pixels, so chroma edges sit on and
between MCU boundaries) are encoded here with Pillow's libjpeg-turbo in the
layouts cameras and phones write: 4:2:0, 4:2:2 and 4:4:4 baseline, 4:2:0
progressive, 4:2:0 with partial MCUs, greyscale and an EXIF orientation 6
4:2:0 file. The JPEG bytes are stored in the fixture, so the encoder only
chooses the inputs; the oracle compiles the pinned Engine.apply (shared with
media_assembly_processor_reference.py) and decodes each file with the same
public calls as the pinned still paths:

  export   CGImageSourceCreateImageAtIndex(kCGImageSourceShouldCacheImmediately)
           -> CIImage(cgImage:).oriented(forExifOrientation:) -> Engine.apply
           (identity) -> CIContext(working sRGB).createCGImage(.RGBA8, sRGB)
  preview  CGImageSourceCreateThumbnailAtIndex(FromImageAlways, WithTransform,
           MaxPixelSize 1024, ShouldCacheImmediately) -> CIImage(cgImage:)
           -> Engine.apply (identity) -> the same createCGImage

Every file is smaller than the 1024 px preview bound, so both paths decode
at full size and the records isolate the JPEG decoder (IDCT, chroma
upsampling and YCbCr conversion as Core Image receives them) from the
thumbnail resampler. The Pillow decode of each file is recorded as a
summary only (largest and mean channel difference from the export), as a
reference point for an independent libjpeg-turbo decoder.

The thumbnail decoder of the preview path also departs from the full
decoder of the export path in some high-contrast 8 x 8 blocks, whatever
the chroma layout. To locate those departures without reading coefficients,
every colour file gets a grey companion with the same luma coefficients:
its samples are libjpeg's own RGB -> Y conversion (jccolor.c, 16-bit fixed
point) of the same pixels, encoded with the same quality, progression and
EXIF orientation (Pillow decodes the companion's samples identically to the
colour file's Y plane). A grey JPEG has no chroma, so wherever the two
paths still disagree on the companion, the thumbnail decoder departed; those
blocks (output frame, 8-pixel aligned) and their largest difference are
recorded per image as thumbnailDepartures [x, y, worst].
"""
from pathlib import Path
import base64, importlib.util, io, json, subprocess, sys, tempfile

from PIL import Image, features

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('media_assembly_processor_reference', here / 'media_assembly_processor_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

PALETTE = [(255, 0, 0), (0, 255, 0), (0, 0, 255), (255, 255, 0), (0, 255, 255), (255, 0, 255)]
# name, width, height, Pillow save options, mode, EXIF orientation
CASES = [
    ('q90-420', 96, 64, {'quality': 90, 'subsampling': 2}, 'RGB', 1),
    ('q90-422', 96, 64, {'quality': 90, 'subsampling': 1}, 'RGB', 1),
    ('q95-444', 96, 64, {'quality': 95, 'subsampling': 0}, 'RGB', 1),
    ('q85-420-progressive', 96, 64, {'quality': 85, 'subsampling': 2, 'progressive': True}, 'RGB', 1),
    ('q90-420-partial-mcu', 61, 45, {'quality': 90, 'subsampling': 2}, 'RGB', 1),
    ('q90-grey', 96, 64, {'quality': 90}, 'L', 1),
    ('q90-420-orientation6', 96, 64, {'quality': 90, 'subsampling': 2}, 'RGB', 6),
]


def content(width, height):
    """Left half: smooth gradients; right half: saturated 4x4 cells with
    every ninth diagonal pixel inverted (top-left rows, RGB)."""
    pixels = []
    half = width // 2
    for y in range(height):
        for x in range(width):
            if x < half:
                pixels.append((255 * x // max(1, half - 1), 255 * y // max(1, height - 1), (7 * x + 3 * y) % 256))
            else:
                r, g, b = PALETTE[((x // 4) + (y // 4)) % len(PALETTE)]
                if (x + y) % 9 == 0:
                    r, g, b = 255 - r, 255 - g, 255 - b
                pixels.append((r, g, b))
    return pixels


def luma(pixels):
    """libjpeg rgb_ycc_convert luma (jccolor.c: SCALEBITS 16, ONE_HALF)."""
    fix = lambda v: int(v * 65536 + 0.5)
    return [(fix(0.299) * r + fix(0.587) * g + fix(0.114) * b + 32768) >> 16 for r, g, b in pixels]


def encode(name, width, height, options, mode, orientation, companion=False):
    image = Image.new('RGB', (width, height))
    image.putdata(content(width, height))
    if mode == 'L':
        image = image.convert('L')
    elif companion:
        image = Image.new('L', (width, height))
        image.putdata(luma(content(width, height)))
    save = dict(options)
    if companion:
        save.pop('subsampling', None)
    if orientation != 1:
        exif = Image.Exif()
        exif[0x0112] = orientation
        save['exif'] = exif.tobytes()
    out = io.BytesIO()
    image.save(out, 'JPEG', **save)
    return out.getvalue()


DRIVER = r'''
let color=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
func bytes(_ image:CIImage)->Data{
 let cg=context.createCGImage(image,from:image.extent,format:.RGBA8,colorSpace:color)!
 let data=cg.dataProvider!.data! as Data;var packed=Data()
 for y in 0..<cg.height{packed.append(data.subdata(in:y*cg.bytesPerRow..<(y*cg.bytesPerRow+cg.width*4)))}
 return packed
}
var images:[[String:Any]]=[]
for file in CommandLine.arguments.dropFirst(2){
 let url=URL(fileURLWithPath:file)
 // Export decode (pinned MediaAssemblyEngine.export, still branch).
 let source=CGImageSourceCreateWithURL(url as CFURL,[kCGImageSourceShouldCache:false] as CFDictionary)!
 let decoded=CGImageSourceCreateImageAtIndex(source,0,[kCGImageSourceShouldCacheImmediately:true] as CFDictionary)!
 let properties=CGImageSourceCopyPropertiesAtIndex(source,0,nil) as? [CFString:Any]
 let orientation=(properties?[kCGImagePropertyOrientation] as? NSNumber)?.int32Value ?? 1
 let exported=try Original.apply(MediaAssemblyAdjustments(),to:CIImage(cgImage:decoded).oriented(forExifOrientation:orientation),includeStickers:true)
 // Preview decode (pinned MediaAssemblyEngine.preview, still branch).
 let thumbnail=CGImageSourceCreateThumbnailAtIndex(source,0,[kCGImageSourceCreateThumbnailFromImageAlways:true,
   kCGImageSourceCreateThumbnailWithTransform:true,kCGImageSourceThumbnailMaxPixelSize:1024,
   kCGImageSourceShouldCacheImmediately:true] as CFDictionary)!
 let previewed=try Original.apply(MediaAssemblyAdjustments(),to:CIImage(cgImage:thumbnail),includeStickers:false)
 let e=bytes(exported),p=bytes(previewed)
 var record:[String:Any]=["file":url.lastPathComponent,"width":Int(exported.extent.width),"height":Int(exported.extent.height),
   "previewWidth":Int(previewed.extent.width),"previewHeight":Int(previewed.extent.height),"orientation":Int(orientation),
   "decodedColorSpace":(decoded.colorSpace?.name as String?) ?? "","thumbnailColorSpace":(thumbnail.colorSpace?.name as String?) ?? "",
   "export":e.base64EncodedString(),"previewSameAsExport":e==p]
 if e != p {record["preview"]=p.base64EncodedString()}
 images.append(record)
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["images":images],options:[.sortedKeys]))
'''


def main():
    output = Path(sys.argv[1])
    images = []
    with tempfile.TemporaryDirectory(prefix='endfield-media-jpeg-') as tmp:
        directory = Path(tmp)
        files, companions = [], []
        for name, width, height, options, mode, orientation in CASES:
            data = encode(name, width, height, options, mode, orientation)
            path = directory / (name + '.jpg')
            path.write_bytes(data)
            files.append(str(path))
            decoded = Image.open(io.BytesIO(data))
            image = {'name': name, 'encodedWidth': width, 'encodedHeight': height, 'mode': mode, 'orientation': orientation,
                     'quality': options['quality'], 'subsampling': {0: '4:4:4', 1: '4:2:2', 2: '4:2:0'}.get(options.get('subsampling'), 'none'),
                     'progressive': bool(options.get('progressive')), 'jpeg': base64.b64encode(data).decode(),
                     '_pillow': decoded.convert('RGBA').tobytes(), '_pillowSize': decoded.size}
            if mode != 'L':
                grey = encode(name, width, height, options, mode, orientation, companion=True)
                # The companion's samples decode exactly like the colour file's Y plane.
                colour = Image.open(io.BytesIO(data))
                colour.draft('YCbCr', colour.size)
                assert colour.mode == 'YCbCr' and colour.getchannel(0).tobytes() == Image.open(io.BytesIO(grey)).tobytes(), name
                (directory / (name + '.luma.jpg')).write_bytes(grey)
                companions.append(str(directory / (name + '.luma.jpg')))
                image['lumaCompanion'] = base64.b64encode(grey).decode()
            images.append(image)
        sources = reference.pinned_sources()
        oracle = reference.build_oracle(sources, DRIVER, directory)
        result = json.loads(subprocess.check_output([str(oracle), str(reference.assets)] + files + companions))
    rendered_by_file = {r['file']: r for r in result['images']}
    for image in images:
        pillow, size = image.pop('_pillow'), image.pop('_pillowSize')
        rendered = rendered_by_file[image['name'] + '.jpg']
        image.update({k: v for k, v in rendered.items() if k != 'file'})
        # Thumbnail decoder departures: the grey companion (or a grey file
        # itself) through both paths, per 8-pixel output block.
        grey = rendered_by_file.get(image['name'] + '.luma.jpg', rendered)
        assert (grey['width'], grey['height']) == (image['width'], image['height'])
        departures = []
        if not grey['previewSameAsExport']:
            g_export, g_preview = base64.b64decode(grey['export']), base64.b64decode(grey['preview'])
            w, h = grey['width'], grey['height']
            for by in range(0, h, 8):
                for bx in range(0, w, 8):
                    worst = max(abs(g_export[(y * w + x) * 4 + c] - g_preview[(y * w + x) * 4 + c])
                                for y in range(by, min(h, by + 8)) for x in range(bx, min(w, bx + 8)) for c in range(3))
                    if worst:
                        departures.append([bx, by, worst])
        image['thumbnailDepartures'] = departures
        # Pillow does not apply the EXIF orientation: compare unrotated files only.
        if image['orientation'] == 1 and size == (image['width'], image['height']):
            export = base64.b64decode(image['export'])
            diffs = [abs(pillow[i] - export[i]) for i in range(len(export)) if i % 4 != 3]
            image['pillowVsExport'] = {'worst': max(diffs), 'mean': round(sum(diffs) / len(diffs), 4)}
    fixture = {'sourceCommit': reference.commit, 'sourcePins': reference.source_pins(sources), 'usesAppOrWindow': False,
               'encoder': 'Pillow %s (libjpeg-turbo %s)' % (Image.__version__, features.version('libjpeg_turbo')), 'images': images,
               'synthetic': 'left half r=255x/(w/2-1), g=255y/(h-1), b=(7x+3y)%256; right half 4x4 cells cycling red, green, blue, yellow, cyan, magenta, '
                            'inverted where (x+y)%9==0 (integer division, top-left rows)',
               'lumaCompanion': 'grey JPEG of libjpeg rgb_ycc_convert luma of the same pixels, same quality, progression and orientation '
                                '(same luma coefficients); thumbnailDepartures lists its 8-pixel output blocks where the preview and export paths differ',
               'scope': 'Owned synthetic JPEG files decoded with the same public calls as the pinned still preview and export (identity edits). '
                        'No user media, app or window.'}
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(fixture, sort_keys=True, separators=(',', ':')) + '\n')
    print(output, len(images), output.stat().st_size)
    for image in images:
        print(image['name'], image['width'], image['height'], image['decodedColorSpace'], 'preview==export' if image['previewSameAsExport'] else 'preview differs',
              image.get('pillowVsExport'), 'thumbnail departures', len(image['thumbnailDepartures']),
              max((d[2] for d in image['thumbnailDepartures']), default=0))


if __name__ == '__main__':
    main()
