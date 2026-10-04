#!/usr/bin/env python3
"""Compare packaged desktop raw pixels against a saved pre-change renderer."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("baseline", type=Path)
    p.add_argument("--resources", type=Path)
    p.add_argument("--output", type=Path)
    p.add_argument("--source-directory", type=Path, help="Use an already frozen Sources directory for both variants")
    p.add_argument("--baseline-source-directory", type=Path, help="Compare the entire renderer/layout baseline, not just its Metal renderer")
    p.add_argument("--jobs", type=int, choices=(1, 2), default=2, help="Maximum concurrent fixture compilers")
    p.add_argument("--optimization", choices=("stencil", "textures", "bindings", "uniforms", "batches", "geometry"), default="stencil")
    a = p.parse_args()
    root = Path(__file__).resolve().parent.parent
    output = (a.output or root / "build/desktop-renderer-parity").resolve()
    resources = (a.resources or root / "build/dev/EndfieldHUD.app/Contents/Resources/WatchSource").resolve()
    output.mkdir(parents=True, exist_ok=True)
    baseline = a.baseline.resolve()
    source = (root / "scripts/render-source-watch-previews.sh").read_text()
    names = re.search(r"SOURCE_NAMES=\((.*?)\)", source, re.S).group(1).split()
    # Both binaries consume one immutable snapshot even while unrelated app
    # work proceeds. Record its hashes so this proof identifies exact inputs.
    snapshot = output / "source-snapshot"
    snapshot.mkdir(exist_ok=True)
    hashes = {}
    for name in names:
        data = ((a.source_directory or root / "Sources") / (name + ".swift")).read_bytes()
        (snapshot / (name + ".swift")).write_bytes(data)
        hashes[name] = hashlib.sha256(data).hexdigest()
    fixture = snapshot / "VerifyDesktopRenderer.swift"
    fixture.write_bytes((root / "scripts/VerifyDesktopRenderer.swift").read_bytes())
    sdk = subprocess.check_output([str(root / "scripts/build.sh"), "--print-sdk"], cwd=root, text=True).strip()
    jobs = []
    baseline_hashes = {}
    for mode in ("baseline", "current"):
        exe = output / (mode + "-fixture")
        sources = []
        for name in names:
            path = snapshot / (name + ".swift")
            if mode == "baseline":
                if a.baseline_source_directory:
                    frozen = output / "baseline-source-snapshot" / (name + ".swift")
                    frozen.parent.mkdir(exist_ok=True)
                    frozen.write_bytes((a.baseline_source_directory / (name + ".swift")).read_bytes())
                    path = frozen
                elif name == "HUDSourceMetalRenderer":
                    path = baseline
                baseline_hashes[name] = hashlib.sha256(path.read_bytes()).hexdigest()
            sources.append(str(path))
        command = ["xcrun", "swiftc", "-swift-version", "5", "-O", "-whole-module-optimization", "-parse-as-library",
                   "-D", "HUD_SOURCE_RENDER_PREVIEW", "-sdk", sdk, "-module-cache-path", str(output / (mode + "-module-cache")),
                   "-framework", "Cocoa", "-framework", "Metal", "-framework", "MetalKit"]
        command += sources + [str(fixture), "-o", str(exe)]
        if mode == "current" and a.optimization == "uniforms":
            command += ["-D", "HUD_SOURCE_PREPARED_UNIFORM_VERIFY"]
        if mode == "current" and a.optimization == "batches":
            command += ["-D", "HUD_SOURCE_ADJACENT_MERGE_VERIFY"]
        if mode == "current" and a.optimization == "geometry":
            command += ["-D", "HUD_SOURCE_INDEX_TOPOLOGY_VERIFY"]
        log = (output / (mode + "-compile.log")).open("w")
        proc = subprocess.Popen(command, cwd=root, stdout=log, stderr=subprocess.STDOUT)
        jobs.append((mode, exe, log, proc))
        if a.jobs == 1:
            status = proc.wait()
            if status:
                log.close()
                raise RuntimeError("Compilation failed: " + mode + "; see " + str(output))
    failed = []
    for mode, exe, log, proc in jobs:
        status = proc.wait(); log.close()
        if status: failed.append(mode)
    if failed: raise RuntimeError("Compilation failed: " + ", ".join(failed) + "; see " + str(output))
    for mode, exe, _, _ in jobs:
        with (output / (mode + "-run.log")).open("w") as log:
            subprocess.run([str(exe), str(resources), str(output / mode), "--ui-test"], cwd=root, stdout=log, stderr=subprocess.STDOUT, check=True)
    expected = {p.name for p in (output / "baseline").glob("*.bgra")}
    actual = {p.name for p in (output / "current").glob("*.bgra")}
    if not expected or expected != actual: raise RuntimeError("Pixel file set changed or empty")
    records = []
    for name in sorted(expected):
        before = (output / "baseline" / name).read_bytes()
        after = (output / "current" / name).read_bytes()
        if before != after:
            count = sum(x != y for x, y in zip(before, after))
            raise RuntimeError("Desktop pixel mismatch: " + name + "; differing bytes=" + str(count))
        records.append({"file": name, "bytes": len(after), "sha256": hashlib.sha256(after).hexdigest()})
    before = json.loads((output / "baseline/report.json").read_text())
    after = json.loads((output / "current/report.json").read_text())
    for b, c in zip(before["frames"], after["frames"]):
        for key in ("name", "batches", "batchStates", "frameDiagnostics"):
            assert b[key] == c[key], (c["name"], key)
        if c["name"].endswith("-fallback") or a.optimization != "stencil":
            assert c["depthFormat"] == b["depthFormat"] and c["attachmentBytes"] == b["attachmentBytes"]
        else:
            assert c["depthFormat"] != b["depthFormat"] and c["attachmentBytes"] < b["attachmentBytes"]
    assert before["usedTextureIDs"] == after["usedTextureIDs"]
    if a.optimization == "textures":
        assert after["initialStatistics"]["textureCount"] == 1
        assert after["initialStatistics"] == after["inventoryStatistics"]
        assert after["statistics"]["textureBytes"] < before["statistics"]["textureBytes"]
        assert after["statistics"]["textureCount"] < before["statistics"]["textureCount"]
        assert after["statistics"]["sourceTextureLoads"] + 2 == after["statistics"]["textureCount"]
    if a.optimization == "bindings":
        assert after["statistics"]["textureBytes"] == before["statistics"]["textureBytes"]
        assert after["statistics"]["textureCount"] == before["statistics"]["textureCount"]
        assert after["statistics"]["encoderBindingSkips"] > after["statistics"]["encoderBindingChanges"]
    if a.optimization == "uniforms":
        assert after["statistics"]["textureBytes"] == before["statistics"]["textureBytes"]
        assert after["statistics"]["textureCount"] == before["statistics"]["textureCount"]
        assert after["statistics"]["preparedUniformHits"] > 10_000
        assert after["statistics"]["preparedUniformResolutions"] > 0
    if a.optimization == "batches":
        assert after["statistics"]["textureBytes"] == before["statistics"]["textureBytes"]
        assert after["statistics"]["textureCount"] == before["statistics"]["textureCount"]
        assert after["statistics"]["mergedBatches"] > 0
        assert after["statistics"]["encodedPassDraws"] < after["statistics"]["sourcePassDraws"]
    report = {"baselineSHA256": hashlib.sha256(baseline.read_bytes()).hexdigest(),
              "baselineSourceSHA256": baseline_hashes,
              "rendererSHA256": hashes["HUDSourceMetalRenderer"], "sourceSHA256": hashes,
              "resources": str(resources), "exactPixelFiles": records,
              "optimization": a.optimization,
              "baselineStatistics": before["statistics"], "currentStatistics": after["statistics"],
              "combinedAttachmentBytes": before["frames"][0]["attachmentBytes"],
              "stencilAttachmentBytes": after["frames"][0]["attachmentBytes"],
              "usedTextureIDs": after["usedTextureIDs"], "fallbackVerified": True}
    (output / "parity-report.json").write_text(json.dumps(report, indent=2) + "\n")
    print("PASS:", len(records), "exact desktop GPU frames and batch states;", a.optimization, "allocation checks and combined-depth fallback pass")


if __name__ == "__main__": main()
