#!/usr/bin/env python3
"""Prove restart runtime inputs against immutable GitHub Git objects."""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys

ORIGIN = "https://github.com/DDDuoDuo/EndfieldHUD.git"
REF = "codex/windows-migration"
BASELINE = "4036174a3facf935260f4d0a9c63bfff33b98c37"
PROVENANCE = "source-provenance.json"
REPOSITORY = Path(__file__).resolve().parents[2]


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def blob_id(data: bytes) -> str:
    return hashlib.sha1(b"blob " + str(len(data)).encode("ascii") + b"\0" + data).hexdigest()


def relative_path(name: str) -> Path:
    path = PurePosixPath(name)
    if not name or "\\" in name or ":" in name or path.is_absolute() or any(p in ("", ".", "..") for p in name.split("/")):
        raise ValueError("Unsafe source provenance path: " + name)
    return Path(*path.parts)


def ordinary_path(root: Path, name: str) -> Path:
    path = root / relative_path(name)
    for candidate in [path, *path.parents]:
        if candidate == root.parent:
            break
        try:
            info = candidate.lstat()
        except FileNotFoundError as error:
            raise ValueError("Missing provenance input: " + name) from error
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            raise ValueError("Symbolic/reparse provenance path: " + name)
    if not path.is_file() or not path.resolve().is_relative_to(root):
        raise ValueError("Provenance input escapes the repository: " + name)
    return path


class SourceAuthority:
    def __init__(self, repository: Path, *, expected_repository: Path = REPOSITORY,
                 baseline: str = BASELINE, origin: str = ORIGIN, ref: str = REF, git: str = "git"):
        # Production callers cannot supply another checkout through the CLI.
        # Constructor overrides support isolated, synthetic Git test fixtures.
        self.repository = repository.absolute()
        if self.repository.resolve() != expected_repository.resolve() or self.repository.is_symlink():
            raise ValueError("Runtime source must be this fresh repository; no local fallback")
        self.repository = self.repository.resolve()
        self.baseline, self.origin, self.ref, self.git = baseline, origin, ref, git
        if self.run("rev-parse", "--show-toplevel").decode().strip().replace("\\", "/").casefold() != self.repository.as_posix().casefold():
            raise ValueError("Ambiguous Git source root")
        origins = self.run("remote", "get-url", "--all", "origin").decode().splitlines()
        if origins != [origin]:
            raise ValueError("Wrong or ambiguous GitHub source origin")
        if self.run("symbolic-ref", "--short", "HEAD").decode().strip() != ref:
            raise ValueError("Wrong source branch; detached/local substitutes are forbidden")
        self.commit = self.run("rev-parse", "HEAD").decode().strip()
        self.remote_commit = self.run("rev-parse", "refs/remotes/origin/" + ref).decode().strip()
        for descendant in (self.commit, self.remote_commit):
            self.run("merge-base", "--is-ancestor", baseline, descendant)
        self.baseline_tree = self.tree(baseline)
        self.head_tree = self.tree("HEAD")
        self.used: dict[str, dict] = {}
        self.audit: dict[str, dict] = {}

    def run(self, *arguments: str) -> bytes:
        try:
            return subprocess.check_output([self.git, "-C", str(self.repository), *arguments], stderr=subprocess.PIPE, timeout=30)
        except (OSError, subprocess.SubprocessError) as error:
            raise ValueError("Cannot establish the pinned Git source authority: " + " ".join(arguments[:2])) from error

    def tree(self, revision: str) -> dict[str, tuple[str, str]]:
        files = {}
        for record in self.run("ls-tree", "-r", "-z", "--full-tree", revision).split(b"\0"):
            if not record:
                continue
            metadata, name = record.split(b"\t", 1)
            mode, kind, oid = metadata.split()
            if kind == b"blob":
                files[name.decode("utf-8")] = (mode.decode(), oid.decode())
        return files

    def read(self, name: str, *, used: bool = True) -> bytes:
        path = ordinary_path(self.repository, name)
        expected = self.baseline_tree.get(name)
        if not expected or expected[0] not in ("100644", "100755"):
            raise ValueError("Input is not a regular tracked GitHub baseline blob: " + name)
        if self.head_tree.get(name) != expected:
            raise ValueError("Committed source input differs from pinned GitHub baseline: " + name)
        data = path.read_bytes()
        if blob_id(data) != expected[1]:
            raise ValueError("Source bytes differ from canonical Git blob (modified input or newline conversion): " + name)
        if used:
            self.used[name] = {"path": name, "git_blob": expected[1], "bytes": len(data), "sha256": digest(data)}
        return data

    def audit_tree(self, prefix: str) -> dict:
        root = self.repository / relative_path(prefix)
        expected = {name for name in self.baseline_tree if name.startswith(prefix + "/")}
        actual = set()
        for path in root.rglob("*"):
            info = path.lstat()
            if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
                raise ValueError("Symbolic/reparse source tree input")
            if path.is_file():
                actual.add(path.relative_to(self.repository).as_posix())
        if actual != expected:
            raise ValueError("Pinned source tree has missing or untracked inputs: " + prefix)
        count_bytes = 0
        for name in sorted(expected):
            count_bytes += len(self.read(name, used=False))
        result = {"path": prefix, "files": len(expected), "bytes": count_bytes,
                  "policy": "all-present-source-bytes-match-baseline-git-blobs; only approved runtime subset is bundled"}
        self.audit[prefix] = result
        return result

    def metadata(self) -> dict:
        return {"origin": self.origin, "ref": "refs/heads/" + self.ref, "baseline_commit": self.baseline,
                "checkout_commit": self.commit, "fetched_remote_commit": self.remote_commit,
                "baseline_tree": self.run("rev-parse", self.baseline + "^{tree}").decode().strip(),
                "checkout_is_baseline_descendant": True, "local_fallback": False}


def output_record(path: Path, root: Path, *, inputs: list[str], encoding: str = "identity", decoded: bytes | None = None, algorithm: str | None = None) -> dict:
    data = path.read_bytes()
    record = {"path": path.relative_to(root).as_posix(), "bytes": len(data), "sha256": digest(data),
              "encoding": encoding, "inputs": sorted(set(inputs))}
    if decoded is not None:
        record.update(decoded_bytes=len(decoded), decoded_sha256=digest(decoded))
    if algorithm:
        record["algorithm"] = algorithm
    return record


def create_manifest(authority: SourceAuthority, outputs: list[dict], selection: Path) -> dict:
    # Windows TEMP can spell an existing directory with an 8.3 alias, while
    # SourceAuthority stores its resolved long path. Check the supplied path
    # before resolving it so canonicalization cannot hide a symbolic/reparse
    # component, then compare the canonical locations.
    selection = selection.absolute()
    if ".." in selection.parts:
        raise ValueError("Unsafe selection provenance path")
    for candidate in [selection, *selection.parents]:
        try:
            info = candidate.lstat()
        except FileNotFoundError as error:
            raise ValueError("Missing selection provenance input") from error
        if stat.S_ISLNK(info.st_mode) or getattr(info, "st_file_attributes", 0) & getattr(stat, "FILE_ATTRIBUTE_REPARSE_POINT", 0x400):
            raise ValueError("Symbolic/reparse selection provenance path")
    selection = selection.resolve()
    if not selection.is_relative_to(authority.repository):
        raise ValueError("Selection provenance input escapes the repository")
    policy_name = selection.relative_to(authority.repository).as_posix()
    policy = ordinary_path(authority.repository, policy_name).read_bytes()
    return {"schema": 1, "policy": "pinned-github-source-no-local-fallback", "acceptance": "restart-build-only-unverified",
            "source": authority.metadata(), "selection": {"path": policy_name,
            "bytes": len(policy), "sha256": digest(policy), "checkout_blob": blob_id(policy),
            "matches_head_blob": authority.head_tree.get(policy_name, (None, None))[1] == blob_id(policy)}, "source_audits": list(authority.audit.values()),
            "inputs": [authority.used[name] for name in sorted(authority.used)], "outputs": sorted(outputs, key=lambda r: r["path"])}


def verify_manifest(resources: Path, *, authority: SourceAuthority | None = None) -> dict:
    from stage_resources import decode_resource, json_bytes, source_assets, windows_io_path
    authority = authority or SourceAuthority(REPOSITORY)
    resources = resources.absolute()
    build = authority.repository / "windows" / "build"
    if resources.is_symlink() or not resources.resolve().is_relative_to(build.resolve()):
        raise ValueError("Staged resources must belong to this fresh repository's windows/build")
    resources = resources.resolve()
    manifest = json.loads(ordinary_path(resources, PROVENANCE).read_bytes())
    if manifest.get("schema") != 1 or manifest.get("policy") != "pinned-github-source-no-local-fallback" or manifest.get("acceptance") != "restart-build-only-unverified":
        raise ValueError("Missing or unsupported restart source provenance")
    if manifest.get("source") != authority.metadata():
        raise ValueError("Staged source authority is stale or differs from this GitHub checkout")
    inputs = {}
    for record in manifest["inputs"]:
        name = record["path"]
        if name in inputs:
            raise ValueError("Duplicate provenance input")
        data = authority.read(name)
        if record != authority.used[name]:
            raise ValueError("Provenance source Git blob/hash differs: " + name)
        inputs[name] = data
    selection = manifest["selection"]
    policy = ordinary_path(authority.repository, selection["path"]).read_bytes()
    if len(policy) != selection["bytes"] or digest(policy) != selection["sha256"] or blob_id(policy) != selection["checkout_blob"]:
        raise ValueError("Resource selection changed after staging")
    for audit in manifest.get("source_audits", []):
        if authority.audit_tree(audit["path"]) != audit:
            raise ValueError("Canonical source audit differs")
    expected = set()
    generated = {"WatchSource/runtime-selection.json": "canonical-desktop-selection-json",
                 "WatchSource/runtime-materials.json": "selected-original-json-tokens",
                 "WatchSource/runtime-inventory.json": "canonical-watch-runtime-inventory",
                 "NativeScene/native-scene-inventory.json": "decoded-native-scene-inventory"}
    for record in manifest["outputs"]:
        name = record["path"]
        if name in expected:
            raise ValueError("Duplicate provenance output")
        expected.add(name)
        data = ordinary_path(resources, name).read_bytes()
        if len(data) != record["bytes"] or digest(data) != record["sha256"]:
            raise ValueError("Staged provenance output differs: " + name)
        if not record["inputs"] or any(source not in inputs for source in record["inputs"]):
            raise ValueError("Output has missing/ambiguous canonical source dependencies: " + name)
        if record["encoding"] not in ("identity", "raw-deflate-v1"):
            raise ValueError("Unknown provenance encoding: " + name)
        decoded = decode_resource(data, require_container=record["encoding"] == "raw-deflate-v1")
        if "decoded_bytes" in record and (len(decoded) != record["decoded_bytes"] or digest(decoded) != record["decoded_sha256"]):
            raise ValueError("Decoded provenance output differs: " + name)
        algorithm = record.get("algorithm")
        if algorithm:
            if generated.get(name) != algorithm:
                raise ValueError("Unknown provenance derivation: " + name)
        else:
            canonical_name = ({"LICENSE.txt": "LICENSE", "CREDITS.md": "CREDITS.md", "zlib-LICENSE.txt": "windows/dependencies/zlib-LICENSE.txt"}.get(name)
                or ("Resources/WatchSource/" + name.removeprefix("NativeScene/") if name.startswith("NativeScene/") else "Resources/" + name))
            if record["inputs"] != [canonical_name] or decoded != inputs[canonical_name]:
                raise ValueError("Staged output is not byte-exact canonical source: " + name)
    actual = {path.relative_to(resources).as_posix() for path in resources.rglob("*") if path.is_file()}
    if actual != expected | {PROVENANCE, "resources-inventory.json"}:
        raise ValueError("Provenance has missing or extra staged outputs")
    # Recompute the original packer's complete desktop selection and every
    # derived container under UTF-8, without making a new staging directory.
    watch_generated = expected & {"WatchSource/runtime-selection.json", "WatchSource/runtime-materials.json", "WatchSource/runtime-inventory.json"}
    if authority.baseline == BASELINE and len(watch_generated) != 3:
        raise ValueError("Complete canonical desktop Watch provenance is required")
    if watch_generated:
        authority.read("scripts/package-watch-resources.py")
        try:
            subprocess.check_output([sys.executable, "-X", "utf8", str(authority.repository / "scripts/package-watch-resources.py"),
                "verify", windows_io_path(authority.repository / "Resources/WatchSource"), windows_io_path(resources / "WatchSource"), "--profile", "desktop"], stderr=subprocess.PIPE, timeout=120)
        except (OSError, subprocess.SubprocessError) as error:
            raise ValueError("Staged desktop Watch bytes differ from the canonical GitHub packer") from error
        if {a["path"] for a in manifest.get("source_audits", [])} != {"Resources/WatchSource"}:
            raise ValueError("Canonical Watch source-tree audit is required")
        policy_document = json.loads(policy)
        native_names = policy_document["native_scene_metadata"]
        if "Scene/desktop-profile-card.json" not in native_names:
            raise ValueError("Canonical desktop profile card is missing")
        native_records = []
        for name in native_names:
            data = authority.read("Resources/WatchSource/" + name)
            native_records.append({"path": name, "bytes": len(data), "sha256": digest(data), "source_path": "WatchSource/" + name, "source_sha256": digest(data)})
        native_inventory = {"schema": 1, "policy": "byte-exact-decoded-approved-metadata", "files": native_records,
                            "texture_root": "../WatchSource/Scene", "bytes": sum(r["bytes"] for r in native_records)}
        if ordinary_path(resources, "NativeScene/native-scene-inventory.json").read_bytes() != json_bytes(native_inventory):
            raise ValueError("Derived native desktop metadata inventory differs")
        extras = {name for name, _ in source_assets(authority.repository / "Resources", policy_document)}
        notices = {d["destination"] for d in policy_document.get("native_dependency_notices", [])}
        expected_non_watch = extras | notices | {"LICENSE.txt", "CREDITS.md", "NativeScene/native-scene-inventory.json"} | {"NativeScene/" + n for n in native_names}
        if {name for name in expected if not name.startswith("WatchSource/")} != expected_non_watch:
            raise ValueError("Staged output differs from the approved desktop asset selection")
    return manifest


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=("audit", "verify"))
    parser.add_argument("--repository", type=Path, default=REPOSITORY)
    parser.add_argument("--resources", type=Path)
    options = parser.parse_args()
    authority = SourceAuthority(options.repository)
    if options.mode == "verify":
        if options.resources is None:
            parser.error("verify requires --resources")
        manifest = verify_manifest(options.resources, authority=authority)
        print(json.dumps({"source": manifest["source"], "inputs": len(manifest["inputs"]), "outputs": len(manifest["outputs"]), "acceptance": manifest["acceptance"]}, indent=2))
    else:
        audit = authority.audit_tree("Resources")
        print(json.dumps({"source": authority.metadata(), "source_audit": audit, "acceptance": "restart-build-only-unverified"}, indent=2))


if __name__ == "__main__":
    main()
