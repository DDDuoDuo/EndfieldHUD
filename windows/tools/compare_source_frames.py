#!/usr/bin/env python3
"""Compare owned raw BGRA8_sRGB render targets, never screenshots or PNG pixels.

Example: compare_source_frames.py --oracle PACK/verification-oracles.json \
  --frame desktop-shell-1280x800-top --actual windows.raw-bgra.bin \
  --width 1280 --height 800 --output comparison.json [--png-prefix comparison]
No capture API, application, network, or global input is used.
"""
import argparse
import hashlib
import json
import math
import pathlib
from dataclasses import dataclass


def decode_srgb(value):
    return value / 12.92 if value <= .04045 else ((value + .055) / 1.055) ** 2.4


def encode_srgb(value):
    return value * 12.92 if value <= .0031308 else 1.055 * value ** (1 / 2.4) - .055


DECODE = tuple(decode_srgb(i / 255) for i in range(256))


@dataclass(frozen=True)
class Frame:
    data: bytes
    width: int
    height: int
    row_bytes: int

    def __post_init__(self):
        if not (0 < self.width <= 8192 and 0 < self.height <= 8192 and self.width * self.height <= 16_777_216):
            raise ValueError('Render target dimensions exceed the bounded comparison size')
        if not (self.width * 4 <= self.row_bytes <= self.width * 4 + 65536):
            raise ValueError('Invalid render target row stride')
        if len(self.data) != self.row_bytes * self.height:
            raise ValueError('Raw byte count differs from rowBytes × height')

    def pixels(self):
        for row in range(self.height):
            start = row * self.row_bytes
            for offset in range(start, start + self.width * 4, 4):
                yield self.data[offset:offset + 4]

    def active_bytes(self):
        return b''.join(self.data[y * self.row_bytes:y * self.row_bytes + self.width * 4] for y in range(self.height))


class ErrorAccumulator:
    def __init__(self):
        self.count = self.absolute = self.squared = self.maximum = self.within1 = self.within2 = 0

    def add(self, difference):
        difference = abs(difference)
        self.count += 1; self.absolute += difference; self.squared += difference * difference
        self.maximum = max(self.maximum, difference)
        self.within1 += difference <= 1; self.within2 += difference <= 2

    def result(self):
        return {'samples': self.count, 'meanAbsoluteError': self.absolute / self.count if self.count else None,
                'maxAbsoluteError': self.maximum if self.count else None,
                'rmse': math.sqrt(self.squared / self.count) if self.count else None,
                'within1BytePercent': 100 * self.within1 / self.count if self.count else None,
                'within2BytesPercent': 100 * self.within2 / self.count if self.count else None}


def compare(reference, actual, support_threshold=2):
    if (reference.width, reference.height) != (actual.width, actual.height):
        raise ValueError('Raw render target dimensions differ; no resize or image registration is performed')
    if not 0 <= support_threshold <= 255:
        raise ValueError('Invalid support threshold')
    channels = {name: ErrorAccumulator() for name in 'BGRA'}; all_channels = ErrorAccumulator()
    rgb_overlap = ErrorAccumulator(); rgb_outside = ErrorAccumulator(); linear_rgb = ErrorAccumulator()
    hypotheses = {name: ErrorAccumulator() for name in (
        'identity', 'linearBytesWithoutSRGBEncoding', 'doubleSRGBEncoding',
        'encodedRGBMultipliedByAlphaAgain', 'encodedSpacePremultipliedReadback')}
    coverage = {name: dict.fromkeys(('zeroAlpha', 'fractionalAlpha', 'opaqueAlpha', 'positiveRGBAtZeroAlpha'), 0)
                for name in ('reference', 'actual')}
    masks = {name: dict.fromkeys(('referencePixels','actualPixels','intersectionPixels','unionPixels','xorPixels'), 0)
             for name in ('alpha','rgb','alphaOrRGB')}
    exact = within1 = within2 = 0
    for expected, found in zip(reference.pixels(), actual.pixels()):
        differences = [abs(e - a) for e, a in zip(expected, found)]
        maximum = max(differences); exact += maximum == 0; within1 += maximum <= 1; within2 += maximum <= 2
        for name, difference in zip('BGRA', differences):
            channels[name].add(difference); all_channels.add(difference)
        for name, pixel in (('reference', expected), ('actual', found)):
            alpha = pixel[3]
            coverage[name]['zeroAlpha' if alpha == 0 else 'opaqueAlpha' if alpha == 255 else 'fractionalAlpha'] += 1
            coverage[name]['positiveRGBAtZeroAlpha'] += alpha == 0 and any(pixel[:3])
        reference_alpha, actual_alpha = expected[3] > support_threshold, found[3] > support_threshold
        reference_rgb, actual_rgb = max(expected[:3]) > support_threshold, max(found[:3]) > support_threshold
        for name, r, a in (('alpha', reference_alpha, actual_alpha), ('rgb', reference_rgb, actual_rgb),
                           ('alphaOrRGB', reference_alpha or reference_rgb, actual_alpha or actual_rgb)):
            mask = masks[name]
            mask['referencePixels'] += r; mask['actualPixels'] += a
            mask['intersectionPixels'] += r and a; mask['unionPixels'] += r or a; mask['xorPixels'] += r != a
        common_support = (reference_alpha or reference_rgb) and (actual_alpha or actual_rgb)
        if common_support:
            for difference in differences[:3]: rgb_overlap.add(difference)
        else:
            for difference in differences[:3]: rgb_outside.add(difference)
        for e, a in zip(expected[:3], found[:3]): linear_rgb.add((DECODE[e] - DECODE[a]) * 255)
        # These are diagnostic candidate fits, not a causal classifier. Equal
        # alpha and overlapping support avoid counting wholly displaced pixels.
        if common_support and expected[3] == found[3]:
            alpha = expected[3] / 255
            for e, a in zip(expected[:3], found[:3]):
                hypotheses['identity'].add(e - a)
                hypotheses['linearBytesWithoutSRGBEncoding'].add(DECODE[e] * 255 - a)
                hypotheses['doubleSRGBEncoding'].add(encode_srgb(e / 255) * 255 - a)
                hypotheses['encodedRGBMultipliedByAlphaAgain'].add(e * alpha - a)
                # Additive pixels can be unrepresentable as conventional
                # premultiplied sRGB. Skip them rather than clamp their emission.
                if alpha > 0 and DECODE[e] <= alpha:
                    hypotheses['encodedSpacePremultipliedReadback'].add(encode_srgb(DECODE[e] / alpha) * alpha * 255 - a)
    count = reference.width * reference.height
    for mask in masks.values():
        mask['intersectionOverUnion'] = mask['intersectionPixels'] / mask['unionPixels'] if mask['unionPixels'] else 1.0
        mask['xorPercentOfImage'] = 100 * mask['xorPixels'] / count
        mask['actualMinusReferencePixels'] = mask['actualPixels'] - mask['referencePixels']
    return {'schemaVersion': 1, 'width': reference.width, 'height': reference.height, 'pixels': count,
            'comparison': 'unmodified active BGRA8_sRGB bytes; row padding excluded; no alpha conversion, resize, registration, or native CALayers',
            'channels': {name: value.result() for name, value in channels.items()}, 'allChannels': all_channels.result(),
            'pixelsWithinTolerance': {'exactPercent': 100 * exact / count, 'within1BytePercent': 100 * within1 / count,
                                      'within2BytesPercent': 100 * within2 / count},
            'alphaCoverage': coverage, 'supportThresholdByte': support_threshold, 'coverageMasks': masks,
            'rgbErrorOnCommonSupport': rgb_overlap.result(), 'rgbErrorOutsideCommonSupport': rgb_outside.result(),
            'linearDecodedRGBErrorIn255Units': linear_rgb.result(),
            'transferCandidateFitsOnEqualAlphaCommonSupport': {name: value.result() for name, value in hypotheses.items()},
            'interpretationLimits': [
                'Support-mask differences locate coverage changes; geometry, clipping, blending, or thresholded color may cause them.',
                'Transfer candidate fits are diagnostics, not proof of an alpha or color-space cause; spatially different content can share support.',
                'Cross-API rasterization can differ at edges. No perceptual or parity pass threshold is imposed by this tool.',
                'Native text/icons and module layers are excluded from these source-render-target comparisons.']}


def read_oracle(path, frame_name):
    manifest = json.loads(path.read_text())
    matches = [f for f in manifest['frames'] if f['name'] == frame_name]
    if len(matches) != 1: raise ValueError('Expected exactly one named raw oracle')
    item = matches[0]; root = path.resolve().parent
    relative = pathlib.PurePosixPath(item['file'])
    if relative.is_absolute() or '..' in relative.parts or '\\' in str(relative): raise ValueError('Unsafe oracle path')
    raw = (root / str(relative)).resolve()
    if not raw.is_relative_to(root): raise ValueError('Oracle path escapes its package')
    if item['pixelFormat'] != 'bgra8Unorm_srgb' or item.get('blackMatteApplied') is not False or item.get('desktopCapture') is not False:
        raise ValueError('Only an owned unmodified BGRA8_sRGB target oracle is accepted')
    data = raw.read_bytes()
    if len(data) != item['bytes'] or hashlib.sha256(data).hexdigest() != item['sha256']:
        raise ValueError('Raw oracle checksum or byte length differs')
    return Frame(data,item['width'],item['height'],item['rowBytes']), item


def previews(reference, actual, prefix, gain=16):
    try: from PIL import Image, ImageDraw
    except ImportError: return {'status': 'unavailable', 'reason': 'Pillow is not installed; raw metrics are complete'}
    def image(frame):
        rgba = bytearray(frame.active_bytes())
        for i in range(0,len(rgba),4): rgba[i],rgba[i+2],rgba[i+3] = rgba[i+2],rgba[i],255
        return Image.frombytes('RGBA',(frame.width,frame.height),bytes(rgba)).convert('RGB')
    prefix.parent.mkdir(parents=True,exist_ok=True)
    side = Image.new('RGB',(reference.width*2,reference.height+24),(18,18,18)); draw=ImageDraw.Draw(side)
    draw.text((5,5),'Mac raw RGB over black (preview only)',fill='white')
    draw.text((reference.width+5,5),'Windows raw RGB over black (preview only)',fill='white')
    side.paste(image(reference),(0,24)); side.paste(image(actual),(reference.width,24))
    diff=bytearray(); alpha=bytearray()
    for expected,found in zip(reference.pixels(),actual.pixels()):
        d=[min(255,abs(e-a)*gain) for e,a in zip(expected,found)]
        diff.extend((d[2],d[1],d[0]));alpha.extend((d[3],)*3)
    difference=Image.new('RGB',(reference.width*2,reference.height+24),(18,18,18));draw=ImageDraw.Draw(difference)
    draw.text((5,5),f'Absolute RGB difference x{gain}',fill='white')
    draw.text((reference.width+5,5),f'Absolute alpha difference x{gain}',fill='white')
    difference.paste(Image.frombytes('RGB',(reference.width,reference.height),bytes(diff)),(0,24))
    difference.paste(Image.frombytes('RGB',(reference.width,reference.height),bytes(alpha)),(reference.width,24))
    side_path=pathlib.Path(str(prefix)+'-side-by-side.png');diff_path=pathlib.Path(str(prefix)+'-difference.png')
    side.save(side_path);difference.save(diff_path)
    return {'status':'written','sideBySide':str(side_path.resolve()),'difference':str(diff_path.resolve()),'differenceGain':gain,
            'note':'Opaque previews only; comparison metrics use original raw bytes.'}


def main():
    parser=argparse.ArgumentParser(description=__doc__,formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--oracle',type=pathlib.Path,required=True);parser.add_argument('--frame',required=True)
    parser.add_argument('--actual',type=pathlib.Path,required=True);parser.add_argument('--width',type=int,required=True)
    parser.add_argument('--height',type=int,required=True);parser.add_argument('--row-bytes',type=int)
    parser.add_argument('--support-threshold',type=int,default=2);parser.add_argument('--output',type=pathlib.Path,required=True)
    parser.add_argument('--png-prefix',type=pathlib.Path)
    args=parser.parse_args()
    reference,descriptor=read_oracle(args.oracle,args.frame)
    actual=Frame(args.actual.read_bytes(),args.width,args.height,args.row_bytes or args.width*4)
    report=compare(reference,actual,args.support_threshold)
    report['reference']={'name':args.frame,'sha256':descriptor['sha256'],'rowBytes':reference.row_bytes}
    report['actual']={'file':str(args.actual.resolve()),'sha256':hashlib.sha256(actual.data).hexdigest(),'rowBytes':actual.row_bytes}
    if args.png_prefix: report['previews']=previews(reference,actual,args.png_prefix)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(report,indent=2,sort_keys=True,allow_nan=False)+'\n')
    print(f"Raw comparison: MAE {report['allChannels']['meanAbsoluteError']:.6f}, RMSE {report['allChannels']['rmse']:.6f}, "
          f"max {report['allChannels']['maxAbsoluteError']}, pixels within2 {report['pixelsWithinTolerance']['within2BytesPercent']:.4f}%")


if __name__=='__main__': main()
