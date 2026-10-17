import AppKit
import QuartzCore
// Actual source view in an owned offscreen, nonkey fixture; no HUD/services/data.
enum L10n {static func text(_ en:String,_ zh:String)->String {en}}
enum HUDRuntimeAppearance {static let reduceMotion=false}
enum HUDMotionMath {static func transform(normalizedPoint:CGPoint,depth:CGFloat,travel:CGFloat,reducedMotion:Bool,parallaxIntensity:CGFloat,perspectiveIntensity:CGFloat,projectionBounds:CGRect)->CATransform3D {CATransform3DIdentity}}
@main enum Reference {
 static func main()throws {
  precondition(CommandLine.arguments.count==2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
  NSApplication.shared.setActivationPolicy(.prohibited)
  let left=(NSScreen.screens.map{$0.frame.minX}.min() ?? 0)-4096
  let panel=NSPanel(contentRect:NSRect(x:left,y:0,width:640,height:480),styleMask:[.borderless,.nonactivatingPanel],backing:.buffered,defer:false)
  panel.isReleasedWhenClosed=false;panel.ignoresMouseEvents=true;panel.hidesOnDeactivate=false;panel.animationBehavior = .none
  defer {panel.orderOut(nil);panel.close()}
  let root=NSView(frame:NSRect(x:0,y:0,width:640,height:480));root.wantsLayer=true;panel.contentView=root;panel.orderFrontRegardless()
  func wait(_ seconds:Double=0.04){CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:seconds))}
  func color(_ c:CGColor)->[Double] {let n=NSColor(cgColor:c)!.usingColorSpace(.sRGB)!;return[n.redComponent,n.greenComponent,n.blueComponent,n.alphaComponent]}
  var records:[[String:Any]]=[]
  for dark in [true,false] {for action in ["press","focus"] {
   let view=HUDQuitConfirmationView(frame:root.bounds,reduceMotion:{false});root.addSubview(view);view.setContent(title:"Synthetic",message:"Synthetic",cancel:"Revert",confirm:"Keep",focusConfirm:true);view.configure(dark:dark,accent:NSColor(srgbRed:250.0/255,green:212.0/255,blue:31.0/255,alpha:1));view.show();view.layoutSubtreeIfNeeded();wait(0.25)
   let card=view.subviews[0],button=card.subviews[3] as! NSButton,plate=card.layer!.sublayers![2] as! CAShapeLayer
   if action=="press" {button.highlight(true)}else{panel.makeFirstResponder(button)}
   CATransaction.flush()
   let animations=(plate.animationKeys() ?? []).filter{["fillColor","strokeColor","lineWidth"].contains($0)}.compactMap{key->CABasicAnimation? in guard let a=plate.animation(forKey:key)?.copy() as? CABasicAnimation else{return nil};a.keyPath=key;if a.toValue==nil{a.toValue=plate.value(forKey:key)};return a}
   precondition(!animations.isEmpty,"Original attached control must produce actual animations")
   struct Probe {let phase:Double,layer:CAShapeLayer}
   var probes:[Probe]=[]
   let phases=(0...100).map{Double($0)/100}+[0.0037,0.1234567,0.54321,0.87654321]
   for phase in phases {let layer=CAShapeLayer();layer.frame=root.bounds;layer.path=plate.path;layer.fillColor=plate.fillColor;layer.strokeColor=plate.strokeColor;layer.lineWidth=plate.lineWidth;layer.speed=0;layer.timeOffset=1+phase*0.12;root.layer!.addSublayer(layer)
    for original in animations {let a=original.copy() as! CABasicAnimation;a.beginTime=1;a.fillMode = .both;a.isRemovedOnCompletion=false;layer.add(a,forKey:a.keyPath)}
    probes.append(Probe(phase:phase,layer:layer))
   };wait()
   let sampled=probes.map{p->[String:Any] in guard let l=p.layer.presentation() else{fatalError("Missing presentation: \(dark) \(action) \(p.phase)")};guard l.fillColor != nil && l.strokeColor != nil else{fatalError("Missing shape colors: \(dark) \(action) \(p.phase)")};return["phase":p.phase,"fill":color(l.fillColor!),"stroke":color(l.strokeColor!),"width":l.lineWidth]}
   let descriptors=animations.map{a->[String:Any] in var row:[String:Any]=["path":a.keyPath!,"duration":a.duration];let c=a.timingFunction ?? CAMediaTimingFunction(name:.default);var x:[Float]=[0,0],y:[Float]=[0,0];c.getControlPoint(at:1,values:&x);c.getControlPoint(at:2,values:&y);row["timing"]=[x,y];if a.keyPath=="lineWidth" {row["from"]=(a.fromValue as! NSNumber).doubleValue;row["to"]=(a.toValue as! NSNumber).doubleValue}else{row["from"]=color(a.fromValue as! CGColor);row["to"]=color(a.toValue as! CGColor)};return row}
   var interruption:Any=NSNull()
   if action=="press" {plate.removeAllAnimations();plate.speed=0;plate.timeOffset=1+0.37*0.12;for original in animations {let a=original.copy() as! CABasicAnimation;a.beginTime=1;a.fillMode = .both;a.isRemovedOnCompletion=false;plate.add(a,forKey:a.keyPath)};wait();let before=plate.presentation()!;let prior:[String:Any]=["fill":color(before.fillColor!),"stroke":color(before.strokeColor!)];button.highlight(false);CATransaction.flush();var from:[String:Any]=[:];for key in ["fillColor","strokeColor"] {if let a=plate.animation(forKey:key) as? CABasicAnimation,let value=a.fromValue{from[key]=color(value as! CGColor)}};interruption=["phase":0.37,"before":prior,"from":from]}
   records.append(["dark":dark,"action":action,"animations":descriptors,"samples":sampled,"interruption":interruption]);probes.forEach{$0.layer.removeFromSuperlayer()};view.dismiss(animated:false);view.removeFromSuperview();wait()
  }}
  precondition(!panel.isKeyWindow && !panel.isMainWindow && NSScreen.screens.allSatisfy{!panel.frame.intersects($0.frame)})
  let result:[String:Any]=["actualSource":true,"ownOffscreenNonactivatingPanel":true,"screenCaptured":false,"records":records]
  try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]));print("Actual Settings safety feedback: \(records.count) transitions")
 }
}
