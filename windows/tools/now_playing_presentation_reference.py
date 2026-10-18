#!/usr/bin/env python3
"""Execute original Canvas against inert injected controllers; no app/window/IO provider.

Shared feedback decoration is omitted by an inert shim; the original Canvas,
button glyphs, layer layout, animation declarations and input methods execute
unchanged. This is a model-layer oracle, not a raster/font pixel-parity claim.
"""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2];out=root/'build/now-playing-presentation-reference';out.mkdir(exist_ok=True)
authority='ca04f142185c7de40acd8523bdb563195d90a1d1'
paths=['Sources/NowPlayingCanvas.swift','Sources/NowPlayingController.swift','Sources/NowPlayingLyrics.swift']
texts={p:(root/p).read_text() for p in paths}
for p in paths:assert (root/p).read_bytes()==subprocess.check_output(['git','show',authority+':'+p],cwd=root)
controller=texts[paths[1]];lyrics=texts[paths[2]]
stubs=r'''import AppKit
import QuartzCore
protocol HUDModuleContentFactory {}
enum HUDModule {case nowPlaying}
struct HUDModuleContentStyle {var dark:Bool;var accent:NSColor;var contentsScale:CGFloat}
enum L10n {static func text(_ en:String,_ zh:String)->String {en}}
enum HUDRuntimeAppearance {static var reduceMotion=false;static var accent=NSColor(srgbRed:0.98,green:0.87,blue:0.13,alpha:1)}
enum HUDRenderScale {static func contentScale(for layer:CALayer,baseScale:CGFloat)->CGFloat{baseScale}}
struct VolumeCanvasAction {var id:String;var label:String;var rect:CGRect;var enabled:Bool}
struct VolumeCanvasSlider {var id:String;var label:String;var rect:CGRect;var value:Double?;var minimum:Double;var maximum:Double;var enabled:Bool;var help:String?=nil}
class HUDControlHighlightLayer:CALayer {
 enum Shape {case rounded,cutCorner,ellipse}
 static func add(to parent:CALayer,rect:CGRect,shape:Shape,enabled:Bool,framed:Bool)->HUDControlHighlightLayer {let layer=HUDControlHighlightLayer();layer.frame=rect;parent.addSublayer(layer);return layer}
 func setEnabled(_ value:Bool){}
}
struct AudioDeviceInfo {var id:UInt32=1}
struct AudioApplicationInfo {var id:UInt32;var pid:Int32;var name:String;var isRunningOutput:Bool;var applicationURL:URL?;var icon:NSImage?}
struct AudioDeviceSnapshot {var outputs:[AudioDeviceInfo]=[];var defaultOutputID:UInt32?;var availableApplications:[AudioApplicationInfo]=[];var activeApplications:[AudioApplicationInfo]=[]}
final class AudioDeviceController {var snapshot=AudioDeviceSnapshot();func observe(_ f:@escaping()->Void)->UUID{UUID()};func removeObserver(_ id:UUID){};func start(){};func stop(){}}
struct PerAppAudioSession {enum State {case active,preparing,failed};var processID:UInt32;var pid:Int32;var name:String;var applicationURL:URL?;var icon:NSImage?;var gain:Double;var state:State;var error:String?}
final class PerAppAudioController {
 var sessions:[PerAppAudioSession]=[]
 func observe(_ f:@escaping()->Void)->UUID{UUID()};func removeObserver(_ id:UUID){}
 func availability(application:AudioApplicationInfo,output:AudioDeviceInfo)->String?{"Unavailable fixture"}
 func stop(processID:UInt32){};func setGain(_ gain:Double,processID:UInt32)->Bool{false}
 func start(application:AudioApplicationInfo,output:AudioDeviceInfo,initialGain:Double)->Bool{false}
}
final class NowPlayingController {
 var snapshot=NowPlayingSnapshot(),isRequestingPermission=false,artworkImage:CGImage?,playerVolume:Double?,lyrics:NowPlayingLyrics?,onEvent:((NowPlayingEvent)->Void)?
 var listeners:[UUID:()->Void]=[:];var commands:[NowPlayingCommand]=[]
 func observe(_ f:@escaping()->Void)->UUID{let id=UUID();listeners[id]=f;return id};func removeObserver(_ id:UUID){listeners.removeValue(forKey:id)}
 func activate(){};func deactivate(){};func refresh(){listeners.values.forEach{$0()}};func connect(){};func select(_ source:NowPlayingSource){}
 func perform(_ command:NowPlayingCommand){commands.append(command)};func setPlayerVolume(_ value:Double)->Bool{playerVolume=value;return true}
}
'''
source=stubs+controller[controller.index('enum NowPlayingSource:'):controller.index('protocol NowPlayingBackend:')]+lyrics[lyrics.index('struct NowPlayingLyricLine:'):lyrics.index('\n/// At most two requests')]
(out/'stubs.swift').write_text(source)
main=r'''import AppKit
import QuartzCore
func rect(_ r:CGRect)->[Double]{[r.minX,r.minY,r.width,r.height].map(Double.init)}
func rgba(_ value:CGColor?)->Any {guard let value,let color=NSColor(cgColor:value)?.usingColorSpace(.sRGB) else{return NSNull()};return [color.redComponent,color.greenComponent,color.blueComponent,color.alphaComponent]}
func path(_ value:CGPath?)->Any {guard let value else{return NSNull()};var rows:[[String:Any]]=[];value.applyWithBlock{element in let e=element.pointee;let count:Int;let op:String;switch e.type{case .moveToPoint:count=1;op="move";case .addLineToPoint:count=1;op="line";case .addQuadCurveToPoint:count=2;op="quad";case .addCurveToPoint:count=3;op="cubic";case .closeSubpath:count=0;op="close";@unknown default:fatalError()};rows.append(["op":op,"points":(0..<count).map{[e.points[$0].x,e.points[$0].y]}])};return rows}
func timing(_ value:CAMediaTimingFunction?)->Any {guard let value else{return NSNull()};var a:[Float]=[0,0],b:[Float]=[0,0];value.getControlPoint(at:1,values:&a);value.getControlPoint(at:2,values:&b);return a+b}
func animation(_ a:CAAnimation)->[String:Any]{var d:[String:Any]=["duration":a.duration,"timing":timing(a.timingFunction)];if let a=a as? CABasicAnimation{d["keyPath"]=a.keyPath;d["from"]=a.fromValue;d["to"]=a.toValue};if let a=a as? CAAnimationGroup{d["children"]=a.animations?.map(animation)};if let a=a as? CATransition{d["type"]=a.type.rawValue};return d}
func nodes(_ root:CALayer)->[[String:Any]] {var result:[[String:Any]]=[];func visit(_ layer:CALayer,_ id:String){if layer is HUDControlHighlightLayer{return};let id=layer.name ?? id;var row:[String:Any]=["id":id,"frame":rect(layer.frame),"bounds":rect(layer.bounds),"opacity":layer.opacity,"hidden":layer.isHidden,"cornerRadius":layer.cornerRadius,"background":rgba(layer.backgroundColor),"border":rgba(layer.borderColor),"borderWidth":layer.borderWidth,"masks":layer.masksToBounds,"groupOpacity":layer.allowsGroupOpacity];if let text=layer as? CATextLayer{row["text"]=text.string as? String ?? "";row["fontSize"]=text.fontSize;row["foreground"]=rgba(text.foregroundColor);row["alignment"]=text.alignmentMode.rawValue;row["truncation"]=text.truncationMode.rawValue};if let shape=layer as? CAShapeLayer{row["path"]=path(shape.path);row["fill"]=rgba(shape.fillColor);row["stroke"]=rgba(shape.strokeColor);row["lineWidth"]=shape.lineWidth};if let gradient=layer as? CAGradientLayer{row["colors"]=(gradient.colors as? [CGColor] ?? []).map(rgba);row["locations"]=gradient.locations ?? [];row["start"]=[gradient.startPoint.x,gradient.startPoint.y];row["end"]=[gradient.endPoint.x,gradient.endPoint.y]};row["animations"]=Dictionary(uniqueKeysWithValues:(layer.animationKeys() ?? []).compactMap{key in layer.animation(forKey:key).map{(key,animation($0))}});result.append(row);for(index,child)in(layer.sublayers ?? []).enumerated(){visit(child,id+"/"+String(index))}};visit(root,"root");return result}
var time=100.0,reduced=false
let controller=NowPlayingController(),audio=AudioDeviceController(),routes=PerAppAudioController()
let canvas=NowPlayingCanvas(controller:controller,audio:audio,perAppAudio:routes,now:{time},reduceMotion:{reduced},scheduleDisplayUpdate:{_,_ in {}})
let style=HUDModuleContentStyle(dark:true,accent:HUDRuntimeAppearance.accent,contentsScale:2)
let root=canvas.makeContent(for:.nowPlaying,style:style)
func track(_ title:String="中文 노래",position:Double=12.25,playing:Bool=true)->NowPlayingTrack{NowPlayingTrack(title:title,artist:"Artist",album:"Album",duration:240,position:position,isPlaying:playing,sampledAt:time)}
func sample(_ name:String)->[String:Any]{["name":name,"nodes":nodes(root),"actions":canvas.accessibleActions.map{["id":$0.id,"rect":rect($0.rect),"label":$0.label,"enabled":$0.enabled]},"sliders":canvas.accessibleSliders.map{["id":$0.id,"rect":rect($0.rect),"value":$0.value as Any? ?? NSNull(),"minimum":$0.minimum,"maximum":$0.maximum,"enabled":$0.enabled]}]}
var cases=[sample("empty-dark")]
canvas.activate();controller.snapshot.application=NowPlayingApplication(source:.system,pid:7,bundleURL:URL(fileURLWithPath:"/synthetic-only"));controller.snapshot.track=track();controller.lyrics=NowPlayingLyrics(lrc:"[00:00]First\n[00:20]Second\n[00:50]Third");controller.refresh();cases.append(sample("playing-dark"))
canvas.tickForVerification();cases.append(sample("progress-interpolation"))
_ = canvas.mouseDown(at:CGPoint(x:220,y:388));cases.append(sample("seek-preview"));canvas.mouseUp()
time=121;canvas.tickForVerification();cases.append(sample("lyric-forward"))
controller.snapshot.track=track(position:1,playing:false);controller.refresh();cases.append(sample("lyric-backward"))
canvas.perform(actionID:"volume");cases.append(sample("unavailable-volume"))
controller.playerVolume=0.42;controller.refresh();cases.append(sample("available-volume"))
canvas.perform(actionID:"lyrics");cases.append(sample("lyrics-hidden"))
controller.snapshot.track=track("Changed song",position:1,playing:false);controller.refresh();cases.append(sample("metadata-change"))
controller.snapshot.failure = .timedOut;controller.refresh();cases.append(sample("failed-transport"))
_ = canvas.makeContent(for:.nowPlaying,style:HUDModuleContentStyle(dark:false,accent:HUDRuntimeAppearance.accent,contentsScale:2));cases.append(sample("light"))
reduced=true;_ = canvas.makeContent(for:.nowPlaying,style:style);cases.append(sample("reduced-motion"))
canvas.deactivate();cases.append(sample("deactivated"))
try JSONSerialization.data(withJSONObject:["cases":cases],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
print("Original NowPlayingCanvas: \(cases.count) detached model-layer cases")
'''
(out/'main.swift').write_text(main)
subprocess.run(['/usr/bin/swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),'-framework','AppKit','-framework','QuartzCore',str(out/'stubs.swift'),str(root/paths[0]),str(out/'main.swift'),'-o',str(out/'oracle')],check=True)
subprocess.run([str(out/'oracle'),str(out/'raw.json')],check=True)
value=json.loads((out/'raw.json').read_text());value.update({'sourceCommit':authority,'sourcePins':{p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in paths},'scope':'Original Canvas and button paths execute unchanged; controllers, scheduler, localization English selection, content scale and shared feedback decoration are inert injected shims. Detached model-layer geometry/animation/input oracle, not raster or font pixel parity.'})
# Reuse the independently captured nil-timing CABasicAnimation proof. The same
# .16s source opacity declaration is measured on a detached CARenderer; no app,
# window, controller or second source compilation is needed for these samples.
timing_path='windows/tests/fixtures/orbipom-presentation-source.json'
timing_bytes=(root/timing_path).read_bytes()
assert hashlib.sha256(timing_bytes).hexdigest()=='403de9835d812b3d71dd809df4779845f7b33fc711eaf922a3d803c5c046e2f2'
timing_source=json.loads(timing_bytes)
value['opacityTiming']=timing_source['opacityTiming']
value['opacityTimingProvenance']={'fixture':timing_path,'sha256':hashlib.sha256(timing_bytes).hexdigest(),'tool':'windows/tools/orbipom_timing_reference.swift','toolSHA256':timing_source['timingToolSHA256']}
destination=Path(sys.argv[1]) if len(sys.argv)>1 else out/'source.json'
if destination.exists():raise RuntimeError('Choose a new immutable output path')
destination.write_text(json.dumps(value,separators=(',',':'),ensure_ascii=False)+'\n');print(destination,len(destination.read_bytes()),hashlib.sha256(destination.read_bytes()).hexdigest())
