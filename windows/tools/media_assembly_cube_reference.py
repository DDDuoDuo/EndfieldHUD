#!/usr/bin/env python3
"""Run the unchanged original cube reader/filter on synthetic one-pixel colors."""
import argparse
import hashlib
import json
import subprocess
import tempfile
from pathlib import Path

COMMIT = "ca04f142185c7de40acd8523bdb563195d90a1d1"
DRIVER = r'''
let color=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
let inputs:[[Float]]=[[0,0,0,1],[1,1,1,1],[1,0,0,1],[0,1,0,1],[0,0,1,1],
 [0.1,0.5,0.9,1],[0.173,0.719,0.233,1],[0.5,0.5,0.5,1],[0.25,0.75,0.125,1],
 [0.999,0.001,0.03225806,1],[0.09677419,0.29032258,0.80645161,1]]
var rows:[[String:Any]]=[]
for preset in MediaAssemblyFilter.allCases where preset != .none {
 for input in inputs {
  let bytes=input.withUnsafeBytes{Data($0)}
  let image=CIImage(bitmapData:bytes,bytesPerRow:16,size:CGSize(width:1,height:1),format:.RGBAf,colorSpace:color)
  let filtered=try MediaAssemblyAssetCatalog.apply(preset,to:image)
  var output=[Float](repeating:0,count:4)
  output.withUnsafeMutableBytes {context.render(filtered,toBitmap:$0.baseAddress!,rowBytes:16,bounds:CGRect(x:0,y:0,width:1,height:1),format:.RGBAf,colorSpace:color)}
  rows.append(["filter":preset.rawValue,"input":input,"output":output])
 }
}
MediaAssemblyAssetCatalog.clearCaches()
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["rows":rows],options:[.sortedKeys]))
'''


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("assets", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    repo = Path(__file__).resolve().parents[2]
    sources = {name: subprocess.check_output(["git", "show", f"{COMMIT}:Sources/{name}"], cwd=repo).decode() for name in ["MediaAssemblyModel.swift", "MediaAssemblyAssetCatalog.swift"]}
    model = sources["MediaAssemblyModel.swift"].split("struct MediaAssemblyDocument {")[0]
    glue = r'''
enum L10n {static func text(_ en:String,_ cn:String)->String{en}}
enum MediaAssemblyError:Error {case invalidAdjustment,unavailable,unsupported,exportFailed}
enum HUDResources {static func url(for relative:String)->URL? {
 guard relative.hasPrefix("MediaAssembly/") else {return nil}
 return URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(String(relative.dropFirst("MediaAssembly/".count)))
}}
'''
    with tempfile.TemporaryDirectory(prefix="endfield-media-cube-") as temp:
        temp = Path(temp)
        (temp / "main.swift").write_text(model + sources["MediaAssemblyAssetCatalog.swift"] + glue + DRIVER)
        subprocess.run(["/usr/bin/swiftc", "-O", "-sdk", "/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk", "-module-cache-path", str(repo / "build/windows-module-reference/.compiler/module-cache"), str(temp / "main.swift"), "-o", str(temp / "reference")], check=True)
        result = json.loads(subprocess.check_output([str(temp / "reference"), str(args.assets.resolve())]))
    result.update({"sourceCommit": COMMIT, "sourcePins": {name: hashlib.sha256(value.encode()).hexdigest() for name, value in sources.items()}, "usesAppOrWindow": False})
    data = json.dumps(result, sort_keys=True, separators=(",", ":")).encode() + b"\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(data)
    print(json.dumps({"samples": len(result["rows"]), "sha256": hashlib.sha256(data).hexdigest()}))


if __name__ == "__main__":
    main()
