#!/usr/bin/env python3
"""Synthetic filesystem/numeric tests; no HUD, source resources or user stores."""
import hashlib
import importlib.util
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.dont_write_bytecode = True
spec = importlib.util.spec_from_file_location("runtime_builder", pathlib.Path(__file__).parents[1] / "tools/build_watch_runtime_input.py")
builder = importlib.util.module_from_spec(spec)
spec.loader.exec_module(builder)


class RuntimeBuildTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="endfield-runtime-builder-test-")
        self.root = pathlib.Path(self.temporary.name) / "source"
        self.root.mkdir()
        self.output = self.root.parent / "output"
        self.image = b"synthetic owned image bytes"
        self.image_hash = hashlib.sha256(self.image).hexdigest()
        self.image_file = "raster/" + self.image_hash + ".png"
        self.animation = {
            "scene": {"root_node_id": "n", "nodes": [{"id": "n", "name": "node", "path": "node", "parent_id": None, "child_ids": [],
                "game_object": {"data": {"m_IsActive": True, "unused": "archive"}}, "transform": {"type": "Transform", "raw": {"m_LocalPosition": {"x": builder.Number("-0.0"), "y": 0, "z": 0}}}}]},
            "mountedDocument": {"components": {}, "buttons": [], "animators": [], "spriteByComponent": {"image": {
                "id": "sprite", "name": "source", "raw_sprite": {"m_Rect": {"x": builder.Number("0.100000000000000005551"), "y": 0, "width": 1, "height": 2}, "m_Border": {"x": 1, "y": 2, "z": 3, "w": 4}, "m_PixelsToUnits": 100, "m_RD": {"archive": "discard"}}, "texture": {"id": "tex", "payload": "discard"}, "decoded_render_mesh": "discard"}}},
            "library": {"clips": []}, "runtimeRoot": [], "frameBuilder": {"desktopSettings": {}, "scope": "discard"}, "controllerTransitions": {}}
        self.native = {"nativeLayers": {"children": [{"contents": {"asset": self.image_file, "sha256": self.image_hash}}]}, "nativeNavigation": [], "nativeProfileBindings": [], "batches": ["oracle-only"]}
        self.save_fixture()

    def tearDown(self):
        self.temporary.cleanup()

    def put(self, file, data):
        path = self.root / file
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return {"file": file, "bytes": len(data), "sha256": hashlib.sha256(data).hexdigest()}

    def save_fixture(self):
        self.manifest = {"animation": self.put("animation.json", builder.encode(self.animation).encode()), "frames": [], "nativeRasterAssets": [self.put(self.image_file, self.image)]}
        for name in ("top", "bottom"):
            descriptor = self.put("frame/" + name + ".json", builder.encode(self.native).encode())
            descriptor["name"] = "desktop-shell-1280x800-" + name
            self.manifest["frames"].append(descriptor)
        settings = self.put("frame/opening-4.json", builder.encode({"builderInput": {"desktopSettings": {"settingsOrigin": "initial"}}}).encode())
        settings["name"] = "desktop-shell-1280x800-opening-4"
        self.manifest["frames"].append(settings)
        (self.root / "shell-packet.json").write_text(json.dumps(self.manifest))

    def test_lossless_projected_output(self):
        result = builder.build(self.root, self.output)
        self.assertEqual(result["rasterAssets"], 1)
        mounted = (self.output / "parts/mountedDocument.json").read_text()
        self.assertIn("0.100000000000000005551", mounted)
        self.assertNotIn("discard", mounted)
        self.assertIn("-0.0", (self.output / "parts/scene.json").read_text())
        self.assertNotIn("oracle-only", (self.output / "parts/nativeTop.json").read_text())
        self.assertIn('"settingsOrigin":"initial"', (self.output / "parts/frameBuilder.json").read_text())
        manifest = json.loads((self.output / "runtime-input.json").read_text())
        for descriptor in list(manifest["parts"].values()) + manifest["rasterAssets"]:
            data = (self.output / descriptor["file"]).read_bytes()
            self.assertEqual(len(data), descriptor["bytes"])
            self.assertEqual(hashlib.sha256(data).hexdigest(), descriptor["sha256"])
        self.assertEqual(list(self.output.glob("**/*.bin")), [])

    def test_existing_output_rejected(self):
        self.output.mkdir()
        sentinel = self.output / "keep"
        sentinel.write_text("keep")
        with self.assertRaises(ValueError):
            builder.build(self.root, self.output)
        self.assertEqual(sentinel.read_text(), "keep")

    def test_hash_and_length_enforced(self):
        (self.root / "animation.json").write_text("{}")
        with self.assertRaisesRegex(ValueError, "integrity"):
            builder.build(self.root, self.output)
        self.assertFalse(self.output.exists())

    def test_missing_raster_rejected(self):
        self.manifest["nativeRasterAssets"] = []
        (self.root / "shell-packet.json").write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, "Unresolved"):
            builder.build(self.root, self.output)

    def test_path_confinement_and_symlink(self):
        for value in ("../outside", "/outside", "a//b", "a/./b", "C:outside", "a\\b"):
            with self.assertRaises(ValueError):
                builder.safe_path(self.root, value)
        try:
            (self.root / "alias").symlink_to(self.root / "animation.json")
        except (OSError, NotImplementedError):
            return  # Windows without symlink privilege still tests all paths.
        with self.assertRaisesRegex(ValueError, "symlink"):
            builder.safe_path(self.root, "alias")

    def test_duplicate_keys_and_non_json_numbers(self):
        for value in ('{"x":1,"x":2}', '{"x":NaN}', '{"x":Infinity}'):
            with self.assertRaises(ValueError):
                builder.parse(value)
        self.assertEqual(builder.encode(builder.parse('{"x":-0.000e+9,"id":18446744073709551616}')), '{"id":18446744073709551616,"x":-0.000e+9}')

    def test_deterministic_repeat(self):
        first = builder.build(self.root, self.output)
        other = self.root.parent / "other"
        second = builder.build(self.root, other)
        self.assertEqual(first, second)
        for path in self.output.rglob("*"):
            if path.is_file():
                self.assertEqual(path.read_bytes(), (other / path.relative_to(self.output)).read_bytes())

    def compiler(self):
        path = self.root.parent / "explicit-compiler"
        path.write_bytes(b"unit-test compiler placeholder; never executed")
        return path

    @staticmethod
    def compiled_empty(args, **kwargs):
        # Mock only process execution. The builder must still validate the
        # real envelope, bind source identity and publish bounded descriptors.
        payload = (0).to_bytes(4, "little")
        header = (b"EHANIM01" + (1).to_bytes(4, "little") + (0x01020304).to_bytes(4, "little") +
                  len(payload).to_bytes(8, "little") + bytes.fromhex(args[2]) + hashlib.sha256(payload).digest())
        pathlib.Path(args[3]).write_bytes(header + payload)
        return mock.Mock(returncode=0, stderr="", stdout="")

    def test_compiled_library_manifest_and_no_json_duplicate(self):
        with mock.patch.object(builder.subprocess, "run", side_effect=self.compiled_empty) as run:
            builder.build(self.root, self.output, animation_compiler=self.compiler())
        manifest = json.loads((self.output / "runtime-input.json").read_text())
        self.assertEqual(manifest["schemaVersion"], 2)
        descriptor = manifest["parts"]["library"]
        self.assertEqual(descriptor["encoding"], "endfield-animation-v1")
        self.assertEqual(descriptor["file"], "parts/library.ehanim")
        self.assertFalse((self.output / "parts/library.json").exists())
        data = (self.output / descriptor["file"]).read_bytes()
        self.assertEqual(data[24:56].hex(), manifest["sourcePins"]["sourceManifestSHA256"])
        self.assertEqual(descriptor["sha256"], hashlib.sha256(data).hexdigest())
        self.assertEqual(run.call_count, 1)

    def test_compiler_failure_and_wrong_pin_are_atomic(self):
        compiler = self.compiler()
        def invalid(args, **kwargs):
            result = self.compiled_empty(args, **kwargs)
            path = pathlib.Path(args[3])
            data = bytearray(path.read_bytes())
            data[24] ^= 1
            path.write_bytes(data)
            return result
        for effect, reason in ((lambda *a, **k: mock.Mock(returncode=1, stderr="invalid key"), "rejected"),
                               (invalid, "unpinned envelope")):
            with mock.patch.object(builder.subprocess, "run", side_effect=effect):
                with self.assertRaisesRegex(ValueError, reason):
                    builder.build(self.root, self.output, animation_compiler=compiler)
            self.assertFalse(self.output.exists())
            self.assertEqual(list(self.root.parent.glob("output.tmp-*")), [])

    def test_missing_explicit_compiler_rejected(self):
        with self.assertRaisesRegex(ValueError, "compiler executable"):
            builder.build(self.root, self.output, animation_compiler=self.root / "missing")


if __name__ == "__main__":
    unittest.main()
