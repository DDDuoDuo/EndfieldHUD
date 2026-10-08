#!/usr/bin/env python3
"""Compile an explicit synthetic Mac export to bounded, source-derived runtime inputs.

This is a build tool. It never opens a HUD, user store, network or desktop. Output
must be new. JSON decimal tokens survive verbatim. An optional explicit compiler
stores the existing typed animation parser's exact binary64 values in schema 2.
GPU programs/textures remain the separate compiled scene, pinned by its caller.
"""
import argparse
import hashlib
import json
import pathlib
import re
import shutil
import subprocess
import tempfile


class Number(str):
    pass


def fail(message):
    raise ValueError(message)


def pairs(items):
    result = {}
    for key, value in items:
        if key in result:
            fail("Duplicate JSON key")
        result[key] = value
    return result


def parse(data):
    return json.loads(data, parse_int=Number, parse_float=Number,
                      parse_constant=lambda _: fail("Non-JSON number"), object_pairs_hook=pairs)


def encode(value):
    if isinstance(value, Number):
        if not re.fullmatch(r"-?(?:0|[1-9][0-9]*)(?:\.[0-9]+)?(?:[eE][+-]?[0-9]+)?", value):
            fail("Invalid number token")
        return str(value)
    if isinstance(value, dict):
        return "{" + ",".join(encode(k) + ":" + encode(value[k]) for k in sorted(value)) + "}"
    if isinstance(value, list):
        return "[" + ",".join(map(encode, value)) + "]"
    return json.dumps(value, ensure_ascii=False, separators=(",", ":"), allow_nan=False)


def digest(data):
    return hashlib.sha256(data).hexdigest()


def safe_path(root, relative):
    if not isinstance(relative, str) or isinstance(relative, Number) or not relative or "\\" in relative or ":" in relative:
        fail("Invalid package path")
    parts = relative.split("/")
    if any(p in ("", ".", "..") for p in parts):
        fail("Package path escapes root")
    path = root
    for part in parts:
        path = path / part
        if path.is_symlink():
            fail("Package symlink is not allowed")
    return path


def read(root, relative, limit):
    path = safe_path(root, relative)
    if not path.is_file() or path.stat().st_size > limit:
        fail("Missing or oversized input: " + relative)
    with path.open("rb") as handle:
        result = handle.read(limit + 1)
    if len(result) > limit:
        fail("Input grew beyond limit")
    return result


def blob(root, descriptor, limit):
    size = int(descriptor["bytes"])
    if size < 0 or size > limit or not re.fullmatch("[0-9a-f]{64}", descriptor["sha256"]):
        fail("Invalid source blob descriptor")
    data = read(root, descriptor["file"], limit)
    if len(data) != size or digest(data) != descriptor["sha256"]:
        fail("Source blob integrity mismatch")
    return data


def subset(value, keys):
    if not isinstance(value, dict):
        fail("Expected source object")
    return {key: value[key] for key in keys if key in value}


def project_sprite(value):
    result = subset(value, ("id", "name"))
    raw = value.get("raw_sprite")
    if raw is not None:
        projected = subset(raw, ("m_PixelsToUnits",))
        for key, fields in (("m_Border", ("x", "y", "z", "w")),
                            ("m_Rect", ("x", "y", "width", "height"))):
            if key in raw:
                projected[key] = None if raw[key] is None else subset(raw[key], fields)
        result["raw_sprite"] = projected
    texture = value.get("texture")
    if texture is not None:
        result["texture"] = subset(texture, ("id",))
    return result


def project_scene(scene):
    nodes = []
    for node in scene["nodes"]:
        result = subset(node, ("id", "name", "path", "parent_id", "child_ids"))
        result["game_object"] = {"data": subset(node["game_object"]["data"], ("m_IsActive",))}
        result["transform"] = {"type": node["transform"]["type"], "raw": subset(node["transform"]["raw"], (
            "m_LocalPosition", "m_LocalScale", "m_LocalRotation", "m_AnchorMin", "m_AnchorMax",
            "m_AnchoredPosition", "m_SizeDelta", "m_Pivot"))}
        nodes.append(result)
    return {"root_node_id": scene["root_node_id"], "nodes": nodes}


def referenced_rasters(value):
    result = set()
    def visit(node):
        if isinstance(node, dict):
            for key, child in node.items():
                if key in ("asset", "path", "file") and isinstance(child, str) and child.startswith("raster/"):
                    result.add(child)
                visit(child)
        elif isinstance(node, list):
            for child in node:
                visit(child)
    visit(value)
    return result


def build(packet_root, output, chrome=None, animation_compiler=None):
    root = pathlib.Path(packet_root).absolute()
    target = pathlib.Path(output).absolute()
    if root.is_symlink() or not root.is_dir() or target.exists() or target.is_symlink():
        fail("Explicit source directory and new output directory required")
    compiler = pathlib.Path(animation_compiler).absolute() if animation_compiler else None
    if compiler and (compiler.is_symlink() or not compiler.is_file()):
        fail("Explicit ordinary animation compiler executable required")
    manifest_bytes = read(root, "shell-packet.json", 16 * 1024 * 1024)
    manifest = parse(manifest_bytes)
    animation_bytes = blob(root, manifest["animation"], 48 * 1024 * 1024)
    animation = parse(animation_bytes)
    mounted = subset(animation["mountedDocument"], ("components", "buttons", "animators"))
    mounted["spriteByComponent"] = {key: project_sprite(value) for key, value in animation["mountedDocument"]["spriteByComponent"].items()}
    parts = {"scene": project_scene(animation["scene"]), "mountedDocument": mounted,
             "library": animation["library"], "runtimeRoot": animation["runtimeRoot"],
             "frameBuilder": subset(animation["frameBuilder"], [k for k in animation["frameBuilder"] if k != "scope"]),
             "controllerTransitions": animation["controllerTransitions"]}
    pins = {"sourceManifestSHA256": digest(manifest_bytes), "animationSHA256": digest(animation_bytes)}
    for role, name in (("nativeTop", "desktop-shell-1280x800-top"), ("nativeBottom", "desktop-shell-1280x800-bottom")):
        candidates = [f for f in manifest["frames"] if f["name"] == name]
        if len(candidates) != 1:
            fail("Missing unique source native snapshot")
        data = blob(root, candidates[0], 16 * 1024 * 1024)
        parts[role] = subset(parse(data), ("nativeLayers", "nativeNavigation", "nativeProfileBindings"))
        pins[role + "SHA256"] = digest(data)
    # The export's global frameBuilder captures its final selected button. The
    # preview starts with the settled opening reference's original selection.
    # Extract just that configuration; no sampled pose or draw state is retained.
    settings_frames = [f for f in manifest["frames"] if f["name"] == "desktop-shell-1280x800-opening-4"]
    if len(settings_frames) != 1:
        fail("Missing unique initial desktop settings snapshot")
    settings_bytes = blob(root, settings_frames[0], 16 * 1024 * 1024)
    parts["frameBuilder"]["desktopSettings"] = parse(settings_bytes)["builderInput"]["desktopSettings"]
    pins["initialDesktopSettingsSHA256"] = digest(settings_bytes)
    if chrome:
        path = pathlib.Path(chrome).absolute()
        data = read(path.parent, path.name, 8 * 1024 * 1024)
        # NativeChromePresentation consumes static templates/bindings. Timeline
        # projections/layout oracles and diagnostic summaries never ship.
        original = parse(data)
        parts["chrome"] = subset(original, ("schemaVersion", "bindings", "header", "footer"))
        parts["chrome"]["styles"] = []
        # Match NativeChromePresentation::retainedReference: its first exact
        # style/hover tree wins, including all source colors and geometry.
        for style in ("digital", "split", "dial", "rail", "stacked"):
            for hover in (False, True):
                row = next((r for r in original["styles"] if r["style"] == style and r["hover"] == hover), None)
                if row is None:
                    fail("Missing source clock style")
                parts["chrome"]["styles"].append(subset(row, ("style", "hover", "status")))
        pins["chromeSHA256"] = digest(data)
        if referenced_rasters(parts["chrome"]):
            fail("Chrome raster assets need an explicit source manifest")
    required = referenced_rasters([parts["nativeTop"], parts["nativeBottom"]])
    native = {row["file"]: row for row in manifest["nativeRasterAssets"]}
    if len(native) != len(manifest["nativeRasterAssets"]) or not required.issubset(native):
        fail("Unresolved or duplicate native raster references")
    if len(required) > 512:
        fail("Too many native raster assets")
    target.parent.mkdir(parents=True, exist_ok=True)
    staging = pathlib.Path(tempfile.mkdtemp(prefix=target.name + ".tmp-", dir=target.parent))
    try:
        output_manifest = {"format": "endfield-watch-runtime-input", "schemaVersion": 2 if compiler else 1,
                           "sourcePins": pins, "parts": {}, "rasterAssets": []}
        total = 0
        for role, value in sorted(parts.items()):
            data = encode(value).encode("utf-8")
            if len(data) > 8 * 1024 * 1024:
                fail("Runtime section exceeds 8 MiB")
            file = "parts/" + role + ".json"
            path = safe_path(staging, file)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            if compiler and role == "library":
                binary_file = "parts/library.ehanim"
                binary_path = safe_path(staging, binary_file)
                result = subprocess.run([str(compiler), str(path), pins["sourceManifestSHA256"], str(binary_path)],
                                        stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, check=False)
                if result.returncode:
                    fail("Animation compiler rejected source input: " + result.stderr.strip())
                data = read(staging, binary_file, 8 * 1024 * 1024)
                # The typed compiler validates the payload; bind the fixed
                # envelope here before publishing a new runtime manifest.
                if (len(data) < 88 or data[:8] != b"EHANIM01" or
                        data[8:12] != (1).to_bytes(4, "little") or
                        data[12:16] != (0x01020304).to_bytes(4, "little") or
                        int.from_bytes(data[16:24], "little") != len(data) - 88 or
                        data[24:56].hex() != pins["sourceManifestSHA256"] or
                        data[56:88].hex() != digest(data[88:])):
                    fail("Animation compiler produced an invalid or unpinned envelope")
                path.unlink()
                file = binary_file
            output_manifest["parts"][role] = {"file": file, "bytes": len(data), "sha256": digest(data)}
            if compiler and role == "library":
                output_manifest["parts"][role]["encoding"] = "endfield-animation-v1"
            total += len(data)
        if total > 32 * 1024 * 1024:
            fail("Runtime JSON exceeds aggregate bound")
        for file in sorted(required):
            data = blob(root, native[file], 16 * 1024 * 1024)
            if file != "raster/" + digest(data) + ".png":
                fail("Native raster must use its content address")
            path = safe_path(staging, file)
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            output_manifest["rasterAssets"].append({"file": file, "bytes": len(data), "sha256": digest(data)})
            total += len(data)
        if total > 96 * 1024 * 1024:
            fail("Runtime input exceeds aggregate bound")
        data = encode(output_manifest).encode("utf-8")
        (staging / "runtime-input.json").write_bytes(data)
        if target.exists() or target.is_symlink():
            fail("Output appeared while building")
        staging.rename(target)
        return {"parts": len(parts), "rasterAssets": len(required), "bytes": total + len(data),
                "manifestSHA256": digest(data)}
    finally:
        if staging.exists():
            shutil.rmtree(staging)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("packet_root")
    parser.add_argument("new_output")
    parser.add_argument("--chrome")
    parser.add_argument("--animation-compiler", help="Explicit compiled compile_source_animation executable; emits schema 2")
    args = parser.parse_args()
    print(json.dumps(build(args.packet_root, args.new_output, args.chrome, args.animation_compiler), sort_keys=True))
