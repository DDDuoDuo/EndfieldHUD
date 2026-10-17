import Foundation
import QuartzCore
import Metal
import CoreGraphics
// Exact source opacity animation: nil timingFunction, duration0.16. The source
// lines are hash-locked by the calling reference tool. Detached pixels only.
let output=URL(fileURLWithPath:CommandLine.arguments[1]);let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
let desc=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false);desc.storageMode = .shared;desc.usage=[.renderTarget,.shaderRead]
let target=device.makeTexture(descriptor:desc)!,space=CGColorSpace(name:CGColorSpace.sRGB)!
var rows:[[String:Any]]=[]
for phase in [0.0,0.05,0.1,0.2,0.3,0.5,0.7,0.9,0.95,1.0] {
 let buffer=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor();pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear;pass.colorAttachments[0].storeAction = .store;buffer.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();buffer.commit();buffer.waitUntilCompleted()
 CATransaction.begin();CATransaction.setDisableActions(true)
 let root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8);let layer=CALayer();layer.frame=root.bounds;layer.backgroundColor=CGColor(colorSpace:space,components:[1,1,1,1]);layer.opacity=1;layer.speed=0;layer.timeOffset=1+phase*0.16;root.addSublayer(layer)
 if phase<1 {let fade=CABasicAnimation(keyPath:"opacity");fade.fromValue=Float(0);fade.toValue=Float(1);fade.duration=0.16;fade.beginTime=1;fade.fillMode = .both;fade.isRemovedOnCompletion=false;layer.add(fade,forKey:"orbipom.boardDimming")}
 CATransaction.commit();let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:space]);renderer.layer=root;renderer.bounds=root.bounds;CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame();let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted();var bytes=[UInt8](repeating:0,count:256);target.getBytes(&bytes,bytesPerRow:32,from:MTLRegionMake2D(0,0,8,8),mipmapLevel:0);rows.append(["phase":phase,"bgra":Array(bytes[144..<148])])
}
try JSONSerialization.data(withJSONObject:["duration":0.16,"usesAppOrWindow":false,"rows":rows],options:[.sortedKeys]).write(to:output)
