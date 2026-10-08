import AppKit
import QuartzCore
// Actual animations are obtained from the unmodified Canvas. Only their
// begin/fill values and the detached probe clock are changed for sampling.
enum HUDRuntimeAppearance {static var accent=NSColor.systemYellow;static let reduceMotion=false}
enum L10n {static func text(_ english:String,_ chinese:String)->String {chinese}}
struct HUDModuleContentStyle {let dark:Bool;let contentsScale:CGFloat}
protocol HUDModuleContentFactory {func makeContent(for module:HUDModule,style:HUDModuleContentStyle)->CALayer}
final class FixtureTimer:WorkModeTimer {func invalidate(){}}
struct Probe {let phase:Double,name:String,layer:CALayer,animation:CABasicAnimation}
@main enum Main {
 static func main()throws {
  precondition(CommandLine.arguments.count==2);let output=URL(fileURLWithPath:CommandLine.arguments[1])
  let canvas=WorkModeCanvas(controller:WorkModeController(clock:{0},scheduleTimer:{_,_,_,_ in FixtureTimer()}),reduceMotion:{false})
  _ = canvas.makeContent(for:.workMode,style:.init(dark:true,contentsScale:2));canvas.activate();canvas.perform(actionID:"work:start")
  func named(_ name:String,_ node:CALayer)->CALayer? {if node.name==name{return node};for child in node.sublayers ?? [] {if let found=named(name,child){return found}};return nil}
  let specifications=[("layoutPosition","workMode.clockViewport","workLayout.position"),("layoutTransform","workMode.clockViewport","workLayout.transform"),("layoutOpacity","workMode.configuration","workLayout.opacity"),("button","workMode.button.work:pause","workFeedback.expand"),("clock","workMode.clock","workFeedback.clockIndex")]
  var original:[(String,CABasicAnimation)]=[]
  for (name,layer,key) in specifications {guard let animation=named(layer,canvas.layer)?.animation(forKey:key) as? CABasicAnimation else {throw NSError(domain:"work-oracle",code:1,userInfo:[NSLocalizedDescriptionKey:key])};original.append((name,animation))}
  canvas.setFocusStatusMessage("Synthetic permission",needsAccessibilityPermission:true);canvas.perform(actionID:"work:focusAccess")
  guard let focus=canvas.layer.sublayers?.compactMap({$0.animation(forKey:"workFeedback.focusAccess") as? CABasicAnimation}).first else {throw NSError(domain:"work-oracle",code:2)};original.append(("focus",focus));canvas.deactivate()
  let app=NSApplication.shared;app.setActivationPolicy(.prohibited);let minimumX=NSScreen.screens.map{$0.frame.minX}.min() ?? 0
  let panel=NSPanel(contentRect:NSRect(x:minimumX-4096,y:0,width:16,height:16),styleMask:[.borderless,.nonactivatingPanel],backing:.buffered,defer:false);panel.isReleasedWhenClosed=false;panel.ignoresMouseEvents=true;panel.hidesOnDeactivate=false;panel.backgroundColor = .clear;panel.isOpaque=false;panel.animationBehavior = .none
  let view=NSView(frame:NSRect(x:0,y:0,width:16,height:16));view.wantsLayer=true;panel.contentView=view;defer{panel.orderOut(nil);panel.close()};let root=view.layer!
  let phases=(0...100).map{Double($0)/100}+[0.0037,0.1234567,0.54321,0.87654321];var probes:[Probe]=[]
  CATransaction.begin();CATransaction.setDisableActions(true)
  for (name,animation) in original {for phase in phases {let layer=CALayer();layer.bounds=CGRect(x:0,y:0,width:1,height:1);layer.speed=0;layer.timeOffset=1+phase*animation.duration+(phase==1 ? 0.0001 : 0);let copy=animation.copy() as! CABasicAnimation;copy.beginTime=1;copy.fillMode = .both;copy.isRemovedOnCompletion=false;layer.setValue(copy.toValue,forKeyPath:copy.keyPath!);root.addSublayer(layer);if phase<1 {layer.add(copy,forKey:"fixture")};probes.append(.init(phase:phase,name:name,layer:layer,animation:copy))}}
  CATransaction.commit();panel.orderFrontRegardless();CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:0.08));precondition(!panel.isKeyWindow && !panel.isMainWindow && NSScreen.screens.allSatisfy{!panel.frame.intersects($0.frame)})
  func sample(_ probe:Probe)throws->[Double]{guard let layer=probe.layer.presentation()else{throw NSError(domain:"work-oracle",code:3)};switch probe.animation.keyPath! {case "position":return [layer.position.x,layer.position.y];case "opacity":return [Double(layer.opacity)];case "transform":let m=layer.transform;return [m.m11,m.m22,m.m41,m.m42,m.m43];default:throw NSError(domain:"work-oracle",code:4)}}
  let first=try probes.map(sample);CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:0.08));var maximumRepeatDifference=0.0,rows:[[String:Any]]=[]
  for (n,probe) in probes.enumerated(){let value=try sample(probe);for(a,b)in zip(first[n],value){maximumRepeatDifference=max(maximumRepeatDifference,abs(a-b))};rows.append(["name":probe.name,"phase":probe.phase,"duration":probe.animation.duration,"value":value])}
  let payload:[String:Any]=["rows":rows,"maximumRepeatDifference":maximumRepeatDifference,"isolation":["ownOffscreenNonactivatingPanel":true,"screenCaptured":false,"liveServices":false,"realTimers":false],"animationEdits":["beginTime=1","fillMode=both","removedOnCompletion=false","probeLayer speed=0/timeOffset","phase=1 omits completed animation, matching original isRemovedOnCompletion=true model endpoint"]]
  try JSONSerialization.data(withJSONObject:payload,options:[.sortedKeys]).write(to:output.appendingPathComponent("reference.json"));print("Original Work Mode: \(rows.count) genuine CA samples, repeat \(maximumRepeatDifference)")
 }
}
