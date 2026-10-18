import importlib.util
from pathlib import Path
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("shader_translation", Path(__file__).parents[1] / "tools/translate_source_shaders.py")
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SourceShaderContracts(unittest.TestCase):
    def test_original_uniform_offsets_and_names(self):
        source = "vertex main0_out main0(constant _12& _13 [[buffer(0)]]) {}"
        metadata = {"uniforms": [{"index": 0, "name": "UnityPerFrame", "size": 368,
                                  "fields": [{"name": "unity_MatrixVP", "offset": 272}]}]}
        reflection = {"ubos": [{"name": "_12_13", "block_size": 368}]}
        result = module.metal_bindings(source, metadata, reflection)
        self.assertEqual(result[0]["hlslName"], "_12_13")
        self.assertEqual(result[0]["sourceFields"][0]["offset"], 272)
        reflection["ubos"][0]["block_size"] = 384
        with self.assertRaises(ValueError):
            module.metal_bindings(source, metadata, reflection)

    def test_unmapped_uniform_fails(self):
        with self.assertRaises(ValueError):
            module.metal_bindings("", {"uniforms": []}, {"ubos": [{"name": "unknown"}]})

    def test_texture_stage_and_sampler_match_mac_defaults(self):
        source = "texture2d<float> _44 [[texture(2)]], sampler _45 [[sampler(2)]]"
        for extra in ({}, {"stage": None, "sampler_index": None}, {"stage": "fragment", "sampler_index": 2}):
            metadata = {"textures": [{"name": "_MainTex", "index": 2, **extra}]}
            self.assertEqual(module.texture_bindings(source, metadata, "vertex"), [])
            self.assertEqual(module.texture_bindings(source, metadata, "fragment"),
                             [{"name": "_MainTex", "hlslTexture": "_44", "hlslSampler": "_45"}])

    def test_explicit_zero_sampler_stays_zero(self):
        metadata = {"textures": [{"name": "mask", "index": 2, "sampler_index": 0}]}
        result = module.texture_bindings("_44 [[texture(2)]] _45 [[sampler(0)]]", metadata, "fragment")
        self.assertEqual(result[0]["hlslSampler"], "_45")

    def test_paths_stay_inside_source_package(self):
        with tempfile.TemporaryDirectory() as temporary:
            folder = Path(temporary)
            root = folder / "source"; root.mkdir()
            (root / "shader.spv").write_bytes(b"fixture")
            (folder / "outside.spv").write_bytes(b"fixture")
            self.assertEqual(module.confined(root, "shader.spv"), (root / "shader.spv").resolve())
            for name in ("../outside.spv", str(folder / "outside.spv"), "..\\outside.spv"):
                with self.assertRaises(ValueError): module.confined(root, name)
            try: (root / "escape.spv").symlink_to(folder / "outside.spv")
            except OSError: return  # Windows may restrict creation of test symlinks.
            with self.assertRaises(ValueError): module.confined(root, "escape.spv")


if __name__ == "__main__":
    unittest.main()
