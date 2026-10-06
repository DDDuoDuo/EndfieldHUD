import AppKit
import AVFoundation
import CoreImage
import ImageIO
import AudioToolbox

/// Real, generated PNG/video/audio fixtures in one disposable directory. No
/// applications, user media, real preferences or production stores are touched.
enum MediaAssemblyTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func wait(_ predicate: () -> Bool, seconds: Double = 10) {
            let end = Date().addingTimeInterval(seconds)
            while !predicate(), Date() < end { RunLoop.main.run(until: Date().addingTimeInterval(0.01)) }
            check(predicate(), "Media Assembly asynchronous operation completes")
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("MediaAssembly-\(UUID())", isDirectory: true)
        try! FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        let source = directory.appendingPathComponent("quadrants.png")
        let image = fixtureImage(width: 64, height: 32)
        write(image, to: source)
        let original = try! Data(contentsOf: source)
        let engine = MediaAssemblyEngine()
        let document = try! engine.open(source)
        check(!document.isVideo && document.pixelSize == CGSize(width: 64, height: 32), "Import records source metadata without copying the photograph")
        check(MediaAssemblyEngine.exportExtensions(for: document).contains("png"), "Image export formats are discovered from native encoders")
        var edit = MediaAssemblyAdjustments(); edit.crop = MediaAssemblyCrop(x: 0, y: 0, width: 0.5, height: 0.5)
        let crop = try! engine.preview(document, adjustments: edit, time: 0)
        check(crop.width == 32 && crop.height == 16 && red(pixel(crop, x: 12, y: 5)), "Normalized top-left crop selects the actual upper-left red source pixels")
        edit.rotationQuarterTurns = 1
        let rotated = try! engine.preview(document, adjustments: edit, time: 0)
        check(rotated.width == 16 && rotated.height == 32, "Quarter rotation swaps cropped dimensions without extra rounding pixels")
        edit = MediaAssemblyAdjustments(); edit.mirrored = true
        let mirrored = try! engine.preview(document, adjustments: edit, time: 0)
        check(green(pixel(mirrored, x: 4, y: 4)) && red(pixel(mirrored, x: 58, y: 4)), "Mirror reflects actual image pixels")
        var invalid = MediaAssemblyAdjustments(); invalid.crop.width = .nan
        check(!invalid.isValid, "Nonfinite crop values are rejected")
        invalid = MediaAssemblyAdjustments(); invalid.levelsBlack = 0.9; invalid.levelsWhite = 0.1
        check(!invalid.isValid, "Reversed levels cannot enter Core Image")
        invalid = MediaAssemblyAdjustments(); invalid.curve = [0, 1]
        check(!invalid.isValid, "Tone curve requires exactly five finite normalized points")
        let base = try! engine.preview(document, adjustments: MediaAssemblyAdjustments(), time: 0)
        let tonalURL = directory.appendingPathComponent("tonal.png")
        let tonal = CGContext(data: nil, width: 8, height: 8, bitsPerComponent: 8, bytesPerRow: 32, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        tonal.setFillColor(CGColor(red: 0.2, green: 0.35, blue: 0.5, alpha: 1)); tonal.fill(CGRect(x: 0, y: 0, width: 8, height: 8)); write(tonal.makeImage()!, to: tonalURL)
        let tonalDoc = try! engine.open(tonalURL), tonalBase = try! engine.preview(tonalDoc, adjustments: MediaAssemblyAdjustments(), time: 0)
        for mutation in [
            { (p: inout MediaAssemblyAdjustments) in p.brightness = -0.35 },
            { (p: inout MediaAssemblyAdjustments) in p.contrast = 0.3 },
            { (p: inout MediaAssemblyAdjustments) in p.saturation = 0 },
            { (p: inout MediaAssemblyAdjustments) in p.temperature = 3500 },
            { (p: inout MediaAssemblyAdjustments) in p.tint = 150 },
            { (p: inout MediaAssemblyAdjustments) in p.highlights = 0.1 },
            { (p: inout MediaAssemblyAdjustments) in p.shadows = 0.8 },
            { (p: inout MediaAssemblyAdjustments) in p.exposure = -1.5 },
            { (p: inout MediaAssemblyAdjustments) in p.curve = [0.1, 0.1, 0.2, 0.4, 0.8] },
            { (p: inout MediaAssemblyAdjustments) in p.levelsBlack = 0.2; p.levelsWhite = 0.85; p.levelsGamma = 2 }
        ] {
            var value = MediaAssemblyAdjustments(); mutation(&value)
            let output = try! engine.preview(tonalDoc, adjustments: value, time: 0)
            check(output.width == tonalBase.width && output.height == tonalBase.height && bytes(output) != bytes(tonalBase),
                  "Each native color/curve/level adjustment changes real pixels while retaining geometry")
        }
        var desaturated = MediaAssemblyAdjustments(); desaturated.saturation = 0
        let monochrome = pixel(try! engine.preview(document, adjustments: desaturated, time: 0), x: 8, y: 8)
        check(abs(monochrome.0 - monochrome.1) < 3 && abs(monochrome.1 - monochrome.2) < 3, "Saturation renders monochrome pixels rather than only changing UI state")
        var sticker = MediaAssemblyAdjustments(); sticker.stickers = [MediaAssemblySticker(kind: .sticker7, size: 0.8)]
        let stamped = try! engine.preview(document, adjustments: sticker, time: 0)
        check(bytes(stamped) != bytes(base), "Authentic original-game stickers are composited into photograph pixels")
        check(bytes(try! engine.preview(document, adjustments: sticker, time: 0, includeStickers: false)) == bytes(base),
              "Interactive base preview excludes decals so direct sticker layers never double-composite")
        check(MediaAssemblyFilter.allCases.count == 15 && MediaAssemblyStickerKind.allCases.count == 24,
              "Catalog exposes exactly the fourteen current filters, None, and twenty-four retained original stickers")
        for removed in ["sticker_mkting_1", "sticker_mkting_2", "sticker_mkting_4", "sticker_mkting_5"] {
            check(MediaAssemblyStickerKind(rawValue: removed) == nil,
                  "Removed promotional stickers cannot be selected or constructed")
            let encoded = try! JSONEncoder().encode(removed)
            check((try? JSONDecoder().decode(MediaAssemblyStickerKind.self, from: encoded)) == nil,
                  "An obsolete sticker identifier is rejected rather than silently replaced with unrelated art")
            check(MediaAssemblyAssetCatalog.resourceURL("stickers/\(removed).png") == nil,
                  "Removed stickers are excluded from the distributed resource set")
        }
        for kind in MediaAssemblyStickerKind.allCases {
            check((try? JSONDecoder().decode(MediaAssemblyStickerKind.self, from: JSONEncoder().encode(kind))) == kind,
                  "Retained original sticker identifiers round-trip without renumbering")
            let full = MediaAssemblyAssetCatalog.stickerImage(kind), thumbnail = MediaAssemblyAssetCatalog.stickerThumbnail(kind)
            check(full?.width == Int(kind.pixelSize.width) && full?.height == Int(kind.pixelSize.height)
                    && thumbnail != nil && max(thumbnail!.width, thumbnail!.height) <= 96,
                  "Every catalog sticker loads its authentic transparent image and bounded thumbnail")
        }
        let sample = lookupFixture(), lookupContext = CIContext(options: [.workingColorSpace: CGColorSpace(name: CGColorSpace.sRGB)!])
        for preset in MediaAssemblyFilter.allCases where preset != .none {
            let filtered = try! MediaAssemblyAssetCatalog.apply(preset, to: CIImage(cgImage: sample))
            let rendered = lookupContext.createCGImage(filtered, from: filtered.extent, format: .RGBA8, colorSpace: CGColorSpace(name: CGColorSpace.sRGB))!
            let data = try! Data(contentsOf: MediaAssemblyAssetCatalog.resourceURL("luts/\(preset.rawValue).rgb8")!)
            check(MediaAssemblyAssetCatalog.filterThumbnail(preset)?.width == 168,
                  "Each real LUT preset uses its supplied original icon")
            for (index,input) in [(0,(0,0,0)),(1,(255,255,255)),(2,(64,128,192)),(3,(200,90,30))] {
                let expected = lookupSample(data, rgb: input), actual = srgbPixel(rendered,x:index,y:0)
                check(abs(expected.0-actual.0) <= 3 && abs(expected.1-actual.1) <= 3 && abs(expected.2-actual.2) <= 3,
                      "Native LUT pixels agree with independent red-fastest trilinear sampling for \(preset.rawValue): expected \(expected), actual \(actual)")
            }
        }
        let blank = CIImage(color:CIColor(red:0,green:0,blue:0,alpha:0)).cropped(to:CGRect(x:0,y:0,width:64,height:64))
        var direct = MediaAssemblyAdjustments(); direct.stickers = [MediaAssemblySticker(kind:.sticker7,size:1)]
        let unturned = lookupContext.createCGImage(try! MediaAssemblyEngine.apply(direct,to:blank,includeStickers:true),from:blank.extent,format:.RGBA8,colorSpace:CGColorSpace(name:CGColorSpace.sRGB))!
        direct.stickers[0].rotation = 90
        let clockwise = lookupContext.createCGImage(try! MediaAssemblyEngine.apply(direct,to:blank,includeStickers:true),from:blank.extent,format:.RGBA8,colorSpace:CGColorSpace(name:CGColorSpace.sRGB))!
        let originalStamp = unturned.dataProvider!.data! as Data, rotatedStamp = clockwise.dataProvider!.data! as Data
        var largestRotationError = 0
        for y in 0..<64 { for x in 0..<64 { for c in 0..<4 {
            largestRotationError = max(largestRotationError,abs(Int(rotatedStamp[y*clockwise.bytesPerRow+x*4+c])-Int(originalStamp[(63-x)*unturned.bytesPerRow+y*4+c])))
        } } }
        check(largestRotationError <= 2,"Positive sticker rotation is clockwise in the top-left UI coordinate plane, matching direct layer handles")
        let translucent = CIImage(color:CIColor(red:0.25,green:0.5,blue:0.75,alpha:0.5)).cropped(to:CGRect(x:0,y:0,width:1,height:1))
        let translucentResult = lookupContext.createCGImage(try! MediaAssemblyAssetCatalog.apply(.filter3,to:translucent),from:translucent.extent,format:.RGBA8,colorSpace:CGColorSpace(name:CGColorSpace.sRGB))!
        let alpha = Int((translucentResult.dataProvider!.data! as Data)[3])
        check(abs(alpha-128)<=1,"Native LUT processing preserves source transparency instead of making the photo opaque")
        check(MediaAssemblyAssetCatalog.retainedCubeCount <= 2 && MediaAssemblyAssetCatalog.retainedImageBytes <= MediaAssemblyAssetCatalog.maximumImageCacheBytes,
              "Browsing all authentic presets retains only two LUTs and a bounded decoded-image cache")
        check(try! Data(contentsOf: source) == original, "Preview, filters and stickers never modify the source")
        func export(_ doc: MediaAssemblyDocument, _ value: MediaAssemblyAdjustments, _ target: URL, overwrite: Bool = false,
                    engine chosenEngine: MediaAssemblyEngine? = nil, ticket: MediaAssemblyExportTicket = MediaAssemblyExportTicket()) -> Result<URL, Error> {
            var result: Result<URL, Error>?
            (chosenEngine ?? engine).export(doc, adjustments: value, to: target, overwrite: overwrite, ticket: ticket) { value in DispatchQueue.main.async { result = value } }
            wait { result != nil }; return result!
        }
        var assembled = sticker; assembled.filter = .filter8
        let assembledURL = directory.appendingPathComponent("authentic-lut-sticker.png")
        check((try? export(document,assembled,assembledURL).get()) == assembledURL,
              "Real LUT and original transparent sticker export through the native photo encoder")
        let assembledExport = CGImageSourceCreateImageAtIndex(CGImageSourceCreateWithURL(assembledURL as CFURL,nil)!,0,nil)!
        check(bytes(assembledExport) == bytes(try! engine.preview(document,adjustments:assembled,time:0)),
              "Exported authentic filter and sticker pixels match the preview composition exactly")
        var photoEdit = MediaAssemblyAdjustments(); photoEdit.crop = MediaAssemblyCrop(x: 0, y: 0, width: 0.5, height: 0.5); photoEdit.rotationQuarterTurns = 1
        let photoOutput = directory.appendingPathComponent("edited.png")
        check((try? export(document, photoEdit, photoOutput).get()) == photoOutput, "Save As writes an actual native image file")
        let exportedPhoto = CGImageSourceCreateImageAtIndex(CGImageSourceCreateWithURL(photoOutput as CFURL, nil)!, 0, nil)!
        check(exportedPhoto.width == 16 && exportedPhoto.height == 32 && red(pixel(exportedPhoto, x: 5, y: 8)), "Full-resolution photo export applies crop and rotation to its pixels")
        check(try! Data(contentsOf: source) == original, "Save As preserves the original bytes")
        check((try? export(document, photoEdit, photoOutput).get()) == nil && (try! Data(contentsOf: source)) == original,
              "Existing destination is rejected without explicit overwrite")
        let cancelled = MediaAssemblyExportTicket(); cancelled.cancel()
        let cancelURL = directory.appendingPathComponent("cancelled.png")
        check((try? export(document, photoEdit, cancelURL, ticket: cancelled).get()) == nil && !FileManager.default.fileExists(atPath: cancelURL.path),
              "Cancellation before export creates no result")
        let lateTicket = MediaAssemblyExportTicket(), lateEngine = MediaAssemblyEngine(beforeCommit: { lateTicket.cancel() })
        check((try? export(document, photoEdit, source, overwrite: true, engine: lateEngine, ticket: lateTicket).get()) == nil
                && (try! Data(contentsOf: source)) == original,
              "Cancellation after encoding but before atomic commit preserves the original")
        let failEngine = MediaAssemblyEngine(beforeCommit: { try! Data("external change".utf8).write(to: photoOutput) })
        check((try? export(document, photoEdit, photoOutput, overwrite: true, engine: failEngine).get()) == nil
                && (try! String(contentsOf: photoOutput)) == "external change" && (try! Data(contentsOf: source)) == original,
              "An externally changed destination is not overwritten by a stale export")
        check((try? export(document, photoEdit, directory.appendingPathComponent("unsupported.gif")).get()) == nil,
              "Unsupported native output formats report failure instead of writing mismatched file bytes")
        let overwriteURL = directory.appendingPathComponent("overwrite.png"); try! original.write(to: overwriteURL)
        let overwriteDocument = try! engine.open(overwriteURL)
        check((try? export(overwriteDocument, photoEdit, overwriteURL, overwrite: true).get()) != nil
                && (try! Data(contentsOf: overwriteURL)) != original,
              "Confirmed overwrite atomically replaces the original only after encoding succeeds")
        check(try! FileManager.default.contentsOfDirectory(atPath: directory.path).allSatisfy { !$0.hasPrefix(".endfield-export-") },
              "Success, failure and cancellation remove all staged export files")
        let bigURL = directory.appendingPathComponent("large.png"); write(fixtureImage(width: 2304, height: 1536), to: bigURL)
        let large = try! engine.preview(engine.open(bigURL), adjustments: MediaAssemblyAdjustments(), time: 0)
        check(max(large.width, large.height) <= MediaAssemblyEngine.maximumPreviewDimension,
              "Large source photographs are downsampled before preview processing")
        let gifURL = directory.appendingPathComponent("animation.gif")
        let gif = CGImageDestinationCreateWithURL(gifURL as CFURL, "com.compuserve.gif" as CFString, 2, nil)!
        CGImageDestinationAddImage(gif, image, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.1]] as CFDictionary)
        CGImageDestinationAddImage(gif, fixtureImage(width: 64, height: 32), [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.1]] as CFDictionary)
        precondition(CGImageDestinationFinalize(gif))
        let gifBytes = try! Data(contentsOf: gifURL), gifDocument = try! engine.open(gifURL)
        check(gifDocument.isAnimated && (try? engine.preview(gifDocument, adjustments: MediaAssemblyAdjustments(), time: 0)) != nil,
              "Animated GIF can be explicitly edited as a static first-frame preview")
        check((try? export(gifDocument, MediaAssemblyAdjustments(), gifURL, overwrite: true).get()) == nil
                && (try! Data(contentsOf: gifURL)) == gifBytes,
              "GIF cannot be silently flattened by overwrite")
        check((try? export(gifDocument, MediaAssemblyAdjustments(), directory.appendingPathComponent("gif-first-frame.png")).get()) != nil,
              "GIF first-frame editing can save a clearly static PNG without touching the GIF")

        let movieURL = directory.appendingPathComponent("fixture.mov")
        try! writeMovie(to: movieURL)
        let movieBytes = try! Data(contentsOf: movieURL), movie = try! engine.open(movieURL)
        check(movie.isVideo && movie.duration >= 1.9 && movie.duration < 2.2, "Generated movie imports real native duration")
        var movieEdit = MediaAssemblyAdjustments(); movieEdit.crop = MediaAssemblyCrop(x: 0, y: 0, width: 0.5, height: 0.5)
        movieEdit.rotationQuarterTurns = 1; movieEdit.trimStart = 0.25; movieEdit.trimEnd = 1.25; movieEdit.saturation = 0
        let videoOutput = directory.appendingPathComponent("trimmed.mov")
        let videoResult = export(movie, movieEdit, videoOutput)
        if case .failure(let failure) = videoResult { fatalError("Native video export failed: \(failure)") }
        let asset = AVURLAsset(url: videoOutput), track = asset.tracks(withMediaType: .video).first!
        check(track.naturalSize == CGSize(width: 16, height: 32), "Video export uses cropped and rotated output dimensions")
        check(abs(CMTimeGetSeconds(asset.duration) - 1) < 0.15, "Video trim changes actual exported duration")
        check(asset.tracks(withMediaType: .audio).count == 1, "Native video export preserves the generated audio track")
        let generator = AVAssetImageGenerator(asset: asset); generator.appliesPreferredTrackTransform = true
        let frame = try! generator.copyCGImage(at: CMTime(seconds: 0.3, preferredTimescale: 600), actualTime: nil)
        let grey = pixel(frame, x: 5, y: 8)
        check(abs(grey.0 - grey.1) < 8 && abs(grey.1 - grey.2) < 8 && grey.0 > 20,
              "Exported video frames contain the requested native color filter")
        check(try! Data(contentsOf: movieURL) == movieBytes, "Video processing preserves original container bytes")
        let videoTicket = MediaAssemblyExportTicket(); videoTicket.cancel()
        check((try? export(movie, movieEdit, movieURL, overwrite: true, ticket: videoTicket).get()) == nil
                && (try! Data(contentsOf: movieURL)) == movieBytes, "Cancelled video overwrite preserves the source")

        let controller = MediaAssemblyController(); var events: [String] = []; controller.onEvent = { events.append($0) }
        controller.setActive(true); controller.importURL(source)
        wait { controller.document != nil && controller.preview != nil }
        for i in 0..<100 { controller.updateAdjustments { $0.brightness = Double(i % 20) / 100 } }
        check(controller.pendingPreviewCount <= 1, "Rapid slider edits coalesce to one latest pending preview")
        wait { controller.preview != nil && controller.adjustments.brightness == 0.19 }
        check(controller.previewPixelCount <= MediaAssemblyEngine.maximumPreviewDimension * MediaAssemblyEngine.maximumPreviewDimension,
              "Retained previews are bounded independently of original export resolution")
        controller.setActive(false)
        check(controller.player == nil && controller.preview == nil && !controller.hasActivePlaybackObserver,
              "Hiding releases decoded preview, player and all playback observers")
        check(events.filter { $0 == "edited" }.count == 1, "One hundred slider updates produce one edit event after the burst")
        controller.setActive(true); wait { controller.preview != nil }
        check(controller.adjustments.brightness == 0.19, "Reopening retains non-destructive session edits")
        let oldDocumentID = controller.document!.id
        let stalePanelOutput = directory.appendingPathComponent("stale-save-panel.png")
        let renderCount = controller.completedPreviewCount
        let directSticker = MediaAssemblySticker(kind: .sticker7)
        controller.updateAdjustments { $0.stickers = [directSticker] }
        for index in 0..<100 { controller.updateAdjustments { $0.stickers[0].x = Double(index) / 100 } }
        RunLoop.main.run(until: Date().addingTimeInterval(0.1))
        check(controller.completedPreviewCount == renderCount && controller.pendingPreviewCount == 0,
              "One hundred direct sticker moves do not schedule or finish a Core Image preview render")
        controller.importURL(overwriteURL)
        var busyExportRejected = false
        controller.export(to: stalePanelOutput, overwrite: false, expectedDocumentID: oldDocumentID) {
            if case .failure = $0 { busyExportRejected = true }
        }
        check(busyExportRejected && !controller.isExporting && !FileManager.default.fileExists(atPath: stalePanelOutput.path),
              "Replacement import rejects exporting the old document before the new import completes")
        wait { controller.document?.sourceURL == overwriteURL && controller.preview != nil }
        var staleExportRejected = false
        controller.export(to: stalePanelOutput, overwrite: false, expectedDocumentID: oldDocumentID) {
            if case .failure = $0 { staleExportRejected = true }
        }
        check(staleExportRejected && !controller.isExporting && !FileManager.default.fileExists(atPath: stalePanelOutput.path),
              "A save panel bound to a previous document cannot export its replacement into the stale destination")
        controller.updateAdjustments { $0.exposure = -1 }
        var reloadedExportFinished = false
        controller.export(to: overwriteURL, overwrite: true) { result in precondition((try? result.get()) != nil); reloadedExportFinished = true }
        wait { reloadedExportFinished && !controller.isBusy && controller.document?.identity == (try? MediaAssemblyFileIdentity.read(overwriteURL)) }
        check(controller.adjustments == MediaAssemblyAdjustments(), "Successful overwrite reloads source identity and resets applied edits to prevent double processing")
        controller.updateAdjustments { $0.brightness = -0.1 }; wait { controller.preview != nil }
        check(controller.error == nil, "Further edits after overwrite use the refreshed file identity")
        controller.importURL(movieURL); wait { controller.document?.isVideo == true && controller.preview != nil }
        controller.togglePlayback(); wait { controller.isPlaying && controller.player != nil }
        check(controller.hasActivePlaybackObserver, "Video playback owns an event-driven native progress observer only while needed")
        controller.setActive(false)
        check(!controller.isPlaying && controller.player == nil && !controller.hasActivePlaybackObserver, "Closing video editing stops decoding and observer callbacks")
        return count
    }
    private static func srgbPixel(_ image: CGImage, x: Int, y: Int) -> (Int,Int,Int) {
        // createCGImage(.RGBA8, sRGB) already contains encoded sRGB bytes.
        // NSBitmapImageRep.colorAt returns generic RGB NSColor here and would
        // apply an extra profile conversion, invalidating a numeric LUT check.
        let data = image.dataProvider!.data! as Data, offset = y*image.bytesPerRow+x*4
        return (Int(data[offset]),Int(data[offset+1]),Int(data[offset+2]))
    }
    private static func lookupFixture() -> CGImage {
        var values: [UInt8] = [0,0,0,255,255,255,255,255,64,128,192,255,200,90,30,255]
        return values.withUnsafeMutableBytes { p in
            CGContext(data: p.baseAddress,width:4,height:1,bitsPerComponent:8,bytesPerRow:16,
                      space:CGColorSpace(name: CGColorSpace.sRGB)!,bitmapInfo:CGImageAlphaInfo.premultipliedLast.rawValue)!.makeImage()!
        }
    }
    private static func lookupSample(_ bytes: Data, rgb: (Int,Int,Int)) -> (Int,Int,Int) {
        let scaled = [rgb.0,rgb.1,rgb.2].map { Double($0) / 255 * 31 }
        let low = scaled.map { Int(floor($0)) }, high = low.map { min(31,$0+1) }
        let weight = zip(scaled,low).map { $0 - Double($1) }
        var output = [Double](repeating:0,count:3)
        for b in 0...1 { for g in 0...1 { for r in 0...1 {
            let offset = (((b == 0 ? low[2] : high[2])*32+(g == 0 ? low[1] : high[1]))*32+(r == 0 ? low[0] : high[0]))*3
            let w = (r == 0 ? 1-weight[0] : weight[0])*(g == 0 ? 1-weight[1] : weight[1])*(b == 0 ? 1-weight[2] : weight[2])
            for c in 0..<3 { output[c] += Double(bytes[offset+c])*w }
        } } }
        return (Int(output[0].rounded()),Int(output[1].rounded()),Int(output[2].rounded()))
    }
    private static func red(_ p: (Int, Int, Int)) -> Bool { p.0 > 210 && p.1 < 45 && p.2 < 45 }
    private static func green(_ p: (Int, Int, Int)) -> Bool { p.1 > 210 && p.0 < 45 && p.2 < 45 }
    private static func pixel(_ image: CGImage, x: Int, y: Int) -> (Int, Int, Int) {
        let color = NSBitmapImageRep(cgImage: image).colorAt(x: x, y: y)!.usingColorSpace(.deviceRGB)!
        return (Int(color.redComponent * 255), Int(color.greenComponent * 255), Int(color.blueComponent * 255))
    }
    private static func bytes(_ image: CGImage) -> Data { NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])! }
    private static func fixtureImage(width: Int, height: Int) -> CGImage {
        var data = Data(count: width * height * 4)
        data.withUnsafeMutableBytes { (raw: UnsafeMutableRawBufferPointer) in
            let pixels = raw.bindMemory(to: UInt8.self)
            for y in 0..<height { for x in 0..<width {
                let offset = (y * width + x) * 4, left = x < width / 2, top = y < height / 2
                pixels[offset] = left ? (top ? 255 : 0) : (top ? 0 : 255)
                pixels[offset + 1] = left ? 0 : 255; pixels[offset + 2] = left && !top ? 255 : 0; pixels[offset + 3] = 255
            } }
        }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
            space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue), provider: CGDataProvider(data: data as CFData)!, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
    }
    private static func write(_ image: CGImage, to url: URL) {
        let sink = CGImageDestinationCreateWithURL(url as CFURL, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(sink, image, nil); precondition(CGImageDestinationFinalize(sink))
    }
    private static func writeMovie(to url: URL) throws {
        let writer = try AVAssetWriter(outputURL: url, fileType: .mov)
        let video = AVAssetWriterInput(mediaType: .video, outputSettings: [AVVideoCodecKey: AVVideoCodecType.h264, AVVideoWidthKey: 64, AVVideoHeightKey: 32])
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: video, sourcePixelBufferAttributes: [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA, kCVPixelBufferWidthKey as String: 64, kCVPixelBufferHeightKey as String: 32])
        let audio = AVAssetWriterInput(mediaType: .audio, outputSettings: [AVFormatIDKey: kAudioFormatMPEG4AAC, AVSampleRateKey: 48000, AVNumberOfChannelsKey: 1, AVEncoderBitRateKey: 64000])
        writer.add(video); writer.add(audio); precondition(writer.startWriting()); writer.startSession(atSourceTime: .zero)
        let videoFinished = DispatchGroup(); videoFinished.enter()
        DispatchQueue(label: "MediaAssemblyTests.VideoWriter").async {
        let deadline = Date().addingTimeInterval(10)
        for index in 0..<60 {
            while !video.isReadyForMoreMediaData && writer.status == .writing && Date() < deadline { Thread.sleep(forTimeInterval: 0.001) }
            precondition(video.isReadyForMoreMediaData, "Generated video writer becomes ready")
            var buffer: CVPixelBuffer?; precondition(CVPixelBufferPoolCreatePixelBuffer(nil, adaptor.pixelBufferPool!, &buffer) == kCVReturnSuccess)
            CVPixelBufferLockBaseAddress(buffer!, [])
            let row = CVPixelBufferGetBytesPerRow(buffer!), data = CVPixelBufferGetBaseAddress(buffer!)!.assumingMemoryBound(to: UInt8.self)
            for y in 0..<32 { for x in 0..<64 { let p = y * row + x * 4; data[p] = 0; data[p + 1] = x < 32 ? 0 : 255; data[p + 2] = x < 32 ? 255 : 0; data[p + 3] = 255 } }
            CVPixelBufferUnlockBaseAddress(buffer!, [])
            precondition(adaptor.append(buffer!, withPresentationTime: CMTime(value: Int64(index), timescale: 30)))
        }
        video.markAsFinished(); videoFinished.leave()
        }
        var format = AudioStreamBasicDescription(mSampleRate: 48000, mFormatID: kAudioFormatLinearPCM, mFormatFlags: kAudioFormatFlagIsSignedInteger | kAudioFormatFlagIsPacked,
            mBytesPerPacket: 2, mFramesPerPacket: 1, mBytesPerFrame: 2, mChannelsPerFrame: 1, mBitsPerChannel: 16, mReserved: 0)
        var description: CMAudioFormatDescription?
        precondition(CMAudioFormatDescriptionCreate(allocator: nil, asbd: &format, layoutSize: 0, layout: nil, magicCookieSize: 0, magicCookie: nil, extensions: nil, formatDescriptionOut: &description) == noErr)
        let audioDeadline = Date().addingTimeInterval(10)
        for index in 0..<20 {
            while !audio.isReadyForMoreMediaData && writer.status == .writing && Date() < audioDeadline { Thread.sleep(forTimeInterval: 0.001) }
            precondition(audio.isReadyForMoreMediaData, "Generated audio writer becomes ready")
            var block: CMBlockBuffer?
            precondition(CMBlockBufferCreateWithMemoryBlock(allocator: nil, memoryBlock: nil, blockLength: 9600, blockAllocator: nil, customBlockSource: nil, offsetToData: 0, dataLength: 9600, flags: 0, blockBufferOut: &block) == noErr)
            precondition(CMBlockBufferFillDataBytes(with: 0, blockBuffer: block!, offsetIntoDestination: 0, dataLength: 9600) == noErr)
            var timing = CMSampleTimingInfo(duration: CMTime(value: 1, timescale: 48000), presentationTimeStamp: CMTime(value: Int64(index * 4800), timescale: 48000), decodeTimeStamp: .invalid)
            var sample: CMSampleBuffer?
            precondition(CMSampleBufferCreateReady(allocator: nil, dataBuffer: block, formatDescription: description, sampleCount: 4800,
                sampleTimingEntryCount: 1, sampleTimingArray: &timing, sampleSizeEntryCount: 0, sampleSizeArray: nil, sampleBufferOut: &sample) == noErr)
            precondition(audio.append(sample!))
        }
        audio.markAsFinished(); precondition(videoFinished.wait(timeout: .now() + 10) == .success)
        let completed = DispatchSemaphore(value: 0)
        writer.finishWriting { completed.signal() }; precondition(completed.wait(timeout: .now() + 10) == .success)
        if writer.status != .completed { throw writer.error ?? MediaAssemblyError.exportFailed }
    }
}
