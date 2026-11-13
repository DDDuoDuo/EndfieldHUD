#!/usr/bin/env python3
"""Check Mac source identity without reading app data or running the app.

With --inputs, check the frozen Mac-exported shell inputs instead
(windows/inputs/shell/inputs.json: every file, its size and SHA-256).
"""
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def git(root, *args):
    return subprocess.check_output(["git", "-C", str(root), *args], stderr=subprocess.PIPE)


def verify(root):
    authority = json.loads((root / "windows/source-authority.json").read_text(encoding="utf-8"))
    revision = authority["commit"]
    failures = []
    for directory, expected_tree in authority["trees"].items():
        actual_tree = git(root, "rev-parse", f"{revision}:{directory}").decode().strip()
        if actual_tree != expected_tree:
            failures.append(f"Mac baseline tree differs: {directory}")
            continue
        entries = git(root, "ls-tree", "-rz", revision, "--", directory).split(b"\0")
        expected_files = set()
        for entry in filter(None, entries):
            metadata, raw_path = entry.split(b"\t", 1)
            mode, kind, expected = metadata.split()
            relative = raw_path.decode("utf-8")
            expected_files.add(relative)
            path = root / relative
            if mode not in (b"100644", b"100755"):
                failures.append(f"Unsupported reference source type: {relative}")
                continue
            if kind != b"blob" or path.is_symlink() or not path.is_file():
                failures.append(f"Missing or substituted Mac source: {relative}")
                continue
            content = path.read_bytes()
            actual = hashlib.sha1(b"blob " + str(len(content)).encode() + b"\0" + content).hexdigest()
            if actual != expected.decode("ascii"):
                failures.append(f"Changed Mac reference: {relative}")
        actual_files = {p.decode("utf-8") for p in git(root, "ls-files", "-z", "--cached", "--others", "--exclude-standard", "--", directory).split(b"\0") if p}
        failures.extend(f"Unexpected reference source: {path}" for path in sorted(actual_files - expected_files))
    if failures:
        raise ValueError("\n".join(failures[:20]) + (f"\n… {len(failures)} failures" if len(failures) > 20 else ""))
    return authority


INPUTS = "windows/inputs/shell"


def verify_inputs(root):
    folder = root / INPUTS
    manifest = json.loads((folder / "inputs.json").read_text(encoding="utf-8"))
    failures = []
    for relative, expected in manifest["files"].items():
        path = folder / relative
        if path.is_symlink() or not path.is_file():
            failures.append(f"Missing input: {relative}")
            continue
        content = path.read_bytes()
        if len(content) != expected["bytes"] or hashlib.sha256(content).hexdigest() != expected["sha256"]:
            failures.append(f"Changed input: {relative}")
    present = {p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file()}
    failures.extend(f"Unlisted input: {path}" for path in sorted(present - set(manifest["files"]) - {"inputs.json"}))
    if failures:
        raise ValueError("\n".join(failures[:20]) + (f"\n… {len(failures)} failures" if len(failures) > 20 else ""))
    return manifest


if __name__ == "__main__":
    root = Path(__file__).resolve().parents[2]
    try:
        if sys.argv[1:] == ["--inputs"]:
            manifest = verify_inputs(root)
            print(f"Shell inputs verified: {len(manifest['files'])} files exported {manifest['exported']} (Mac {manifest['provenance']['macCommit'][:12]})")
        else:
            authority = verify(root)
            print(f"Mac source verified: {authority['release']} build {authority['build']} ({authority['commit'][:12]})")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Source authority check failed: {error}", file=sys.stderr)
        sys.exit(1)
