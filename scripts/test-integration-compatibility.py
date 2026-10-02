#!/usr/bin/env python3
"""Guard the stable desktop/data contract while replacing its visual shell.

This reads repository files only. --behavioral additionally runs the existing
core suite, whose persistence cases use temporary stores and defaults suites.
It never starts the application or reads the user's Application Support folder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "Tests/Fixtures/stable-integration-contract.json"


def entries(source):
    return {line.strip().removesuffix(",") for line in source.splitlines()
            if line.strip().startswith("Entry(")}


def check(root, baseline):
    failures = []
    for name, expected in baseline["files"].items():
        source = root / name
        if not source.is_file():
            failures.append(f"Missing stable functional file: {name}")
        elif hashlib.sha256(source.read_bytes()).hexdigest() != expected:
            failures.append(f"Stable behavior/data contract changed: {name}")
    try:
        info = plistlib.loads((root / "Resources/Info.plist").read_bytes())
        for key, expected in baseline["infoPlist"].items():
            if info.get(key) != expected:
                failures.append(f"Stable application/update identity changed: {key}")
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        failures.append(f"Cannot inspect Info.plist: {error}")
    try:
        current = entries((root / "Sources/LocalizationCatalog.swift").read_text())
        for entry in baseline["translations"]:
            if entry not in current:
                failures.append(f"Stable translated text changed or removed: {entry[:100]}")
    except OSError as error:
        failures.append(f"Cannot inspect localization catalog: {error}")
    return failures


def self_test(baseline):
    # Mutations stay entirely inside this owned temporary checkout fixture.
    with tempfile.TemporaryDirectory(prefix="EndfieldHUD-Compatibility-") as temporary:
        root = Path(temporary)
        names = list(baseline["files"]) + ["Resources/Info.plist", "Sources/LocalizationCatalog.swift"]
        for name in names:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        assert not check(root, baseline), "Current repository must pass before mutation checks"
        store = root / "Sources/UserProfileStore.swift"
        original = store.read_bytes()
        store.write_bytes(original.replace(b"EndfieldCharge/Profile", b"EndfieldHUD/Profile"))
        assert any("UserProfileStore" in failure for failure in check(root, baseline)), "A changed data path must fail"
        store.write_bytes(original)
        plist = root / "Resources/Info.plist"
        original = plist.read_bytes()
        values = plistlib.loads(original)
        values["CFBundleIdentifier"] = "example.incompatible"
        plist.write_bytes(plistlib.dumps(values))
        assert any("CFBundleIdentifier" in failure for failure in check(root, baseline)), "A changed defaults/permission identity must fail"
        plist.write_bytes(original)
        catalog = root / "Sources/LocalizationCatalog.swift"
        original = catalog.read_text()
        catalog.write_text(original.replace(baseline["translations"][0], "", 1))
        assert any("translated text" in failure for failure in check(root, baseline)), "Removed stable copy must fail"
        catalog.write_text(original + '\nEntry("Additional text", "新增", "新增", "追加"),\n')
        assert not check(root, baseline), "Additional integration strings may coexist with all stable translations"
    print("Passed 4 isolated compatibility-guard mutation checks.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Verify guard failures in temporary copies")
    parser.add_argument("--behavioral", action="store_true", help="Also compile/run the existing isolated core tests")
    args = parser.parse_args()
    baseline = json.loads(MANIFEST.read_text())
    failures = check(ROOT, baseline)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("Review against the stable baseline; do not regenerate hashes to bypass a regression.")
        return 1
    print(f"Stable {baseline['baselineCommit'][:7]} contract preserved: "
          f"{len(baseline['files'])} functional/localization files, "
          f"{len(baseline['translations'])} translated entries, and bundle/update identity.")
    if args.self_test:
        self_test(baseline)
    if args.behavioral:
        return subprocess.call(["bash", str(ROOT / "scripts/test.sh")], cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
