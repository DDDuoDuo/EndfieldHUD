#!/usr/bin/env python3
"""Exercise unchanged macOS canvas input/layout with an inert controller.

No window, decoder, player, disk media or real user state is opened. Controller
assignments are synchronous dependency shims, not replacements of canvas math.
"""
from pathlib import Path
import hashlib, json, subprocess, sys

root=Path(__file__).resolve().parents[2]
commit='ca04f142185c7de40acd8523bdb563195d90a1d1'
out=root/'build/media-assembly-presentation-reference';out.mkdir(exist_ok=True)
names=['MediaAssemblyModel.swift','MediaAssemblyViewport.swift','MediaAssemblyCanvas.swift','MediaAssemblyControls.swift']
source={n:subprocess.check_output(['git','show',commit+':Sources/'+n],cwd=root).decode() for n in names}
stubs=r'''import AppKit
import AVFoundation
protocol HUDModuleContentFactory {}
enum HUDModule {case mediaAssembly}
struct HUDModuleContentStyle {var dark=true;var contentsScale:CGFloat=2}
enum L10n {static var resolvedLanguage="en";static func text(_ en:String,_ cn:String)->String{en}}
enum HUDRuntimeAppearance {static var reduceMotion=false;static var accent=NSColor.yellow}
enum HUDSectionHeading {static func text(_ s:String)->String{"// "+s}}
enum MediaAssemblyError:Error{case invalidAdjustment}
enum MediaAssemblyAssetCatalog {
 static func filterThumbnail(_ x:MediaAssemblyFilter)->CGImage?{nil}
 static func stickerThumbnail(_ x:MediaAssemblyStickerKind)->CGImage?{nil}
 static func stickerImage(_ x:MediaAssemblyStickerKind)->CGImage?{nil}
}
enum HUDControlHighlightLayer {enum Shape{case cutCorner};static func add(to:CALayer,rect:CGRect,shape:Shape,enabled:Bool,framed:Bool){}}
struct MediaAssemblyDocument {var id=UUID();var pixelSize=CGSize(width:1920,height:1080);var isVideo=false;var duration:Double=120}
final class MediaAssemblyController {
 var document:MediaAssemblyDocument?,adjustments=MediaAssemblyAdjustments(),preview:CGImage?,player:AVPlayer?
 var isBusy=false,isExporting=false,isPlaying=false,isCropPreview=false,currentTime:Double=0,progress:Double=0,error:Error?
 var onChange:(()->Void)?
 func setActive(_ value:Bool){}
 func setCropPreview(_ value:Bool){if isCropPreview != value{isCropPreview=value;onChange?()}}
 func updateAdjustments(_ value:MediaAssemblyAdjustments){guard value.isValid else{return};adjustments=value;onChange?()}
 func updateTrim(start:Double,end:Double,scrubTo:Double){adjustments.trimStart=start;adjustments.trimEnd=end;seek(to:scrubTo)}
 func seek(to:Double){currentTime=min(max(adjustments.trimStart,to),max(adjustments.trimStart,(adjustments.trimEnd ?? document?.duration ?? 0)-0.001));onChange?()}
 func togglePlayback(){isPlaying.toggle();onChange?()}
 func reset(){adjustments=MediaAssemblyAdjustments();onChange?()}
 func cancelExport(){isExporting=false;onChange?()}
 func closeDocument()->Bool{document=nil;adjustments=MediaAssemblyAdjustments();onChange?();return true}
}
'''
model=source[names[0]].split('struct MediaAssemblyDocument {')[0]
parameter=source[names[3]].split('/// Source/export')[0]
(out/'stubs.swift').write_text(stubs+model+parameter)
(out/'viewport.swift').write_text(source[names[1]])
(out/'canvas.swift').write_text(source[names[2]])
driver=r'''import AppKit
func rect(_ x:CGRect)->[Double]{[x.minX,x.minY,x.width,x.height]}
func point(_ x:CGPoint)->[Double]{[x.x,x.y]}
let c=MediaAssemblyController(),p=MediaAssemblyCanvas(controller:c)
_ = p.makeContent(for:.mediaAssembly,style:HUDModuleContentStyle());p.setPresented(true)
func adjustments()->[String:Any]{let a=c.adjustments;return ["crop":[a.crop.x,a.crop.y,a.crop.width,a.crop.height],"turns":a.rotationQuarterTurns,"mirror":a.mirrored,"brightness":a.brightness,"contrast":a.contrast,"saturation":a.saturation,"temperature":a.temperature,"tint":a.tint,"highlights":a.highlights,"shadows":a.shadows,"exposure":a.exposure,"curve":a.curve,"black":a.levelsBlack,"white":a.levelsWhite,"gamma":a.levelsGamma,"start":a.trimStart,"end":a.trimEnd.map{$0 as Any} ?? NSNull(),"filter":a.filter.rawValue,"stickers":a.stickers.map{["kind":$0.kind.rawValue,"x":$0.x,"y":$0.y,"size":$0.size,"rotation":$0.rotation]}]}
func snapshot()->[String:Any]{return ["image":rect(p.imageRect),"zoom":p.viewport.zoom,"pan":point(p.viewport.pan),"drawer":p.drawer?.rawValue as Any? ?? NSNull(),"tool":p.activeTool?.rawValue as Any? ?? NSNull(),"offset":p.drawerOffset,"crop":rect(p.displayCropRect),"cropHandles":p.cropHandlePoints.map(point),"selected":p.selectedStickerID != nil,"dragging":p.isDragging,"time":c.currentTime,"adjustments":adjustments(),"parameters":p.inlineParameters.map{["id":$0.id,"title":$0.title,"low":$0.range.lowerBound,"high":$0.range.upperBound,"value":$0.value,"step":$0.step]},"actions":p.actions.map{["id":$0.id,"title":$0.title,"rect":rect($0.rect),"enabled":$0.enabled]}]}
let operations:[[String:Any]]=[
 ["op":"sample"],["op":"photo"],["op":"action","id":"tools"],["op":"action","id":"adjust"],
 ["op":"parameter","id":"brightness","value":-0.375],["op":"parameter","id":"temperature","value":5432.0],
 ["op":"scroll","x":80.0,"y":210.0,"dx":0.0,"dy":92.0],["op":"down","x":130.0,"y":224.0],["op":"drag","x":179.0,"y":224.0],["op":"up"],
 ["op":"action","id":"curves"],["op":"parameter","id":"curve3","value":0.677],["op":"action","id":"levels"],["op":"parameter","id":"black","value":0.77],["op":"parameter","id":"white","value":0.33],
 ["op":"action","id":"crop"],["op":"down","x":421.0,"y":315.0],["op":"drag","x":320.0,"y":278.0],["op":"up"],
 ["op":"action","id":"mirror"],["op":"action","id":"rotate"],["op":"action","id":"rotate"],["op":"action","id":"cropReset"],
 ["op":"action","id":"toolBack"],["op":"action","id":"tools"],["op":"zoom","value":3.2,"x":280.0,"y":260.0],
 ["op":"scroll","x":250.0,"y":250.0,"dx":70.0,"dy":140.0],["op":"down","x":300.0,"y":240.0],["op":"drag","x":260.0,"y":185.0],["op":"up"],
 ["op":"action","id":"zoomReset"],["op":"action","id":"stickers"],["op":"scroll","x":80.0,"y":210.0,"dx":0.0,"dy":77.0],["op":"action","id":"sticker:sticker_1"],
 ["op":"down","x":220.0,"y":198.0],["op":"drag","x":279.0,"y":234.0],["op":"up"],
 ["op":"stickerKey","dx":0.05,"dy":-0.01,"size":0.05,"rotation":5.0],["op":"action","id":"deleteSticker"],
 ["op":"action","id":"filters"],["op":"scroll","x":80.0,"y":210.0,"dx":0.0,"dy":1000.0],["op":"action","id":"filter:filter_3"],["op":"action","id":"reset"],
 ["op":"video"],["op":"action","id":"trim"],["op":"trim","beginning":true,"value":12.321],["op":"trim","beginning":false,"value":98.765],
 ["op":"down","x":55.0,"y":178.0],["op":"drag","x":64.0,"y":178.0],["op":"up"],
 ["op":"down","x":158.0,"y":178.0],["op":"drag","x":139.0,"y":178.0],["op":"up"],
 ["op":"action","id":"trimReset"],["op":"action","id":"toolBack"],["op":"action","id":"tools"],["op":"down","x":210.0,"y":344.0],["op":"drag","x":289.0,"y":344.0],["op":"up"],
 ["op":"action","id":"play"],["op":"action","id":"crop"],["op":"hide"],["op":"show"],["op":"action","id":"closeMedia"]
]
var rows:[[String:Any]]=[]
for op in operations {
 let kind=op["op"] as! String
 func n(_ k:String)->Double{op[k] as? Double ?? 0};func id()->String{op["id"] as! String}
 switch kind {
 case "photo","video":c.document=MediaAssemblyDocument(isVideo:kind=="video");c.adjustments=MediaAssemblyAdjustments();c.currentTime=0;c.onChange?()
 case "action":p.perform(id())
 case "parameter":p.setParameter(id(),to:n("value"))
 case "trim":p.setTrimEndpoint(beginning:op["beginning"] as! Bool,to:n("value"))
 case "scroll":_ = p.scroll(at:CGPoint(x:n("x"),y:n("y")),deltaX:n("dx"),deltaY:n("dy"))
 case "zoom":_ = p.zoom(by:n("value"),at:CGPoint(x:n("x"),y:n("y")))
 case "down":_ = p.mouseDown(at:CGPoint(x:n("x"),y:n("y")))
 case "drag":p.mouseDragged(to:CGPoint(x:n("x"),y:n("y")))
 case "up":p.mouseUp()
 case "stickerKey":_ = p.adjustSelectedSticker(dx:n("dx"),dy:n("dy"),size:n("size"),rotation:n("rotation"))
 case "hide":p.setPresented(false)
 case "show":p.setPresented(true)
 default:break
 };rows.append(["operation":op,"state":snapshot()])
}
let data=try JSONSerialization.data(withJSONObject:["cases":rows],options:[.sortedKeys]);try data.write(to:URL(fileURLWithPath:CommandLine.arguments[1]));print("Original MediaAssemblyCanvas: \(rows.count) operations")
'''
(out/'main.swift').write_text(driver)
subprocess.run(['/usr/bin/swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'stubs.swift'),str(out/'viewport.swift'),str(out/'canvas.swift'),str(out/'main.swift'),'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle'),str(out/'raw.json')],check=True)
value=json.loads((out/'raw.json').read_text());value['sourceCommit']=commit;value['sourcePins']={n:hashlib.sha256(v.encode()).hexdigest() for n,v in source.items()}
value['scope']='Original canvas input and model layout, inert controller/asset/feedback adapters; no native raster or Windows owner claim.'
dest=Path(sys.argv[1]);dest.write_text(json.dumps(value,ensure_ascii=False,separators=(',',':'))+'\n');print(dest,len(dest.read_bytes()),hashlib.sha256(dest.read_bytes()).hexdigest())
