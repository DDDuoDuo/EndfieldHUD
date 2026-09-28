#!/usr/bin/env python3
"""Prepare bounded, offline Natural Earth country outlines for the HUD.

Developer dependencies: pyshp, shapely. No dependencies or downloads run in-app.
The requested China presentation groups CHN, TWN, HKG and MAC source geometries;
the source classifications remain recorded in Countries-SOURCES.json.
"""
import argparse
import hashlib
import io
import json
import pathlib
import struct
import urllib.request
import zipfile

import shapefile
from shapely.geometry import shape
from shapely.ops import transform, unary_union
from shapely import make_valid

SOURCE_URL = 'https://naturalearth.s3.amazonaws.com/10m_cultural/ne_10m_admin_0_countries.zip'
SOURCE_PAGE = 'https://www.naturalearthdata.com/downloads/10m-cultural-vectors/10m-admin-0-countries/'
SOURCE_LICENSE = 'https://www.naturalearthdata.com/about/terms-of-use/'
PREFIX = 'ne_10m_admin_0_countries'
CHINA_SOURCE_IDS = ['CHN', 'TWN', 'HKG', 'MAC']
WIDTH, HEIGHT = 1024.0, 512.0
QUANTIZATION = 16777215
GENERAL_TOLERANCE = 0.085
CHINA_TOLERANCE = 0.006
MAX_BYTES = 1024 * 1024
MAX_VERTICES = 100000


def polygons(geometry):
    if geometry.geom_type == 'Polygon':
        yield geometry
    elif hasattr(geometry, 'geoms'):
        for child in geometry.geoms:
            yield from polygons(child)


def encode_ring(ring):
    points = []
    for x, y in ring.coords:
        point = (round(min(WIDTH, max(0, x)) / WIDTH * QUANTIZATION),
                 round(min(HEIGHT, max(0, y)) / HEIGHT * QUANTIZATION))
        if not points or points[-1] != point:
            points.append(point)
    if points and points[0] == points[-1]:
        points.pop()
    if len(set(points)) < 3:
        return None
    encoded = struct.pack('<I', len(points)) + b''.join(struct.pack('<II', *p) for p in points)
    return encoded, len(points)


def encode_country(identifier, name, geometry):
    components = []
    vertices = 0
    for polygon in polygons(geometry):
        exterior = encode_ring(polygon.exterior)
        if exterior is None:
            continue
        rings = [exterior] + [encoded for ring in polygon.interiors if (encoded := encode_ring(ring))]
        components.append(struct.pack('<I', len(rings)) + b''.join(ring[0] for ring in rings))
        vertices += sum(ring[1] for ring in rings)
    assert components, identifier
    identifier_bytes, name_bytes = identifier.encode('ascii'), name.encode('utf-8')
    assert 1 <= len(identifier_bytes) <= 12 and 1 <= len(name_bytes) <= 128
    encoded = (struct.pack('<B', len(identifier_bytes)) + identifier_bytes + struct.pack('<H', len(name_bytes)) + name_bytes
               + struct.pack('<I', len(components)) + b''.join(components))
    return encoded, len(components), vertices


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=pathlib.Path, default=pathlib.Path('/tmp/ne_10m_admin_0_countries.zip'))
    parser.add_argument('--output', type=pathlib.Path, default=pathlib.Path(__file__).resolve().parents[1] / 'Resources' / 'WorldMap')
    args = parser.parse_args()
    if not args.source.exists():
        args.source.parent.mkdir(parents=True, exist_ok=True)
        with urllib.request.urlopen(SOURCE_URL, timeout=120) as response:
            raw = response.read(10 * 1024 * 1024 + 1)
        assert len(raw) <= 10 * 1024 * 1024
        args.source.write_bytes(raw)
    records, china_geometries, source_count = [], [], 0
    with zipfile.ZipFile(args.source) as archive:
        version = archive.read(PREFIX + '.VERSION.txt').decode().strip()
        reader = shapefile.Reader(shp=io.BytesIO(archive.read(PREFIX + '.shp')),
                                 dbf=io.BytesIO(archive.read(PREFIX + '.dbf')),
                                 shx=io.BytesIO(archive.read(PREFIX + '.shx')), encoding='utf-8')
        source_count = len(reader)
        for item in reader.iterShapeRecords():
            attributes = item.record.as_dict()
            identifier = attributes['ADM0_A3']
            geometry = transform(lambda x, y: ((x + 180) / 360 * WIDTH, (90 - y) / 180 * HEIGHT), shape(item.shape.__geo_interface__))
            if not geometry.is_valid:
                geometry = make_valid(geometry)
            if identifier in CHINA_SOURCE_IDS:
                china_geometries.append(geometry)
                continue
            records.append((identifier, attributes['NAME_EN'], geometry, [identifier]))
    records.append(('CHN', 'China', unary_union(china_geometries), CHINA_SOURCE_IDS))
    records.sort(key=lambda value: value[0])
    assert len({r[0] for r in records}) == len(records)
    result = bytearray(b'EHUDCTY1') + struct.pack('<I', len(records))
    statistics, total_vertices = [], 0
    for identifier, name, geometry, source_ids in records:
        tolerance = CHINA_TOLERANCE if identifier == 'CHN' else GENERAL_TOLERANCE
        geometry = geometry.simplify(tolerance, preserve_topology=True)
        encoded, components, vertices = encode_country(identifier, name, geometry)
        result += encoded
        total_vertices += vertices
        statistics.append({'id': identifier, 'name': name, 'source_ids': source_ids, 'components': components, 'vertices': vertices})
    assert len(result) <= MAX_BYTES, len(result)
    assert total_vertices <= MAX_VERTICES, total_vertices
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / 'Countries.bin').write_bytes(result)
    manifest = {
        'source': 'Natural Earth 1:10m Admin 0 Countries', 'version': version,
        'source_url': SOURCE_URL, 'source_page': SOURCE_PAGE,
        'source_sha256': hashlib.sha256(args.source.read_bytes()).hexdigest(),
        'license': 'Public domain', 'license_url': SOURCE_LICENSE,
        'accessed': '2026-09-27', 'source_records': source_count,
        'presentation_grouping': {
            'CHN': {'name': 'China', 'source_ids': CHINA_SOURCE_IDS,
                    'reason': "Application presentation requested by the user: group Taiwan, Hong Kong and Macao components with China. This is a derivative grouping, not a change to Natural Earth's source classification."}
        },
        'projection': 'equirectangular; x=0 is longitude -180; y=0 is latitude 90',
        'world_size': [WIDTH, HEIGHT], 'coordinate_quantization': QUANTIZATION,
        'general_simplification_tolerance_map_units': GENERAL_TOLERANCE,
        'china_simplification_tolerance_map_units': CHINA_TOLERANCE,
        'countries': len(records), 'vertices': total_vertices, 'bytes': len(result),
        'sha256': hashlib.sha256(result).hexdigest(), 'records': statistics,
    }
    (args.output / 'Countries-SOURCES.json').write_text(json.dumps(manifest, indent=2, ensure_ascii=False) + '\n')
    print(json.dumps({k: manifest[k] for k in ['countries', 'vertices', 'bytes', 'sha256']}))


if __name__ == '__main__':
    main()
