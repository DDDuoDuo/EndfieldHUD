#!/usr/bin/env python3
"""Unchanged original MediaAssemblyEngine.apply on small, owned synthetic pixels.

Builds a detached oracle from the pinned macOS sources: Engine.apply, the asset
catalog (original LUTs and sticker PNGs from windows/resources/media-assembly)
and the same explicit sRGB-working CIContext as the original preview/export.
No user media, decoder, window, app or account is used.

Sections written to the fixture:
  cases   one-pixel associated RGBAf inputs through every operator, including
          dense temperature/tint, tone-curve and highlight/shadow grids (with
          the highlight/shadow filter's identity window and its edges). These
          are the evidence for the derived Windows formulas.
  frames  opaque synthetic RGBA8 images through crop/mirror/rotation, colour
          operators and sticker compositing, rendered as the original
          createCGImage(.RGBA8) bytes (associated alpha).
"""
from pathlib import Path
import base64, hashlib, json, subprocess, sys, tempfile

root = Path(__file__).resolve().parents[2]
commit = 'ca04f142185c7de40acd8523bdb563195d90a1d1'
names = ['MediaAssemblyEngine.swift', 'MediaAssemblyModel.swift', 'MediaAssemblyAssetCatalog.swift']
sources = {n: subprocess.check_output(['git', 'show', commit + ':Sources/' + n], cwd=root).decode() for n in names}
engine = sources[names[0]]
start = engine.index('    static func apply(')
end = engine.index('    func makePlaybackItem(', start)
glue = r'''
enum L10n {static func text(_ en:String,_ cn:String)->String{en}}
enum MediaAssemblyError:Error{case invalidAdjustment,unsupported,exportFailed,unavailable}
enum HUDResources {static func url(for path:String)->URL? {guard path.hasPrefix("MediaAssembly/") else{return nil};return URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(String(path.dropFirst("MediaAssembly/".count)))}}
'''
driver = r'''
let color=CGColorSpace(name:CGColorSpace.sRGB)!
let context=CIContext(options:[.cacheIntermediates:false,.workingColorSpace:color])
let inputs:[[Float]]=[[0,0,0,1],[1,1,1,1],[1,0,0,1],[0,1,0,1],[0,0,1,1],
 [0.001,0.018,0.04045,1],[0.1,0.5,0.9,1],[0.173,0.719,0.233,1],[0.25,0.75,0.125,1],[0.999,0.001,0.03225806,1],
 [0.025,0.15,0.4,0.5],[0.1,0.025,0.075,0.25],[0,0,0,0],[0.02,0.01,0.04,0.1],
 [-0.05,0.1,1.2,1],[0.011,0.013,0.017,1],[0.49,0.5,0.51,1],[0.74,0.75,0.76,1]]
var operations:[(String,MediaAssemblyAdjustments)]=[]
func add(_ name:String,_ edit:(inout MediaAssemblyAdjustments)->Void){var a=MediaAssemblyAdjustments();edit(&a);operations.append((name,a))}
add("identity"){_ in};for x in [-4.0,-0.37,0.5,2,4]{add("exposure\(x)"){$0.exposure=x}}
for x in [-0.3,0.27]{add("brightness\(x)"){$0.brightness=x}}
for x in [0.0,0.5,1.6,4]{add("contrast\(x)"){$0.contrast=x}}
for x in [0.0,0.5,1.7,2]{add("saturation\(x)"){$0.saturation=x}}
add("controls"){$0.brightness=0.13;$0.contrast=1.25;$0.saturation=0.35}
add("levels"){$0.levelsBlack=0.13;$0.levelsWhite=0.83}
for x in [0.1,0.7,1.7,4]{add("gamma\(x)"){$0.levelsGamma=x}}
add("combined"){$0.exposure=0.37;$0.brightness = -0.12;$0.contrast=1.5;$0.saturation=0.6;$0.levelsBlack=0.02;$0.levelsWhite=0.91;$0.levelsGamma=1.3}
for f in MediaAssemblyFilter.allCases where f != .none{add(f.rawValue){$0.filter=f}}
add("combined-lut"){$0.exposure = -0.43;$0.contrast=1.2;$0.saturation=0.7;$0.levelsGamma=1.7;$0.filter = .filter3}
// Former evidence-only records, now characterised by the dense grids below.
add("unproven-temperature"){$0.temperature=4700;$0.tint=37}
add("unproven-highlight-shadow"){$0.highlights=0.4;$0.shadows=0.6}
add("unproven-tonecurve"){$0.curve=[0,0.14,0.61,0.88,1]}
// CITemperatureAndTint over the whole source slider domain, including the
// Robertson isotherm row near 3077 K and both tint extremes.
for t in [2000.0,2500,3000,3077,3100,3500,4700,5600,6500,8000,10000,12000]{for n in [-200.0,-37,0,1,37,200] where !(t==6500&&n==0){add("temperature\(Int(t))/\(Int(n))"){$0.temperature=t;$0.tint=n}}}
for (i,c) in [[0,1,0,1,0],[0,0.9,0.1,0.9,1],[1,0.75,0.5,0.25,0],[0.2,0.5,0.5,0.6,0.9],[0.05,0.25,0.5,0.75,0.95],[0,0.3,0.3,0.31,1]].enumerated(){add("tonecurve\(i)"){$0.curve=c}}
for h in [0.0,0.25,0.4,0.7,0.93,1]{for s in [0.0,0.07,0.3,0.6,1] where !(h==1&&s==0){add("highlight\(h)/shadow\(s)"){$0.highlights=h;$0.shadows=s}}}
// The filter's own identity window (|shadows| < 0.05 and highlights > 0.95),
// its edges, and off-grid amounts for the logistic highlight gain and the
// (|shadows|/0.3)^1.6 no-op mix.
for (h,s) in [(0.95,0.04),(0.951,0.0),(0.96,0.03),(0.97,0.049),(0.99,0.05),(1,0.05),(0.5,0.0),(0.12,0.0),(0.85,0.15),(0.33,0.95),(0.61,0.21),(0.05,0.45)]{add("highlight\(h)/shadow\(s)"){$0.highlights=h;$0.shadows=s}}
add("everything"){$0.exposure=0.21;$0.brightness=0.04;$0.contrast=1.1;$0.saturation=1.2;$0.temperature=5600;$0.tint = -12;$0.highlights=0.8;$0.shadows=0.25;$0.curve=[0.02,0.27,0.52,0.76,0.97];$0.levelsBlack=0.03;$0.levelsWhite=0.96;$0.levelsGamma=1.15;$0.filter = .special2}
var cases:[[String:Any]]=[]
for (name,a) in operations {
 var outputs:[[Float]]=[];var rgba8:[[Int]]=[]
 for input in inputs {
  let data=input.withUnsafeBytes{Data($0)}
  let source=CIImage(bitmapData:data,bytesPerRow:16,size:CGSize(width:1,height:1),format:.RGBAf,colorSpace:color)
  let output=try Original.apply(a,to:source,includeStickers:false)
  var sample=[Float](repeating:0,count:4)
  sample.withUnsafeMutableBytes{context.render(output,toBitmap:$0.baseAddress!,rowBytes:16,bounds:output.extent,format:.RGBAf,colorSpace:color)}
  outputs.append(sample)
  let image=context.createCGImage(output,from:output.extent,format:.RGBA8,colorSpace:color)!
  let bytes=image.dataProvider!.data! as Data;rgba8.append((0..<4).map{Int(bytes[$0])})
 }
 let adjustment=try JSONSerialization.jsonObject(with:JSONEncoder().encode(a))
 cases.append(["name":name,"adjustments":adjustment,"float":outputs,"rgba8":rgba8])
}
// Opaque deterministic synthetic frame. Values are integer formulas so the
// Windows test regenerates exactly the same top-left RGBA8 rows.
func synthetic(_ w:Int,_ h:Int)->[UInt8]{var b=[UInt8](repeating:255,count:w*h*4)
 for y in 0..<h{for x in 0..<w{let i=(y*w+x)*4;b[i]=UInt8((x*255)/(w-1));b[i+1]=UInt8((y*255)/(h-1));b[i+2]=UInt8(((x*7+y*13)*5)%256)}};return b}
func frame(_ w:Int,_ h:Int,_ a:MediaAssemblyAdjustments,stickers:Bool)throws->[String:Any]{
 let bytes=synthetic(w,h)
 let provider=CGDataProvider(data:Data(bytes) as CFData)!
 let cg=CGImage(width:w,height:h,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:w*4,space:color,bitmapInfo:CGBitmapInfo(rawValue:CGImageAlphaInfo.premultipliedLast.rawValue),provider:provider,decode:nil,shouldInterpolate:false,intent:.defaultIntent)!
 let output=try Original.apply(a,to:CIImage(cgImage:cg),includeStickers:stickers)
 let image=context.createCGImage(output,from:output.extent,format:.RGBA8,colorSpace:color)!
 let data=image.dataProvider!.data! as Data;var packed=Data()
 for y in 0..<image.height{packed.append(data.subdata(in:y*image.bytesPerRow..<(y*image.bytesPerRow+image.width*4)))}
 return ["width":image.width,"height":image.height,"rgba8":packed.base64EncodedString()]
}
var frames:[[String:Any]]=[]
func addFrame(_ name:String,_ w:Int,_ h:Int,stickers:Bool=false,_ edit:(inout MediaAssemblyAdjustments)->Void)throws{
 var a=MediaAssemblyAdjustments();edit(&a);var f=try frame(w,h,a,stickers:stickers)
 f["name"]=name;f["sourceWidth"]=w;f["sourceHeight"]=h;f["includeStickers"]=stickers
 f["adjustments"]=try JSONSerialization.jsonObject(with:JSONEncoder().encode(a));frames.append(f)
}
try addFrame("plain",48,32){_ in}
try addFrame("crop",48,32){$0.crop=MediaAssemblyCrop(x:0.1,y:0.2,width:0.7,height:0.5)}
try addFrame("crop-rotate1-mirror",48,32){$0.crop=MediaAssemblyCrop(x:0.1,y:0.2,width:0.7,height:0.5);$0.rotationQuarterTurns=1;$0.mirrored=true}
try addFrame("rotate2",48,32){$0.rotationQuarterTurns=2}
try addFrame("rotate3-crop",45,31){$0.crop=MediaAssemblyCrop(x:0.25,y:0.15,width:0.5,height:0.6);$0.rotationQuarterTurns=3}
try addFrame("rotate-negative1",40,30){$0.rotationQuarterTurns = -1;$0.mirrored=true}
try addFrame("crop-sum-one",50,34){$0.crop=MediaAssemblyCrop(x:0,y:0.7,width:1,height:0.3)}
try addFrame("highlight-shadow",48,32){$0.highlights=0.3;$0.shadows=0.7}
try addFrame("colour-crop",48,32){$0.crop=MediaAssemblyCrop(x:0.05,y:0.1,width:0.9,height:0.8);$0.exposure=0.4;$0.saturation=1.4;$0.temperature=4200;$0.tint=20;$0.curve=[0,0.2,0.55,0.8,1];$0.filter = .filter2}
let s1=MediaAssemblySticker(kind:.sticker7,x:0.4,y:0.6,size:0.5,rotation:30)
let s2=MediaAssemblySticker(kind:.sticker3,x:0.8,y:0.2,size:0.3,rotation: -75)
let s3=MediaAssemblySticker(kind:.sticker21,x:0.05,y:0.95,size:0.9,rotation:0)
try addFrame("stickers",96,64,stickers:true){$0.stickers=[s1,s2]}
try addFrame("stickers-cropped-rotated",96,64,stickers:true){$0.stickers=[s1,s3];$0.crop=MediaAssemblyCrop(x:0.1,y:0,width:0.8,height:0.9);$0.rotationQuarterTurns=1}
try addFrame("sticker-large-scale",160,120,stickers:true){$0.stickers=[MediaAssemblySticker(kind:.sticker1,x:0.5,y:0.5,size:1,rotation:12)]}
try addFrame("stickers-excluded",96,64,stickers:false){$0.stickers=[s1,s2]}
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:["inputs":inputs,"cases":cases,"frames":frames],options:[.sortedKeys]))
'''
model = sources[names[1]].split('struct MediaAssemblyDocument {')[0]
with tempfile.TemporaryDirectory(prefix='endfield-media-pixels-') as tmp:
    p = Path(tmp)
    (p / 'main.swift').write_text(model + sources[names[2]] + glue + '\nenum Original {\n' + engine[start:end] + '\n}\n' + driver)
    # The Swift module cache stays inside the temporary directory so the
    # generator never writes into shared build trees.
    subprocess.run(['/usr/bin/swiftc', '-O', '-sdk', '/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk', '-module-cache-path',
                    str(p / 'module-cache'), str(p / 'main.swift'), '-o', str(p / 'oracle')], check=True)
    result = json.loads(subprocess.check_output([str(p / 'oracle'), str(root / 'windows/resources/media-assembly')]))
result.update({'sourceCommit': commit, 'sourcePins': {n: hashlib.sha256(v.encode()).hexdigest() for n, v in sources.items()},
               'usesAppOrWindow': False,
               'synthetic': 'r=x*255/(w-1), g=y*255/(h-1), b=((7x+13y)*5)%256, a=255 (integer division), top-left rows',
               'scope': 'Actual unchanged Engine.apply and AssetCatalog on synthetic associated RGBAf sRGB pixels and opaque synthetic RGBA8 frames; same explicit source preview context, original LUTs and sticker PNGs. No user data or decoder.'})
output = Path(sys.argv[1])
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(result, separators=(',', ':')) + '\n')
print(output, len(result['cases']), len(result['frames']), output.stat().st_size)
