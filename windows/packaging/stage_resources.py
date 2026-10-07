#!/usr/bin/env python3
"""Stage approved Windows runtime assets; never mutate the source resource tree."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import struct
import subprocess
import sys
import tempfile
import time
import zlib

from source_provenance import (PROVENANCE, SourceAuthority, create_manifest,
                               output_record, verify_manifest as verify_source_provenance)

MAGIC = b"EHUDZ01\0"
MAX_RESOURCE_BYTES = 128 * 1024 * 1024
INVENTORY = "resources-inventory.json"


def windows_io_path(path: Path) -> str:
    value = str(path.resolve())
    if os.name == "nt" and not value.startswith("\\\\?\\"):
        return "\\\\?\\UNC\\" + value[2:] if value.startswith("\\\\") else "\\\\?\\" + value
    return value


def run_watch_packer(command: list[str]) -> None:
    """Keep the source packer unchanged; retry temporary Windows scan locks."""
    for attempt in range(4):
        result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", errors="replace")
        if result.returncode == 0:
            print(result.stdout, end="")
            return
        transient = any(marker in result.stderr for marker in ("[WinError 5]", "[WinError 32]", "[WinError 145]"))
        if os.name != "nt" or not transient or attempt == 3:
            print(result.stderr, file=sys.stderr, end="")
            raise subprocess.CalledProcessError(result.returncode, command, output=result.stdout, stderr=result.stderr)
        print(f"Windows temporarily locked staged Watch files; retrying {attempt + 2}/4")
        time.sleep(0.25 * (attempt + 1))


def rename_directory(source: Path, destination: Path) -> None:
    for attempt in range(4):
        try:
            source.rename(destination)
            return
        except OSError as error:
            if getattr(error, "winerror", None) not in (5, 32, 145) or attempt == 3:
                raise
            time.sleep(0.25 * (attempt + 1))


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def decode_resource(data: bytes, *, require_container: bool = False) -> bytes:
    """Bound raw-DEFLATE output and require exactly one complete EHUDZ01 stream."""
    if not data.startswith(MAGIC):
        if require_container or data.startswith(b"EHUDZ"):
            raise ValueError("Invalid EHUDZ01 resource magic")
        if len(data) > MAX_RESOURCE_BYTES:
            raise ValueError("Resource exceeds the 128 MiB decoder bound")
        return data
    if len(data) < 16:
        raise ValueError("Truncated EHUDZ01 resource header")
    expected, = struct.unpack("<Q", data[8:16])
    if not 0 < expected <= MAX_RESOURCE_BYTES:
        raise ValueError("Invalid EHUDZ01 uncompressed length (1..128 MiB)")
    stream = zlib.decompressobj(wbits=-15)
    try:
        decoded = stream.decompress(data[16:], expected + 1)
    except zlib.error as error:
        raise ValueError("Invalid EHUDZ01 raw-DEFLATE stream") from error
    if len(decoded) != expected or not stream.eof or stream.unused_data or stream.unconsumed_tail:
        raise ValueError("Incomplete, oversized, length-mismatched or trailing EHUDZ01 payload")
    return decoded


def safe_relative(name: str) -> Path:
    relative = PurePosixPath(name)
    if not name or "\\" in name or ":" in name or relative.is_absolute() or any(part in ("..", ".") for part in name.split("/")):
        raise ValueError("Unsafe runtime asset path: " + name)
    return Path(*relative.parts)


def checked_file(root: Path, name: str) -> Path:
    relative = safe_relative(name)
    path = root / relative
    if not path.is_file() or any(parent.is_symlink() for parent in [path, *path.parents] if parent != root.parent):
        raise ValueError("Missing or symbolic runtime asset: " + name)
    if not path.resolve().is_relative_to(root.resolve()):
        raise ValueError("Runtime asset escapes its source root: " + name)
    return path


def record_file(path: Path, root: Path, **metadata: object) -> dict:
    data = path.read_bytes()
    return {"path": path.relative_to(root).as_posix(), "bytes": len(data), "sha256": sha256(data), **metadata}


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True) + "\n").encode("utf-8")


def source_assets(source: Path, selection: dict) -> list[tuple[str, Path]]:
    if selection.get("schema") != 1 or selection.get("watch_profile") != "desktop":
        raise ValueError("Unsupported Windows runtime asset selection")
    selected: dict[str, Path] = {}
    for name in selection["files"]:
        selected[name] = checked_file(source, name)
    for tree in selection["trees"]:
        relative = safe_relative(tree["source"])
        tree_root = source / relative
        if not tree_root.is_dir() or tree_root.is_symlink():
            raise ValueError("Missing approved runtime asset tree: " + tree["source"])
        excludes = set(tree.get("exclude", []))
        for pattern in tree["patterns"]:
            safe_relative(pattern)
            files = [path for path in sorted(tree_root.glob(pattern)) if path.is_file()]
            if not files:
                raise ValueError("Runtime asset selection has no matches: " + str(relative / pattern))
            for path in files:
                if path.relative_to(tree_root).as_posix() in excludes:
                    continue
                name = path.relative_to(source).as_posix()
                selected[name] = checked_file(source, name)
    for pin in selection.get("pins", []):
        data = checked_file(source, pin["path"]).read_bytes()
        license_data = checked_file(source, pin["license_path"]).read_bytes()
        if pin["path"] not in selected or pin["license_path"] not in selected or len(data) != pin["bytes"] or sha256(data) != pin["sha256"] or sha256(license_data) != pin["license_sha256"]:
            raise ValueError("Pinned runtime dependency/license changed: " + pin["path"])
    return sorted(selected.items())


def audit_watch(staged_watch: Path) -> None:
    inventory = json.loads(checked_file(staged_watch, "runtime-inventory.json").read_bytes())
    if inventory.get("schema") != 1 or inventory.get("profile") != "desktop":
        raise ValueError("Windows must stage the current desktop Watch inventory")
    for record in inventory["files"] + inventory.get("derived_files", []):
        data = checked_file(staged_watch, record["path"]).read_bytes()
        if len(data) != record["bytes"] or sha256(data) != record["sha256"]:
            raise ValueError("Altered staged Watch resource: " + record["path"])
        decoded = decode_resource(data, require_container=record["encoding"] == "raw-deflate-v1")
        expected_bytes = record.get("source_bytes", record.get("decoded_bytes"))
        if expected_bytes is not None and len(decoded) != expected_bytes:
            raise ValueError("Watch decoded resource length differs: " + record["path"])
        if "source_bytes" in record and sha256(decoded) != record["source_sha256"]:
            raise ValueError("Watch source bytes were changed: " + record["path"])


def verify_resources(destination: Path) -> dict:
    manifest = json.loads(checked_file(destination, INVENTORY).read_bytes())
    if manifest.get("schema") != 1 or manifest.get("policy") != "explicit-runtime-assets-only":
        raise ValueError("Unsupported Windows resources inventory")
    expected = {record["path"] for record in manifest["files"]}
    if len(expected) != len(manifest["files"]):
        raise ValueError("Duplicate Windows runtime asset record")
    actual = set()
    for path in destination.rglob("*"):
        if path.is_symlink():
            raise ValueError("Symbolic files/directories are not runtime assets")
        if path.is_file():
            actual.add(path.relative_to(destination).as_posix())
    if actual != expected | {INVENTORY}:
        raise ValueError("Windows runtime inventory has missing or extra files")
    for record in manifest["files"]:
        data = checked_file(destination, record["path"]).read_bytes()
        if len(data) != record["bytes"] or sha256(data) != record["sha256"]:
            raise ValueError("Altered Windows runtime asset: " + record["path"])
    audit_watch(destination / "WatchSource")
    verify_source_provenance(destination)
    return manifest


def stage_resources(repository: Path, destination: Path) -> dict:
    authority = SourceAuthority(repository)
    repository, destination = repository.resolve(), destination.resolve()
    source = repository / "Resources"
    if destination == source or destination.is_relative_to(source) or source.is_relative_to(destination):
        raise ValueError("Windows resources staging must be outside the source asset tree")
    if not destination.is_relative_to(repository / "windows" / "build"):
        raise ValueError("Windows resources staging must stay inside windows/build")
    if destination.exists() and (not destination.is_dir() or destination.is_symlink()):
        raise ValueError("Windows resources destination must be an ordinary directory")
    selection_path = repository / "windows" / "resources" / "runtime-assets.json"
    selection = json.loads(selection_path.read_bytes())
    assets = source_assets(source, selection)
    packer = repository / "scripts" / "package-watch-resources.py"
    authority.read("scripts/package-watch-resources.py")
    authority.read("WINDOWS-MIGRATION.md")
    # Catalog/selection discovery must not observe an untracked or altered
    # extraction file, even if that file would later be omitted from packaging.
    authority.audit_tree("Resources/WatchSource")
    for name, _ in assets:
        authority.read("Resources/" + name)
    for name in ("LICENSE", "CREDITS.md"):
        authority.read(name)
    for dependency in selection.get("native_dependency_notices", []):
        authority.read(dependency["repository_path"])
    if "Scene/desktop-profile-card.json" not in selection["native_scene_metadata"]:
        raise ValueError("The canonical desktop profile card is required for restart staging")
    destination.parent.mkdir(parents=True, exist_ok=True)
    # A short staging parent also avoids nesting the authoritative packer's
    # own temporary directory beneath the long VS configuration directory.
    temporary_parent = repository / "windows" / "build"
    with tempfile.TemporaryDirectory(prefix=".ehud-assets-", dir=temporary_parent) as temporary:
        temporary_root = Path(temporary)
        staged = temporary_root / "Resources"
        staged.mkdir()
        # The authoritative Mac packer reads UTF-8 JSON with read_text().
        # Python's Windows locale can otherwise decode it as GBK/ANSI.
        command = [sys.executable, "-X", "utf8", str(packer)]
        arguments = [windows_io_path(source / "WatchSource"), windows_io_path(staged / "WatchSource"), "--profile", "desktop"]
        run_watch_packer([*command, "stage", *arguments])
        run_watch_packer([*command, "verify", *arguments])
        audit_watch(staged / "WatchSource")
        provenance_outputs = []
        watch = json.loads((staged / "WatchSource/runtime-inventory.json").read_bytes())
        watch_inputs = []
        for record in watch["files"]:
            source_name = "Resources/WatchSource/" + record["path"]
            source_data = authority.read(source_name)
            target = checked_file(staged / "WatchSource", record["path"])
            decoded = decode_resource(target.read_bytes(), require_container=record["encoding"] == "raw-deflate-v1")
            if decoded != source_data:
                raise ValueError("Packed output differs from canonical GitHub blob: " + source_name)
            watch_inputs.append(source_name)
            provenance_outputs.append(output_record(target, staged, inputs=[source_name], encoding=record["encoding"], decoded=decoded))
        selection_inputs = ["Resources/WatchSource/" + name for name in (
            "materials.json", "textures.json", "Scene/materials.json", "Scene/sprites.json",
            "Scene/fonts.json", "Scene/scene.json", "Scene/desktop-profile-card.json")]
        for name in selection_inputs:
            authority.read(name)
        for name, algorithm in [("runtime-selection.json", "canonical-desktop-selection-json"),
                                ("runtime-materials.json", "selected-original-json-tokens"),
                                ("runtime-inventory.json", "canonical-watch-runtime-inventory")]:
            target = staged / "WatchSource" / name
            data = target.read_bytes()
            provenance_outputs.append(output_record(target, staged,
                inputs=sorted(set(watch_inputs + selection_inputs)) if name == "runtime-inventory.json" else selection_inputs,
                encoding="raw-deflate-v1" if data.startswith(MAGIC) else "identity",
                decoded=decode_resource(data), algorithm=algorithm))
        for name, path in assets:
            target = staged / safe_relative(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            source_name = "Resources/" + name
            target.write_bytes(authority.read(source_name))
            provenance_outputs.append(output_record(target, staged, inputs=[source_name]))
        for name, target_name in [("LICENSE", "LICENSE.txt"), ("CREDITS.md", "CREDITS.md")]:
            target = staged / target_name
            target.write_bytes(authority.read(name))
            provenance_outputs.append(output_record(target, staged, inputs=[name]))
        native_dependencies = selection.get("native_dependency_notices", [])
        for dependency in native_dependencies:
            notice = authority.read(dependency["repository_path"])
            if len(notice) != dependency["license_bytes"] or sha256(notice) != dependency["license_sha256"]:
                raise ValueError("Native dependency license pin changed: " + dependency["name"])
            target = staged / safe_relative(dependency["destination"])
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(notice)
            provenance_outputs.append(output_record(target, staged, inputs=[dependency["repository_path"]]))
        native_records = []
        for name in selection["native_scene_metadata"]:
            packed_path = checked_file(staged / "WatchSource", name)
            source_name = "Resources/WatchSource/" + name
            data = decode_resource(packed_path.read_bytes())
            if data != authority.read(source_name):
                raise ValueError("Native scene metadata is not byte-exact: " + name)
            target = staged / "NativeScene" / safe_relative(name)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(data)
            native_records.append(record_file(target, staged / "NativeScene", source_path="WatchSource/" + name, source_sha256=sha256(data)))
            provenance_outputs.append(output_record(target, staged, inputs=[source_name], decoded=data))
        (staged / "NativeScene" / "native-scene-inventory.json").write_bytes(json_bytes({
            "schema": 1, "policy": "byte-exact-decoded-approved-metadata", "files": native_records,
            "texture_root": "../WatchSource/Scene", "bytes": sum(record["bytes"] for record in native_records),
        }))
        provenance_outputs.append(output_record(staged / "NativeScene/native-scene-inventory.json", staged,
            inputs=["Resources/WatchSource/" + name for name in selection["native_scene_metadata"]], algorithm="decoded-native-scene-inventory"))
        (staged / PROVENANCE).write_bytes(json_bytes(create_manifest(authority, provenance_outputs, selection_path)))
        files = [record_file(path, staged) for path in sorted(staged.rglob("*")) if path.is_file()]
        if any(part.startswith(".") for record in files for part in PurePosixPath(record["path"]).parts):
            raise ValueError("A temporary resource directory remains locked; refusing to bundle staging artifacts")
        manifest = {
            "schema": 1, "policy": "explicit-runtime-assets-only", "files": files,
            "installed_bytes": sum(record["bytes"] for record in files),
            "selection_sha256": sha256(selection_path.read_bytes()), "watch_packer_sha256": sha256(packer.read_bytes()),
            "native_scene_duplicate_bytes": sum(record["bytes"] for record in native_records),
            "native_dependencies": native_dependencies,
        }
        (staged / INVENTORY).write_bytes(json_bytes(manifest))
        verify_resources(staged)
        previous = temporary_root / "previous"
        if destination.exists():
            rename_directory(destination, previous)
        try:
            rename_directory(staged, destination)
        except BaseException:
            if previous.exists():
                rename_directory(previous, destination)
            raise
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("stage", "verify"))
    parser.add_argument("--repository", type=Path, default=Path(__file__).resolve().parents[2])
    parser.add_argument("--destination", type=Path, required=True)
    arguments = parser.parse_args()
    manifest = stage_resources(arguments.repository, arguments.destination) if arguments.mode == "stage" else verify_resources(arguments.destination.resolve())
    print(f"Windows resources: {len(manifest['files'])} files; {manifest['installed_bytes'] / 1048576:.2f} MiB; byte-exact hashes verified")


if __name__ == "__main__":
    main()
