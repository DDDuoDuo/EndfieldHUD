#!/usr/bin/env python3
"""Build-only translation of the current Mac renderer's original shader programs.

No shader arithmetic is rewritten. SPIRV-Cross consumes the exact SPIR-V paired
with each referenced Metal stage. D3DCompile's reflection supplies final SM5
binding slots; Vulkan descriptor sets cannot be used as D3D11 register indices.
The application ships compiled bytecode, not this tool or SPIRV-Cross.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

CONVERTER_COMMIT = "aa217aeb6c9f0ace7a0ab233b28807edf45eb165"


def digest(data):
    return hashlib.sha256(data).hexdigest()


def confined(root, relative):
    if not isinstance(relative, str) or "\\" in relative:
        raise ValueError("Shader path must be package-relative POSIX text")
    path = root / relative
    if Path(relative).is_absolute() or ".." in Path(relative).parts:
        raise ValueError("Shader path escapes package root")
    resolved = path.resolve(strict=True)
    if not resolved.is_relative_to(root.resolve()) or not resolved.is_file():
        raise ValueError("Shader path is not a contained ordinary file")
    return resolved


def metal_bindings(source, descriptor, reflection):
    # These are read-only source declarations, not guessed register numbers.
    buffers = {}
    for kind, variable, index in re.findall(
            r"constant\s+(\w+)\s*&\s*(\w+)\s*\[\[buffer\((\d+)\)\]\]", source):
        buffers[int(index)] = (kind, variable)
    ubos = {ubo["name"]: ubo for ubo in reflection.get("ubos", [])}
    result = []
    for uniform in descriptor.get("uniforms", []):
        kind, variable = buffers[uniform["index"]]
        name = f"{kind}_{variable.lstrip('_')}"
        if name not in ubos or ubos[name]["block_size"] != uniform["size"]:
            raise ValueError(f"Metal/SPIR-V buffer contract differs for {uniform['name']}")
        result.append({"name": uniform["name"], "hlslName": name,
                       "sourceSize": uniform["size"], "sourceFields": uniform["fields"],
                       "sourceMembers": uniform.get("members", []),
                       "spirvType": kind, "spirvVariable": variable})
    if len(result) != len(ubos):
        raise ValueError("Unmapped SPIR-V uniform buffer")
    return result


def texture_bindings(source, descriptor, stage):
    textures = {int(i): name for name, i in re.findall(
        r"\b(\w+)\s*\[\[texture\((\d+)\)\]\]", source)}
    samplers = {int(i): name for name, i in re.findall(
        r"\b(\w+)\s*\[\[sampler\((\d+)\)\]\]", source)}
    result = []
    for binding in descriptor.get("textures", []):
        if (binding.get("stage") or "fragment") != stage:
            continue
        sampler = binding.get("sampler_index")
        if sampler is None:
            sampler = binding["index"]
        result.append({"name": binding["name"],
                       "hlslTexture": textures[binding["index"]],
                       # Same fallback used by HUDSourceMetalRenderer's TexturePlan.
                       "hlslSampler": samplers[sampler]})
    return result


def translate(root, output, converter, descriptors):
    output.mkdir(parents=True, exist_ok=True)
    programs = []
    for descriptor_path in descriptors:
        relative = descriptor_path.relative_to(root).as_posix()
        descriptor_bytes = confined(root, relative).read_bytes()
        descriptor = json.loads(descriptor_bytes)
        for stage, record in descriptor["stages"].items():
            if stage not in {"vertex", "fragment"}:
                raise ValueError(f"Unsupported source stage: {stage}")
            metal = confined(root, record["file"])
            spirv = confined(root, str(Path(record["file"]).with_suffix(".spv")))
            spirv_bytes = spirv.read_bytes()
            if len(spirv_bytes) > 4 * 1024 * 1024 or spirv_bytes[:4] != b"\x03\x02\x23\x07":
                raise ValueError("Invalid source SPIR-V container")
            key = digest((relative + ":" + stage).encode())[:24]
            hlsl = output / (key + ".hlsl")
            reflection = json.loads(subprocess.check_output(
                [str(converter), str(spirv), "--reflect"], timeout=30))
            subprocess.run([str(converter), str(spirv), "--hlsl", "--shader-model", "50",
                            "--hlsl-auto-binding", "all", "--output", str(hlsl)],
                           check=True, timeout=30, capture_output=True)
            hlsl_source = hlsl.read_text()
            buffers = metal_bindings(metal.read_text(), record, reflection)
            if any(f"cbuffer {b['hlslName']}" not in hlsl_source for b in buffers):
                raise ValueError("Converter did not preserve the reflected buffer names")
            programs.append({"id": relative + ":" + stage, "sourceDescriptor": relative,
                "sourceDescriptorSHA256": digest(descriptor_bytes), "stage": stage,
                "profile": "vs_5_0" if stage == "vertex" else "ps_5_0", "entry": "main",
                "hlslFile": hlsl.name, "hlslSHA256": digest(hlsl.read_bytes()),
                "metalFile": record["file"], "metalSHA256": digest(metal.read_bytes()),
                "spirvSHA256": digest(spirv_bytes), "uniforms": buffers,
                "textures": texture_bindings(metal.read_text(), descriptor, stage),
                "inputs": record.get("inputs", []), "outputs": record.get("outputs", [])})
    manifest = {"schemaVersion": 1, "converter": {"project": "KhronosGroup/SPIRV-Cross",
                "sourceCommit": CONVERTER_COMMIT, "executableSHA256": digest(converter.read_bytes())},
                "coordinatePolicy": "Unmodified source SPIR-V; consume the exact submitted Mac GPU camera. Do not add a second Y flip.",
                "programs": programs}
    (output / "shaders.json").write_text(json.dumps(manifest, indent=2, sort_keys=True) + "\n")
    return manifest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--resource-root", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--spirv-cross", type=Path, required=True)
    parser.add_argument("--converter-source", type=Path, required=True)
    parser.add_argument("--descriptor", action="append", required=True,
                        help="Source-relative shader descriptor; repeat for each referenced material")
    args = parser.parse_args()
    revision = subprocess.check_output(["git", "-C", str(args.converter_source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(args.converter_source), "status", "--porcelain"], text=True)
    if revision != CONVERTER_COMMIT or dirty:
        parser.error("Use the pinned unmodified SPIRV-Cross source revision")
    root = args.resource_root.resolve(strict=True)
    files = [confined(root, name) for name in sorted(set(args.descriptor))]
    manifest = translate(root, args.output.resolve(), args.spirv_cross.resolve(strict=True), files)
    print(f"Translated {len(manifest['programs'])} source stages; D3D compilation and visual parity still require verification")


if __name__ == "__main__":
    main()
