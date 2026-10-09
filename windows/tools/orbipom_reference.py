#!/usr/bin/env python3
"""Regenerate an isolated original OrbiPom oracle; no app/defaults/window/network.

The existing Swift module cache is reused. Original runtime/artwork/tests are
compiled unchanged; the geometry helper extracts exact source declarations.
Normal C++ tests consume the immutable JSON and require no macOS checkout.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[2]
MAIN = r'''import Foundation
import JavaScriptCore
enum HUDResources {
 static func url(for value:String)->URL? { URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true).appendingPathComponent("Resources").appendingPathComponent(value) }
}
__SOURCE_GEOMETRY__
let count=OrbiPomRuntimeTests.run()
let game=try OrbiPomRuntime()
_ = game.start(seed:12345)
var frames:[[String:Any]]=[]
func capture(_ label:String) {
 guard let raw=game.evaluateForTesting("JSON.stringify(OrbiPom.snapshot())")?.toString(),let data=raw.data(using:.utf8),let obj=try? JSONSerialization.jsonObject(with:data) else { fatalError("snapshot") }
 frames.append(["label":label,"snapshot":obj])
}
capture("start")
for second in 0..<10 {
 game.movePointer(x:Double(30+(second*47)%160),y:40)
 _ = game.drop()
 for _ in 0..<60 { game.advance(seconds:1/60) }
 capture("second-\(second+1)")
}
var availability:[[Int]]=[]
let phases:[String?]=[nil,"armed","selecting","casting"]
for (stateIndex,state) in ["idle","playing","over"].enumerated(){for paused in [false,true]{for (phaseIndex,phase) in phases.enumerated(){for energy in 0...3{for charge in [0,5,6]{
 var snapshot=OrbiPomSnapshot();snapshot.state=state;snapshot.paused=paused;snapshot.skillPhase=phase;snapshot.energy=energy;snapshot.swapCharge=charge
 let bits=OrbiPomSkill.allCases.enumerated().reduce(0){$0 | (snapshot.canUse($1.element) ? (1 << $1.offset):0)}
 availability.append([stateIndex,paused ? 1:0,phaseIndex,energy,charge,bits])
}}}}}
let points=[CGPoint(x:87,y:88),CGPoint(x:340,y:396),CGPoint(x:87,y:55),CGPoint(x:120.25,y:181.75),CGPoint(x:-1,y:800),CGPoint(x:0,y:0),CGPoint(x:440,y:440)]
let worlds=points.map{p -> [Double] in let q=SourceGeometry.world(p);return [p.x,p.y,q.x,q.y]}
let output:[String:Any]=["originalRuntimeChecks":count,"seed":12345,"frames":frames,"availability":availability,"worlds":worlds]
try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[2]))
print("Original OrbiPom runtime: \(count) checks, \(frames.count) snapshots")
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default="/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk")
    parser.add_argument("--output", type=Path, default=ROOT / "build/orbipom-reference/source.json")
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError("Choose a new output path; original source fixtures are immutable")
    out = ROOT / "build/orbipom-reference"
    out.mkdir(parents=True, exist_ok=True)
    cache = ROOT / "build/windows-module-reference/.compiler/module-cache"
    if not cache.is_dir():
        raise RuntimeError("Expected existing shared Swift module cache is unavailable")
    canvas = (ROOT / "Sources/OrbiPomCanvas.swift").read_text()
    board = re.search(r"    static let board = .*", canvas)[0]
    play = re.search(r"    static let playArea = .*", canvas)[0]
    world = re.search(r"    private func world\(_ point: CGPoint\) -> CGPoint \{.*?\n    \}", canvas, re.S)[0].replace("private func world", "static func world")
    geometry = "enum SourceGeometry {\n" + board + "\n" + play + "\n" + world + "\n}"
    source = out / "main.swift"
    source.write_text(MAIN.replace("__SOURCE_GEOMETRY__", geometry))
    sdk = args.sdk
    sources = ["Sources/OrbiPomRuntime.swift", "Sources/OrbiPomArtwork.swift", "Tests/OrbiPomRuntimeTests.swift"]
    command = ["/usr/bin/swiftc", "-O", "-sdk", sdk, "-module-cache-path", str(cache), "-framework", "AppKit", "-framework", "JavaScriptCore", "-framework", "CryptoKit"]
    subprocess.run(command + [str(ROOT / p) for p in sources] + [str(source), "-o", str(out / "oracle")], check=True)
    raw = out / "original-output.json"
    subprocess.run([str(out / "oracle"), str(ROOT), str(raw)], check=True)
    result = json.loads(raw.read_text())
    pins = ["Sources/OrbiPomRuntime.swift", "Sources/OrbiPomSession.swift", "Sources/OrbiPomCanvas.swift", "Sources/OrbiPomArtwork.swift", "Tests/OrbiPomRuntimeTests.swift", "Resources/OrbiPom/matter-0.20.0.js", "Resources/OrbiPom/orbipom.js"]
    result["authority"] = "ca04f142185c7de40acd8523bdb563195d90a1d1"
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", result["authority"], "--"] + pins, check=True)
    result["sourcePins"] = {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in pins}
    result["contract"] = "Original JavaScriptCore physics transcript, not evidence that a future Windows runtime matches. availability rows: state index(idle,playing,over),paused,phase index(null,armed,selecting,casting),energy,swapCharge,enabled skill bitmask(clear,wind,shake,swap). worlds extracts original board/world expressions."
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n")
    print(str(args.output), hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
