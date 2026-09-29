#!/usr/bin/env python3
"""Set/check distributable bundle modes without following links or changing bytes.

Run normalization only on a build or packaging staging copy. --check is read-only.
"""
import argparse
import os
from pathlib import Path
import stat
import sys


def standard_mode(mode):
    if stat.S_ISDIR(mode):
        return 0o755
    if stat.S_ISREG(mode):
        return 0o755 if mode & 0o111 else 0o644
    raise ValueError("Only directories and regular files have normalized modes")


def process_bundle(path, check=False):
    requested = Path(path).absolute()
    if requested.is_symlink() or not requested.is_dir() or requested.suffix != ".app":
        raise ValueError("Expected a real .app directory, not a symlink")
    root = requested.resolve()
    entries = [root]
    for folder, directories, files in os.walk(root, followlinks=False, onerror=lambda error: (_ for _ in ()).throw(error)):
        entries.extend(Path(folder) / name for name in directories + files)
    # Reject unsafe entries before changing any modes. In particular, chmod of
    # a hard link could alter a file outside the caller's staging directory.
    for entry in entries:
        info = entry.lstat()
        if stat.S_ISLNK(info.st_mode):
            resolved = entry.resolve()
            if resolved != root and root not in resolved.parents:
                raise ValueError(f"Symlink escapes bundle: {entry.relative_to(root)}")
        elif stat.S_ISREG(info.st_mode):
            if info.st_nlink != 1:
                raise ValueError(f"Hard-linked file is not a standalone staging copy: {entry.relative_to(root)}")
        elif not stat.S_ISDIR(info.st_mode):
            raise ValueError(f"Special file is not distributable: {entry.relative_to(root)}")

    changed = 0
    checked = 0

    def visit(descriptor, relative):
        nonlocal changed, checked
        info = os.fstat(descriptor)
        if stat.S_ISREG(info.st_mode) and info.st_nlink != 1:
            raise ValueError(f"Hard-linked file changed during traversal: {relative}")
        wanted = standard_mode(info.st_mode)
        checked += 1
        if stat.S_IMODE(info.st_mode) != wanted:
            if check:
                raise ValueError(f"Nonstandard permissions: {relative} is {stat.S_IMODE(info.st_mode):04o}; expected {wanted:04o}")
            os.fchmod(descriptor, wanted)
            changed += 1
        if stat.S_ISDIR(info.st_mode):
            with os.scandir(descriptor) as children:
                names = sorted(entry.name for entry in children)
            for name in names:
                child_info = os.stat(name, dir_fd=descriptor, follow_symlinks=False)
                if stat.S_ISLNK(child_info.st_mode):
                    continue
                flags = os.O_RDONLY | os.O_NOFOLLOW
                if stat.S_ISDIR(child_info.st_mode):
                    flags |= os.O_DIRECTORY
                elif not stat.S_ISREG(child_info.st_mode):
                    raise ValueError(f"Unexpected special file: {relative}/{name}")
                child = os.open(name, flags, dir_fd=descriptor)
                try:
                    visit(child, f"{relative}/{name}" if relative != "." else name)
                finally:
                    os.close(child)

    descriptor = os.open(root, os.O_RDONLY | os.O_DIRECTORY | os.O_NOFOLLOW)
    try:
        visit(descriptor, ".")
    finally:
        os.close(descriptor)
    return checked, changed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify exact standard modes without changing anything")
    parser.add_argument("app", type=Path)
    arguments = parser.parse_args()
    try:
        checked, changed = process_bundle(arguments.app, check=arguments.check)
    except (OSError, ValueError, RuntimeError) as error:
        print(error, file=sys.stderr)
        return 1
    print(f"{'Checked' if arguments.check else 'Normalized'} {checked} bundle entries; {changed} permission changes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
