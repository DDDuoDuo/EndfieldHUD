#!/usr/bin/env python3
"""Stage the Watch runtime dependency inventory without source/extraction copies.

Compression is lossless: every transformed payload must recover the exact
source bytes, including authored mip levels, float words and signed asset IDs.
The original resource tree remains the authority and is never modified.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import zlib

MAGIC = b"EHUDZ01\0"
MAX_RESOURCE_BYTES = 128 * 1024 * 1024
INVENTORY = "runtime-inventory.json"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def compact_json_tokens(source):
    """Remove only JSON whitespace; numeric/string token bytes stay intact."""
    output, quoted, escaped = [], False, False
    for character in source:
        if quoted:
            output.append(character)
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == '"':
                quoted = False
        elif character == '"':
            quoted = True
            output.append(character)
        elif character not in " \t\r\n":
            output.append(character)
    if quoted or escaped:
        raise ValueError("Unterminated material JSON string")
    return "".join(output)


def runtime_materials(root, selection):
    """Select original record tokens without round-tripping any source number."""
    source = (root / "materials.json").read_text()
    indices = selection["material_indices"]
    if indices != sorted(set(indices)) or not indices or indices[0] < 0:
        raise ValueError("Invalid compact material selection")
    selected, chunks, found = set(indices), [], []
    decoder = json.JSONDecoder()
    position, index = 0, 0

    def skip_space(offset):
        while offset < len(source) and source[offset] in " \t\r\n":
            offset += 1
        return offset

    position = skip_space(position)
    if source[position:position + 1] != "[":
        raise ValueError("Source materials must be an array")
    position = skip_space(position + 1)
    while source[position:position + 1] != "]":
        record, end = decoder.raw_decode(source, position)
        if not isinstance(record, dict):
            raise ValueError("Invalid original material record")
        if index in selected:
            chunks.append(compact_json_tokens(source[position:end]))
            found.append(index)
        index += 1
        position = skip_space(end)
        if source[position:position + 1] == ",":
            position = skip_space(position + 1)
        elif source[position:position + 1] != "]":
            raise ValueError("Invalid source material delimiter")
    if found != indices or skip_space(position + 1) != len(source):
        raise ValueError("Compact material selection exceeds source catalog")
    header = {"schema": 1, "source_sha256": selection["materials_sha256"], "source_indices": indices}
    prefix = json.dumps(header, separators=(",", ":"))[:-1]
    return (prefix + ',"materials":[' + ",".join(chunks) + "]}\n").encode()


def compressed(data):
    compressor = zlib.compressobj(level=6, wbits=-15)
    return MAGIC + struct.pack("<Q", len(data)) + compressor.compress(data) + compressor.flush()


def decoded(data):
    if not data.startswith(MAGIC):
        return data
    if len(data) < 16:
        raise ValueError("Truncated Watch resource header")
    expected, = struct.unpack("<Q", data[8:16])
    if not 0 < expected <= MAX_RESOURCE_BYTES:
        raise ValueError("Invalid Watch resource length")
    stream = zlib.decompressobj(wbits=-15)
    result = stream.decompress(data[16:], expected + 1)
    if len(result) != expected or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ValueError("Invalid Watch resource payload")
    return result


def desktop_selection(root):
    """Keep the Watch shell's records and transitive material/texture bindings.

    The desktop adapter supplies native module content and one explicitly
    selected source profile card. Full game Domain/news widgets remain only
    in source-reference fixtures.
    Catalog records and referenced payloads are never resampled or rewritten.
    """
    def load(name):
        return json.loads((root / name).read_bytes())
    materials, textures = load("materials.json"), load("textures.json")
    scene_materials, sprites, fonts = load("Scene/materials.json"), load("Scene/sprites.json"), load("Scene/fonts.json")
    material_ids = {m["id"] for m in scene_materials["materials"]}
    names = {m["name"] for m in scene_materials["materials"]}
    card_path = root / "Scene/desktop-profile-card.json"
    card = json.loads(card_path.read_bytes()) if card_path.is_file() else None
    if card:
        material_ids.update(m["id"] for m in card["materials"]["materials"])
        names.update(m["name"] for m in card["materials"]["materials"])
    names.update(m["name"] for m in materials if m["name"].startswith("__ui_")
                 or m.get("shader", {}).get("path_id") == "2786552470741801451")
    while True:
        extra = {name for m in materials if m["name"] in names or m.get("id") in material_ids
                 for name in m.get("clip_variants", {}).values()} - names
        if not extra:
            break
        names.update(extra)
    # Desktop text is native CATextLayer content. Font metrics remain available
    # for original layout, but no source UIText batch samples an SDF atlas.
    material_indices = [i for i, m in enumerate(materials)
                        if (m["name"] in names or m.get("id") in material_ids)
                        and m.get("shader", {}).get("path_id") != "2786552470741801451"]
    texture_ids = {t["id"] for t in sprites["source_textures"]}
    if card:
        texture_ids.update(t["id"] for t in card["sprites"]["source_textures"])
    font_atlas_ids = {t["id"] for t in fonts["source_files"]["atlas_textures"]}
    for index in material_indices:
        for binding in materials[index]["texture_bindings"]:
            texture = binding.get("texture")
            if texture:
                texture_ids.add(texture["path_id"])
                if "cab" in texture:
                    texture_ids.add(texture["cab"] + ":" + texture["path_id"])
    scene_text = (root / "Scene/scene.json").read_text()
    if card:
        scene_text += json.dumps(card["scene"])
    for texture in textures:
        if texture["path_id"] in scene_text:
            texture_ids.add(texture["path_id"])
    texture_indices = [i for i, t in enumerate(textures)
                       if (t["path_id"] in texture_ids or t.get("cab", "") + ":" + t["path_id"] in texture_ids)
                       and t["path_id"] not in font_atlas_ids
                       and t.get("cab", "") + ":" + t["path_id"] not in font_atlas_ids]
    return {"schema": 1, "profile": "desktop-shell", "source_text": False, "material_indices": material_indices,
            "texture_indices": texture_indices, "materials_sha256": digest((root / "materials.json").read_bytes()),
            "textures_sha256": digest((root / "textures.json").read_bytes())}


def runtime_files(root, profile):
    """Explicit runtime roots plus paths referenced by their typed catalogs."""
    selected = set()

    def add(path):
        path = str(path)
        relative = Path(path)
        if relative.is_absolute() or ".." in relative.parts or not (root / relative).is_file():
            raise ValueError("Missing or unsafe Watch runtime dependency: " + path)
        selected.add(path)
        return path

    def document(path):
        add(path)
        return json.loads((root / path).read_bytes())

    for name in ("NOTICE.txt", "render-color-policy.json",
                 "Cursor/player-default-icon_mouse.png", "Cursor/player-default.json"):
        add(name)
    # The native map uses only these original player-marker images. Retain
    # their exact bytes without packaging the game's Domain scene or atlases.
    for name in ("sprites/icon_char---2308601083109874541.png",
                 "sprites/deco_readio_mask--2444265073359569955.png",
                 "textures/T_fx_mask_02_M--4275033587688225551.png"):
        add("Scene/Domain/" + name)
    # Native account gauge reuses three tiny original MoneyCell textures.
    for name in ("item_ap--2524b69d--8210737671276829403.png",
                 "bg_walletbar_1--cfe92272--8572312840272182184.png",
                 "bg_walletbar_2--cfe92272--7683125525266349835.png"):
        add("Scene/sprites/source-textures/" + name)
    # Thirteen exact wallet glyphs retain the game font without a full atlas.
    add("Scene/account-numerals.json")
    if profile == "reference":
        add("materials.json")
    # These are the complete shader families used by the runtime's variant
    # selector. Reflection/SPIR-V/extraction binaries are source evidence only.
    shaders = sorted(root.glob("*-shader.json"))
    if not shaders:
        raise ValueError("Missing Watch shader catalog")
    for path in shaders:
        shader = document(path.name)
        for stage in shader["stages"].values():
            add(stage["file"])
    textures = document("textures.json")
    if profile == "desktop":
        textures = [textures[i] for i in desktop_selection(root)["texture_indices"]]
    texture_ids = set()
    for texture in textures:
        add(texture["data_file"])
        if texture["texture_format"] == 25:
            add(texture["decoded_mips_file"])
        texture_ids.add(texture["path_id"])
        if "cab" in texture:
            texture_ids.add(texture["cab"] + ":" + texture["path_id"])
    for name in ("Equipring", "watchline", "Plane", "Cylinder"):
        add("Meshes/" + name + ".json")
    for name in ("scene", "clips", "sprites", "materials",
                 "watch-blur", "controller-transitions", "runtime-root-camera"):
        add("Scene/" + name + ".json")
    if (root / "Scene/desktop-profile-card.json").is_file():
        add("Scene/desktop-profile-card.json")
    if profile == "reference":
        for name in ("fonts", "labels"):
            add("Scene/" + name + ".json")
        for name in ("widget", "banner-runtime"):
            add("Scene/Widgets/" + name + ".json")
    # Retain any actual font fallback; most exported PNGs duplicate the R8
    # atlas already registered from textures.json and are never read at runtime.
    fonts = json.loads((root / "Scene/fonts.json").read_bytes())
    for atlas in fonts["source_files"]["atlas_textures"]:
        if profile == "reference" and atlas["id"] not in texture_ids:
            add("Scene/" + atlas["png"]["file"])
    if profile == "reference":
        domain = document("Scene/Domain/manifest.json")
        for name in ("level-bindings", "instances", "sprites", "texts", "texture-mips", "materials", "level-clips"):
            add("Scene/Domain/" + name + ".json")
        for record in domain["scenes"] + domain["meshes"]:
            add("Scene/Domain/" + record["file"])
    # Runtime HDR reads only the selected original Metal modules. Their
    # manifests retain all authoring evidence and original shader hashes.
    frosted = document("HDR/FrostedGlass/manifest.json")
    passes = [v for v in frosted["variants"] if v["program"] in (3, 8)] + [frosted["capture_copy"]]
    composite = document("HDR/Composite/manifest.json")
    passes += [v for v in composite["variants"] if v["program"] == 726]
    for record in passes:
        for stage in record["stages"].values():
            for file in stage["files"]:
                if file["path"].endswith(".metal"):
                    add("HDR/" + file["path"])
    for name in ("material-contract", "material-runtime", "scene"):
        add("HDR/WatchBlur/" + name + ".json")
    return selected


def inventory(root, profile="desktop"):
    # These small files are read directly by view/frame construction. Shader
    # text and cursor images also stay native; original shader hashes apply.
    native = {"Cursor/player-default.json", "Scene/runtime-root-camera.json"}
    native.update("Meshes/" + name + ".json" for name in ("Equipring", "watchline", "Plane", "Cylinder"))
    outputs, records = {}, []
    for name in sorted(runtime_files(root, profile)):
        original = (root / name).read_bytes()
        packed = original
        if (name.endswith(".json") or name.endswith(".bin")) and name not in native and original:
            if len(original) > MAX_RESOURCE_BYTES:
                raise ValueError("Watch resource exceeds decoder bound: " + name)
            candidate = compressed(original)
            if len(candidate) < len(original):
                packed = candidate
        if decoded(packed) != original:
            raise ValueError("Watch resource packing changed source bytes: " + name)
        outputs[name] = packed
        records.append({"path": name, "source_bytes": len(original), "source_sha256": digest(original),
                        "bytes": len(packed), "sha256": digest(packed),
                        "encoding": "raw-deflate-v1" if packed is not original else "identity"})
    derived = []
    if profile == "desktop":
        selection = desktop_selection(root)
        outputs["runtime-selection.json"] = (json.dumps(selection, sort_keys=True) + "\n").encode()
        material_data = runtime_materials(root, selection)
        material_packed = compressed(material_data)
        if decoded(material_packed) != material_data:
            raise ValueError("Compact material container changed metadata")
        outputs["runtime-materials.json"] = material_packed
        derived.append({"path": "runtime-materials.json", "source_path": "materials.json",
                        "source_sha256": selection["materials_sha256"], "source_indices": selection["material_indices"],
                        "policy": "selected-original-json-tokens", "decoded_bytes": len(material_data),
                        "bytes": len(material_packed), "sha256": digest(material_packed), "encoding": "raw-deflate-v1"})
    manifest = {"schema": 1, "policy": "byte-exact-runtime-dependencies", "profile": profile, "files": records,
                "derived_files": derived,
                "source_tree_bytes": sum(p.stat().st_size for p in root.rglob("*") if p.is_file()),
                "runtime_source_bytes": sum(r["source_bytes"] for r in records),
                "runtime_bytes": sum(r["bytes"] for r in records) + sum(r["bytes"] for r in derived)}
    outputs[INVENTORY] = (json.dumps(manifest, indent=2, ensure_ascii=False) + "\n").encode()
    return outputs, manifest


def verify(root, destination, profile="desktop"):
    outputs, manifest = inventory(root, profile)
    actual = {p.relative_to(destination).as_posix() for p in destination.rglob("*") if p.is_file()}
    if actual != outputs.keys():
        raise ValueError("Watch runtime inventory differs; missing=" + repr(sorted(outputs.keys() - actual))
                         + " extra=" + repr(sorted(actual - outputs.keys())))
    for name, expected in outputs.items():
        if (destination / name).is_symlink() or (destination / name).read_bytes() != expected:
            raise ValueError("Missing, stale or altered Watch runtime file: " + name)
    return manifest


def stage(root, destination, profile="desktop"):
    if root == destination or root in destination.parents or destination in root.parents:
        raise ValueError("Watch staging destination must be outside the source tree")
    outputs, manifest = inventory(root, profile)
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".watch-runtime-", dir=destination.parent) as temporary:
        temporary = Path(temporary)
        staged = temporary / "WatchSource"
        for name, data in outputs.items():
            path = staged / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
        previous = temporary / "previous"
        if destination.exists():
            destination.rename(previous)
        try:
            staged.rename(destination)
        except BaseException:
            if previous.exists():
                previous.rename(destination)
            raise
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("stage", "verify"))
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--profile", choices=("desktop", "reference"), default="desktop")
    args = parser.parse_args()
    result = (stage if args.mode == "stage" else verify)(args.source.resolve(), args.destination.resolve(), args.profile)
    print(f"Watch runtime: {len(result['files'])} files; {result['source_tree_bytes'] / 1048576:.2f} MiB source tree"
          f" -> {result['runtime_bytes'] / 1048576:.2f} MiB bundled; every retained source byte verified")


if __name__ == "__main__":
    main()
