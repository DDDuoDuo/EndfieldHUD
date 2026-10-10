import Foundation
import QuartzCore
import CoreGraphics
import Metal
// Isolated probe of WorldMapCanvas's exact marker hierarchy: its beam is
// screen-composited inside the marker whose group opacity animates .45→1.
// Constant patches isolate grouping semantics from texture sampling/edge AA.
let directory=URL(fileURLWithPath:CommandLine.arguments[1])
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!,srgb=CGColorSpace(name:CGColorSpace.sRGB)!
func color(_ values:[Double])->CGColor {CGColor(colorSpace:srgb,components:values.map{CGFloat($0)})!}
let size=8,rect=CGRect(x:0,y:0,width:8,height:8)
let td=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:size,height:size,mipmapped:false)
td.storageMode = .shared;td.usage = [.renderTarget,.shaderRead]
let backgrounds=[[0.12,0.12,0.12,0.89],[0.81,0.81,0.81,0.89],[0.4,0.3,0.2,0.6],[0,0,0,0]]
let halo=[0.8,0.9,0.2,0.149],pulse=[1.0,0.9898965359,0.305660367,0.35],beam=[1.0,0.9898965359,0.305660367,0.504],glyph=[0.6,0.2,0.8,0.4]
var rows:[[String:Any]]=[]
for background in backgrounds {for opacity in [1.0,0.999,0.7,0.45,0.0] {for active in [0,1,2] {
 let target=device.makeTexture(descriptor:td)!,clear=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor()
 pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear;pass.colorAttachments[0].storeAction = .store;pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0)
 clear.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();clear.commit();clear.waitUntilCompleted()
 CATransaction.begin();CATransaction.setDisableActions(true)
 let root=CALayer();root.frame=rect
 let back=CALayer();back.frame=rect;back.backgroundColor=color(background);root.addSublayer(back)
 let marker=CALayer();marker.frame=rect;marker.opacity=Float(opacity);root.addSublayer(marker)
 let colors=active==0 ? [beam] : active==1 ? [halo,pulse,beam] : [halo,pulse,beam,glyph]
 for (n,c) in colors.enumerated(){let layer=CALayer();layer.frame=rect;layer.backgroundColor=color(c);if (active==0 && n==0)||(active>0 && n==2){layer.compositingFilter="screenBlendMode"};marker.addSublayer(layer)}
 CATransaction.commit()
 let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:srgb])
 renderer.layer=root;renderer.bounds=rect;CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(rect);renderer.render();renderer.endFrame()
 let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
 var bytes=[UInt8](repeating:0,count:256);target.getBytes(&bytes,bytesPerRow:32,from:MTLRegionMake2D(0,0,size,size),mipmapLevel:0)
 rows.append(["background":background,"markerOpacity":opacity,"groupOpacity":marker.allowsGroupOpacity,"colors":colors,"screenIndex":active==0 ? 0 : 2,"bgra":Array(bytes[144..<148])])
}}}
try JSONSerialization.data(withJSONObject:["rows":rows,"kind":"actual-CARenderer-nested-marker-group","usesAppOrWindow":false],options:[.sortedKeys]).write(to:directory.appendingPathComponent("output.json"))
print("Original marker grouping: \(rows.count) cases")
