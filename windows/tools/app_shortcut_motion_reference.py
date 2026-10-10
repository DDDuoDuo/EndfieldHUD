#!/usr/bin/env python3
"""Genuine detached CA samples of the unchanged AppShortcutCanvas animation block."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2];out=root/'build/app-shortcut-reference';out.mkdir(parents=True,exist_ok=True)
s=(root/'Sources/AppShortcutCanvas.swift').read_text()
def method(mark):
 start=s.index(mark);brace=s.index('{',start);depth=1;i=brace+1
 while depth:
  depth+=(s[i]=='{')-(s[i]=='}');i+=1
 return s[start:i]
path=method('private func shutterPath').replace('private func','func',1)
start=s.index('        let timing = CAMediaTimingFunction(controlPoints: 0.2, 0.78, 0.27, 1)')
end=s.index('        CATransaction.commit()',start)
block=s[start:end]
head='''import AppKit
import QuartzCore
import Metal
final class OriginalShortcutMotion {
 static let transitionDuration:TimeInterval=0.26
 let layer=CALayer(),artwork=CALayer(),previous=CALayer(),reveal=CAShapeLayer(),retract=CAShapeLayer()
 init(_ direction:CGFloat) {
  layer.bounds=CGRect(x:0,y:0,width:400,height:334)
  artwork.frame=layer.bounds;previous.frame=layer.bounds
  layer.addSublayer(previous);layer.addSublayer(artwork)
  reveal.frame=artwork.bounds;retract.frame=previous.bounds
  reveal.path=shutterPath(progress:1,direction:direction);retract.path=shutterPath(progress:0,direction:-direction)
  artwork.mask=reveal;previous.mask=retract
  CATransaction.begin();CATransaction.setDisableActions(true)
'''+block+''' CATransaction.commit()
 }
'''+path+'\n}\n'
driver=r'''
func matrix(_ m:CATransform3D)->[Double]{[m.m11,m.m12,m.m13,m.m14,m.m21,m.m22,m.m23,m.m24,m.m31,m.m32,m.m33,m.m34,m.m41,m.m42,m.m43,m.m44].map(Double.init)}
func commands(_ path:CGPath)->[[String:Any]]{var out:[[String:Any]]=[];path.applyWithBlock{p in let e=p.pointee;let op:String,n:Int;switch e.type{case .moveToPoint:op="move";n=1;case .addLineToPoint:op="line";n=1;case .addQuadCurveToPoint:op="quadratic";n=2;case .addCurveToPoint:op="cubic";n=3;case .closeSubpath:op="close";n=0;@unknown default:fatalError()};out.append(["op":op,"points":(0..<n).map{[Double(e.points[$0].x),Double(e.points[$0].y)]}])};return out}
let dev=MTLCreateSystemDefaultDevice()!,queue=dev.makeCommandQueue()!
let desc=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false);desc.storageMode = .shared;desc.usage=[.renderTarget,.shaderRead]
let tex=dev.makeTexture(descriptor:desc)!,root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8)
struct Probe {let direction:Int;let phase:Double;let incoming:Bool;let node:CALayer;let mask:CAShapeLayer}
var probes:[Probe]=[];CATransaction.begin();CATransaction.setDisableActions(true)
for direction in [-1,1] {let original=OriginalShortcutMotion(CGFloat(direction))
 for phase in (0...100).map({Double($0)/100})+[0.0037,0.1234567,0.54321,0.87654321] {
  for incoming in [true,false] {let authored=incoming ? original.artwork : original.previous;let authoredMask=incoming ? original.reveal : original.retract
   let node=CALayer();node.bounds=CGRect(x:0,y:0,width:400,height:334);node.speed=0;node.timeOffset=1+phase*0.26;root.addSublayer(node)
   let anim=authored.animation(forKey:incoming ? "apps.transition.arrive" : "apps.transition.depart")!.copy() as! CABasicAnimation;anim.beginTime=1;anim.fillMode = .both;anim.isRemovedOnCompletion=false;node.transform=(anim.toValue as! NSValue).caTransform3DValue;node.add(anim,forKey:"motion")
   let mask=CAShapeLayer();mask.bounds=node.bounds;mask.path=authoredMask.path;node.mask=mask;let wipe=authoredMask.animation(forKey:"apps.transition.shutter")!.copy() as! CABasicAnimation;wipe.beginTime=1;wipe.fillMode = .both;wipe.isRemovedOnCompletion=false;mask.add(wipe,forKey:"wipe")
   probes.append(Probe(direction:direction,phase:phase,incoming:incoming,node:node,mask:mask))
  }
 }
}
CATransaction.commit();let render=CARenderer(mtlTexture:tex,options:[kCARendererMetalCommandQueue:queue]);render.layer=root;render.bounds=root.bounds;CATransaction.flush();render.beginFrame(atTime:0,timeStamp:nil);render.addUpdate(root.bounds);render.render();render.endFrame();let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
let rows=probes.map{p->[String:Any] in guard let shown=p.node.presentation(),let mask=p.mask.presentation() else{fatalError("Missing genuine detached CA presentation")};return ["direction":p.direction,"phase":p.phase,"incoming":p.incoming,"matrix":matrix(shown.transform),"path":commands(mask.path!)]}
try JSONSerialization.data(withJSONObject:["version":1,"kind":"genuine-detached-CoreAnimation","usesAppOrWindow":false,"rows":rows],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
(out/'motion.swift').write_text(head+driver)
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'motion.swift'),'-o',str(out/'motion-reference')],check=True)
subprocess.run([str(out/'motion-reference'),str(out/'motion-raw.json')],check=True)
v=json.loads((out/'motion-raw.json').read_text());v['sourceSHA256']=hashlib.sha256(s.encode()).hexdigest();v['extractedSHA256']=hashlib.sha256((block+path).encode()).hexdigest()
(out/'motion-source.json').write_text(json.dumps(v,separators=(',',':'))+'\n');print(out/'motion-source.json')
