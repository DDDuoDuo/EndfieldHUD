import Foundation
import QuartzCore
import Metal
import CoreGraphics
// sourceVolumeFade below is supplied by the exporter from the unchanged
// original renderVolume animation block. This probe keeps the source hierarchy
// flags and uses two synthetic overlapping leaves, avoiding text/font APIs.
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!,space=CGColorSpace(name:CGColorSpace.sRGB)!
var rows:[[String:Any]]=[]
for palette in ["primary","volume"] {for animated in [false,true] {for opacity in [0.0,0.5,0.99,0.995,0.998,0.999,1.0] {
 let colors:[[CGFloat]]=palette=="primary" ? [[1,0,0,1],[0,0,1,0.5]] : [[0,0,0,0.3],[0.08,0.08,0.08,0.98]]
 let desc=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false);desc.storageMode = .shared;desc.usage=[.renderTarget,.shaderRead];let target=device.makeTexture(descriptor:desc)!
 CATransaction.begin();CATransaction.setDisableActions(true)
 let root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8)
 let wrapper=CALayer();wrapper.frame=root.bounds;wrapper.allowsGroupOpacity=false;wrapper.opacity=0.5;root.addSublayer(wrapper)
 let factory=CALayer();factory.frame=root.bounds;wrapper.addSublayer(factory)
 let content=CALayer();content.frame=root.bounds;content.allowsGroupOpacity=false;factory.addSublayer(content)
 let container=CALayer();container.frame=root.bounds;container.opacity=Float(opacity);content.addSublayer(container)
 for color in colors {let leaf=CALayer();leaf.frame=root.bounds;leaf.backgroundColor=CGColor(colorSpace:space,components:color);container.addSublayer(leaf)}
 if animated {container.opacity=1;sourceVolumeFade(container,0);let fade=container.animation(forKey:"nowPlaying.popover.visibility")!.copy() as! CAAnimation;fade.beginTime=1;fade.fillMode = .both;fade.isRemovedOnCompletion=false;container.speed=0;container.timeOffset=1+opacity*0.16;container.add(fade,forKey:"nowPlaying.popover.visibility")}
 CATransaction.commit()
 let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:space]);renderer.layer=root;renderer.bounds=root.bounds
 let command=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor();pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear;pass.colorAttachments[0].storeAction = .store;pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0);command.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();command.commit();command.waitUntilCompleted()
 CATransaction.flush();renderer.beginFrame(atTime:1,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame();let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted();var bytes=[UInt8](repeating:0,count:256);target.getBytes(&bytes,bytesPerRow:32,from:MTLRegionMake2D(0,0,8,8),mipmapLevel:0)
 rows.append(["palette":palette,"animated":animated,"inheritedOpacity":0.5,"ownOpacity":opacity,"leafRGBA":colors,"bgra":Array(bytes[144..<148])])
}}}
try JSONSerialization.data(withJSONObject:["rows":rows,"usesAppOrWindow":false],options:.sortedKeys).write(to:URL(fileURLWithPath:CommandLine.arguments[1]))
