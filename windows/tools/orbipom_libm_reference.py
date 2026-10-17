#!/usr/bin/env python3
"""Export the original JavaScriptCore Math primitive results of the OrbiPom transcript.

ECMA-262 leaves Math.sin/cos/atan2/... "implementation-approximated", so the
ten-second Mac transcript (orbipom-source.json) depends on Apple's libm. This
oracle reruns exactly that transcript through the unchanged Mac
Sources/OrbiPomRuntime.swift (JavaScriptCore + unchanged Resources/OrbiPom
scripts) with recording wrappers installed before start, and exports every
distinct (function, arguments) -> result as IEEE-754 bit patterns.

Self-checks (the oracle fails instead of writing a fixture):
  * the recorded run reproduces every orbipom-source.json frame exactly, so the
    wrappers did not perturb the transcript;
  * no implementation-approximated Math call happens while the unchanged scripts
    load (no libm-derived hidden state exists before a table can be installed);
  * neither script uses the ** operator (Number::exponentiate is also
    implementation-approximated and cannot be wrapped).

It also classifies every recorded result against a correctly rounded value
computed here with 80-digit decimal arithmetic. Apple's libm is not correctly
rounded on some recorded inputs, so no portable correctly rounded (or other
open) libm can reproduce the Mac transcript bit for bit.

No app, defaults, window, network or user data is touched. Normal C++ tests
consume the immutable JSON and need no macOS checkout.
"""
import argparse
import decimal
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[2]
AUTHORITY = "ca04f142185c7de40acd8523bdb563195d90a1d1"
PINS = ["Sources/OrbiPomRuntime.swift", "Resources/OrbiPom/matter-0.20.0.js", "Resources/OrbiPom/orbipom.js"]
TRANSCRIPT = ROOT / "windows/tests/fixtures/orbipom-source.json"
# ECMA-262 21.3.2: every Math function whose result is implementation-approximated.
APPROXIMATED = ["acos", "acosh", "asin", "asinh", "atan", "atanh", "atan2", "cbrt", "cos", "cosh", "exp", "expm1",
                "hypot", "log", "log1p", "log10", "log2", "pow", "sin", "sinh", "tan", "tanh"]

RECORDER = r'''var __libm=(function(){
 const names=NAMES,view=new DataView(new ArrayBuffer(8));
 const bits=v=>{view.setFloat64(0,v);return view.getUint32(0).toString(16).padStart(8,'0')+view.getUint32(4).toString(16).padStart(8,'0');};
 const rows={},seen={},counts={};let calls=0;
 for(const name of names){const original=Math[name];rows[name]=[];seen[name]=new Map();counts[name]=0;
  Math[name]=function(...args){const result=original.apply(Math,args);++calls;++counts[name];
   const key=args.map(a=>bits(Number(a))).join(','),encoded=bits(result),prior=seen[name].get(key);
   if(prior===undefined){seen[name].set(key,encoded);rows[name].push(args.map(a=>bits(Number(a))).concat([encoded]));}
   else if(prior!==encoded)throw new Error('Nondeterministic original Math.'+name);
   return result;};}
 return {calls:()=>calls,exportRows:()=>JSON.stringify({rows,counts,calls})};
})();undefined'''.replace("NAMES", json.dumps(APPROXIMATED))

MAIN = r'''import Foundation
import JavaScriptCore
enum HUDResources {
 static func url(for value:String)->URL? { URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true).appendingPathComponent("Resources").appendingPathComponent(value) }
}
let recorder=try String(contentsOfFile:CommandLine.arguments[2],encoding:.utf8)
// 1. Module-load probe: recording wrappers precede the unchanged scripts.
guard let probe=JSContext() else { fatalError("JavaScriptCore unavailable") }
probe.evaluateScript(recorder)
for name in ["matter-0.20.0.js","orbipom.js"] {
 guard let url=HUDResources.url(for:"OrbiPom/"+name) else { fatalError("missing \(name)") }
 probe.evaluateScript(try String(contentsOf:url,encoding:.utf8),withSourceURL:url)
 if let exception=probe.exception { fatalError("probe \(exception)") }
}
let loadCalls=probe.evaluateScript("__libm.calls()").toInt32()
// 2. The orbipom-source.json transcript through the unchanged Mac runtime.
let game=try OrbiPomRuntime()
_ = game.evaluateForTesting(recorder)
guard game.error==nil else { fatalError("recorder \(game.error!)") }
_ = game.start(seed:12345)
var frames:[[String:Any]]=[]
func capture(_ label:String) {
 guard let raw=game.evaluateForTesting("JSON.stringify(OrbiPom.snapshot())")?.toString(),game.error==nil,let data=raw.data(using:.utf8),let obj=try? JSONSerialization.jsonObject(with:data) else { fatalError("snapshot") }
 frames.append(["label":label,"snapshot":obj])
}
capture("start")
for second in 0..<10 {
 game.movePointer(x:Double(30+(second*47)%160),y:40)
 _ = game.drop()
 for _ in 0..<60 { game.advance(seconds:1/60) }
 capture("second-\(second+1)")
}
guard let table=game.evaluateForTesting("__libm.exportRows()")?.toString(),game.error==nil,let tableData=table.data(using:.utf8),let tableObject=try? JSONSerialization.jsonObject(with:tableData) else { fatalError("export") }
let output:[String:Any]=["moduleLoadCalls":loadCalls,"frames":frames,"table":tableObject]
try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[3]))
print("Original JavaScriptCore: \(frames.count) transcript snapshots with recorded Math primitives")
'''

decimal.getcontext().prec = 80
D = decimal.Decimal


def _pi():
    context = decimal.getcontext()
    context.prec += 10

    def arctan_inverse(n):
        x = D(1) / n
        square, total, term, k = x * x, x, x, 1
        while True:
            term *= -square
            k += 2
            step = term / k
            if abs(step) < D(10) ** -(context.prec + 2):
                break
            total += step
        return total
    value = 4 * (4 * arctan_inverse(5) - arctan_inverse(239))
    context.prec -= 10
    return +value


PI = _pi()
TINY = D(10) ** -95


def _series_sin(x):
    total, term, k, square = x, x, 1, x * x
    while abs(term) > TINY:
        term *= -square / ((k + 1) * (k + 2))
        k += 2
        total += term
    return total


def _series_cos(x):
    total, term, k, square = D(1), D(1), 0, x * x
    while abs(term) > TINY:
        term *= -square / ((k + 1) * (k + 2))
        k += 2
        total += term
    return total


def _quadrant(x):
    x = D(x)
    n = int((x / (PI / 2)).to_integral_value(rounding=decimal.ROUND_HALF_EVEN))
    return x - n * (PI / 2), n % 4


def exact_sin(x):
    r, q = _quadrant(x)
    return [_series_sin(r), _series_cos(r), -_series_sin(r), -_series_cos(r)][q]


def exact_cos(x):
    r, q = _quadrant(x)
    return [_series_cos(r), -_series_sin(r), -_series_cos(r), _series_sin(r)][q]


def _atan(x):
    negative, x, inverted, halvings = x < 0, abs(x), False, 0
    if x > 1:
        x, inverted = 1 / x, True
    while x > D("0.1"):
        x = x / (1 + (1 + x * x).sqrt())
        halvings += 1
    total, term, k, square = x, x, 1, x * x
    while abs(term) > TINY:
        term *= -square
        k += 2
        total += term / k
    value = total * (2 ** halvings)
    if inverted:
        value = PI / 2 - value
    return -value if negative else value


def exact_atan2(y, x):
    y, x = D(y), D(x)
    if x > 0:
        return _atan(y / x)
    if x < 0:
        return _atan(y / x) + (PI if y >= 0 else -PI)
    return PI / 2 if y > 0 else -PI / 2 if y < 0 else None


EXACT = {"sin": exact_sin, "cos": exact_cos, "atan2": exact_atan2}


def decode(bits):
    return struct.unpack(">d", bytes.fromhex(bits))[0]


def ulp_distance(a, b):
    def ordered(v):
        i = struct.unpack("<q", struct.pack("<d", v))[0]
        return -(1 << 63) - i if i < 0 else i
    return abs(ordered(a) - ordered(b))


def classify(name, row):
    """Return 'correctlyRounded', 'faithful' (other neighbour) or None if not evaluated."""
    function = EXACT.get(name)
    args = [decode(v) for v in row[:-1]]
    if function is None or any(v == 0 or v != v or abs(v) == float("inf") for v in args):
        return None
    exact = function(*args)
    if exact is None:
        return None
    nearest = float(exact)  # Decimal -> float conversion is correctly rounded
    # A value this close to a rounding midpoint would make the 80-digit
    # reference ambiguous; none is expected, so refuse instead of guessing.
    below, above = math.nextafter(nearest, -math.inf), math.nextafter(nearest, math.inf)
    spacing = D(above) - D(nearest)
    if min(abs(exact - (D(nearest) + D(below)) / 2), abs(exact - (D(nearest) + D(above)) / 2)) < spacing * D("1e-30"):
        raise RuntimeError(f"Correct rounding of Math.{name}{tuple(args)} is ambiguous at 80 digits")
    actual = decode(row[-1])
    distance = ulp_distance(actual, nearest)
    if distance > 1:
        raise RuntimeError(f"Original Math.{name}{tuple(args)} is not faithfully rounded")
    return "correctlyRounded" if distance == 0 else "faithful"


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--sdk", default="/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk")
    parser.add_argument("--output", type=Path, default=ROOT / "build/orbipom-libm-reference/source.json")
    parser.add_argument("--work", type=Path, default=ROOT / "build/orbipom-libm-reference")
    args = parser.parse_args()
    if args.output.exists():
        raise RuntimeError("Choose a new output path; original source fixtures are immutable")
    subprocess.run(["git", "-C", str(ROOT), "diff", "--quiet", AUTHORITY, "--"] + PINS, check=True)
    for script in PINS[1:]:
        if b"**" in (ROOT / script).read_bytes():
            raise RuntimeError(f"{script} uses the ** operator; its result cannot be recorded through Math")
    out = args.work
    out.mkdir(parents=True, exist_ok=True)
    cache = ROOT / "build/windows-module-reference/.compiler/module-cache"
    if not cache.is_dir():
        raise RuntimeError("Existing shared Swift module cache is required")
    (out / "main.swift").write_text(MAIN)
    (out / "recorder.js").write_text(RECORDER)
    subprocess.run(["/usr/bin/swiftc", "-O", "-sdk", args.sdk, "-module-cache-path", str(cache), "-framework", "JavaScriptCore",
                    str(ROOT / PINS[0]), str(out / "main.swift"), "-o", str(out / "oracle")], check=True)
    raw = out / "raw.json"
    raw.unlink(missing_ok=True)
    subprocess.run([str(out / "oracle"), str(ROOT), str(out / "recorder.js"), str(raw)], check=True)
    recorded = json.loads(raw.read_text())
    transcript = json.loads(TRANSCRIPT.read_text())
    if recorded["moduleLoadCalls"] != 0:
        raise RuntimeError("Unchanged scripts call implementation-approximated Math while loading")
    if recorded["frames"] != transcript["frames"]:
        raise RuntimeError("Recording wrappers changed the original transcript")
    table = recorded["table"]
    functions, classes = {}, {}
    for name in APPROXIMATED:
        rows = table["rows"][name]
        if not rows:
            continue
        functions[name] = rows
        tally = {"correctlyRounded": 0, "faithful": 0, "notEvaluated": 0}
        for row in rows:
            tally[classify(name, row) or "notEvaluated"] += 1
        classes[name] = tally
    result = {
        "authority": AUTHORITY,
        "sourcePins": {p: hashlib.sha256((ROOT / p).read_bytes()).hexdigest() for p in PINS},
        "transcriptSHA256": hashlib.sha256(TRANSCRIPT.read_bytes()).hexdigest(),
        "seed": 12345,
        "moduleLoadCalls": recorded["moduleLoadCalls"],
        "calls": {name: table["counts"][name] for name in functions},
        "totalCalls": table["calls"],
        "appleVersusCorrectlyRounded": classes,
        "functions": functions,
        "contract": "Rows are [argument bits..., JavaScriptCore result bits] as big-endian IEEE-754 hex for every distinct "
                    "implementation-approximated Math call made while the unchanged Mac runtime reproduces "
                    "orbipom-source.json exactly (wrappers installed after script load and before start(12345)). "
                    "appleVersusCorrectlyRounded counts results equal to the correctly rounded value versus the other "
                    "faithful neighbour (1 ULP away), computed with 80-digit decimal arithmetic.",
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, separators=(",", ":"), sort_keys=True) + "\n")
    print(args.output, hashlib.sha256(args.output.read_bytes()).hexdigest(), {k: len(v) for k, v in functions.items()}, classes)


if __name__ == "__main__":
    main()
