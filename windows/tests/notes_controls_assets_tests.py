#!/usr/bin/env python3
"""Synthetic bundle preparation tests; all files belong to temporary fixtures."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import struct
import sys
import tempfile
import unittest
import zlib
from unittest import mock

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location(
    "notes_assets_builder", pathlib.Path(__file__).parents[1] / "tools/build_notes_controls_assets.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)


def png(color, width=36, height=36, pixel_stream=None):
    data = pixel_stream if pixel_stream is not None else (b"\0" + bytes(color) * width) * height
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(data)) + chunk(b"IEND", b""))


class NotesAssetsTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="endfield-notes-assets-test-")
        self.base = pathlib.Path(self.temporary.name)
        self.export = self.base / "export"
        self.source = self.base / "source"
        self.output = self.base / "output"
        self.export.mkdir()
        self.source.mkdir()
        notes = '''private var primary: NSColor { NSColor(white: dark ? 0.94 : 0.11, alpha: 1) }
let color = primary
let iconRect = CGRect(x: 9, y: 5, width: 18, height: 18)
action.id == "tool:text" ? .operationalManual : action.id == "tool:todo" ? .mission : nil
tint: color, contentsScale: scale
'''
        icons = '''case mission = "Mission_Icon"
case operationalManual = "Operational_Manual_icon"
max(rect.width, rect.height) * contentsScale
context.setBlendMode(.sourceIn)
context.setFillColor(tint.cgColor)
context.fill(bounds)
layer.contentsGravity = .resizeAspect
'''
        self.originals = {"Sources/NotesCanvas.swift": notes.encode(), "Sources/EndfieldGameIcon.swift": icons.encode()}
        self.exporters = {name: ("Owned synthetic exporter: " + name).encode() for name in builder.EXPORTERS}
        for _, name, _ in builder.ICONS:
            self.originals["Resources/AppIconSources/EndfieldWiki/" + name + ".png"] = b"Owned original source resource " + name.encode()
        for relative, data in {**self.originals, **self.exporters}.items():
            self.put(self.source, relative, data)
        self.authority = {"schema": 1, "release": "v1.2.0", "build": 18, "commit": "a" * 40}
        self.provenance = {"baselineRequested": "a" * 7, "sourceAndResourcesMatchBaseline": True,
                           "exporterSHA256": {p: builder.sha256(b) for p, b in self.exporters.items()},
                           "sourceAndResourceSHA256": {p: builder.sha256(b) for p, b in self.originals.items()}}
        self.nodes = []
        self.assets = []
        self.blobs = {}
        for n, (_, name, identity) in enumerate(builder.ICONS):
            data = png((239, 239, 239, 255 - n))
            sha = builder.sha256(data)
            path = "raster/" + sha + ".png"
            node = {"id": identity, "kind": "layer", "class": "CALayer", "name": "endfield.icon." + name,
                    "bounds": [0, 0, 18, 18], "frame": [9, 5, 18, 18], "position": [18, 14],
                    "anchorPoint": [.5, .5], "anchorPointZ": 0, "zPosition": 0,
                    "contentsScale": 2, "contentsGravity": "resizeAspect", "contentsFormat": "RGBA8",
                    "contentsRect": [0, 0, 1, 1], "contentsCenter": [0, 0, 1, 1],
                    "opacity": 1, "hidden": False, "contentsAreFlipped": False, "geometryFlipped": False,
                    "borderWidth": 0, "cornerRadius": 0, "backgroundColor": None,
                    "mask": None, "masksToBounds": False, "shadowOpacity": 0,
                    "transform": builder.IDENTITY, "sublayerTransform": builder.IDENTITY, "children": [],
                    "contents": {"asset": path, "sha256": sha}}
            self.nodes.append(node)
            self.assets.append({"path": path, "sha256": sha, "width": 36, "height": 36,
                                "sourceBitsPerComponent": 8, "sourceBitsPerPixel": 32, "sourceAlphaInfo": 1,
                                "sourceColorSpace": "kCGColorSpaceDeviceRGB"})
            self.blobs[path] = data
        unused = png((100, 100, 100, 255))
        path = "raster/" + builder.sha256(unused) + ".png"
        self.assets.append({**self.assets[0], "path": path, "sha256": builder.sha256(unused)})
        self.blobs[path] = unused
        self.manifest = {"schemaVersion": 1, "sourceBaseline": "a" * 7, "fixture": {"theme": "dark", "scale": 2},
                         "isolation": {"temporaryStoresOnly": True, "isolatedPreferencesSuite": True,
                                       "clipboardAccessed": False, "liveProvidersCreated": False, "profileActivated": False,
                                       "systemHUDViewCreated": False, "windowCreated": False},
                         "entries": [{"module": "notes", "state": "default", "file": "module-notes-default.json"}],
                         "rasterAssets": self.assets, "unsupported": []}
        self.document = {"schemaVersion": 1, "module": "notes", "state": "default",
                         "roots": [{"space": "design-host", "layer": {"id": "owned-root", "children": self.nodes}}]}
        self.save()

    def tearDown(self):
        self.temporary.cleanup()

    def put(self, root, name, data):
        path = root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)

    def save(self):
        self.put(self.source, "windows/source-authority.json", builder.encode(self.authority))
        self.put(self.export, "provenance.json", builder.encode(self.provenance))
        self.put(self.export, "modules.json", builder.encode(self.manifest))
        self.put(self.export, "module-notes-default.json", builder.encode(self.document))
        for name, data in self.blobs.items():
            self.put(self.export, name, data)

    def build(self, output=None):
        return builder.build(self.export, self.source, output or self.output)

    def rejects(self, expression=None):
        self.save()
        with self.assertRaises(ValueError) if expression is None else self.assertRaisesRegex(ValueError, expression):
            self.build()
        self.assertFalse(self.output.exists())

    def test_exact_minimal_bindings_and_original_bytes(self):
        result = self.build()
        self.assertEqual(result["images"], 2)
        self.assertEqual(result["rasterAssets"], 2)
        manifest = json.loads((self.output / "notes-controls-assets.json").read_bytes())
        self.assertEqual(manifest["preparedFor"], {"theme": "dark", "contentsScale": 2})
        self.assertEqual(result["manifestSHA256"], builder.sha256((self.output / "notes-controls-assets.json").read_bytes()))
        for row, expected in zip(manifest["images"], builder.ICONS):
            dependency = row["dependency"]
            self.assertEqual(dependency["layerID"], expected[0])
            self.assertEqual(dependency["sourceResource"], "AppIconSources/EndfieldWiki/" + expected[1] + ".png")
            self.assertEqual(dependency["rect"], [9, 5, 18, 18])
            self.assertEqual(dependency["tint"], [.94, .94, .94, 1])
            self.assertEqual(dependency["requestedPixels"], 36)
            self.assertTrue(dependency["sourceInTint"] and dependency["resizeAspect"])
            data = (self.output / row["contents"]["asset"]).read_bytes()
            self.assertEqual(data, self.blobs[row["contents"]["asset"]])
            self.assertEqual(row["raster"]["bytes"], len(data))
        files = list(self.output.rglob("*"))
        self.assertEqual(sum(p.is_file() for p in files), 3)
        self.assertFalse((self.output / "module-notes-default.json").exists())
        self.assertEqual(self.build(self.base / "repeat"), result)
        for row in manifest["images"]:
            name = row["contents"]["asset"]
            self.assertEqual((self.output / name).read_bytes(), (self.base / "repeat" / name).read_bytes())

    def test_existing_output_preserved(self):
        self.output.mkdir()
        sentinel = self.output / "keep"
        sentinel.write_text("keep")
        with self.assertRaisesRegex(ValueError, "New output"):
            self.build()
        self.assertEqual(sentinel.read_text(), "keep")

    def test_source_exporter_hash_and_authority_enforced(self):
        self.put(self.source, "Sources/NotesCanvas.swift", b"changed source")
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            self.build()
        self.put(self.source, "Sources/NotesCanvas.swift", self.originals["Sources/NotesCanvas.swift"])
        self.put(self.source, builder.EXPORTERS[0], b"changed exporter")
        with self.assertRaisesRegex(ValueError, "provenance mismatch"):
            self.build()
        self.put(self.source, builder.EXPORTERS[0], self.exporters[builder.EXPORTERS[0]])
        self.authority["commit"] = "b" * 40
        self.rejects("authority differs")

    def test_pinned_but_changed_tint_source_rejected(self):
        data = self.originals["Sources/NotesCanvas.swift"].replace(b"0.94", b"0.95")
        self.put(self.source, "Sources/NotesCanvas.swift", data)
        self.provenance["sourceAndResourceSHA256"]["Sources/NotesCanvas.swift"] = builder.sha256(data)
        self.rejects("source icon contract changed")

    def test_incomplete_and_unisolated_export_rejected(self):
        self.manifest["isolation"]["clipboardAccessed"] = True
        self.rejects("isolation")
        self.manifest["isolation"]["clipboardAccessed"] = False
        self.manifest["fixture"]["theme"] = "light"
        self.rejects("dark/scale-2")
        self.manifest["fixture"]["theme"] = "dark"
        self.manifest["fixture"]["scale"] = 3
        self.rejects("dark/scale-2")
        self.manifest["fixture"]["scale"] = 2
        self.provenance["sourceAndResourcesMatchBaseline"] = False
        self.rejects("Mac baseline")

    def test_missing_duplicate_or_changed_icon_layer_rejected(self):
        original = copy.deepcopy(self.nodes[0])
        for key, value in (("frame", [9, 5, 19, 18]), ("contentsScale", 1), ("contentsGravity", "resize"),
                           ("contentsAreFlipped", True), ("opacity", .5), ("name", "replacement")):
            with self.subTest(key=key):
                self.nodes[0][key] = value
                self.rejects("geometry/preparation")
                self.nodes[0] = copy.deepcopy(original)
        self.nodes.pop()
        self.rejects("Missing exact")
        self.nodes.append(copy.deepcopy(original))
        self.rejects("duplicate source layer ID")

    def test_unsupported_icon_effect_and_duplicate_raster_rejected(self):
        self.manifest["unsupported"] = [{"node": self.nodes[0]["id"], "feature": "effect"}]
        self.rejects("unsupported export effects")
        self.manifest["unsupported"] = []
        self.assets.append(copy.deepcopy(self.assets[0]))
        self.rejects("Duplicate source raster")

    def test_raster_metadata_hash_pixels_and_crc_enforced(self):
        original = copy.deepcopy(self.assets[0])
        self.assets[0]["width"] = 35
        self.rejects("preparation metadata")
        self.assets[0] = original
        self.blobs[self.nodes[0]["contents"]["asset"]] = b"changed pixels"
        self.rejects("integrity mismatch")
        for data, error in ((png((239, 239, 239, 255), width=35), "exact 36x36"),
                            (png((239, 239, 239, 255))[:-1] + b"x", "checksum mismatch"),
                            (png((239, 239, 239, 255), pixel_stream=b"x" * 1000000), "pixel stream")):
            with self.subTest(error=error):
                sha = builder.sha256(data)
                path = "raster/" + sha + ".png"
                self.nodes[0]["contents"] = {"asset": path, "sha256": sha}
                self.assets[0] = {**original, "path": path, "sha256": sha}
                self.blobs[path] = data
                self.rejects(error)

    def test_confined_paths_and_duplicate_json(self):
        for path in ("../outside", "/outside", "a//b", "a/./b", "C:outside", "a\\b", "a\0b"):
            with self.subTest(path=path), self.assertRaises(ValueError):
                builder.safe_path(self.export, path)
        with self.assertRaisesRegex(ValueError, "Duplicate JSON key"):
            builder.parse(b'{"key":1,"key":2}')
        with self.assertRaisesRegex(ValueError, "Non-JSON number"):
            builder.parse(b'{"key":NaN}')
        self.put(self.export, "modules.json", b"x" * (builder.MAX_JSON + 1))
        with self.assertRaisesRegex(ValueError, "oversized"):
            self.build()

    def test_symlink_confinement(self):
        link = self.export / "link"
        try:
            link.symlink_to(self.source, target_is_directory=True)
        except OSError as error:
            if sys.platform == "win32" and getattr(error, "winerror", None) == 1314:
                self.skipTest("This Windows test identity lacks symlink creation privilege")
            raise
        with self.assertRaisesRegex(ValueError, "symlink"):
            builder.safe_path(self.export, "link/owned")

    def test_failed_publication_leaves_no_partial_bundle(self):
        original = pathlib.Path.write_bytes
        def failing(path, data):
            if path.name == "notes-controls-assets.json":
                raise OSError("Owned fixture write failure")
            return original(path, data)
        with mock.patch.object(pathlib.Path, "write_bytes", failing), self.assertRaises(OSError):
            self.build()
        self.assertFalse(self.output.exists())
        self.assertEqual(list(self.base.glob("output.tmp-*")), [])


if __name__ == "__main__":
    unittest.main()
