#!/usr/bin/env python3
"""Check Mac source identity without reading app data or running the app."""
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


if __name__ == "__main__":
    try:
        authority = verify(Path(__file__).resolve().parents[2])
        print(f"Mac source verified: {authority['release']} build {authority['build']} ({authority['commit'][:12]})")
    except (OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        print(f"Source authority check failed: {error}", file=sys.stderr)
        sys.exit(1)
