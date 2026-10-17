import AppKit
import QuartzCore

enum HUDRuntimeAppearance {static var accent=NSColor(srgbRed:250/255,green:212/255,blue:31/255,alpha:1);static let reduceMotion=true}
enum L10n {static func text(_ english:String,_ chinese:String)->String {chinese}}
struct HUDModuleContentStyle {let dark:Bool;let contentsScale:CGFloat}
protocol HUDModuleContentFactory {func makeContent(for module:HUDModule,style:HUDModuleContentStyle)->CALayer}
final class FixtureTimer:WorkModeTimer {func invalidate(){}}
@main enum Main {
 static func main()throws {
  precondition(CommandLine.arguments.count==2)
  let output=URL(fileURLWithPath:CommandLine.arguments[1]),encoder=try ModuleReferenceLayerEncoder(output:output)
  var now=0.0
  let controller=WorkModeController(clock:{now},scheduleTimer:{_,_,_,_ in FixtureTimer()})
  let canvas=WorkModeCanvas(controller:controller,reduceMotion:{true})
  _ = canvas.makeContent(for:.workMode,style:.init(dark:true,contentsScale:2));canvas.activate()
  var cases:[[String:Any]]=[]
  func record(_ name:String)throws {cases.append(["name":name,"layer":try encoder.encode(canvas.layer,id:"workMode"),"status":canvas.accessibilityStatus,"actions":canvas.accessibleActions.map{["id":$0.id,"label":$0.label,"rect":ModuleReferenceLayerEncoder.rect($0.rect)]}])}
  try record("idle");canvas.perform(actionID:"work:preset:5");try record("preset5");_ = canvas.setCustomDuration("1:07");try record("custom67");_ = canvas.setCustomDuration("bad");try record("invalid");canvas.cancelCustomEditing();canvas.perform(actionID:"work:start");try record("running");now=12.25;controller.refresh();try record("running12");canvas.perform(actionID:"work:pause");try record("paused");canvas.perform(actionID:"work:reset");canvas.perform(actionID:"work:stopwatch");try record("stopwatch");canvas.perform(actionID:"work:start");now=41.75;controller.refresh();try record("stopwatchRunning");canvas.perform(actionID:"work:pause");_ = canvas.makeContent(for:.workMode,style:.init(dark:false,contentsScale:2));try record("lightPaused");canvas.setFocusStatusMessage("Unavailable on this platform");try record("focusUnavailable");canvas.setFocusStatusMessage("Permission required",needsAccessibilityPermission:true);try record("focusAccess");canvas.deactivate()
  // Canonical source ring raster samples. These are only oracle images, not
  // production substitutes; the path and strokeEnd are unchanged source data.
  var rings:[[String:Any]]=[]
  for width in [2.5,4.0] {for fraction in [0.0,0.001,0.01,0.045,0.1,0.25,0.333333,0.5,0.75,0.999,1.0] {
   let ring=CAShapeLayer();ring.bounds=WorkModeDialGeometry.canvas;ring.path=WorkModeDialGeometry.countdownPath();ring.strokeColor=NSColor.white.cgColor;ring.fillColor=nil;ring.lineCap = .butt;ring.lineWidth=width;ring.strokeEnd=fraction
   let pixels=880,rowBytes=pixels*4;var bytes=Data(count:rowBytes*pixels)
   let image:CGImage=bytes.withUnsafeMutableBytes {raw in
    let context=CGContext(data:raw.baseAddress,width:pixels,height:pixels,bitsPerComponent:8,bytesPerRow:rowBytes,space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!
    context.scaleBy(x:2,y:2);ring.render(in:context);return context.makeImage()!
   }
   let name="ring-\(width)-\(fraction).png",dest=CGImageDestinationCreateWithURL(output.appendingPathComponent(name) as CFURL,"public.png" as CFString,1,nil)!
   CGImageDestinationAddImage(dest,image,nil);precondition(CGImageDestinationFinalize(dest));rings.append(["width":width,"fraction":fraction,"png":name])
  }}
  let payload:[String:Any]=["cases":cases,"rings":rings,"unsupported":encoder.unsupported,"isolation":["detachedLayers":true,"realTimer":false,"windowCreated":false,"liveServices":false]]
  try JSONSerialization.data(withJSONObject:payload,options:[.sortedKeys]).write(to:output.appendingPathComponent("reference.json"));print("Original Work Mode: \(cases.count) detached artwork scenes, \(rings.count) ring samples")
 }
}
