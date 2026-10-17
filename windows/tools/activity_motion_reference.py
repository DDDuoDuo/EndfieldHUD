#!/usr/bin/env python3
"""Build-only original Activity graph/page animation oracle, isolated paused CA.
No app services, activation, providers or user data. Uses the existing cache.
"""
import hashlib,json,os,pathlib,subprocess,sys,tempfile
root=pathlib.Path(__file__).resolve().parents[2]
out=pathlib.Path(sys.argv[1]);out.mkdir(parents=True,exist_ok=True)
source=root/'Sources/TelemetryCanvases.swift'
text=source.read_text().split('final class StorageCanvas')[0]
stub='''import AppKit
import QuartzCore
enum HUDRuntimeAppearance { static let accent=NSColor.yellow }
enum HUDRenderScale { static func contentScale(for root:CALayer,baseScale:CGFloat)->CGFloat{baseScale} }
'''
fixture=r'''
func rect(_ r:CGRect)->[Double]{[r.origin.x,r.origin.y,r.width,r.height]}
func points(_ p:CGPath)->[[Double]]{var out:[[Double]]=[];p.applyWithBlock{e in let n:Int;switch e.pointee.type{case .moveToPoint,.addLineToPoint:n=0;case .addQuadCurveToPoint:n=1;case .addCurveToPoint:n=2;default:return};let p=e.pointee.points[n];out.append([p.x,p.y])};return out}
NSApplication.shared.setActivationPolicy(.prohibited)
let minX=NSScreen.screens.map{$0.frame.minX}.min() ?? 0
let panel=NSPanel(contentRect:NSRect(x:minX-4096,y:0,width:440,height:440),styleMask:[.borderless,.nonactivatingPanel],backing:.buffered,defer:false)
panel.isReleasedWhenClosed=false;panel.ignoresMouseEvents=true;panel.hidesOnDeactivate=false;panel.isOpaque=false;panel.backgroundColor = .clear
let view=NSView(frame:NSRect(x:0,y:0,width:440,height:440));view.wantsLayer=true;panel.contentView=view;let root=view.layer!
panel.orderFrontRegardless();precondition(!panel.isKeyWindow && !panel.isMainWindow && NSScreen.screens.allSatisfy{!panel.frame.intersects($0.frame)})
let phases=(0...32).map{Double($0)/32} + [0.0001,0.001,0.02,0.29,0.68,0.99]
struct H {let phase:Double;let direction:Int;let a:CALayer;let b:CALayer}
struct G {let phase:Double;let graph:TelemetryGraph;let from:[[Double]];let to:[[Double]]}
var handoffs:[H]=[],graphs:[G]=[]
CATransaction.begin();CATransaction.setDisableActions(true)
for direction in [-1,1] {for phase in phases {
 let a=CALayer(),b=CALayer();a.frame=CGRect(x:0,y:0,width:400,height:334);b.frame=a.frame;root.addSublayer(a);root.addSublayer(b)
 let owner=HUDSubsectionHandoff(first:a,second:b,viewport:CGRect(x:8,y:54,width:384,height:258))
 if direction<0 {owner.select(1,direction:1,animated:false)}
 owner.select(direction>0 ? 1:0,direction:CGFloat(direction),animated:true)
 var animations:[(CALayer,CAAnimation,String)]=[]
 for page in [a,b] {let mask=page.mask!;for (layer,key,newkey) in [(page,HUDSubsectionTransition.movementKey,"oracle.depth"),(mask,HUDSubsectionTransition.revealKey,"oracle.mask")] {let animation=layer.animation(forKey:key)!.copy() as! CAAnimation;animation.beginTime=1;animation.fillMode = .both;animation.isRemovedOnCompletion=false;animations.append((layer,animation,newkey))}}
 owner.settle()
 for page in [a,b]{page.isHidden=false;page.speed=0;page.timeOffset=1+phase*0.26}
 for (layer,animation,key) in animations {layer.add(animation,forKey:key)}
 handoffs.append(H(phase:phase,direction:direction,a:a,b:b))
}}
for phase in phases {
 let graph=TelemetryGraph(name:"fixture",frame:CGRect(x:0,y:0,width:165,height:36),colors:[.yellow]);root.addSublayer(graph.layer)
 let old=(0..<60).map{Optional(Double(($0*17)%90))},next=(0..<60).map{$0%9==3 ? nil:Optional(Double(($0*13)%110))}
 graph.update(series:[old],ceiling:100,animated:false,timestamps:(0..<60).map{Double($0)})
 let line=graph.layer.sublayers!.first{$0.name=="fixture.line.0"} as! CAShapeLayer;let from=points(line.path!)
 graph.update(series:[next],ceiling:100,animated:true,timestamps:(0..<60).map{Double($0*3)})
 let to=points(line.path!),animation=line.animation(forKey:"telemetry.path")!.copy() as! CAAnimation;animation.beginTime=1;animation.fillMode = .both;animation.isRemovedOnCompletion=false;line.removeAllAnimations();line.add(animation,forKey:"oracle.path");graph.layer.speed=0;graph.layer.timeOffset=1+phase*0.32
 graphs.append(G(phase:phase,graph:graph,from:from,to:to))
}
CATransaction.commit();CATransaction.flush();RunLoop.current.run(until:Date(timeIntervalSinceNow:0.06))
let hs=handoffs.map { h->[String:Any] in
 let values=[h.a,h.b].map{page->[String:Any] in let mask=page.mask as! CAShapeLayer;return ["x":page.presentation()!.sublayerTransform.m41,"clip":rect(mask.presentation()!.path!.boundingBoxOfPath)]}
 return ["phase":h.phase,"direction":h.direction,"pages":values]
}
let gs=graphs.map{g->[String:Any] in let line=g.graph.layer.sublayers!.first{$0.name=="fixture.line.0"} as! CAShapeLayer;return ["phase":g.phase,"from":g.from,"to":g.to,"points":points(line.presentation()!.path!)]}
let result:[String:Any]=["handoffs":hs,"graphs":gs]
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]))
panel.orderOut(nil);panel.close()
'''
(out/'main.swift').write_text(stub+text+fixture)
source_paths=['Sources/TelemetryCanvases.swift','Sources/HUDSubsectionTransition.swift']
pins={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in source_paths}
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),'-framework','AppKit','-framework','QuartzCore',str(root/'Sources/HUDSubsectionTransition.swift'),str(out/'main.swift'),'-o',str(out/'reference')],check=True)
with tempfile.TemporaryDirectory(prefix='ehud-activity-motion-') as home:
 result=json.loads(subprocess.check_output([str(out/'reference')],env=dict(os.environ,CFFIXED_USER_HOME=home)))
assert pins=={p:hashlib.sha256((root/p).read_bytes()).hexdigest() for p in source_paths}
result['sourceSHA256']=pins;result['scope']='Actual paused original graph line path and complementary page handoff; own offscreen nonactivating panel; no application providers or data.'
(out/'activity-motion.json').write_text(json.dumps(result,separators=(',',':'))+'\n')
print(out/'activity-motion.json')
