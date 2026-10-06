import AppKit
import ImageIO
import AVFoundation

/// Exercises real retained controls with disposable PNGs and an offscreen host.
/// It never opens a file chooser, writes production preferences or plays audio.
enum MediaAssemblyCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func wait(_ predicate: () -> Bool) {
            let deadline = Date().addingTimeInterval(8)
            while !predicate(), Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(predicate(), "Media Assembly canvas fixture finishes its asynchronous operation")
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("MediaAssemblyCanvas-\(UUID())", isDirectory: true)
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        func writeImage(_ name: String, red: CGFloat) -> URL {
            let context = CGContext(data: nil, width: 24, height: 16, bitsPerComponent: 8, bytesPerRow: 96,
                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.setFillColor(CGColor(red: red, green: 0.4, blue: 0.2, alpha: 1))
            context.fill(CGRect(x: 0, y: 0, width: 24, height: 16))
            let url = directory.appendingPathComponent(name)
            let destination = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil)!
            CGImageDestinationAddImage(destination, context.makeImage()!, nil)
            precondition(CGImageDestinationFinalize(destination)); return url
        }
        let firstURL = writeImage("first.png", red: 0.8), secondURL = writeImage("second.png", red: 0.1)
        let originalFirst = try! Data(contentsOf: firstURL), originalSecond = try! Data(contentsOf: secondURL)
        let _ = NSApplication.shared
        let host = NSView(frame: CGRect(x: 0, y: 0, width: 640, height: 640)); host.wantsLayer = true
        let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 640, height: 640),
            styleMask: .borderless, backing: .buffered, defer: false)
        window.isReleasedWhenClosed = false; window.contentView = host
        defer { window.close() }
        let canvas = MediaAssemblyCanvas(controller: MediaAssemblyController())
        func descendants(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(descendants) }
        canvas.updateRenderScale(3)
        check(canvas.actions.isEmpty && !descendants(canvas.layer).contains { $0 is AVPlayerLayer },
              "An unopened media panel does not build controls or initialize AVPlayerLayer when the HUD sets scale")
        let model = canvas.controller
        let input = HUDMediaAssemblyInteraction(canvas: canvas, host: host)
        defer { input.deactivate() }
        let transform = CGAffineTransform(a: 1.1, b: 0.12, c: 0.08, d: 0.95, tx: 24, ty: 20)
        input.project = { $0.applying(transform) }; input.unproject = { $0.applying(transform.inverted()) }
        host.layer!.addSublayer(canvas.makeContent(for: .mediaAssembly, style: .init(dark: true, accent: .systemGreen, contentsScale: 2)))
        input.setActive(true)
        check(!descendants(canvas.layer).contains { $0 is AVPlayerLayer }, "The empty media panel needs no video presentation layer")
        check(canvas.actions.first { $0.id == "export" }?.enabled == false, "Empty Media Assembly cannot export")
        let openButton = canvas.actions.first { $0.id == "open" }!.rect, closeButton = canvas.actions.first { $0.id == "closeMedia" }!
        check(!closeButton.enabled && closeButton.rect.minY > openButton.maxY && closeButton.rect.maxX == openButton.maxX,
              "Compact media close control sits directly below Open and is disabled without a source")
        model.importURL(firstURL)
        wait { !model.isBusy && model.preview != nil }
        check(!descendants(canvas.layer).contains { $0 is AVPlayerLayer }, "Image editing does not initialize video presentation")
        let firstID = model.document!.id
        let initialPreview = model.preview
        let center = CGPoint(x: MediaAssemblyCanvas.previewRect.midX, y: MediaAssemblyCanvas.previewRect.midY)
        check(canvas.zoom(by: 10, at: center) && canvas.viewport.zoom == 10, "Media preview supports 10x zoom")
        check(model.preview === initialPreview && model.pendingPreviewCount == 0, "View zoom reuses its texture without CI rendering")
        canvas.mouseDown(at: center); canvas.mouseDragged(to: CGPoint(x: center.x + 70, y: center.y + 35)); canvas.mouseUp()
        check(canvas.viewport.pan.x != 0 && canvas.viewport.pan.y != 0, "Zoomed image pans directly on the stage")
        canvas.perform("reset")
        check(canvas.viewport.zoom == 1 && canvas.viewport.pan == .zero && canvas.imageRect.intersects(MediaAssemblyCanvas.previewRect), "Reset repaints the view even when media adjustments are already default")
        canvas.perform("filters")
        check(canvas.drawer == .filters && canvas.actions.contains { $0.id == "filter:sp_filter_1" }, "Original filters appear in the left Photo Mode grid")
        check(canvas.actions.filter { $0.id.hasPrefix("filter:") }.allSatisfy { MediaAssemblyCanvas.presetRect.contains($0.rect) }, "Partially visible preset cells keep hit bounds within the clipped drawer")
        canvas.perform("filters"); canvas.perform("stickers")
        _ = canvas.scroll(at: CGPoint(x: 60,y: 140),deltaY: 1000)
        check(canvas.drawerOffset > 0 && canvas.actions.contains { $0.id == "sticker:sticker_giftpack_1" }, "All sticker rows remain accessible by scrolling")
        canvas.perform("sticker:sticker_1")
        check(model.adjustments.stickers.count == 1 && canvas.selectedStickerID != nil && canvas.drawer == nil, "Selecting an original sticker creates a directly editable item and clears the drawer")
        let s = model.adjustments.stickers[0], r = MediaAssemblyViewport.stickerRect(s,in:canvas.imageRect)
        canvas.mouseDown(at: CGPoint(x:r.midX,y:r.midY)); canvas.mouseDragged(to: CGPoint(x:r.midX+40,y:r.midY+20)); canvas.mouseUp()
        check(model.adjustments.stickers[0].x > s.x && model.adjustments.stickers[0].y > s.y, "Sticker position changes with direct drag")
        let moved = model.adjustments.stickers[0], mr = MediaAssemblyViewport.stickerRect(moved,in:canvas.imageRect)
        canvas.mouseDown(at: CGPoint(x:mr.maxX,y:mr.maxY)); canvas.mouseDragged(to: CGPoint(x:mr.maxX+20,y:mr.maxY+20)); canvas.mouseUp()
        check(model.adjustments.stickers[0].size > moved.size, "Corner handles resize stickers without a slider")
        let scaled = model.adjustments.stickers[0], sr = MediaAssemblyViewport.stickerRect(scaled,in:canvas.imageRect)
        canvas.mouseDown(at: CGPoint(x:sr.midX,y:sr.minY-17)); canvas.mouseDragged(to: CGPoint(x:sr.maxX+17,y:sr.midY)); canvas.mouseUp()
        check(abs(model.adjustments.stickers[0].rotation - 90) < 0.01, "Rotation handle applies clockwise quarter-turn around the sticker center")
        check(model.preview === initialPreview && model.pendingPreviewCount == 0, "Direct sticker transforms never reprocess the base image")
        canvas.removeSelectedSticker()
        check(model.adjustments.stickers.isEmpty && canvas.selectedStickerID == nil, "Selected sticker removal does not affect base edits")
        var pure = MediaAssemblyViewport()
        pure.magnify(by:100,at:center,size:CGSize(width:24,height:16),in:MediaAssemblyCanvas.previewRect)
        check(pure.zoom == 20, "Preview zoom is bounded at 20x")
        pure.move(by:CGPoint(x:100000,y:100000),size:CGSize(width:24,height:16),in:MediaAssemblyCanvas.previewRect)
        let bounded = pure.imageRect(size:CGSize(width:24,height:16),in:MediaAssemblyCanvas.previewRect)
        check(bounded.minX <= MediaAssemblyCanvas.previewRect.minX && bounded.minY <= MediaAssemblyCanvas.previewRect.minY, "Pan cannot reveal an empty outer edge")

        check(canvas.actions.first { $0.id == "export" }?.enabled == true, "A loaded source enables Export")

        canvas.perform("export")
        let exportMenu = input.secondaryMenu as! MediaAssemblyControlMenu
        check(exportMenu.artwork.superlayer === canvas.layer && abs(exportMenu.bounds.width - exportMenu.contentSize.width) < 0.000001 && abs(exportMenu.bounds.height - exportMenu.contentSize.height) < 0.000001,
              "Export menu artwork remains on the central tilted plane with logical content bounds")
        let anchor = canvas.actions.first { $0.id == "export" }!.rect
        check(exportMenu.artwork.frame.maxY < anchor.minY && exportMenu.artwork.frame.maxX <= canvas.layer.bounds.maxX,
              "Export menu anchors above its bottom-right button and remains inside the central content")
        let logical = CGRect(origin: exportMenu.artwork.position, size: exportMenu.contentSize)
        check(exportMenu.frame == logical.applying(transform), "Native menu accessibility frame follows the projected bounds")
        let inside = CGPoint(x: logical.midX, y: logical.midY).applying(transform)
        check(input.hitTestMenu(at: inside) === exportMenu, "Tilted menu hit testing inverse-projects an interior point")
        check(input.hitTestMenu(at: CGPoint(x: -100, y: -100)) == nil, "Tilted menu does not claim points outside its logical bounds")
        exportMenu.perform("overwrite")
        let staleConfirmation = input.secondaryMenu as! MediaAssemblyControlMenu
        check(staleConfirmation !== exportMenu && staleConfirmation.items.contains { $0.id == "confirm" },
              "Overwrite requires a separate retained confirmation before touching a source")

        model.importURL(secondURL)
        check(model.isBusy && model.document?.id == firstID && canvas.actions.first { $0.id == "export" }?.enabled == false,
              "Starting replacement import disables Export while retaining the outgoing source")
        staleConfirmation.perform("confirm")
        check(!model.isExporting && model.lastExport == nil, "An old overwrite confirmation cannot export during replacement import")
        wait { !model.isBusy && model.document?.id != firstID && model.preview != nil }
        staleConfirmation.perform("confirm")
        check(!model.isExporting && model.lastExport == nil, "An old confirmation cannot overwrite after a different document becomes current")
        check((try! Data(contentsOf: firstURL)) == originalFirst && (try! Data(contentsOf: secondURL)) == originalSecond,
              "Both original files remain byte-for-byte unchanged after stale overwrite callbacks")

        var adjustments = model.adjustments; adjustments.crop.width = 0.5; model.updateAdjustments(adjustments)
        wait { model.preview?.width == 12 }
        canvas.perform("tools"); canvas.perform("crop")
        check(input.secondaryMenu == nil && canvas.activeTool == .crop && canvas.inlineParameters.isEmpty,
              "Crop replaces the tool grid and has no slider menu")
        wait { model.preview?.width == 24 }
        check(model.adjustments.crop.width == 0.5 && model.isCropPreview,
              "Full-source crop presentation preserves the actual saved crop")
        let renders = model.completedPreviewCount
        let cropStart = canvas.cropHandlePoints[3]
        canvas.mouseDown(at:cropStart); canvas.mouseDragged(to:CGPoint(x:cropStart.x+canvas.imageRect.width*0.2,y:cropStart.y-canvas.imageRect.height*0.2)); canvas.mouseUp()
        check(abs(model.adjustments.crop.width-0.7) < 0.00001 && abs(model.adjustments.crop.height-0.8) < 0.00001,
              "Direct crop corners expand an existing crop and resize both dimensions")
        check(model.pendingPreviewCount == 0 && model.completedPreviewCount == renders,
              "Crop pointer movement only updates retained handles and schedules no pixel work")
        let crop = MediaAssemblyCrop(x:0.2,y:0.1,width:0.4,height:0.7)
        for turn in 0..<4 { for mirrored in [false,true] {
            let display = MediaAssemblyViewport.displayCrop(crop,quarterTurns:turn,mirrored:mirrored)
            let restored = MediaAssemblyViewport.sourceCrop(display,quarterTurns:turn,mirrored:mirrored)
            check(abs(restored.x-crop.x)<0.00001 && abs(restored.y-crop.y)<0.00001 && abs(restored.width-crop.width)<0.00001 && abs(restored.height-crop.height)<0.00001,
                  "Crop handles map back to the original source for every rotation/mirror combination")
        } }
        let storedCrop = model.adjustments.crop
        canvas.perform("adjust")
        check(!model.isCropPreview && model.adjustments.crop == storedCrop && canvas.inlineParameters.count == 8 && input.secondaryMenu == nil,
              "Entering color controls closes crop presentation without changing the crop")
        let first = canvas.parameterControls[0].rect
        canvas.mouseDown(at:CGPoint(x:first.maxX-0.001,y:first.midY)); canvas.mouseUp()
        check(model.adjustments.brightness == 1,"Inline color slider routes directly to the edit controller")
        canvas.setParameter("brightness",to:0)
        let visible = canvas.parameterControls.map { $0.parameter.id }
        _ = canvas.scroll(at:CGPoint(x:60,y:200),deltaY:1000)
        check(canvas.drawerOffset > 0 && canvas.parameterControls.contains { $0.parameter.id == "exposure" } && !visible.contains("exposure"),
              "All color rows are reachable through the bounded smoothly scrolling drawer")
        check(canvas.parameterControls.allSatisfy { MediaAssemblyCanvas.inlineScrollRect.contains($0.rect) },"Scrolled controls cannot claim points outside their clip")
        canvas.perform("curves"); check(canvas.inlineParameters.count == 5 && input.secondaryMenu == nil,"Curves replaces the same inline drawer")
        canvas.perform("levels"); canvas.setParameter("white",to:0.4); canvas.setParameter("black",to:0.9)
        check(abs(model.adjustments.levelsBlack-0.39)<0.00001 && input.secondaryMenu == nil,"Inline levels preserve coupled limits")
        canvas.perform("crop"); _ = canvas.zoom(by:3,at:center)
        check(canvas.actions.first { $0.id == "closeMedia" }?.enabled == true,"Loaded photo enables the retained close control")
        check(canvas.mouseDown(at:CGPoint(x:closeButton.rect.midX,y:closeButton.rect.midY)),"The projected close button routes its pointer click")
        check(model.document == nil && model.preview == nil && model.adjustments == MediaAssemblyAdjustments() && !model.isCropPreview && !model.isBusy && model.pendingPreviewCount == 0,
              "Closing a photo clears source, preview, pending work and temporary edit parameters")
        check(canvas.viewport.zoom == 1 && canvas.viewport.pan == .zero && canvas.activeTool == nil && canvas.drawer == nil && canvas.selectedStickerID == nil && !canvas.isDragging,
              "Closing a source resets only the Media Assembly viewport and input state")
        check((try! Data(contentsOf:firstURL)) == originalFirst && (try! Data(contentsOf:secondURL)) == originalSecond,
              "Closing edited media leaves original image bytes unchanged")
        var releasedImport = false
        model.importURL(secondURL,release:{ DispatchQueue.main.async { releasedImport = true } })
        check(model.isBusy && canvas.actions.first { $0.id == "closeMedia" }?.enabled == true,"Close is available during an initial asynchronous import")
        canvas.perform("closeMedia"); wait { releasedImport }
        RunLoop.main.run(until:Date().addingTimeInterval(0.03))
        check(model.document == nil && !model.isBusy && model.preview == nil,"An invalidated import cannot restore media after Close")
        let movie = directory.appendingPathComponent("trim.mov"); try! writeMovie(to:movie)
        let originalMovie = try! Data(contentsOf:movie)
        model.importURL(movie); wait { !model.isBusy && model.document?.isVideo == true && model.preview != nil }
        canvas.perform("trim")
        check(input.secondaryMenu == nil && canvas.activeTool == .trim && canvas.inlineParameters.isEmpty,
              "Trim uses one dual-thumb range rail instead of two slider menus")
        let range = canvas.trimRangeRect,duration = model.document!.duration
        canvas.mouseDown(at:CGPoint(x:range.minX,y:range.midY)); canvas.mouseDragged(to:CGPoint(x:range.minX+range.width*0.2,y:range.midY))
        check(abs(model.adjustments.trimStart-duration*0.2)<0.00001 && abs(model.currentTime-model.adjustments.trimStart)<0.00001,
              "Dragging the start thumb immediately scrubs its exact new source time before mouse-up")
        canvas.mouseUp()
        canvas.mouseDown(at:CGPoint(x:range.maxX,y:range.midY)); canvas.mouseDragged(to:CGPoint(x:range.minX+range.width*0.7,y:range.midY))
        check(abs((model.adjustments.trimEnd ?? 0)-duration*0.7)<0.00001 && abs(model.currentTime-((model.adjustments.trimEnd ?? 0)-0.001))<0.00001,
              "Dragging the end thumb immediately scrubs the last valid frame inside the new endpoint")
        canvas.mouseUp()
        canvas.setTrimEndpoint(beginning:true,to:duration)
        check(abs((model.adjustments.trimEnd ?? 0)-model.adjustments.trimStart-0.05)<0.00001 && model.pendingPreviewCount <= 1,
              "Trim thumbs cannot cross and rapid scrubbing retains only the latest pending preview")
        canvas.perform("export")
        let retiringMenu = input.secondaryMenu!
        input.deactivate()
        check(input.secondaryMenu == nil && !input.capturesPointer && !input.isPresentingPanel
                && retiringMenu.superview == nil && retiringMenu.artwork.superlayer == nil,
              "Hiding the module removes native menu input and retained menu artwork")
        check(model.player == nil && !model.hasActivePlaybackObserver && model.pendingPreviewCount == 0 && model.preview == nil,
              "Closing Media Assembly releases preview state, pending work and all playback observers")
        check(host.subviews.filter { $0 is NSControl }.allSatisfy(\.isHidden), "Closed module leaves no active accessibility controls")
        input.setActive(true); canvas.perform("trimReset"); model.togglePlayback()
        wait { model.isPlaying && model.player != nil && model.hasActivePlaybackObserver }
        canvas.perform("closeMedia")
        check(model.document == nil && model.player == nil && !model.isPlaying && !model.hasActivePlaybackObserver && model.pendingPreviewCount == 0,
              "Closing playing video pauses/releases the player and removes every playback observer")
        check(model.currentTime == 0 && model.adjustments == MediaAssemblyAdjustments() && model.preview == nil,
              "Video Close clears trim time and edit state without retaining the movie")
        check((try! Data(contentsOf:movie)) == originalMovie,"Closing video does not modify its source file")
        input.deactivate()
        check(!canvas.layer.sublayers!.contains { $0.name == "media.closingSource" },"Hiding cancels the finite close poster and releases its textures")
        return count
    }
    private static func writeMovie(to url:URL) throws {
        let writer = try AVAssetWriter(outputURL:url,fileType:.mov)
        let input = AVAssetWriterInput(mediaType:.video,outputSettings:[AVVideoCodecKey:AVVideoCodecType.h264,AVVideoWidthKey:64,AVVideoHeightKey:32])
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput:input,sourcePixelBufferAttributes:[kCVPixelBufferPixelFormatTypeKey as String:kCVPixelFormatType_32BGRA,kCVPixelBufferWidthKey as String:64,kCVPixelBufferHeightKey as String:32])
        writer.add(input); precondition(writer.startWriting()); writer.startSession(atSourceTime:.zero)
        for frame in 0..<24 {
            while !input.isReadyForMoreMediaData { RunLoop.main.run(until:Date().addingTimeInterval(0.001)) }
            var buffer:CVPixelBuffer?; precondition(CVPixelBufferPoolCreatePixelBuffer(nil,adaptor.pixelBufferPool!,&buffer) == kCVReturnSuccess)
            CVPixelBufferLockBaseAddress(buffer!,[])
            memset(CVPixelBufferGetBaseAddress(buffer!),Int32(30+frame*8),CVPixelBufferGetDataSize(buffer!)); CVPixelBufferUnlockBaseAddress(buffer!,[])
            precondition(adaptor.append(buffer!,withPresentationTime:CMTime(value:Int64(frame),timescale:12)))
        }
        input.markAsFinished(); writer.endSession(atSourceTime:CMTime(seconds:2,preferredTimescale:600))
        var done = false; writer.finishWriting { done = true }
        let deadline = Date().addingTimeInterval(8)
        while !done && Date()<deadline { RunLoop.main.run(until:Date().addingTimeInterval(0.01)) }
        precondition(done && writer.status == .completed)
    }

}
