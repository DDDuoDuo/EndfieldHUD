#!/usr/bin/env python3
"""Detached CALayer oracle using verbatim current NotesCanvas methods.

No application/window, providers, user data, capture or production edits. Only
the named generated Swift executable/source/JSON are written under --output.
"""
import argparse
import hashlib
import pathlib
import subprocess

def method(source, signature):
    start = source.index(signature)
    opening = source.index("{", start)
    depth = 1
    end = opening + 1
    while depth:
        depth += (source[end] == "{") - (source[end] == "}")
        end += 1
    return source[start:end]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=pathlib.Path, required=True)
    args = parser.parse_args()
    root = pathlib.Path(__file__).resolve().parents[2]
    original = (root / "Sources/NotesCanvas.swift").read_bytes()
    source = original.decode()
    methods = "\n".join(method(source, name) for name in [
        "private func mediaSeekRect(", "private func updateMediaProgress("])
    swift = r'''
import AppKit
import QuartzCore
struct Reference {var kind = Kind.video; var duration:Double?; enum Kind {case video}}
struct CanvasNote {var id=1;var width:Double;var height:Double;var media:Reference?}
final class Media {var currentTime=0.0;var isPlaying=false}
// Detached layers can discard scheduled animations at commit. Record the exact
// original add call, without changing the authored method or sampling a window.
final class RecordedLayer:CALayer {var recorded:CABasicAnimation?
    override func add(_ animation:CAAnimation,forKey key:String?){recorded=animation.copy() as? CABasicAnimation;super.add(animation,forKey:key)}
}
final class NoteNode {var layer=CALayer();var mediaRail=CALayer();var mediaFill=RecordedLayer();var mediaHandle=CALayer();var media:Media?=Media()}
struct Seek {var id=1;var seconds:Double}
final class Probe {
    var mediaSeek:Seek?;var isVisible=true;var reduced=false
    let muted=NSColor(white:0.4,alpha:0.7),accent=NSColor.orange,primary=NSColor.white
    func reduceMotion()->Bool {reduced}
    METHODS
    func evaluate(width:Double,height:Double,duration:Double,time:Double,preview:Double?,flags:Int)->[String:Any]{
        let item=CanvasNote(width:width,height:height,media:Reference(duration:duration)),node=NoteNode()
        isVisible = flags & 4 == 0; reduced = flags & 8 != 0
        node.media!.isPlaying = flags & 2 == 0;node.media!.currentTime=time
        mediaSeek=preview.map{Seek(seconds:$0)}
        CATransaction.begin();CATransaction.setDisableActions(true)
        updateMediaProgress(item,node:node,animated:flags & 1 == 0)
        CATransaction.commit()
        let seek=mediaSeekRect(item),fill=node.mediaFill.bounds,handle=node.mediaHandle.position
        var animation:Any=NSNull()
        if let value=node.mediaFill.recorded {
            animation=["from":value.fromValue!,"to":value.toValue!,"duration":value.duration]
        }
        return ["width":width,"height":height,"duration":duration,"time":time,"preview":preview as Any? ?? NSNull(),"animated":flags & 1 == 0,"playing":node.media!.isPlaying,"visible":isVisible,"reduced":reduced,
            "seek":[seek.minX,seek.minY,seek.width,seek.height],"fillBounds":[fill.minX,fill.minY,fill.width,fill.height],"handlePosition":[handle.x,handle.y],"animation":animation]
    }
}
var rows=[[String:Any]]();let probe=Probe()
for (w,h) in [(228.0,154.0),(162.0,110.0),(10.0,35.0),(602.5,242.25)] {
    for duration in [0.25,10.0,31536000.0] {for time in [-1.0,0.0,2.5,duration-0.05,duration+2] {
        for flags in 0..<16 {for preview:Double? in [nil,0.0,duration*0.75] {
            rows.append(probe.evaluate(width:w,height:h,duration:duration,time:time,preview:preview,flags:flags))
        }}
    }}
}
let result:[String:Any]=["sourceSHA256":"SOURCE_HASH","cases":rows]
let data=try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys])
try data.write(to:URL(fileURLWithPath:CommandLine.arguments[1]),options:.atomic)
print("PASS detached original Notes media oracle: \(rows.count) cases")
'''.replace("METHODS", methods).replace("SOURCE_HASH", hashlib.sha256(original).hexdigest())
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    if (output / "reference.json").exists():
        raise SystemExit("Refusing to replace an existing media oracle")
    path = output / "original-notes-media.swift"
    path.write_text(swift)
    sdk = subprocess.check_output(["bash", str(root / "scripts/build.sh"), "--print-sdk"], text=True).strip()
    cache = root / "build/windows-shell-packet-live-closure/.compiler/module-cache"
    subprocess.run(["xcrun", "swiftc", "-O", "-sdk", sdk, "-module-cache-path", str(cache), str(path), "-o", str(output / "original-notes-media")], check=True)
    subprocess.run([str(output / "original-notes-media"), str(output / "reference.json")], check=True)

if __name__ == "__main__":
    main()
