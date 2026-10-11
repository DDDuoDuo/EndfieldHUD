#!/usr/bin/env python3
"""Black-box fit of the original Highlights/Shadows edit (CIHighlightShadowAdjust
as MediaAssemblyEngine.apply requests it) from Core Image outputs only.

Provenance rule: this tool observes the operator purely from the outside. It
compiles the unchanged pinned Engine.apply (shared with
media_assembly_processor_reference.py), renders synthetic RGBAf pixels through
the public Core Image API in the original explicit sRGB working space and
reads back the rendered floats. No framework binary, kernel text, debug tree
(CI_PRINT_TREE) or private method is inspected, and no structure or constant
taken from such inspection is used: every model below is a generic
approximation whose parameters come from the rendered probe outputs.

Probe (all values are working-space sRGB-encoded floats, as previews and
exports use them):
  nodes     the amount grid highlights = i/10, shadows = j/10 (121 nodes);
            at each node a 17^3 RGB lattice (red fastest, k/16) and a
            257-step grey ramp.
  fine      a 33^3 and a 65^3 lattice at four nodes, to separate RGB
            resolution from amount interpolation.
  held-out  240 pseudo-random amount pairs, each with 24 random opaque pixels
            and the processor oracle's 18 standard inputs (alpha, extended
            range); plus, at each fine node, 4000 random pixels and 2000 with
            one channel inside the first 1/16 (near-black edges, where the
            outputs bend most; uniform samples rarely reach them).
  locality  one random row, a permutation of it and a 64x64 image of the same
            pixels at one amount pair: equal outputs per pixel show that the
            edit depends on each pixel alone (no neighbourhood).
  structure generic separability tests on the rendered outputs: whether the
            edit is one gain or one offset shared by the three channels
            (encoded or linear sRGB), and finite differences of 21-step
            amount sweeps (a polynomial of degree d in an amount has zero
            differences of order d+1).

Models (amounts are interpolated bilinearly between nodes for every model):
  none        no change (what Windows applies while the gap stands).
  greyCurve   per-channel curve measured on the grey ramp.
  lumaGain    the grey curve's gain at the Rec. 709 luma, applied to RGB.
  poly3       least-squares cubic RGB polynomial per node and channel,
              fitted on the 9^3 subset of the lattice.
  lattice9    trilinear 9^3 RGB lattice (every other probe node).
  lattice17   trilinear 17^3 RGB lattice.
Values outside [0, 1] use the model at the clamped value plus the offset.
At the fine nodes alone (no amount interpolation), a 33^3 lattice with
trilinear and a 65^3 lattice with tetrahedral interpolation (exact across the
channel-order planes r=g, g=b, r=b, where the outputs bend) bound what any
RGB table can reach.

The result is compared with the processor parity bound (float relative error
2e-5 and one RGBA8 level, as every characterised operator is gated in
media_assembly_processor_tests). Only a model inside that bound may replace
the documented "no change applied" gap.

usage: media_assembly_highlight_shadow_fit.py <output.json>
"""
from pathlib import Path
import array, importlib.util, json, math, platform, random, struct, subprocess, sys, tempfile

here = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('media_assembly_processor_reference', here / 'media_assembly_processor_reference.py')
reference = importlib.util.module_from_spec(spec)
spec.loader.exec_module(reference)

SEED = 20261011
NODES = 10                 # amount grid step 1/NODES
LATTICE = 17
RAMP = 257
FINE = 33
FINEST = 65
SWEEP = 21
SWEEP_PIXELS = 500
STRUCTURE_TOLERANCE = 1e-4
FINE_NODES = [(0, 0), (5, 5), (0, 10), (3, 7)]
HELD_AMOUNTS = 240
HELD_PIXELS = 24
NODE_PIXELS = 4000
NODE_EDGE_PIXELS = 2000
EDGE = 1 / 16
ROW = 4096
FLOAT_BOUND = 2e-5
BYTE_BOUND = 1

DRIVER = r'''
let color=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
let request=try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[2]))
var cursor=0
func take<T>(_ type:T.Type)->T{let size=MemoryLayout<T>.size;let v=request.subdata(in:cursor..<cursor+size).withUnsafeBytes{$0.loadUnaligned(as:T.self)};cursor+=size;return v}
precondition(take(UInt32.self)==0x46534857)
let jobs=Int(take(UInt32.self))
var output=Data();output.reserveCapacity(request.count)
for _ in 0..<jobs{
 let h=take(Float64.self),s=take(Float64.self),w=Int(take(UInt32.self)),rows=Int(take(UInt32.self))
 let bytes=request.subdata(in:cursor..<cursor+w*rows*16);cursor+=w*rows*16
 var a=MediaAssemblyAdjustments();a.highlights=h;a.shadows=s
 let source=CIImage(bitmapData:bytes,bytesPerRow:w*16,size:CGSize(width:w,height:rows),format:.RGBAf,colorSpace:color)
 let result=try Original.apply(a,to:source,includeStickers:false)
 precondition(result.extent==CGRect(x:0,y:0,width:w,height:rows))
 var sample=Data(count:w*rows*16)
 sample.withUnsafeMutableBytes{context.render(result,toBitmap:$0.baseAddress!,rowBytes:w*16,bounds:result.extent,format:.RGBAf,colorSpace:color)}
 output.append(sample)
}
try output.write(to:URL(fileURLWithPath:CommandLine.arguments[3]))
'''


def decode(v):
    """sRGB transfer, odd-extended (the linear working-space reading)."""
    s, a = (-1 if v < 0 else 1), abs(v)
    return s * (a / 12.92 if a <= 0.04045 else ((a + 0.055) / 1.055) ** 2.4)


def tetrahedral(table, n, rgb):
    """Tetrahedral lattice interpolation: the cube splits along the channel
    order, so the result is exact on the planes where two channels are equal."""
    x = [clamp01(v) * (n - 1) for v in rgb]
    i = [min(int(v), n - 2) for v in x]
    f = [x[k] - i[k] for k in range(3)]
    order = sorted(range(3), key=lambda k: -f[k])
    corner, out = [0, 0, 0], [0.0, 0.0, 0.0]
    weights = [1 - f[order[0]], f[order[0]] - f[order[1]], f[order[1]] - f[order[2]], f[order[2]]]
    for step, w in enumerate(weights):
        if step:
            corner[order[step - 1]] = 1
        p = table[((i[2] + corner[2]) * n + i[1] + corner[1]) * n + i[0] + corner[0]]
        for c in range(3):
            out[c] += w * p[c]
    return [out[c] + rgb[c] - clamp01(rgb[c]) for c in range(3)]


def lattice_pixels(n):
    return [(r / (n - 1), g / (n - 1), b / (n - 1), 1.0) for b in range(n) for g in range(n) for r in range(n)]


class Probe:
    """Collects jobs; each job is one amount pair and a list of RGBA pixels."""

    def __init__(self):
        self.jobs = []

    def add(self, h, s, pixels, width=None):
        """Returns slices (job index, count) covering the pixels."""
        if width is not None:
            self.jobs.append((h, s, pixels, width, len(pixels) // width))
            return [(len(self.jobs) - 1, len(pixels))]
        parts = []
        for at in range(0, len(pixels), ROW):
            chunk = pixels[at:at + ROW]
            self.jobs.append((h, s, chunk, len(chunk), 1))
            parts.append((len(self.jobs) - 1, len(chunk)))
        return parts

    def run(self, oracle, directory):
        request = bytearray(struct.pack('<II', 0x46534857, len(self.jobs)))
        for h, s, pixels, width, rows in self.jobs:
            request += struct.pack('<ddII', h, s, width, rows)
            request += array.array('f', [v for p in pixels for v in p]).tobytes()
        (directory / 'request.bin').write_bytes(request)
        subprocess.run([str(oracle), str(reference.assets), str(directory / 'request.bin'), str(directory / 'result.bin')], check=True)
        values = array.array('f')
        values.frombytes((directory / 'result.bin').read_bytes())
        self.outputs, at = [], 0
        for _, _, pixels, _, _ in self.jobs:
            self.outputs.append([tuple(values[at + 4 * i:at + 4 * i + 4]) for i in range(len(pixels))])
            at += 4 * len(pixels)
        assert at == len(values)

    def gather(self, parts):
        out = []
        for job, _ in parts:
            out += self.outputs[job]
        return out


def clamp01(v):
    return 0.0 if v < 0 else 1.0 if v > 1 else v


def to_byte(v):
    return int(math.floor(clamp01(v) * 255 + .5))


def bytes_of(rgba):
    a = clamp01(rgba[3])
    return [to_byte(min(rgba[k], a)) for k in range(3)] + [to_byte(a)]


class Node:
    """Measured responses and fitted parameters at one amount node."""

    def __init__(self, lattice, ramp):
        self.lattice = [tuple(p[:3]) for p in lattice]
        self.ramp = [tuple(p[:3]) for p in ramp]
        self.lattice9 = [self.lattice[(2 * b * LATTICE + 2 * g) * LATTICE + 2 * r] for b in range(9) for g in range(9) for r in range(9)]
        self.poly = fit_poly(self.lattice9)

    def grey(self, c, v):
        x = clamp01(v) * (RAMP - 1)
        i = min(int(x), RAMP - 2)
        t = x - i
        return self.ramp[i][c] * (1 - t) + self.ramp[i + 1][c] * t + (v - clamp01(v))


def trilinear(table, n, rgb):
    x = [clamp01(v) * (n - 1) for v in rgb]
    i = [min(int(v), n - 2) for v in x]
    t = [x[k] - i[k] for k in range(3)]
    out = [0.0, 0.0, 0.0]
    for dz in (0, 1):
        for dy in (0, 1):
            for dx in (0, 1):
                w = (t[0] if dx else 1 - t[0]) * (t[1] if dy else 1 - t[1]) * (t[2] if dz else 1 - t[2])
                if w == 0:
                    continue
                p = table[((i[2] + dz) * n + i[1] + dy) * n + i[0] + dx]
                for c in range(3):
                    out[c] += w * p[c]
    return [out[c] + rgb[c] - clamp01(rgb[c]) for c in range(3)]


EXPONENTS = [(a, b, c) for a in range(4) for b in range(4) for c in range(4) if a + b + c <= 3]


def basis(rgb):
    r, g, b = (clamp01(v) for v in rgb)
    return [r ** i * g ** j * b ** k for i, j, k in EXPONENTS]


POLY_ROWS = [basis((r / 8, g / 8, b / 8)) for b in range(9) for g in range(9) for r in range(9)]
POLY_NORMAL = [[sum(row[i] * row[j] for row in POLY_ROWS) for j in range(len(EXPONENTS))] for i in range(len(EXPONENTS))]


def fit_poly(samples):
    """Normal-equation least squares on the 9^3 probe points (red fastest)."""
    n = len(EXPONENTS)
    coefficients = []
    for c in range(3):
        rhs = [sum(POLY_ROWS[k][i] * samples[k][c] for k in range(len(POLY_ROWS))) for i in range(n)]
        coefficients.append(solve([r[:] for r in POLY_NORMAL], rhs))
    return coefficients


def solve(m, v):
    n = len(v)
    for col in range(n):
        pivot = max(range(col, n), key=lambda r: abs(m[r][col]))
        m[col], m[pivot] = m[pivot], m[col]
        v[col], v[pivot] = v[pivot], v[col]
        for r in range(col + 1, n):
            f = m[r][col] / m[col][col]
            if f:
                for k in range(col, n):
                    m[r][k] -= f * m[col][k]
                v[r] -= f * v[col]
    x = [0.0] * n
    for r in range(n - 1, -1, -1):
        x[r] = (v[r] - sum(m[r][k] * x[k] for k in range(r + 1, n))) / m[r][r]
    return x


def node_model(name, node, rgb):
    if name == 'greyCurve':
        return [node.grey(c, rgb[c]) for c in range(3)]
    if name == 'lumaGain':
        y = .2126 * rgb[0] + .7152 * rgb[1] + .0722 * rgb[2]
        if y <= 1e-4:
            return [node.grey(c, rgb[c]) for c in range(3)]
        yc = clamp01(y)
        gain = sum(node.grey(c, yc) for c in range(3)) / 3 / yc
        return [v * gain for v in rgb]
    if name == 'poly3':
        b = basis(rgb)
        return [sum(k * x for k, x in zip(node.poly[c], b)) + rgb[c] - clamp01(rgb[c]) for c in range(3)]
    if name == 'lattice9':
        return trilinear(node.lattice9, 9, rgb)
    if name == 'lattice17':
        return trilinear(node.lattice, LATTICE, rgb)
    raise ValueError(name)


MODELS = [
    ('none', 'No change (the Windows behaviour while the gap stands).', 0),
    ('greyCurve', 'Per-channel curve measured on a 257-step grey ramp at each amount node.', (NODES + 1) ** 2 * RAMP * 3),
    ('lumaGain', 'Grey-ramp gain at the Rec. 709 luma applied to all channels.', (NODES + 1) ** 2 * RAMP * 3),
    ('poly3', 'Least-squares cubic RGB polynomial per channel at each amount node (fitted on 9^3 probe points).', (NODES + 1) ** 2 * len(EXPONENTS) * 3),
    ('lattice9', 'Trilinear 9^3 RGB lattice of Core Image outputs at each amount node.', (NODES + 1) ** 2 * 9 ** 3 * 3),
    ('lattice17', 'Trilinear 17^3 RGB lattice of Core Image outputs at each amount node.', (NODES + 1) ** 2 * LATTICE ** 3 * 3),
]


def evaluate(name, nodes, h, s, rgba):
    a = rgba[3]
    if a <= 0:
        return (0.0, 0.0, 0.0, 0.0 if a < 0 else a)
    straight = [rgba[c] / a for c in range(3)]
    if name == 'none':
        out = straight
    else:
        fh, fs = h * NODES, s * NODES
        i, j = min(int(fh), NODES - 1), min(int(fs), NODES - 1)
        th, ts = fh - i, fs - j
        out = [0.0, 0.0, 0.0]
        for di, wi in ((0, 1 - th), (1, th)):
            for dj, wj in ((0, 1 - ts), (1, ts)):
                w = wi * wj
                if w == 0:
                    continue
                v = node_model(name, nodes[(i + di, j + dj)], straight)
                for c in range(3):
                    out[c] += w * v[c]
    return (out[0] * a, out[1] * a, out[2] * a, a)


def score(name, nodes, samples):
    worst_float, worst_byte, total_byte, over, count = 0.0, 0, 0, 0, 0
    for h, s, rgba, expected in samples:
        got = evaluate(name, nodes, h, s, rgba)
        gb, eb = bytes_of(got), bytes_of(expected)
        for k in range(4):
            worst_float = max(worst_float, abs(got[k] - expected[k]) / max(1.0, abs(expected[k])))
            d = abs(gb[k] - eb[k])
            worst_byte = max(worst_byte, d)
            total_byte += d
            over += d > 1
            count += 1
    return {'samples': len(samples), 'worstFloat': float('%.6g' % worst_float), 'worstByte': worst_byte,
            'meanByte': float('%.4g' % (total_byte / count)), 'over1Fraction': float('%.4g' % (over / count))}


def main():
    output = Path(sys.argv[1])
    rng = random.Random(SEED)
    standard = json.loads((reference.root / 'windows/tests/fixtures/media-assembly-processor-source.json').read_text())['inputs']
    probe = Probe()
    node_parts = {}
    lattice = lattice_pixels(LATTICE)
    ramp = [(k / (RAMP - 1),) * 3 + (1.0,) for k in range(RAMP)]
    for i in range(NODES + 1):
        for j in range(NODES + 1):
            node_parts[(i, j)] = (probe.add(i / NODES, j / NODES, lattice), probe.add(i / NODES, j / NODES, ramp))
    fine_parts = {n: probe.add(n[0] / NODES, n[1] / NODES, lattice_pixels(FINE)) for n in FINE_NODES}
    finest_parts = {n: probe.add(n[0] / NODES, n[1] / NODES, lattice_pixels(FINEST)) for n in FINE_NODES}
    held = []
    for _ in range(HELD_AMOUNTS):
        h, s = rng.random(), rng.random()
        random_pixels = [(rng.random(), rng.random(), rng.random(), 1.0) for _ in range(HELD_PIXELS)]
        held.append((h, s, random_pixels, probe.add(h, s, random_pixels), [tuple(p) for p in standard], probe.add(h, s, [tuple(p) for p in standard])))
    node_held = {}
    for n in FINE_NODES:
        pixels = [(rng.random(), rng.random(), rng.random(), 1.0) for _ in range(NODE_PIXELS)]
        for _ in range(NODE_EDGE_PIXELS):
            edge = [rng.random(), rng.random(), rng.random()]
            edge[rng.randrange(3)] *= EDGE
            pixels.append(tuple(edge) + (1.0,))
        node_held[n] = (pixels, probe.add(n[0] / NODES, n[1] / NODES, pixels))
    local_h, local_s = .35, .55
    row = [(rng.random(), rng.random(), rng.random(), 1.0) for _ in range(ROW)]
    order = list(range(ROW))
    rng.shuffle(order)
    local_row = probe.add(local_h, local_s, row)
    local_permuted = probe.add(local_h, local_s, [row[k] for k in order])
    local_image = probe.add(local_h, local_s, row, width=64)
    # Structure: interior pixels (no channel near a clamp) and amount sweeps.
    interior = [(.1 + .8 * rng.random(), .1 + .8 * rng.random(), .1 + .8 * rng.random(), 1.0) for _ in range(SWEEP_PIXELS)]
    structure_parts = {n: probe.add(n[0] / NODES, n[1] / NODES, interior) for n in FINE_NODES}
    sweep_parts = {(axis, other): [probe.add(*((k / (SWEEP - 1), other) if axis == 'h' else (other, k / (SWEEP - 1))), interior) for k in range(SWEEP)]
                   for axis in ('h', 's') for other in (0.0, 0.5, 1.0)}

    sources = reference.pinned_sources()
    with tempfile.TemporaryDirectory(prefix='endfield-media-highlight-shadow-') as tmp:
        directory = Path(tmp)
        oracle = reference.build_oracle(sources, DRIVER, directory)
        probe.run(oracle, directory)

    nodes = {n: Node(probe.gather(parts[0]), probe.gather(parts[1])) for n, parts in node_parts.items()}
    # Locality: the same pixel gives the same output wherever it sits.
    a, b, image = probe.gather(local_row), probe.gather(local_permuted), probe.gather(local_image)
    permuted = max(abs(b[k][c] - a[order[k]][c]) for k in range(ROW) for c in range(4))
    flipped = [image[(63 - y) * 64 + x] for y in range(64) for x in range(64)]
    planar = min(max(abs(img[k][c] - a[k][c]) for k in range(ROW) for c in range(4)) for img in (image, flipped))
    moved = sum(1 for k in range(ROW) if max(abs(a[k][c] - row[k][c]) for c in range(3)) > 1e-3)

    held_random = [(h, s, p, e) for h, s, pixels, parts, _, _ in held for p, e in zip(pixels, probe.gather(parts))]
    held_standard = [(h, s, p, e) for h, s, _, _, pixels, parts in held for p, e in zip(pixels, probe.gather(parts))]
    at_nodes = [(n[0] / NODES, n[1] / NODES, p, e) for n, (pixels, parts) in node_held.items() for p, e in zip(pixels, probe.gather(parts))]
    fine = {n: [tuple(p[:3]) for p in probe.gather(parts)] for n, parts in fine_parts.items()}
    lattice33 = score_fine(fine, FINE, trilinear, node_held, probe)
    finest = {n: [tuple(p[:3]) for p in probe.gather(parts)] for n, parts in finest_parts.items()}
    lattice65 = score_fine(finest, FINEST, tetrahedral, node_held, probe)
    structure = measure_structure(interior, {n: probe.gather(parts) for n, parts in structure_parts.items()},
                                  {key: [probe.gather(parts) for parts in sweep] for key, sweep in sweep_parts.items()})

    models = []
    for name, description, floats in MODELS:
        result = {'name': name, 'description': description, 'tableFloats': floats,
                  'heldOutRandom': score(name, nodes, held_random), 'heldOutStandardInputs': score(name, nodes, held_standard),
                  'atFineNodes': score(name, nodes, at_nodes)}
        both = (result['heldOutRandom'], result['heldOutStandardInputs'])
        result['reachesBound'] = all(x['worstFloat'] <= FLOAT_BOUND and x['worstByte'] <= BYTE_BOUND for x in both)
        models.append(result)
        print(name, json.dumps(result['heldOutRandom']), json.dumps(result['heldOutStandardInputs']), json.dumps(result['atFineNodes']))
    print('lattice33 at fine nodes', json.dumps(lattice33))
    print('lattice65 tetrahedral at fine nodes', json.dumps(lattice65))
    print('structure', json.dumps(structure))
    candidates = [m for m in models if m['name'] != 'none']
    best = min(candidates, key=lambda m: (m['heldOutRandom']['worstByte'], m['heldOutRandom']['worstFloat']))
    reaches = any(m['reachesBound'] for m in candidates)
    result = {
        'sourceCommit': reference.commit, 'sourcePins': reference.source_pins(sources), 'usesAppOrWindow': False,
        'macOS': platform.mac_ver()[0],
        'provenance': 'Black-box only: rendered Core Image outputs of the unchanged Engine.apply on synthetic pixels. No framework binary, kernel text, CI_PRINT_TREE output or private method was read; the models are generic approximations fitted to the probe.',
        'probe': {'seed': SEED, 'amountNodes': (NODES + 1) ** 2, 'amountStep': 1 / NODES, 'lattice': LATTICE, 'greyRamp': RAMP,
                  'fineLattice': FINE, 'fineNodes': [[n[0] / NODES, n[1] / NODES] for n in FINE_NODES],
                  'heldOutAmounts': HELD_AMOUNTS, 'heldOutRandomPixels': HELD_PIXELS, 'heldOutStandardInputs': len(standard),
                  'nodePixels': NODE_PIXELS, 'nodeEdgePixels': NODE_EDGE_PIXELS, 'nodeEdge': EDGE, 'renderedPixels': sum(len(j[2]) for j in probe.jobs), 'jobs': len(probe.jobs),
                  'expectedBytes': 'RGBA8 from the rendered floats with the test rounding (colour clamped to alpha, round to nearest)'},
        'locality': {'amounts': [local_h, local_s], 'pixels': ROW, 'permutedRowMaxDifference': permuted,
                     'imageMaxDifference': planar, 'pixelsChanged': moved, 'perPixel': permuted == 0 and planar == 0},
        'parityBound': {'float': FLOAT_BOUND, 'rgba8': BYTE_BOUND},
        'models': models, 'lattice33AtFineNodes': lattice33, 'lattice65TetrahedralAtFineNodes': lattice65,
        'structure': structure,
        'best': best['name'], 'reachesBound': reaches,
        'decision': 'model' if reaches else 'gap',
    }
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(result, indent=1, sort_keys=True) + '\n')
    print(output, 'best', best['name'], 'reachesBound', reaches)


def score_fine(fine, size, interpolate, node_held, probe):
    """Pure RGB-resolution error of a size^3 lattice at its own amount node."""
    worst_float, worst_byte = 0.0, 0
    for n, (pixels, parts) in node_held.items():
        for p, e in zip(pixels, probe.gather(parts)):
            v = interpolate(fine[n], size, p[:3]) + [1.0]
            gb, eb = bytes_of(v), bytes_of(e)
            for k in range(4):
                worst_float = max(worst_float, abs(v[k] - e[k]) / max(1.0, abs(e[k])))
                worst_byte = max(worst_byte, abs(gb[k] - eb[k]))
    return {'samples': sum(len(v[0]) for v in node_held.values()), 'worstFloat': float('%.6g' % worst_float), 'worstByte': worst_byte,
            'tableFloatsPerNode': size ** 3 * 3}


def measure_structure(interior, at_nodes, sweeps):
    """Generic separability tests on the rendered outputs (no model)."""
    def spread(values):
        return max(values) - min(values)
    shared = {}
    for space, f in (('encoded', lambda v: v), ('linear', decode)):
        gain = offset = 0.0
        for outputs in at_nodes.values():
            for p, q in zip(interior, outputs):
                x, y = [f(p[c]) for c in range(3)], [f(q[c]) for c in range(3)]
                gain = max(gain, spread([y[c] / x[c] for c in range(3)]))
                offset = max(offset, spread([y[c] - x[c] for c in range(3)]))
        shared[space] = {'gainSpread': float('%.4g' % gain), 'offsetSpread': float('%.4g' % offset)}
    def differences(seq, order):
        for _ in range(order):
            seq = [b - a for a, b in zip(seq, seq[1:])]
        return seq
    sweep = {}
    for (axis, other), outputs in sweeps.items():
        sweep['%s|%s=%g' % (axis, 's' if axis == 'h' else 'h', other)] = [
            float('%.4g' % max(abs(d) for i in range(len(interior)) for c in range(3)
                               for d in differences([outputs[k][i][c] for k in range(SWEEP)], order)))
            for order in (1, 2, 3, 4)]
    # Verdicts from the numbers: a shared gain or offset leaves spreads at
    # float rounding; a cubic amount dependence leaves zero 4th differences.
    return {'interiorPixels': len(interior), 'nodes': [[n[0] / NODES, n[1] / NODES] for n in at_nodes],
            'sharedAcrossChannels': shared, 'amountSweepSteps': SWEEP, 'amountSweepMaxDifferences': sweep,
            'sharedGainOrOffset': any(v <= STRUCTURE_TOLERANCE for s in shared.values() for v in s.values()),
            'amountsAtMostCubic': all(v[3] <= STRUCTURE_TOLERANCE for v in sweep.values())}


if __name__ == '__main__':
    main()
