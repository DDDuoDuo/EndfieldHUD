#!/usr/bin/env python3
"""Build the bundled Earth relief vectors; never run by the application.

Requires numpy, scipy and contourpy. The input is a stride-60 subset of the
NOAA ETOPO 2022 15-arcsecond elevation grid (1440 by 720, 15 arcminutes).
See docs/map-data.md for source, license, exact request and reproduction.
"""
import argparse
import hashlib
import json
import pathlib
import struct
import urllib.request

import contourpy
import numpy as np
from scipy.io import netcdf_file
from scipy.ndimage import gaussian_filter

SOURCE_URL = 'https://coastwatch.pfeg.noaa.gov/erddap/griddap/ETOPO_2022_v1_15s.nc?z[0:60:43199][0:60:86399]'
LEVELS = [-4000, -2000, 0, 250, 500, 1000, 1500, 2000, 3000, 4000, 5000, 6000]
WIDTH, HEIGHT = 1024.0, 512.0


def simplify(points, tolerance=0.10):
    """Iterative Douglas-Peucker in map coordinates; preserves endpoint order."""
    if len(points) < 3:
        return points
    keep = np.zeros(len(points), dtype=bool)
    keep[0] = keep[-1] = True
    stack = [(0, len(points) - 1)]
    while stack:
        a, b = stack.pop()
        if b <= a + 1:
            continue
        delta = points[b] - points[a]
        relative = points[a + 1:b] - points[a]
        length_squared = float(np.dot(delta, delta))
        if length_squared:
            t = np.clip(np.dot(relative, delta) / length_squared, 0, 1)
            distances = np.sum((relative - t[:, None] * delta) ** 2, axis=1)
        else:
            distances = np.sum(relative ** 2, axis=1)
        i = int(np.argmax(distances)) + a + 1
        if distances[i - a - 1] > tolerance ** 2:
            keep[i] = True
            stack.extend(((a, i), (i, b)))
    return points[keep]


def meaningful(points):
    # Small isolated rings disappear below a display pixel at world scale.
    span = np.ptp(points, axis=0)
    return len(points) >= 3 and float(span[0] * span[1]) >= 0.06


def encode_path(points, closed):
    points = simplify(points)
    if closed and np.array_equal(points[0], points[-1]):
        points = points[:-1]
    quantized = np.rint(points / [WIDTH, HEIGHT] * 65535).clip(0, 65535).astype('<u2')
    return struct.pack('<BI', int(closed), len(points)) + quantized.tobytes(), len(points)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=pathlib.Path, default=pathlib.Path('/tmp/etopo2022-15min.nc'))
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / 'Resources' / 'WorldMap')
    args = parser.parse_args()
    if not args.source.exists():
        args.source.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(SOURCE_URL, timeout=180) as response:
            args.source.write_bytes(response.read(8 * 1024 * 1024))
    with netcdf_file(args.source, 'r', mmap=False) as dataset:
        z = dataset.variables['z'].data.copy().astype(np.float64)[::-1]
        lat = dataset.variables['latitude'].data.copy()[::-1]
        lon = dataset.variables['longitude'].data.copy()
    assert z.shape == (720, 1440) and np.isfinite(z).all()
    # A sub-pixel blur removes source-grid stair steps, without inventing relief.
    z = gaussian_filter(z, sigma=0.65, mode=('nearest', 'wrap'))
    # Close the longitude seam and extend the polar edge to the world rectangle.
    z = np.pad(z, ((1, 1), (1, 1)), mode='edge')
    z[:, 0], z[:, -1] = z[:, -2], z[:, 1]
    x = np.r_[0, (lon + 180) / 360 * WIDTH, WIDTH]
    y = np.r_[0, (90 - lat) / 180 * HEIGHT, HEIGHT]
    cg = contourpy.contour_generator(x=x, y=y, z=z, name='serial', line_type='Separate', fill_type='OuterOffset')
    result = bytearray(b'EHUDMAP1') + struct.pack('<I', len(LEVELS))
    statistics = []
    total_points = 0
    for level in LEVELS:
        lines = [p for p in cg.lines(level) if meaningful(p)]
        fills = []
        if level >= 0:
            vertices, offsets = cg.filled(level, 20000)
            for points, rings in zip(vertices, offsets):
                for a, b in zip(rings[:-1], rings[1:]):
                    ring = points[a:b]
                    if meaningful(ring):
                        fills.append(ring)
        result += struct.pack('<iII', level, len(lines), len(fills))
        vertices_count = 0
        for points, closed in [(p, np.array_equal(p[0], p[-1])) for p in lines] + [(p, True) for p in fills]:
            encoded, count = encode_path(points, closed)
            result += encoded
            vertices_count += count
        total_points += vertices_count
        statistics.append({'elevation_m': level, 'contour_paths': len(lines), 'fill_rings': len(fills), 'vertices': vertices_count})
    assert total_points <= 150000, total_points
    assert len(result) < 3 * 1024 * 1024
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'Terrain.bin').write_bytes(result)
    manifest = {
        'source': 'NOAA ETOPO 2022 Global Relief Model',
        'source_url': SOURCE_URL,
        'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
        'citation': 'NOAA National Centers for Environmental Information. 2022: ETOPO 2022 15 Arc-Second Global Relief Model. doi:10.25921/fd45-gt74. Accessed 2026-09-27.',
        'license': 'CC0-1.0',
        'license_url': 'https://www.ncei.noaa.gov/access/metadata/landing-page/bin/iso?id=gov.noaa.ngdc.mgg.dem%3Aetopo_2022',
        'projection': 'equirectangular; x=0 is longitude -180; y=0 is latitude 90',
        'world_size': [WIDTH, HEIGHT],
        'sample_interval_degrees': 0.25,
        'simplification_tolerance_map_units': 0.10,
        'vertices': total_points,
        'bytes': len(result),
        'sha256': hashlib.sha256(result).hexdigest(),
        'bands': statistics,
    }
    (args.output / 'SOURCES.json').write_text(json.dumps(manifest, indent=2) + '\n')
    print(json.dumps({'bytes': len(result), 'vertices': total_points, 'bands': statistics}, indent=2))


if __name__ == '__main__':
    main()
