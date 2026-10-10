#!/usr/bin/env python3
"""Run unchanged original Mac model/viewport methods on synthetic values only."""
from pathlib import Path
import argparse, hashlib, json, subprocess, tempfile

COMMIT = "ca04f142185c7de40acd8523bdb563195d90a1d1"
DRIVER = r'''
func p(_ p:CGPoint)->[Double]{[p.x,p.y]}
func r(_ r:CGRect)->[Double]{[r.minX,r.minY,r.width,r.height]}
func crop(_ c:MediaAssemblyCrop)->[Double]{[c.x,c.y,c.width,c.height]}
var trajectories:[[String:Any]]=[]
for size in [CGSize(width:1920,height:1080),CGSize(width:600,height:1600),CGSize(width:1,height:1),CGSize(width:0,height:0)] {
 for view in [CGRect(x:0,y:0,width:320,height:300),CGRect(x:41,y:53,width:497,height:281)] {
  var v=MediaAssemblyViewport();var rows:[[String:Any]]=[]
  for (index,factor) in [1.3,2,0.75,40,0.1,0.01,1.7,2.4].enumerated() {
   let at=CGPoint(x:view.minX+Double(index+1)*view.width/9,y:view.minY+view.height*0.37)
   v.magnify(by:factor,at:at,size:size,in:view)
   rows.append(["op":"zoom","factor":factor,"point":p(at),"zoom":v.zoom,"pan":p(v.pan),"rect":r(v.imageRect(size:size,in:view))])
   let d=CGPoint(x:Double(index%3-1)*193,y:Double(index%4-2)*131)
   v.move(by:d,size:size,in:view)
   rows.append(["op":"move","point":p(d),"zoom":v.zoom,"pan":p(v.pan),"rect":r(v.imageRect(size:size,in:view))])
  }
  v.reset();rows.append(["op":"reset","zoom":v.zoom,"pan":p(v.pan),"rect":r(v.imageRect(size:size,in:view))])
  trajectories.append(["size":[size.width,size.height],"view":r(view),"rows":rows])
 }
}
var transforms:[[String:Any]]=[]
for turns in -9...9 {for mirrored in [false,true] {for point in [CGPoint(x:0,y:0),CGPoint(x:1,y:1),CGPoint(x:0.173,y:0.719)] {
 let input=MediaAssemblyCrop(x:0.137,y:0.233,width:0.43,height:0.57)
 let display=MediaAssemblyViewport.displayCrop(input,quarterTurns:turns,mirrored:mirrored)
 transforms.append(["turns":turns,"mirrored":mirrored,"point":p(point),"displayPoint":p(MediaAssemblyViewport.displayPoint(point,quarterTurns:turns,mirrored:mirrored)),"sourcePoint":p(MediaAssemblyViewport.sourcePoint(point,quarterTurns:turns,mirrored:mirrored)),"crop":crop(input),"displayCrop":r(display),"roundtripCrop":crop(MediaAssemblyViewport.sourceCrop(display,quarterTurns:turns,mirrored:mirrored))])
}}}
var clamps:[[String:Any]]=[]
for rect in [CGRect(x:-0.2,y:-0.3,width:1.4,height:1.5),CGRect(x:1.2,y:0.999,width:0,height:0.001),CGRect(x:0.94,y:0.96,width:0.2,height:0.3)] {
 for turns in -3...3 {for mirror in [false,true] {clamps.append(["rect":r(rect),"turns":turns,"mirrored":mirror,"crop":crop(MediaAssemblyViewport.sourceCrop(rect,quarterTurns:turns,mirrored:mirror))])}}
}
var rotations:[[String:Any]]=[]
for degrees in [-360.0,-210,-45,0,17,90,180,360] {let point=CGPoint(x:190,y:51),center=CGPoint(x:40,y:90);rotations.append(["point":p(point),"center":p(center),"degrees":degrees,"result":p(MediaAssemblyViewport.unrotate(point,around:center,degrees:degrees))])}
var stickerRows:[[String:Any]]=[]
for kind in MediaAssemblyStickerKind.allCases {
 var s=MediaAssemblySticker();s.kind=kind;s.x=0.17;s.y=0.83;s.size=0.39;s.rotation=37
 let image=CGRect(x:21,y:43,width:625,height:319)
 stickerRows.append(["id":kind.rawValue,"english":kind.title,"chinese":L10n.chinese{kind.title},"pixelSize":[kind.pixelSize.width,kind.pixelSize.height],"rect":r(MediaAssemblyViewport.stickerRect(s,in:image))])
}
var ranges:[[String:Any]]=[]
for duration in [0.0,0.049,0.05,0.1,0.333333333333,12.345,600,86400] {
 for start in [0.0,0.0008333333333333333,0.001,0.05,1.234] {for end in [nil,0.01,0.1,4.731] as [Double?] {
  var a=MediaAssemblyAdjustments();a.trimStart=start;a.trimEnd=end
  var row:[String:Any]=["duration":duration,"start":start,"end":end.map{$0 as Any} ?? NSNull(),"valid":a.isValid]
  if let range=try? a.timeRange(duration:duration){row["range"]=[range.start.value,range.duration.value,Int64(range.start.timescale)]}else{row["range"]=NSNull()}
  ranges.append(row)
 }}
}
let output:[String:Any]=["sourceCommit":"ca04f142185c7de40acd8523bdb563195d90a1d1","trajectories":trajectories,"transforms":transforms,"clamps":clamps,"rotations":rotations,"stickers":stickerRows,"filters":MediaAssemblyFilter.allCases.map{value in ["id":value.rawValue,"english":value.title,"chinese":L10n.chinese{value.title}]},"ranges":ranges]
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys,.prettyPrinted]))
'''

def main():
    parser=argparse.ArgumentParser();parser.add_argument("output",type=Path);args=parser.parse_args()
    repo=Path(__file__).resolve().parents[2]
    def source(name):return subprocess.check_output(["git","show",f"{COMMIT}:Sources/{name}"],cwd=repo).decode()
    model=source("MediaAssemblyModel.swift");viewport=source("MediaAssemblyViewport.swift")
    # Only unrelated document/file/engine ownership is outside this model test.
    prefix=model[:model.index("struct MediaAssemblyDocument {")]
    glue='''\nimport Foundation\nenum L10n {static var zh=false;static func text(_ en:String,_ cn:String)->String{zh ? cn:en};static func chinese(_ body:()->String)->String{zh=true;defer{zh=false};return body()}}\nenum MediaAssemblyError:Error{case invalidAdjustment}\n'''
    with tempfile.TemporaryDirectory(prefix="endfield-media-model-") as temp:
        temp=Path(temp);src=temp/"main.swift";exe=temp/"reference"
        src.write_text(prefix+"\n"+viewport+glue+DRIVER)
        subprocess.run(["/usr/bin/swiftc","-O","-sdk","/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk","-module-cache-path",str(repo/"build/windows-module-reference/.compiler/module-cache"),str(src),"-o",str(exe)],check=True)
        result=subprocess.check_output([str(exe)])
    data=json.loads(result);assert data["sourceCommit"]==COMMIT
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_bytes(result+b"\n")
    print(json.dumps({"fixture":str(args.output),"bytes":len(result)+1,"sha256":hashlib.sha256(result+b"\n").hexdigest(),"sourceModelSHA256":hashlib.sha256(model.encode()).hexdigest(),"sourceViewportSHA256":hashlib.sha256(viewport.encode()).hexdigest()}))

if __name__=="__main__":main()
