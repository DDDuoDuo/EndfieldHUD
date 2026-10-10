import Foundation
import QuartzCore
import Metal
import CoreGraphics
// The calling exporter compiles the unchanged source crossfade(_:) body with
// inert active/rendered/reduceMotion globals. Only its clock is frozen here.
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
let space=CGColorSpace(name:CGColorSpace.sRGB)!
var rows:[[String:Any]]=[]
for pair in [(1.0,1.0),(0.4,0.4),(0.2,0.8),(0.0,0.6)] {
 for phase in [0.0,0.1,0.25,0.5,0.75,0.9,1.0] {
    let old:[CGFloat]=[1,0,0,pair.0],new:[CGFloat]=[0,0,1,pair.1],third:[CGFloat]=[0,1,0,pair.0]
    let desc=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false)
    desc.storageMode = .shared;desc.usage=[.renderTarget,.shaderRead]
    let target=device.makeTexture(descriptor:desc)!
    CATransaction.begin();CATransaction.setDisableActions(true)
    let root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8)
    let layer=CALayer();layer.frame=root.bounds;layer.backgroundColor=CGColor(colorSpace:space,components:old)
    root.addSublayer(layer);CATransaction.commit()
    let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:space])
    renderer.layer=root;renderer.bounds=root.bounds
    func render(_ time:Double)->[UInt8] {
        let buffer=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear
        pass.colorAttachments[0].storeAction = .store;pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0)
        buffer.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();buffer.commit();buffer.waitUntilCompleted()
        CATransaction.flush();renderer.beginFrame(atTime:time,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame()
        let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
        var bytes=[UInt8](repeating:0,count:256)
        target.getBytes(&bytes,bytesPerRow:32,from:MTLRegionMake2D(0,0,8,8),mipmapLevel:0)
        return Array(bytes[144..<148])
    }
    func transition(_ rgba:[CGFloat],begin:Double,phase:Double) {
        CATransaction.begin();CATransaction.setDisableActions(true)
        layer.speed=0;layer.timeOffset=begin+phase*0.2
        crossfade(layer) // Exact original source method, including its timing.
        let fade=layer.animation(forKey:kCATransition)!.copy() as! CAAnimation
        fade.beginTime=begin;fade.fillMode = .both;fade.isRemovedOnCompletion=false
        layer.add(fade,forKey:kCATransition)
        layer.backgroundColor=CGColor(colorSpace:space,components:rgba);CATransaction.commit()
    }
    let prior=render(0);transition(new,begin:1,phase:phase);let blended=render(1)
    transition(third,begin:2,phase:0);let interruptedStart=render(2)
    CATransaction.begin();CATransaction.setDisableActions(true);layer.timeOffset=2.1;CATransaction.commit()
    rows.append(["oldRGBA":old,"newRGBA":new,"thirdRGBA":third,"phase":phase,
        "priorBGRA":prior,"bgra":blended,"interruptedStartBGRA":interruptedStart,"interruptedHalfBGRA":render(2.1)])
 }
}
try JSONSerialization.data(withJSONObject:["duration":0.2,"rows":rows,"usesAppOrWindow":false],options:[.sortedKeys]).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
