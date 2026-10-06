#!/usr/bin/env python3
"""Verify runtime closure, exact source pixels/mips, and stale/extra rejection."""
import importlib.util
import json
from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
spec = importlib.util.spec_from_file_location("watch_package", ROOT / "scripts/package-watch-resources.py")
package = importlib.util.module_from_spec(spec)
spec.loader.exec_module(package)


def main():
    source = ROOT / "Resources/WatchSource"
    with tempfile.TemporaryDirectory(prefix="endfield-watch-resources-") as directory:
        temporary = Path(directory)
        packed = temporary / "WatchSource"
        manifest = package.stage(source, packed)
        package.verify(source, packed)
        assert manifest["runtime_bytes"] < manifest["source_tree_bytes"] / 4
        # The native map needs three original marker images, not the full
        # extracted Domain scene, mesh/atlas collection or scene metadata.
        marker_files = {
            "sprites/icon_char---2308601083109874541.png",
            "sprites/deco_readio_mask--2444265073359569955.png",
            "textures/T_fx_mask_02_M--4275033587688225551.png",
        }
        domain = packed / "Scene/Domain"
        assert {p.relative_to(domain).as_posix() for p in domain.rglob("*") if p.is_file()} == marker_files
        for relative in marker_files:
            assert (domain / relative).read_bytes() == (source / "Scene/Domain" / relative).read_bytes()
        assert not (packed / "Scene/Widgets").exists()
        assert not (packed / "Scene/fonts.json").exists()
        assert not (packed / "Scene/labels.json").exists()
        assert not list(packed.rglob("*.spv"))
        assert not list(packed.rglob("*.reflection.json"))
        selection = json.loads((packed / "runtime-selection.json").read_bytes())
        textures = json.loads((source / "textures.json").read_bytes())
        materials = json.loads((source / "materials.json").read_bytes())
        compact_data = package.decoded((packed / "runtime-materials.json").read_bytes())
        compact = json.loads(compact_data)
        assert compact["source_indices"] == selection["material_indices"]
        assert compact["source_sha256"] == package.digest((source / "materials.json").read_bytes())
        assert compact["materials"] == [materials[i] for i in selection["material_indices"]]
        assert compact_data == package.runtime_materials(source, selection)
        assert len(compact_data) < (source / "materials.json").stat().st_size / 2
        assert not (packed / "materials.json").exists()
        assert "materials.json" in package.runtime_files(source, "reference")
        # Token compaction keeps whitespace inside escaped strings, all number
        # spellings (including negative zero), and unsafe integer/string IDs.
        tokens = ' { "value" : -0.0, "tiny":1.2345678901234567e-120, "id":"9007199254740993", "text":"a \\\" b\\\\c" } '
        assert package.compact_json_tokens(tokens) == '{"value":-0.0,"tiny":1.2345678901234567e-120,"id":"9007199254740993","text":"a \\\" b\\\\c"}'
        assert selection["source_text"] is False
        available = {"__white"}
        for index in selection["texture_indices"]:
            info = textures[index]
            available.add(info["path_id"])
            if "cab" in info:
                available.add(info["cab"] + ":" + info["path_id"])
            assert info["data_file"] in {r["path"] for r in manifest["files"]}
            if info["texture_format"] == 25:
                # Both hardware BC7 and byte-identical older-Intel fallback.
                assert info["decoded_mips_file"] in {r["path"] for r in manifest["files"]}
        for index in selection["material_indices"]:
            material = materials[index]
            assert material.get("shader", {}).get("path_id") != "2786552470741801451"
            for binding in material["texture_bindings"]:
                if binding.get("texture"):
                    assert binding["texture"]["path_id"] in available, (material["name"], binding)
        sprites = json.loads((source / "Scene/sprites.json").read_bytes())
        assert all(t["id"] in available for t in sprites["source_textures"])
        card_path = "Scene/desktop-profile-card.json"
        card = json.loads((source / card_path).read_bytes())
        assert package.decoded((packed / card_path).read_bytes()) == (source / card_path).read_bytes()
        assert all(t["id"] in available for t in card["sprites"]["source_textures"])
        selected_material_ids = {m.get("id") for m in compact["materials"]}
        assert all(m["id"] in selected_material_ids for m in card["materials"]["materials"])
        # Only the immutable card and its declared raw texture dependencies
        # ship. PNG exports embedded in source evidence are not runtime roots.
        assert not any(r["path"].startswith("Scene/Widgets/") for r in manifest["files"])
        def rejected():
            try:
                package.verify(source, packed)
            except ValueError:
                return
            raise AssertionError("Invalid runtime inventory was accepted")
        extra = packed / "obsolete-export.bin"
        extra.write_bytes(b"stale")
        rejected()
        extra.unlink()
        selected = packed / manifest["files"][0]["path"]
        original = selected.read_bytes()
        selected.write_bytes(original + b"altered")
        rejected()
        selected.write_bytes(original)
        derived = packed / "runtime-materials.json"
        derived_original = derived.read_bytes()
        changed = dict(compact)
        changed["materials"] = list(compact["materials"])
        changed["materials"][0] = dict(changed["materials"][0], name="tampered-material")
        derived.write_bytes(package.compressed(json.dumps(changed).encode()))
        rejected()
        derived.write_bytes(derived_original)
        card_packed = packed / card_path
        card_original = card_packed.read_bytes()
        card_packed.unlink()
        rejected()
        card_packed.write_bytes(card_original)
        card_texture = next(textures[i] for i in selection["texture_indices"]
                            if textures[i]["path_id"] == card["sprites"]["source_textures"][0]["path_id"])
        payload = packed / card_texture["data_file"]
        payload_original = payload.read_bytes()
        payload.write_bytes(payload_original + b"altered-card-texture")
        rejected()
        payload.write_bytes(payload_original)
        print("PASS: selected source card JSON/material/texture closure and tamper checks")
        print("PASS:", len(compact["materials"]), "compact material records preserve original JSON tokens and reject tampering")
        sdk = subprocess.check_output([str(ROOT / "scripts/build.sh"), "--print-sdk"], text=True).strip()
        binary = temporary / "NativeWatchResourceProbe"
        subprocess.run(["xcrun", "swiftc", "-swift-version", "5", "-O", "-parse-as-library", "-sdk", sdk,
                        str(ROOT / "Sources/HUDSourceScene.swift"), str(ROOT / "Tests/HUDSourceResourceDataProbe.swift"),
                        "-o", str(binary)], check=True)
        subprocess.run([str(binary), str(source), str(packed)], check=True)
        print(f"PASS: desktop resource closure and tamper checks; {manifest['source_tree_bytes']/1048576:.2f}"
              f" -> {manifest['runtime_bytes']/1048576:.2f} MiB")


if __name__ == "__main__":
    main()
