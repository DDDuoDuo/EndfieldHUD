#!/usr/bin/env python3
"""Original engine geometry on small owned color grids; no app/user media."""
from pathlib import Path
import hashlib,json,subprocess,tempfile,sys
root=Path(__file__).resolve().parents[2]; commit='ca04f142185c7de40acd8523bdb563195d90a1d1'
def source(n):return subprocess.check_output(['git','show',commit+':Sources/'+n],cwd=root).decode()
engine=source('MediaAssemblyEngine.swift');model=source('MediaAssemblyModel.swift')
a=engine.index('    static func outputSize(');b=engine.index('    func makePlaybackItem(',a)
extracted=engine[a:b]
stub='''\nimport AppKit\nimport CoreImage\nenum L10n {static func text(_ en:String,_ cn:String)->String{en}}\nenum MediaAssemblyError:Error{case invalidAdjustment,unsupported,exportFailed,unavailable}\nenum MediaAssemblyAssetCatalog {static func apply(_ f:MediaAssemblyFilter,to image:CIImage)throws->CIImage{precondition(f == .none);return image};static func stickerImage(_ k:MediaAssemblyStickerKind)->CGImage?{nil}}\n'''
driver=r'''
let colorSpace=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:colorSpace])
var rows:[[String:Any]]=[]
for dimensions in [(9,7),(17,11),(1,13)] {
 let (w,h)=dimensions
 var bytes=[UInt8](repeating:0,count:w*h*4)
 for y in 0..<h {for x in 0..<w {let k=(y*w+x)*4;bytes[k]=UInt8(x*11+7);bytes[k+1]=UInt8(y*13+9);bytes[k+2]=71;bytes[k+3]=255}}
 let data=Data(bytes) as CFData
 let input=CGImage(width:w,height:h,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:w*4,space:colorSpace,bitmapInfo:CGBitmapInfo(rawValue:CGImageAlphaInfo.last.rawValue),provider:CGDataProvider(data:data)!,decode:nil,shouldInterpolate:false,intent:.defaultIntent)!
 for crop in [MediaAssemblyCrop(),MediaAssemblyCrop(x:0.13,y:0.17,width:0.61,height:0.59),MediaAssemblyCrop(x:0,y:0.7,width:1,height:0.3)] {
  for turn in -3...3 {for mirror in [false,true] {
   var a=MediaAssemblyAdjustments();a.crop=crop;a.rotationQuarterTurns=turn;a.mirrored=mirror
   let output=try GeometryOracle.apply(a,to:CIImage(cgImage:input),includeStickers:false)
   let image=context.createCGImage(output,from:output.extent,format:.RGBA8,colorSpace:colorSpace)!
   let data=image.dataProvider!.data! as Data
   var pixels:[[Int]]=[]
   for y in 0..<image.height {for x in 0..<image.width {let k=y*image.bytesPerRow+x*4;pixels.append([Int(data[k]),Int(data[k+1]),Int(data[k+2]),Int(data[k+3])])}}
   var sizes:[[String:Any]]=[]
   for bound:CGFloat? in [nil,5,1024] {for even in [false,true] {let s=GeometryOracle.outputSize(CGSize(width:w,height:h),adjustments:a,maximumDimension:bound,even:even);sizes.append(["bound":bound.map{Double($0) as Any} ?? NSNull(),"even":even,"size":[s.width,s.height]])}}
   rows.append(["input":[w,h],"crop":[crop.x,crop.y,crop.width,crop.height],"turn":turn,"mirror":mirror,"extent":[output.extent.minX,output.extent.minY,output.extent.width,output.extent.height],"pixels":pixels,"sizes":sizes])
  }}
 }
}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["cases":rows],options:[.sortedKeys]))
'''
with tempfile.TemporaryDirectory(prefix='endfield-media-geometry-')as tmp:
 p=Path(tmp);(p/'main.swift').write_text(stub+model[:model.index('struct MediaAssemblyDocument {')]+'\nenum GeometryOracle {\n'+extracted+'\n}\n'+driver)
 subprocess.run(['/usr/bin/swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(p/'main.swift'),'-o',str(p/'oracle')],check=True)
 data=json.loads(subprocess.check_output([str(p/'oracle')]))
data['sourceCommit']=commit;data['sourceEngineSHA256']=hashlib.sha256(engine.encode()).hexdigest();data['scope']='Unchanged engine outputSize/apply on synthetic opaque RGBA grids; identity color adjustments, no LUT/stickers, no source files/windows. GPU/codec/color filter parity excluded.'
out=Path(sys.argv[1]);out.write_text(json.dumps(data,separators=(',',':'))+'\n');print('Source geometry',len(data['cases']),'cases;',out.stat().st_size,'bytes')
