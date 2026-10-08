import AppKit
import QuartzCore
import CryptoKit
import simd

// Match SystemHUDView's AppKit-provided flipped backing ancestry without
// constructing that view or any of its live dependencies.
final class ChromeReferenceBackingView:NSView { override var isFlipped:Bool { true } }

@main enum DesktopChromeReference {
    static func check(_ value: @autoclosure () -> Bool,_ message:String) throws { if !value(){throw HUDSourceError.invalid(message)} }
    static func write(_ value:Any,_ url:URL) throws {try JSONSerialization.data(withJSONObject:value,options:[.prettyPrinted,.sortedKeys,.withoutEscapingSlashes]).write(to:url,options:.atomic)}
    static func matrix(_ m:simd_double4x4)->[[Double]] {(0..<4).map { c in (0..<4).map { m[c][$0] } }}
    static func rect(_ r:HUDSourceRect)->[String:Any] {["origin":[r.origin.x,r.origin.y],"size":[r.size.x,r.size.y]]}
    final class FakeTimer:HUDClockTimer {func invalidate(){}}
    static func reading(_ format:HUDClockFormat)->HUDClockReading {
        let clock=HUDClock(now:{Date(timeIntervalSince1970:1728307455)},timeZone:{TimeZone(secondsFromGMT:0)!},scheduleTimer:{_,_ in FakeTimer()})
        clock.setFormat(format);clock.setActive(true);clock.setActive(false);return clock.reading!
    }
    static func png(_ layer:CALayer,size:CGSize,to url:URL)throws {
        let scale:CGFloat=2,width=Int(size.width*scale),height=Int(size.height*scale)
        guard let c=CGContext(data:nil,width:width,height:height,bitsPerComponent:8,bytesPerRow:0,
            space:CGColorSpace(name:CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue) else {throw HUDSourceError.invalid("Chrome bitmap")}
        c.translateBy(x:0,y:CGFloat(height));c.scaleBy(x:scale,y:-scale)
        let transform=layer.transform,position=layer.position,anchor=layer.anchorPoint
        CATransaction.begin();CATransaction.setDisableActions(true);layer.transform=CATransform3DIdentity;layer.anchorPoint = .zero;layer.position = .zero
        layer.render(in:c);layer.transform=transform;layer.anchorPoint=anchor;layer.position=position;CATransaction.commit()
        guard let image=c.makeImage(),let bytes=NSBitmapImageRep(cgImage:image).representation(using:.png,properties:[:]) else {throw HUDSourceError.invalid("Chrome PNG")}
        try bytes.write(to:url,options:.atomic)
    }
    static func run()throws {
        try check(Thread.isMainThread,"Main thread required")
        try check(ProcessInfo.processInfo.arguments.contains("--ui-test")&&ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil,"Use isolated wrapper")
        guard let at=CommandLine.arguments.firstIndex(of:"--output"),at+1<CommandLine.arguments.count else {throw HUDSourceError.invalid("--output required")}
        let output=URL(fileURLWithPath:CommandLine.arguments[at+1],isDirectory:true)
        NSApplication.shared.setActivationPolicy(.prohibited);NSApp.appearance=NSAppearance(named:.darkAqua)
        var config=AppConfiguration.defaults;config.language = .english;config.theme = .dark;config.ambientAnimation=false;config.reduceMotion=true
        HUDRuntimeAppearance.configuration=config
        let encoder=try ModuleReferenceLayerEncoder(output:output)
        let chrome=ChromeSourceFixture();chrome.configuration=config;chrome.layout();chrome.applyAppearance(dark:true)
        let backing=ChromeReferenceBackingView(frame:CGRect(x:0,y:0,width:1280,height:800));backing.wantsLayer=true
        backing.layer!.addSublayer(chrome.canvas)
        try check(backing.window==nil && chrome.clockTime.contentsAreFlipped(),"Detached source text has the original AppKit backing orientation")
        var styles:[[String:Any]]=[]
        let readings:[HUDClockReading?]=[nil,reading(.twentyFourHour),reading(.twelveHour),HUDClockReading(time:"00:00:00",date:"MON Oct 7")]
        for (sample,value) in readings.enumerated(){for style in HUDClockStyle.allCases {
            chrome.configuration.clockStyle=style;chrome.headerClock.reading=value;chrome.clockHovered=sample%2==1
            chrome.workModeController.snapshot.phase=sample==1 ? .running:sample==2 ? .paused:.idle
            chrome.updateWorkPresentation(force:true);chrome.updateStatusPanel()
            let name="clock-\(style.rawValue)-\(sample)"
            styles.append(["name":name,"style":style.rawValue,"reading":value.map {["time":$0.time,"date":$0.date]} ?? NSNull() as Any,
                "hover":chrome.clockHovered,"workPhase":sample==1 ? "running":sample==2 ? "paused":"idle",
                "time":try encoder.encode(chrome.clockTime,id:name+"/time"),"date":try encoder.encode(chrome.clockDate,id:name+"/date"),
                "badge":try encoder.encode(chrome.workBadge,id:name+"/badge"),"artwork":try encoder.encode(chrome.clockStyleArtwork.layer,id:name+"/artwork"),
                "selection":try encoder.encode(chrome.clockStyleArtwork.selection,id:name+"/selection"),
                "status":try encoder.encode(chrome.statusPanel,id:name+"/status"),"png":name+".png"])
            try png(chrome.statusPanel,size:CGSize(width:340,height:145),to:output.appendingPathComponent(name+".png"))
        }}
        var projections:[[String:Any]]=[],bindings:[String:Any]=[:],lastCenter=CATransform3DIdentity,lastStatus=CATransform3DIdentity
        for size in [CGSize(width:1280,height:800),CGSize(width:1920,height:1080)] {
            let view=try HUDSourceWatchView(frame:CGRect(origin:.zero,size:size),desktopMode:true,desktopNavigationEntries:HUDDesktopWatchNavigation.entries(shortcuts:[]))
            defer{view.conceal()}
            view.pointerLocationProvider={CGPoint(x:size.width/2,y:size.height/2)};view.isDesktopPointerLocked={true}
            var profile=UserProfile(awakeningDate:Date(timeIntervalSince1970:0),uid:"1000000000");profile.name="Endministrator";profile.tag="0000"
            view.setDesktopProfile(profile,avatar:nil,background:nil);view.showStable();view.layout()
            var center:CATransform3D?,status:CATransform3D?
            view.onDesktopCenterPlane={center=$0};view.onDesktopStatusPlane={status=$0}
            for (index,time) in [0.137,0.419,view.document.animation.entrance.lastKeyTime].enumerated(){
                let rotation=index==1 ? HUDSourceQuaternion(0.02,-0.04,0.01,0.99895):view.cameraModel.rootRotation
                let base=try view.cameraModel.frame(screenSize:SIMD2(Double(size.width),Double(size.height)),localRotation:rotation)
                let offset:SIMD2<Double>=index==2 ? SIMD2(0.05,-0.04):.zero,scale:Double=index==2 ? 1.12:1
                let camera=HUDSourceWatchCamera.Frame(camera:base.camera,
                    worldRoot:base.worldRoot*HUDSourceGeometry.translation(SIMD3(base.layout.canvasSize.x*offset.x,-base.layout.canvasSize.y*offset.y,0))*HUDSourceGeometry.scale(SIMD3(repeating:scale)),layout:base.layout)
                var pose=try view.document.animation.pose(entranceTime:time,ambientTime:nil,exitTime:nil,canvasResolution:camera.layout.canvasSize)
                view.applyDesktopButtons(to:&pose,at:0,reduceMotion:true,forceRebuild:true)
                let frame=try view.frameBuilder.build(pose:pose,worldRoot:camera.worldRoot,forceRebuild:true)
                let info=try view.chromeReferenceProjection(frame,camera);bindings=["centerNodeID":info["centerNodeID"]!,"statusNodeID":info["statusNodeID"]!]
                let selected=try ["centerNodeID","statusNodeID"].map { key -> [String:Any] in
                    let id=HUDSourceID(rawValue:info[key] as! String);guard let n=frame.node(id) else {throw HUDSourceError.invalid("Chrome source node")}
                    return ["id":id.rawValue,"world":matrix(n.worldMatrix),"rect":n.rect.map(rect) ?? NSNull() as Any]
                }
                projections.append(["viewport":[0,0,size.width,size.height],"entranceTime":time,"hudScale":scale,"hudOffset":[offset.x,offset.y],
                    "view":matrix(camera.camera.view),"projection":matrix(camera.camera.projection),"worldRoot":matrix(camera.worldRoot),"nodes":selected,
                    "center":center.map(ModuleReferenceLayerEncoder.transform) ?? NSNull() as Any,"status":status.map(ModuleReferenceLayerEncoder.transform) ?? NSNull() as Any,
                    "unitsPerPoint":info["unitsPerPoint"]!])
                if let center{lastCenter=center};if let status{lastStatus=status}
            }
            try check(view.window==nil && !view.hasDisplayTimerForVerification && view.sourceCursorSetCountForVerification==0 && !view.backdropPreparingForVerification,"Detached chrome projection has no window/timer/cursor/capture")
        }
        var layouts:[[String:Any]]=[]
        for viewport in [CGSize(width:800,height:600),CGSize(width:1280,height:800),CGSize(width:1920,height:1080)] {
            for scale in [0.8,1.0,1.3] {for module in HUDModule.allCases {
                chrome.bounds=CGRect(origin:.zero,size:viewport);chrome.configuration.hudScale=scale
                chrome.configuration.hudOffsetX=0.05;chrome.configuration.hudOffsetY = -0.04;chrome.selectedModule=module;chrome.layout()
                chrome.applySourceStatusProjection(lastStatus)
                let centerSpatial=HUDMotionMath.sourcePlaneTransform(lastCenter,origin:chrome.designOrigin,scale:chrome.designScale)
                let local=[CGPoint.zero,CGPoint(x:20,y:30),CGPoint(x:module.contentFrame.width,y:module.contentFrame.height)]
                let screen=local.map { point -> [CGFloat] in
                    let design=point.applying(CGAffineTransform(translationX:module.contentFrame.minX,y:module.contentFrame.minY))
                    let placed=chrome.reportRect(CGRect(origin:design,size:.zero)).origin
                    return ModuleReferenceLayerEncoder.point(HUDMotionMath.project(placed,through:lastCenter))
                }
                layouts.append(["viewport":[0,0,viewport.width,viewport.height],"hudScale":scale,"hudOffset":[0.05,-0.04],"module":module.rawValue,
                    "designScale":chrome.designScale,"designOrigin":ModuleReferenceLayerEncoder.point(chrome.designOrigin),"canvasPosition":ModuleReferenceLayerEncoder.point(chrome.canvas.position),
                    "reportScale":chrome.reportScale,"reportCenterY":chrome.reportCenterY,"footerText":ModuleReferenceLayerEncoder.rect(chrome.hintLabel.frame),
                    "sourceCenter":ModuleReferenceLayerEncoder.transform(lastCenter),"sourceStatus":ModuleReferenceLayerEncoder.transform(lastStatus),
                    "centerSpatial":ModuleReferenceLayerEncoder.transform(centerSpatial),"statusLocal":ModuleReferenceLayerEncoder.transform(chrome.statusPanel.transform),
                    "moduleLocalPoints":local.map(ModuleReferenceLayerEncoder.point),"moduleScreenPoints":screen])
            }}
        }
        chrome.clockPage.transition(forward:true,animated:true)
        guard let transition=chrome.clockPage.page.animation(forKey:kCATransition) as? CATransition else {throw HUDSourceError.invalid("Clock transition missing")}
        var c1:[Float]=[0,0],c2:[Float]=[0,0];transition.timingFunction!.getControlPoint(at:1,values:&c1);transition.timingFunction!.getControlPoint(at:2,values:&c2)
        chrome.clockPage.cancelTransition()
        let result:[String:Any]=["schemaVersion":1,"scope":"Extracted current Mac chrome methods and original clock artwork; no SystemHUDView instance",
            "bindings":bindings,"styles":styles,"projections":projections,"layouts":layouts,
            "header":try encoder.encode(chrome.header,id:"chrome.header"),"footer":try encoder.encode(chrome.footer,id:"chrome.footer"),
            "clockTransition":["type":transition.type.rawValue,"subtype":transition.subtype!.rawValue,"duration":transition.duration,"controlPoints":[c1,c2],"pageClip":ModuleReferenceLayerEncoder.rect(HUDClockPageViewport.contentRect)],
            "safety":["systemHUDViewConstructed":false,"windowCreated":false,"OSClockTimersCreated":0,"liveProviders":false,"cursorSets":0],
            "unsupported":encoder.unsupported,"rasterAssets":encoder.rasterAssets,
            "limits":["SystemHUDView provider lifecycle intentionally not instantiated","Battery badge and account gauge omitted","CATransition displacement/pixel phases not yet verified","Cross-OS font raster equality is not claimed"]]
        try write(result,output.appendingPathComponent("chrome.json"));print("Exported source chrome: \(styles.count) clock states, \(projections.count) actual projections, \(layouts.count) layouts")
    }
    static func main(){do{try run()}catch{fputs("Desktop chrome export failed: \(error)\n",stderr);exit(1)}}
}
