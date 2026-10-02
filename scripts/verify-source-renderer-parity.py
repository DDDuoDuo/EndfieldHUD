#!/usr/bin/env python3
"""Compare the full GPU source fixture with an immutable renderer revision.

Only the renderer changes between the two executables. Both use the current
scene/frame builder and public source fixture assets, never desktop captures.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("baseline_ref", help="Git revision containing the authoritative renderer")
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = (args.output or root / "build/renderer-parity").resolve()
    output.mkdir(parents=True, exist_ok=True)
    revision = subprocess.check_output(["git", "rev-parse", args.baseline_ref], cwd=root, text=True).strip()
    baseline = output / "BaselineRenderer.swift"
    baseline.write_bytes(subprocess.check_output(["git", "show", revision + ":Sources/HUDSourceMetalRenderer.swift"], cwd=root))
    source = (root / "scripts/render-source-watch-previews.sh").read_text()
    names = re.search(r"SOURCE_NAMES=\((.*?)\)", source, re.S).group(1).split()
    sdk = subprocess.check_output([str(root / "scripts/build.sh"), "--print-sdk"], cwd=root, text=True).strip()
    jobs = []
    for mode in ("baseline", "current"):
        executable = output / (mode + "-fixture")
        sources = [str(baseline if mode == "baseline" and name == "HUDSourceMetalRenderer"
                       else root / "Sources" / (name + ".swift")) for name in names]
        command = ["xcrun", "swiftc", "-swift-version", "5", "-whole-module-optimization", "-parse-as-library", "-sdk", sdk,
                   "-module-cache-path", str(output / (mode + "-module-cache")),
                   "-framework", "Cocoa", "-framework", "Metal", "-framework", "MetalKit"]
        command += sources + [str(root / "scripts/RenderSourceWatchPreviews.swift"), "-o", str(executable)]
        log = (output / (mode + "-compile.log")).open("w")
        jobs.append((mode, executable, log, subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)))
    failed = []
    for mode, executable, log, process in jobs:
        status = process.wait(); log.close()
        if status: failed.append(mode)
    if failed:
        raise RuntimeError("Fixture compilation failed: " + ", ".join(failed) + "; see " + str(output))
    for mode, executable, _, _ in jobs:
        with (output / (mode + "-run.log")).open("w") as log:
            subprocess.run([str(executable), str(output / mode)], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    expected = {p.name for p in (output / "baseline").iterdir() if p.suffix in (".png", ".bgra", ".rg11b10f")}
    actual = {p.name for p in (output / "current").iterdir() if p.suffix in (".png", ".bgra", ".rg11b10f")}
    if not expected or expected != actual:
        raise RuntimeError("GPU fixture pixel outputs differ or are missing")
    records = []
    for name in sorted(expected):
        before = (output / "baseline" / name).read_bytes()
        after = (output / "current" / name).read_bytes()
        if before != after:
            raise RuntimeError("GPU pixel fixture changed: " + name)
        records.append({"file": name, "bytes": len(after), "sha256": hashlib.sha256(after).hexdigest()})
    report = {"baselineRevision": revision, "exactPixelFiles": records,
              "rendererSHA256": hashlib.sha256((root / "Sources/HUDSourceMetalRenderer.swift").read_bytes()).hexdigest()}
    (output / "parity-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS:", len(records), "exact GPU image/raw-pixel files match renderer", revision)


if __name__ == "__main__":
    main()
