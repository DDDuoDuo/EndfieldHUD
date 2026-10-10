#!/usr/bin/env python3
"""Extract unchanged preset animation into synthetic detached CA layers only."""
from pathlib import Path
import hashlib,json,subprocess
root=Path(__file__).resolve().parents[2]
out=root/'build/app-shortcut-reference';out.mkdir(parents=True,exist_ok=True)
source=(root/'Sources/AppShortcutCanvas.swift').read_text()
start=source.index('    private func animatePreset(');brace=source.index('{',start);depth=1;end=brace+1
while depth:
    depth+=(source[end]=='{')-(source[end]=='}');end+=1
method=source[start:end].replace('private func animatePreset','func animatePreset',1)
swift='''import AppKit
import QuartzCore
import Metal
enum AppShortcutIcon:String,CaseIterable {case original,camera}
final class Original {
 let artwork=CALayer(),active=true,yellow=NSColor.yellow
 func shouldReduceMotion()->Bool {false}
 init() {let face=CALayer();face.bounds=CGRect(x:0,y:0,width:48,height:48);face.name="apps.preset.camera";artwork.addSublayer(face)}
'''+method+'''
}
func commands(_ path:CGPath)->[[String:Any]] {var out:[[String:Any]]=[];path.applyWithBlock{p in let e=p.pointee;let op:String,n:Int;switch e.type{case .moveToPoint:op="move";n=1;case .addLineToPoint:op="line";n=1;case .addQuadCurveToPoint:op="quadratic";n=2;case .addCurveToPoint:op="cubic";n=3;case .closeSubpath:op="close";n=0;@unknown default:fatalError()};out.append(["op":op,"points":(0..<n).map{[Double(e.points[$0].x),Double(e.points[$0].y)]}])};return out}
let original=Original();original.animatePreset(from:.original,to:.camera)
let rim=original.artwork.sublayers!.first!.sublayers!.first as! CAShapeLayer
let sourceAnimation=rim.animation(forKey:"apps.preset.register") as! CABasicAnimation
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
let descriptor=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false)
descriptor.storageMode = .shared;descriptor.usage=[.renderTarget,.shaderRead]
let texture=device.makeTexture(descriptor:descriptor)!,layer=CALayer();layer.frame=CGRect(x:0,y:0,width:8,height:8)
var probes:[(Double,CAShapeLayer)]=[]
CATransaction.begin();CATransaction.setDisableActions(true)
for phase in (0...100).map({Double($0)/100})+[0.0037,0.1234567,0.54321,0.87654321] {
 let shape=CAShapeLayer();shape.frame=rim.bounds;shape.path=rim.path;shape.strokeColor=rim.strokeColor;shape.fillColor=nil;shape.lineWidth=rim.lineWidth;shape.strokeEnd=1
 shape.speed=0;shape.timeOffset=1+phase*sourceAnimation.duration;layer.addSublayer(shape)
 let animation=sourceAnimation.copy() as! CABasicAnimation;animation.beginTime=1;animation.fillMode = .both;animation.isRemovedOnCompletion=false
 shape.add(animation,forKey:"source");probes.append((phase,shape))
}
CATransaction.commit()
let renderer=CARenderer(mtlTexture:texture,options:[kCARendererMetalCommandQueue:queue]);renderer.layer=layer;renderer.bounds=layer.bounds
CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(layer.bounds);renderer.render();renderer.endFrame()
let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
let rows=probes.map {phase,shape -> [String:Any] in guard let value=shape.presentation() else {fatalError("Missing actual CA presentation")};return ["phase":phase,"strokeEnd":value.strokeEnd]}
let result:[String:Any] = ["version":1,"usesAppOrWindow":false,"path":commands(rim.path!),"lineWidth":rim.lineWidth,"lineCap":rim.lineCap.rawValue,"lineJoin":rim.lineJoin.rawValue,"duration":sourceAnimation.duration,"rows":rows]
try JSONSerialization.data(withJSONObject:result,options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
'''
(out/'preset.swift').write_text(swift)
subprocess.run(['xcrun','swiftc','-O','-sdk','/Library/Developer/CommandLineTools/SDKs/MacOSX15.5.sdk','-module-cache-path',str(root/'build/windows-module-reference/.compiler/module-cache'),str(out/'preset.swift'),'-o',str(out/'preset-reference')],check=True)
subprocess.run([str(out/'preset-reference'),str(out/'preset-raw.json')],check=True)
value=json.loads((out/'preset-raw.json').read_text());value['sourceSHA256']=hashlib.sha256(source.encode()).hexdigest();value['extractedSHA256']=hashlib.sha256(source[start:end].encode()).hexdigest()
target=root/'windows/tests/fixtures/app-shortcut-preset-source.json';target.write_text(json.dumps(value,separators=(',',':'))+'\n');print(target)
