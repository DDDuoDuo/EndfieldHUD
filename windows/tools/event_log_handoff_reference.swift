import AppKit
import QuartzCore
private struct Probe {let direction:Double,phase:Double;let incoming:Bool;let layer:CALayer;let mask:CAShapeLayer}
private func matrix(_ m:CATransform3D)->[Double]{[m.m11,m.m12,m.m13,m.m14,m.m21,m.m22,m.m23,m.m24,m.m31,m.m32,m.m33,m.m34,m.m41,m.m42,m.m43,m.m44].map(Double.init)}
private func wait(){CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:0.08))}
@main enum Main {
 static func main()throws {
  precondition(CommandLine.arguments.count==2);let output=URL(fileURLWithPath:CommandLine.arguments[1]);precondition(!FileManager.default.fileExists(atPath:output.path))
  let app=NSApplication.shared;app.setActivationPolicy(.prohibited);let left=NSScreen.screens.map{$0.frame.minX}.min() ?? 0
  let panel=NSPanel(contentRect:CGRect(x:left-4096,y:0,width:400,height:334),styleMask:[.borderless,.nonactivatingPanel],backing:.buffered,defer:false);panel.isReleasedWhenClosed=false;panel.ignoresMouseEvents=true;panel.isOpaque=false;panel.backgroundColor = .clear
  let view=NSView(frame:CGRect(x:0,y:0,width:400,height:334));view.wantsLayer=true;panel.contentView=view;defer{panel.orderOut(nil);panel.close()};let root=view.layer!
  let viewport=CGRect(x:0,y:0,width:376,height:198);let phases=(0...100).map{Double($0)/100}+[0.0037,0.1234567,0.54321,0.87654321];var probes:[Probe]=[]
  CATransaction.begin();CATransaction.setDisableActions(true)
  for direction in [-1.0,1.0] {let first=CALayer(),second=CALayer();first.bounds=viewport;second.bounds=viewport;let source=HUDSubsectionHandoff(first:first,second:second,viewport:viewport);source.select(1,direction:direction,animated:true)
   for incoming in [false,true] {let original=incoming ? second:first;let movement=original.animation(forKey:HUDSubsectionTransition.movementKey) as! CABasicAnimation;let reveal=(original.mask as! CAShapeLayer).animation(forKey:HUDSubsectionTransition.revealKey) as! CABasicAnimation
    for phase in phases {let layer=CALayer();layer.frame=viewport;layer.backgroundColor=NSColor.white.cgColor;layer.sublayerTransform=(movement.toValue as! NSValue).caTransform3DValue;layer.speed=0;layer.timeOffset=1+phase*0.26;root.addSublayer(layer)
     let depth=movement.copy() as! CABasicAnimation;depth.beginTime=1;depth.fillMode = .both;depth.isRemovedOnCompletion=false;layer.add(depth,forKey:"source.depth")
     let mask=CAShapeLayer();mask.frame=viewport;mask.fillColor=NSColor.black.cgColor;mask.path=(reveal.toValue as! CGPath);layer.mask=mask
     let curve=reveal.copy() as! CABasicAnimation;curve.beginTime=1;curve.fillMode = .both;curve.isRemovedOnCompletion=false;mask.add(curve,forKey:"source.reveal");probes.append(Probe(direction:direction,phase:phase,incoming:incoming,layer:layer,mask:mask))
    }
   };source.settle()
  };CATransaction.commit();panel.orderFrontRegardless();wait();precondition(!panel.isKeyWindow && !panel.isMainWindow && NSScreen.screens.allSatisfy{!panel.frame.intersects($0.frame)})
  func sample(_ p:Probe)->[Double]{let presented=p.layer.presentation()!;let path=p.mask.presentation()!.path!;let b=path.boundingBoxOfPath;return matrix(presented.sublayerTransform)+[b.minX,b.minY,b.width,b.height]}
  let first=probes.map(sample);wait();var repeatError=0.0;let records=probes.enumerated().map{index,p->[String:Any] in let value=sample(p);repeatError=max(repeatError,zip(first[index],value).map{abs($0-$1)}.max() ?? 0);return ["direction":p.direction,"phase":p.phase,"incoming":p.incoming,"matrix":Array(value.prefix(16)),"bounds":Array(value.suffix(4))]}
  try JSONSerialization.data(withJSONObject:["kind":"actual-source-Core-Animation-handoff","viewport":[0,0,376,198],"duration":0.26,"maximumRepeatDifference":repeatError,"samples":records],options:[.sortedKeys]).write(to:output);print("Exported \(records.count) actual source handoff samples; repeat \(repeatError)")
 }
}
