#!/usr/bin/env python3
"""Package exact Notes toolbar icon rasters from an explicit isolated Mac export.

This build-only tool reads pinned source/export inputs and writes a NEW bundle.
It never opens a HUD, user store, desktop or network, and never redraws an icon.
The prepared dark-theme/scale-2 bindings are deliberately not a general tint or
image service. The native owner must match every dependency before using them.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import struct
import tempfile
import zlib


MAX_JSON = 4 * 1024 * 1024
MAX_RASTER = 128 * 1024
MAX_SOURCE = 64 * 1024 * 1024
IDENTITY = [[1, 0, 0, 0], [0, 1, 0, 0], [0, 0, 1, 0], [0, 0, 0, 1]]
ICONS = (
    ("tool:text/icon", "Operational_Manual_icon", "notes/default/host/0/0/2/0/1"),
    ("tool:todo/icon", "Mission_Icon", "notes/default/host/0/0/2/1/1"),
)
EXPORTERS = ("windows/tools/module_reference.sh", "windows/tools/module_reference.swift",
             "windows/tools/module_reference_layers.swift")
DEPENDENT_SOURCES = ("Sources/NotesCanvas.swift", "Sources/EndfieldGameIcon.swift")


def fail(message):
    raise ValueError(message)


def need(condition, message):
    if not condition:
        fail(message)


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def pairs(items):
    result = {}
    for key, value in items:
        need(key not in result, "Duplicate JSON key")
        result[key] = value
    return result


def parse(data):
    return json.loads(data, object_pairs_hook=pairs,
                      parse_constant=lambda _: fail("Non-JSON number"))


def encode(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"),
                      allow_nan=False).encode("utf-8")


def safe_path(root, relative):
    need(isinstance(relative, str) and 0 < len(relative) <= 512 and
         "\\" not in relative and ":" not in relative and "\0" not in relative,
         "Invalid confined package path")
    parts = relative.split("/")
    need(not any(p in ("", ".", "..") for p in parts), "Package path escapes root")
    path = root
    for part in parts:
        path = path / part
        need(not path.is_symlink(), "Package symlink is not allowed")
    return path


def read(root, relative, limit):
    path = safe_path(root, relative)
    need(path.is_file() and path.stat().st_size <= limit, "Missing or oversized input: " + relative)
    with path.open("rb") as handle:
        data = handle.read(limit + 1)
    need(len(data) <= limit, "Input grew beyond limit")
    return data


def pinned_sources(source, provenance, authority):
    need(provenance.get("sourceAndResourcesMatchBaseline") is True,
         "Source export did not match its Mac baseline")
    baseline = provenance.get("baselineRequested", "")
    commit = authority.get("commit", "")
    need(isinstance(baseline, str) and re.fullmatch(r"[0-9a-f]{7,40}", baseline) and
         isinstance(commit, str) and re.fullmatch(r"[0-9a-f]{40}", commit) and commit.startswith(baseline),
         "Source authority differs from the exported baseline")
    exporters = provenance.get("exporterSHA256", {})
    originals = provenance.get("sourceAndResourceSHA256", {})
    need(isinstance(exporters, dict) and set(exporters) == set(EXPORTERS) and
         isinstance(originals, dict) and 0 < len(originals) <= 10000,
         "Incomplete source/exporter provenance")
    for relative, expected in list(exporters.items()) + list(originals.items()):
        need(isinstance(expected, str) and re.fullmatch(r"[0-9a-f]{64}", expected), "Invalid source SHA-256")
        need(relative in exporters or relative.startswith(("Sources/", "Resources/")),
             "Provenance contains a non-source path")
        need(sha256(read(source, relative, MAX_SOURCE)) == expected,
             "Source/exporter provenance mismatch: " + relative)
    dependent = list(DEPENDENT_SOURCES) + ["Resources/AppIconSources/EndfieldWiki/" + name + ".png"
                                          for _, name, _ in ICONS]
    need(all(relative in originals for relative in dependent), "Missing icon source provenance")
    return {relative: originals[relative] for relative in dependent}


def verify_source_contract(source):
    notes = read(source, "Sources/NotesCanvas.swift", MAX_JSON).decode("utf-8")
    icons = read(source, "Sources/EndfieldGameIcon.swift", MAX_JSON).decode("utf-8")
    # Exact unchanged-source expressions prove the prepared tint/geometry and
    # the source-in operation. A changed source requires a new export/contract;
    # this tool must not silently label old pixels as a new dependency.
    for expression in (
        "private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }",
        "let color = primary", "let iconRect = CGRect(x: 9, y: 5, width: 18, height: 18)",
        'action.id == "tool:text" ? .operationalManual : action.id == "tool:todo" ? .mission : nil',
        "tint: color, contentsScale: scale"):
        need(expression in notes, "Notes source icon contract changed")
    for expression in (
        'case mission = "Mission_Icon"', 'case operationalManual = "Operational_Manual_icon"',
        "max(rect.width, rect.height) * contentsScale", "context.setBlendMode(.sourceIn)",
        "context.setFillColor(tint.cgColor)", "context.fill(bounds)",
        "layer.contentsGravity = .resizeAspect"):
        need(expression in icons, "Original game icon preparation contract changed")


def icon_nodes(document):
    result = {}
    seen = set()
    count = 0
    def visit(node, depth):
        nonlocal count
        need(isinstance(node, dict) and depth <= 32, "Invalid source layer tree")
        count += 1
        need(count <= 20000, "Source layer tree exceeds bounds")
        identity = node.get("id")
        need(isinstance(identity, str) and identity not in seen, "Missing or duplicate source layer ID")
        seen.add(identity)
        if identity in {row[2] for row in ICONS}:
            result[identity] = node
        children = node.get("children", [])
        need(isinstance(children, list), "Invalid source layer children")
        for child in children:
            visit(child, depth + 1)
    roots = document.get("roots")
    need(isinstance(roots, list) and len(roots) <= 16, "Invalid source roots")
    for root in roots:
        visit(root["layer"], 0)
    need(set(result) == {row[2] for row in ICONS}, "Missing exact original Notes icon nodes")
    return result


def verify_layer(node, name):
    expected = {"kind": "layer", "class": "CALayer", "name": "endfield.icon." + name,
                "bounds": [0, 0, 18, 18], "frame": [9, 5, 18, 18], "position": [18, 14],
                "anchorPoint": [.5, .5], "anchorPointZ": 0, "zPosition": 0,
                "contentsScale": 2, "contentsGravity": "resizeAspect", "contentsFormat": "RGBA8",
                "contentsRect": [0, 0, 1, 1], "contentsCenter": [0, 0, 1, 1],
                "opacity": 1, "hidden": False, "contentsAreFlipped": False, "geometryFlipped": False,
                "borderWidth": 0, "cornerRadius": 0, "backgroundColor": None,
                "mask": None, "masksToBounds": False, "shadowOpacity": 0,
                "transform": IDENTITY, "sublayerTransform": IDENTITY, "children": []}
    need(all(key in node and node[key] == value for key, value in expected.items()),
         "Original icon layer geometry/preparation changed: " + name)
    contents = node.get("contents")
    need(isinstance(contents, dict) and set(contents) == {"asset", "sha256"} and
         isinstance(contents["sha256"], str) and re.fullmatch(r"[0-9a-f]{64}", contents["sha256"]) and
         contents["asset"] == "raster/" + contents["sha256"] + ".png", "Invalid original icon contents")
    return contents


def verify_png(data):
    need(data[:8] == b"\x89PNG\r\n\x1a\n", "Prepared icon is not PNG")
    at = 8
    header = False
    ended = False
    compressed = bytearray()
    while at < len(data):
        need(at + 12 <= len(data), "Truncated PNG chunk")
        size = struct.unpack_from(">I", data, at)[0]
        need(size <= MAX_RASTER and at + size + 12 <= len(data), "Invalid PNG chunk length")
        kind = data[at + 4:at + 8]
        payload = data[at + 8:at + 8 + size]
        expected_crc = struct.unpack_from(">I", data, at + 8 + size)[0]
        need(zlib.crc32(kind + payload) & 0xffffffff == expected_crc, "PNG chunk checksum mismatch")
        if not header:
            need(kind == b"IHDR" and size == 13 and
                 struct.unpack(">IIBBBBB", payload) == (36, 36, 8, 6, 0, 0, 0),
                 "Prepared PNG must be exact 36x36 RGBA8 non-interlaced pixels")
            header = True
        else:
            need(kind != b"IHDR", "Duplicate PNG header")
        if kind == b"IDAT":
            compressed.extend(payload)
        if kind == b"IEND":
            need(size == 0 and at + 12 == len(data), "PNG has trailing bytes")
            ended = True
            break
        need(kind[0] & 32 or kind in (b"IHDR", b"IDAT", b"PLTE"), "Unsupported critical PNG chunk")
        at += size + 12
    need(header and ended and compressed, "Incomplete prepared PNG")
    inflater = zlib.decompressobj()
    expected_bytes = 36 * (1 + 36 * 4)
    pixels = inflater.decompress(compressed, expected_bytes + 1)
    need(inflater.eof and not inflater.unconsumed_tail and not inflater.unused_data and
         len(pixels) == expected_bytes and all(pixels[y * 145] <= 4 for y in range(36)),
         "Invalid or oversized prepared PNG pixel stream")


def build(export_root, source_root, new_output):
    root, source, target = (pathlib.Path(value).absolute() for value in (export_root, source_root, new_output))
    need(root.is_dir() and source.is_dir() and not root.is_symlink() and not source.is_symlink(),
         "Explicit ordinary export/source directories required")
    need(not target.exists() and not target.is_symlink(), "New output directory required")
    manifest_bytes = read(root, "modules.json", MAX_JSON)
    provenance_bytes = read(root, "provenance.json", MAX_JSON)
    authority_bytes = read(source, "windows/source-authority.json", MAX_JSON)
    manifest, provenance, authority = map(parse, (manifest_bytes, provenance_bytes, authority_bytes))
    need(manifest.get("schemaVersion") == 1 and authority.get("schema") == 1,
         "Unsupported source metadata schema")
    sources = pinned_sources(source, provenance, authority)
    need(manifest.get("sourceBaseline") == provenance["baselineRequested"], "Module source baseline mismatch")
    fixture = manifest.get("fixture", {})
    need(fixture.get("theme") == "dark" and fixture.get("scale") == 2,
         "Only exact exported dark/scale-2 icon variants are available")
    isolation = manifest.get("isolation", {})
    need(isolation.get("temporaryStoresOnly") is True and isolation.get("isolatedPreferencesSuite") is True and
         all(isolation.get(key) is False for key in ("clipboardAccessed", "liveProvidersCreated", "profileActivated",
                                                  "systemHUDViewCreated", "windowCreated")),
         "Source export isolation is not established")
    verify_source_contract(source)
    entries = [row for row in manifest.get("entries", []) if row.get("module") == "notes" and row.get("state") == "default"]
    need(len(entries) == 1 and entries[0].get("file") == "module-notes-default.json", "Missing unique Notes source export")
    document_bytes = read(root, entries[0]["file"], MAX_JSON)
    document = parse(document_bytes)
    need(document.get("schemaVersion") == 1 and document.get("module") == "notes" and document.get("state") == "default",
         "Wrong original Notes document")
    nodes = icon_nodes(document)
    rasters = manifest.get("rasterAssets", [])
    need(isinstance(rasters, list) and len(rasters) <= 512, "Source raster table exceeds bounds")
    assets = {row["path"]: row for row in rasters}
    need(len(assets) == len(rasters), "Duplicate source raster descriptor")
    images = []
    files = {}
    for layer, name, original_id in ICONS:
        contents = verify_layer(nodes[original_id], name)
        need(not any(row.get("node") == original_id for row in manifest.get("unsupported", [])),
             "Source icon has unsupported export effects")
        asset = assets.get(contents["asset"], {})
        need(asset.get("sha256") == contents["sha256"] and asset.get("width") == 36 and asset.get("height") == 36 and
             asset.get("sourceBitsPerComponent") == 8 and asset.get("sourceBitsPerPixel") == 32 and
             asset.get("sourceAlphaInfo") == 1 and asset.get("sourceColorSpace") == "kCGColorSpaceDeviceRGB",
             "Source raster preparation metadata differs from the icon dependency")
        data = read(root, contents["asset"], MAX_RASTER)
        need(sha256(data) == contents["sha256"], "Original raster integrity mismatch")
        verify_png(data)
        files[contents["asset"]] = data
        dependency = {"layerID": layer, "sourceResource": "AppIconSources/EndfieldWiki/" + name + ".png",
                      "rect": [9, 5, 18, 18], "tint": [.94, .94, .94, 1], "requestedPixels": 36,
                      "sourceInTint": True, "resizeAspect": True}
        images.append({"dependency": dependency, "contents": contents, "sourceLayerID": original_id,
                       "raster": {"file": contents["asset"], "sha256": contents["sha256"], "bytes": len(data),
                                  "width": 36, "height": 36}})
    output = {"format": "endfield-notes-controls-assets", "schemaVersion": 1,
              "preparedFor": {"theme": "dark", "contentsScale": 2}, "images": images,
              "source": {"release": authority["release"], "build": authority["build"], "commit": authority["commit"]},
              "sourcePins": {"moduleSHA256": sha256(document_bytes), "manifestSHA256": sha256(manifest_bytes),
                             "provenanceSHA256": sha256(provenance_bytes), "authoritySHA256": sha256(authority_bytes),
                             "sourceAndResourceSHA256": sources},
              "scope": "Exact original dark/scale-2 Text and TODO toolbar artwork; no fallback, retint or color-wheel asset"}
    metadata = encode(output)
    need(len(metadata) <= 64 * 1024, "Prepared binding manifest exceeds bounds")
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = pathlib.Path(tempfile.mkdtemp(prefix=target.name + ".tmp-", dir=target.parent))
    try:
        for relative, data in files.items():
            path = safe_path(staging, relative)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        (staging / "notes-controls-assets.json").write_bytes(metadata)
        need(not target.exists() and not target.is_symlink(), "Output appeared while building")
        staging.rename(target)
    finally:
        if staging.exists():
            shutil.rmtree(staging)
    return {"images": len(images), "rasterAssets": len(files), "bytes": len(metadata) + sum(map(len, files.values())),
            "manifestSHA256": sha256(metadata)}


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("export_root")
    parser.add_argument("source_root")
    parser.add_argument("new_output")
    arguments = parser.parse_args()
    print(json.dumps(build(arguments.export_root, arguments.source_root, arguments.new_output), sort_keys=True))
