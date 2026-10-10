import AppKit
import ImageIO
import QuartzCore
import Metal

// The separately compiled WorldMapPinArtwork is the untouched original source.
// Only its resource/localization/model dependencies are replaced by inert shims.
enum HUDResources {
    static func url(for name:String)->URL? { URL(fileURLWithPath:CommandLine.arguments[1]).appendingPathComponent(name) }
}
enum MapPinStyle {case yellow,green,player}
enum L10n {static func text(_ english:String,_ chinese:String)->String {english}}

@main struct MapPlayerReference {
 static func main() throws {
    let directory=URL(fileURLWithPath:CommandLine.arguments[2])
    let srgb=CGColorSpace(name:CGColorSpace.sRGB)!
    var images:[[String:Any]]=[]
    for (name,optionalImage) in [("halo",WorldMapPinArtwork.playerHalo),("beam",WorldMapPinArtwork.playerBeam),("glyph",WorldMapPinArtwork.playerGlyph)] {
        guard let image=optionalImage else {fatalError("Missing original player image")}
        let width=image.width,height=image.height
        let context=CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:width*4,space:srgb,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue)!
        context.draw(image,in:CGRect(x:0,y:0,width:width,height:height))
        let source=context.data!.assumingMemoryBound(to:UInt8.self)
        var straight=[UInt8](repeating:0,count:width*height*4)
        var maximumRoundTrip=0
        for p in stride(from:0,to:straight.count,by:4) {
            let alpha=Int(source[p+3]);straight[p+3]=UInt8(alpha)
            for c in 0..<3 {
                let value=Int(source[p+c])
                straight[p+c]=alpha == 0 ? 0 : UInt8(min(255,(value*255+alpha/2)/alpha))
                maximumRoundTrip=max(maximumRoundTrip,abs((Int(straight[p+c])*alpha+127)/255-value))
            }
        }
        try Data(straight).write(to:directory.appendingPathComponent(name+".rgba"))
        images.append(["name":name,"width":width,"height":height,"bytes":straight.count,"premultipliedRoundTripMaximum":maximumRoundTrip])
    }
    let device=MTLCreateSystemDefaultDevice()!,queue=device.makeCommandQueue()!
    var masks:[[String:Any]]=[]
    for scale in [1,2] {
        let side=440*scale
        let td=MTLTextureDescriptor.texture2DDescriptor(pixelFormat:.bgra8Unorm,width:side,height:side,mipmapped:false)
        td.storageMode = .shared;td.usage = [.renderTarget,.shaderRead]
        let target=device.makeTexture(descriptor:td)!
        let clear=queue.makeCommandBuffer()!,pass=MTLRenderPassDescriptor()
        pass.colorAttachments[0].texture=target;pass.colorAttachments[0].loadAction = .clear;pass.colorAttachments[0].storeAction = .store
        pass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0)
        clear.makeRenderCommandEncoder(descriptor:pass)!.endEncoding();clear.commit();clear.waitUntilCompleted()
        CATransaction.begin();CATransaction.setDisableActions(true)
        let root=CALayer();root.frame=CGRect(x:0,y:0,width:side,height:side)
        let content=CALayer();content.bounds=CGRect(x:0,y:0,width:440,height:440);content.position=CGPoint(x:side/2,y:side/2)
        content.transform=CATransform3DMakeScale(CGFloat(scale),CGFloat(scale),1);content.contentsScale=CGFloat(scale)
        content.backgroundColor=NSColor.white.cgColor;content.mask=sourceMapMask();root.addSublayer(content)
        CATransaction.commit()
        let renderer=CARenderer(mtlTexture:target,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:srgb])
        renderer.layer=root;renderer.bounds=root.bounds;CATransaction.flush();renderer.beginFrame(atTime:0,timeStamp:nil);renderer.addUpdate(root.bounds);renderer.render();renderer.endFrame()
        let done=queue.makeCommandBuffer()!;done.commit();done.waitUntilCompleted()
        var bytes=[UInt8](repeating:0,count:side*side*4);target.getBytes(&bytes,bytesPerRow:side*4,from:MTLRegionMake2D(0,0,side,side),mipmapLevel:0)
        func expected(_ x:Int,_ y:Int)->Double {min(1,max(0,(216-hypot((Double(x)+0.5)/Double(scale)-220,(Double(y)+0.5)/Double(scale)-220))/9))*255}
        var maximum=0.0,sum=0.0,different=0
        for y in 0..<side {for x in 0..<side {
            let error=abs(Double(bytes[(y*side+x)*4+3])-expected(x,y));maximum=max(maximum,error);sum+=error
            if error>1 {different+=1}
        }}
        var samples:[[String:Any]]=[]
        for distance in [0,206,207,208,209,210,211,212,213,214,215,216,217,219] {
            for angle in [0.0,Double.pi/4] {
                let x=min(side-1,Int((220+Double(distance)*cos(angle))*Double(scale))),y=min(side-1,Int((220+Double(distance)*sin(angle))*Double(scale)))
                samples.append(["x":x,"y":y,"alpha":bytes[(y*side+x)*4+3],"formulaAlpha":expected(x,y)])
            }
        }
        let sourceMask=content.mask as! CAGradientLayer
        let settings:[String:Any] = ["type":sourceMask.type.rawValue,"startPoint":[sourceMask.startPoint.x,sourceMask.startPoint.y],"endPoint":[sourceMask.endPoint.x,sourceMask.endPoint.y],"locations":sourceMask.locations!,"colorAlpha":(sourceMask.colors as! [CGColor]).map{$0.alpha}]
        // Fresh root, renderer and cleared target avoid presentation-layer
        // transaction state from the source sample contaminating this control.
        let controlTarget=device.makeTexture(descriptor:td)!
        let controlClear=queue.makeCommandBuffer()!,controlPass=MTLRenderPassDescriptor()
        controlPass.colorAttachments[0].texture=controlTarget;controlPass.colorAttachments[0].loadAction = .clear;controlPass.colorAttachments[0].storeAction = .store
        controlPass.colorAttachments[0].clearColor=MTLClearColorMake(0,0,0,0)
        controlClear.makeRenderCommandEncoder(descriptor:controlPass)!.endEncoding();controlClear.commit();controlClear.waitUntilCompleted()
        CATransaction.begin();CATransaction.setDisableActions(true)
        let controlRoot=CALayer();controlRoot.frame=root.bounds
        let controlContent=CALayer();controlContent.bounds=content.bounds;controlContent.position=content.position;controlContent.transform=content.transform;controlContent.backgroundColor=NSColor.white.cgColor
        let controlMask=sourceMapMask();controlMask.colors=[NSColor.clear.cgColor,NSColor.clear.cgColor,NSColor.clear.cgColor]
        controlContent.mask=controlMask;controlRoot.addSublayer(controlContent);CATransaction.commit()
        let controlRenderer=CARenderer(mtlTexture:controlTarget,options:[kCARendererMetalCommandQueue:queue,kCARendererColorSpace:srgb])
        controlRenderer.layer=controlRoot;controlRenderer.bounds=controlRoot.bounds;CATransaction.flush()
        controlRenderer.beginFrame(atTime:0,timeStamp:nil);controlRenderer.addUpdate(controlRoot.bounds);controlRenderer.render();controlRenderer.endFrame()
        let controlDone=queue.makeCommandBuffer()!;controlDone.commit();controlDone.waitUntilCompleted()
        var control=[UInt8](repeating:0,count:side*side*4);controlTarget.getBytes(&control,bytesPerRow:side*4,from:MTLRegionMake2D(0,0,side,side),mipmapLevel:0)
        let controlMaximum=stride(from:3,to:control.count,by:4).map{control[$0]}.max()!
        if scale==2 {try Data(stride(from:3,to:bytes.count,by:4).map{bytes[$0]}).write(to:directory.appendingPathComponent("feather.alpha"))}
        masks.append(["scale":scale,"pixelWidth":side,"pixelHeight":side,"maximumByteResidual":maximum,"meanByteResidual":sum/Double(side*side),"pixelsOverOneByte":different,"samples":samples,"sourceSettings":settings,"cornerAlpha":bytes[3],"transparentControlMaximumAlpha":controlMaximum])
    }
    func components(_ color:NSColor)->[Double] {let converted=color.usingColorSpace(.sRGB)!;return [converted.redComponent,converted.greenComponent,converted.blueComponent,converted.alphaComponent].map{Double($0)}}
    let colors:[String:Any] = ["calibratedGray":[0.12,0.81,0.95,0.13,0.08,0.94,0.06].map{["white":$0,"sRGB":components(NSColor(white:$0,alpha:1))]},"yellow":components(WorldMapPinArtwork.yellow),"green":components(WorldMapPinArtwork.green)]
    let report:[String:Any] = ["images":images,"colors":colors,"maskProof":["renderer":"actual detached CARenderer Metal sRGB","screenCaptured":false,"usesAppOrWindow":false,"formula":"clamp((216-hypot(pixelCenter/scale-220))/9,0,1)","frames":masks]]
    try JSONSerialization.data(withJSONObject:report,options:[.sortedKeys]).write(to:directory.appendingPathComponent("output.json"))
    print("Exported three unchanged-size original player images and two detached radial-mask frames")
 }
}
