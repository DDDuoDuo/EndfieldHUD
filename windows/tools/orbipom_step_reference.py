#!/usr/bin/env python3
"""Export independent original-JavaScriptCore one-step physics fixtures.

Each case creates a fresh original runtime and constructs the stated initial
body state through unchanged Matter/source APIs. No app, window, timer or user
state is created; generated tests never replace the original physics scripts.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
PINS = ["Sources/OrbiPomRuntime.swift", "Resources/OrbiPom/matter-0.20.0.js", "Resources/OrbiPom/orbipom.js"]


def cases():
    result = []
    def add(name, bodies):
        statements = ["__game.phys.clearBodies()"]
        for index, (level, x, y, vx, vy, angle, angular) in enumerate(bodies):
            body = f"fixtureBody{index}"
            statements += [f"var {body}=__game.phys.spawn({level},{x},{y}).body",
                f"Matter.Body.setAngle({body},{angle})",
                f"Matter.Body.setVelocity({body},{{x:{vx},y:{vy}}})",
                f"Matter.Body.setAngularVelocity({body},{angular})"]
        result.append({"name": name, "seed": 12345, "setup": ";".join(statements) + ";undefined"})
    for level in range(1, 12):
        add(f"profile-{level}-free-fall", [(level, 115, 110, 1.25, -.75, .42, .013)])
    add("left-wall", [(4, 6, 130, -3, .5, -.31, -.017)])
    add("right-wall", [(6, 224, 145, 3, -.2, .28, .012)])
    add("floor", [(5, 140, 270, .5, 3, -.39, .03)])
    add("different-level-contact", [(4, 99, 150, 1.2, .5, .25, .008), (7, 129, 152, -.8, .2, -.35, -.01)])
    add("three-body-floor-contact", [(3, 85, 265, .3, 1, .2, .01), (6, 120, 240, -.3, 1, -.3, -.02), (9, 165, 250, -.7, 1, .4, .01)])
    add("level-one-merge", [(1, 110, 140, 0, 0, 0, 0), (1, 116, 140, 0, 0, 0, 0)])
    add("level-eleven-merge", [(11, 110, 140, 0, 0, 0, 0), (11, 116, 140, 0, 0, 0, 0)])
    return result


MAIN = r'''import Foundation
import JavaScriptCore
enum HUDResources {
 static func url(for value:String)->URL? { URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true).appendingPathComponent("Resources").appendingPathComponent(value) }
}
let data=try Data(contentsOf:URL(fileURLWithPath:CommandLine.arguments[2]))
let inputs=try JSONSerialization.jsonObject(with:data) as! [[String:Any]]
var cases:[[String:Any]]=[]
func capture(_ game:OrbiPomRuntime)->Any {
 guard let text=game.evaluateForTesting("JSON.stringify(OrbiPom.snapshot())")?.toString(),game.error==nil,let data=text.data(using:.utf8),let result=try? JSONSerialization.jsonObject(with:data) else { fatalError("Original source fixture failed") }
 return result
}
for input in inputs {
 let game=try OrbiPomRuntime()
 _ = game.start(seed:(input["seed"] as! NSNumber).uint32Value)
 _ = game.evaluateForTesting(input["setup"] as! String)
 var output=input
 output["before"]=capture(game)
 game.advance(seconds:1/60)
 output["after"]=capture(game)
 cases.append(output)
}
try JSONSerialization.data(withJSONObject:cases,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[3]))
print("Original JavaScriptCore: \(cases.count) independent initial states and single fixed steps")
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=ROOT / "build/orbipom-step-reference/source.json")
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError("Choose a new output path; source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    out = ROOT / "build/orbipom-step-reference"
    out.mkdir(exist_ok=True)
    cache = ROOT / "build/windows-module-reference/.compiler/module-cache"
    if not cache.is_dir():
        raise RuntimeError("Existing shared Swift module cache is required")
    (out / "input.json").write_text(json.dumps(cases(), separators=(",", ":")))
    (out / "main.swift").write_text(MAIN)
    subprocess.run(["/usr/bin/swiftc", "-O", "-sdk", "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk", "-module-cache-path", str(cache), "-framework", "JavaScriptCore", str(ROOT / PINS[0]), str(out / "main.swift"), "-o", str(out / "oracle")], check=True)
    subprocess.run([str(out / "oracle"), str(ROOT), str(out / "input.json"), str(out / "raw.json")], check=True)
    result = {"authority": AUTHORITY, "sourcePins": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PINS}, "contract": "Each fresh original runtime constructs explicit independent body initial conditions through unchanged source spawn and Matter public APIs; before and one 1/60-second step snapshots are authoritative. This does not require bitwise equality of chaotic long trajectories across platform Math implementations.", "cases": json.loads((out / "raw.json").read_text())}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n")
    print(args.output, hashlib.sha256(args.output.read_bytes()).hexdigest())


if __name__ == "__main__":
    main()
