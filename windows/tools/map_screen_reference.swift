import Foundation
import QuartzCore
import Metal
import CoreGraphics

// Own offscreen texture/layers only. The source Map beam uses this exact
// compositingFilter value and source opacity; no app/window/screen capture.
let directory=URL(fileURLWithPath:CommandLine.arguments[1])
let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
let srgb=CGColorSpace(name:CGColorSpace.sRGB)!
let td=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:8,height:8,mipmapped:false)
td.storageMode = .shared;td.usage = [.renderTarget,.shaderRead]
let target=device.makeTexture(descriptor:td)!
func color(_ values:[Double])->CGColor {CGColor(colorSpace:srgb,components:values.map{CGFloat($0)})!}
func image(_ values:[Double])->CGImage {
    let a=values[3]
    let bytes=Data([UInt8((values[0]*a*255).rounded()),UInt8((values[1]*a*255).rounded()),UInt8((values[2]*a*255).rounded()),UInt8((a*255).rounded())])
    return CGImage(width:1,height:1,bitsPerComponent:8,bitsPerPixel:32,bytesPerRow:4,space:srgb,bitmapInfo:CGBitmapInfo(rawValue:CGImageAlphaInfo.premultipliedLast.rawValue),provider:CGDataProvider(data:bytes as CFData)!,decode:nil,shouldInterpolate:false,intent:.defaultIntent)!
}
let pairs:[([Double],[Double])]=[
    ([0.4,0.3,0.2,0.6],[0.5,0.4,0.3,0.7]),([0.12,0.12,0.12,0.89],[1,0.9898965359,0.305660367,1]),
    ([0.81,0.81,0.81,0.89],[1,0.9898965359,0.305660367,0.2]),([0.2,0.7,0.1,0],[0.8,0.4,0.1,0.5]),
    ([0.2,0.7,0.1,1],[0.8,0.4,0.1,0]),([0,0,0,1],[1,1,1,1]),([1,1,1,1],[0.3,0.7,0.2,0.5]),
    ([0.1,0.7,0.3,0.3],[0.8,0.2,0.6,1])]
var rows:[[String:Any]]=[]
for (background,foreground) in pairs {
 for screen in [false,true] {for groupOpacity in [1.0,0.45,0.0] {for bitmap in [false,true] {
    let clear=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor()
    pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear;pass.colorAttachments[0].storeAction = .store
    pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0)
    clear.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();clear.commit();clear.waitUntilCompleted()
    CATransaction.begin();CATransaction.setDisableActions(true)
    let root=CALayer();root.frame=CGRect(x:0,y:0,width:8,height:8)
    let group=CALayer();group.frame=root.bounds;group.opacity=Float(groupOpacity);group.allowsGroupOpacity=true;root.addSublayer(group)
    let back=CALayer();back.frame=root.bounds;back.backgroundColor=color(background);group.addSublayer(back)
    let beam=CALayer();beam.frame=root.bounds
    if bitmap {beam.contents=image(foreground);beam.contentsGravity = .resize} else {beam.backgroundColor=color(foreground)}
    if screen {beam.compositingFilter = "screenBlendMode"};beam.opacity=0.72;group.addSublayer(beam)
    CATransaction.commit()
    let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:srgb])
    renderer.layer=root;renderer.bounds=root.bounds;CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame()
    let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
    var bytes=[UInt8](repeating:0,count:256);target.getBytes(&bytes,bytesPerRow:32,from:MTLRegionMake2D(0,0,8,8),mipmapLevel:0)
    rows.append(["background":background,"foreground":foreground,"screen":screen,"groupOpacity":groupOpacity,"bitmap":bitmap,"beamOpacity":0.72,"bgra":Array(bytes[144..<148])])
 }}}
}
let report:[String:Any] = ["schemaVersion":1,"kind":"actual-CARenderer-Metal-sRGB","screenCaptured":false,"usesAppOrWindow":false,"rows":rows]
try JSONSerialization.data(withJSONObject:report,options:[.sortedKeys]).write(to:directory.appendingPathComponent("output.json"))
print("Actual Core Animation screen reference: \(rows.count) isolated color/group cases")
