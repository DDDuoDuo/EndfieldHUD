import AppKit
import AVFoundation
import CoreImage
import ImageIO
import Darwin

/// A single reusable Core Image context. Previews never decode a full-resolution
/// photograph; only an explicit export may request the original pixel dimensions.
final class MediaAssemblyEngine {
    static let maximumPreviewDimension = 1024
    private let beforeCommit: (() -> Void)?
    init(beforeCommit: (() -> Void)? = nil) { self.beforeCommit = beforeCommit }
    private let contextLock = NSLock()
    private var retainedContext: CIContext?
    private var cachedSource: (id: UUID, time: Double, image: CGImage)?
    private var cacheGeneration = 0
    var context: CIContext {
        contextLock.lock(); defer { contextLock.unlock() }
        if let retainedContext { return retainedContext }
        let value = CIContext(options: [.cacheIntermediates: false, .workingColorSpace: CGColorSpace(name: CGColorSpace.sRGB)!])
        retainedContext = value; return value
    }
    func clearCaches() {
        contextLock.lock(); let existing = retainedContext; cachedSource = nil; cacheGeneration += 1; contextLock.unlock()
        existing?.clearCaches(); MediaAssemblyAssetCatalog.clearCaches()
    }
    func open(_ url: URL) throws -> MediaAssemblyDocument {
        var reference = try NotesMediaFactory.makeReference(from: url)
        guard Int64(reference.pixelWidth) * Int64(reference.pixelHeight) <= 64_000_000 else { throw MediaAssemblyError.unsupported }
        if reference.kind != .video {
            guard let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
                  (CGImageSourceGetCount(source) == 1 || reference.kind == .gif) else { throw MediaAssemblyError.unsupported }
            let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any]
            let orientation = (properties?[kCGImagePropertyOrientation] as? NSNumber)?.intValue ?? 1
            if (5...8).contains(orientation) { swap(&reference.pixelWidth, &reference.pixelHeight) }
        }
        return MediaAssemblyDocument(reference: reference, sourceURL: url, identity: try MediaAssemblyFileIdentity.read(url))
    }
    static func exportExtensions(for document: MediaAssemblyDocument) -> [String] {
        if document.isVideo { return ["mov", "mp4"] }
        let types = Set(CGImageDestinationCopyTypeIdentifiers() as! [String])
        return [("png", "public.png"), ("jpg", "public.jpeg"), ("tiff", "public.tiff"), ("heic", "public.heic")].filter { types.contains($0.1) }.map(\.0)
    }
    func preview(_ document: MediaAssemblyDocument, adjustments: MediaAssemblyAdjustments, time: Double, includeStickers: Bool = true) throws -> CGImage {
        guard adjustments.isValid else { throw MediaAssemblyError.invalidAdjustment }
        let access = try NotesMediaFactory.resolve(document.reference); defer { access.close() }
        guard try MediaAssemblyFileIdentity.read(access.url) == document.identity else { throw MediaAssemblyError.changedOnDisk }
        let sourceTime = document.isVideo ? time : 0
        contextLock.lock(); let cached = cachedSource, generation = cacheGeneration; contextLock.unlock()
        let image: CGImage
        if let cached, cached.id == document.id, cached.time == sourceTime { image = cached.image }
        else if document.isVideo {
            let generator = AVAssetImageGenerator(asset: AVURLAsset(url: access.url))
            generator.appliesPreferredTrackTransform = true
            generator.maximumSize = CGSize(width: Self.maximumPreviewDimension, height: Self.maximumPreviewDimension)
            generator.requestedTimeToleranceBefore = CMTime(seconds: 0.08, preferredTimescale: 600)
            generator.requestedTimeToleranceAfter = CMTime(seconds: 0.08, preferredTimescale: 600)
            image = try generator.copyCGImage(at: CMTime(seconds: max(0, min(document.duration - 0.001, time)), preferredTimescale: 600), actualTime: nil)
        } else {
            guard let source = CGImageSourceCreateWithURL(access.url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
                  let result = CGImageSourceCreateThumbnailAtIndex(source, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                    kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: Self.maximumPreviewDimension,
                    kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { throw MediaAssemblyError.unsupported }
            image = result
        }
        contextLock.lock()
        if generation == cacheGeneration { cachedSource = (document.id, sourceTime, image) }
        contextLock.unlock()
        let output = try Self.apply(adjustments, to: CIImage(cgImage: image), includeStickers: includeStickers && !document.isVideo)
        guard let rendered = context.createCGImage(output, from: output.extent, format: .RGBA8, colorSpace: CGColorSpace(name: CGColorSpace.sRGB)) else { throw MediaAssemblyError.exportFailed }
        return rendered
    }
    static func outputSize(_ input: CGSize, adjustments: MediaAssemblyAdjustments, maximumDimension: CGFloat? = nil, even: Bool = false) -> CGSize {
        var width = max(1, floor(input.width * adjustments.crop.width)), height = max(1, floor(input.height * adjustments.crop.height))
        if abs(adjustments.rotationQuarterTurns) % 2 == 1 { swap(&width, &height) }
        if let maximumDimension { let scale = min(1, maximumDimension / max(width, height)); width = floor(width * scale); height = floor(height * scale) }
        if even { width = max(2, floor(width / 2) * 2); height = max(2, floor(height / 2) * 2) }
        return CGSize(width: width, height: height)
    }
    static func apply(_ value: MediaAssemblyAdjustments, to source: CIImage, includeStickers: Bool) throws -> CIImage {
        guard value.isValid, !source.extent.isInfinite, !source.extent.isEmpty else { throw MediaAssemblyError.invalidAdjustment }
        var image = source.transformed(by: CGAffineTransform(translationX: -source.extent.minX, y: -source.extent.minY))
        let extent = image.extent
        let crop = CGRect(x: floor(extent.width * value.crop.x), y: floor(extent.height * (1 - value.crop.y - value.crop.height)),
                          width: max(1, floor(extent.width * value.crop.width)), height: max(1, floor(extent.height * value.crop.height)))
        image = image.cropped(to: crop).transformed(by: CGAffineTransform(translationX: -crop.minX, y: -crop.minY))
        if value.mirrored { image = image.transformed(by: CGAffineTransform(a: -1, b: 0, c: 0, d: 1, tx: image.extent.width, ty: 0)) }
        switch (value.rotationQuarterTurns % 4 + 4) % 4 {
        case 1: image = image.oriented(.right)
        case 2: image = image.oriented(.down)
        case 3: image = image.oriented(.left)
        default: break
        }
        image = image.transformed(by: CGAffineTransform(translationX: -image.extent.minX, y: -image.extent.minY))
        let outputRect = image.extent.integral
        func filter(_ name: String, _ parameters: [String: Any]) throws {
            guard let filter = CIFilter(name: name) else { throw MediaAssemblyError.unsupported }
            filter.setValue(image, forKey: kCIInputImageKey)
            for (key, value) in parameters { filter.setValue(value, forKey: key) }
            guard let output = filter.outputImage else { throw MediaAssemblyError.exportFailed }; image = output.cropped(to: outputRect)
        }
        if value.exposure != 0 { try filter("CIExposureAdjust", [kCIInputEVKey: value.exposure]) }
        if value.brightness != 0 || value.contrast != 1 || value.saturation != 1 {
            try filter("CIColorControls", [kCIInputBrightnessKey: value.brightness, kCIInputContrastKey: value.contrast, kCIInputSaturationKey: value.saturation])
        }
        if value.temperature != 6500 || value.tint != 0 {
            try filter("CITemperatureAndTint", ["inputNeutral": CIVector(x: 6500, y: 0), "inputTargetNeutral": CIVector(x: value.temperature, y: value.tint)])
        }
        if value.highlights != 1 || value.shadows != 0 { try filter("CIHighlightShadowAdjust", ["inputHighlightAmount": value.highlights, "inputShadowAmount": value.shadows]) }
        if value.curve != [0, 0.25, 0.5, 0.75, 1] {
            try filter("CIToneCurve", Dictionary(uniqueKeysWithValues: value.curve.enumerated().map { ("inputPoint\($0.offset)", CIVector(x: Double($0.offset) / 4, y: $0.element) as Any) }))
        }
        if value.levelsBlack != 0 || value.levelsWhite != 1 {
            let gain = 1 / (value.levelsWhite - value.levelsBlack), bias = -value.levelsBlack * gain
            try filter("CIColorMatrix", ["inputRVector": CIVector(x: gain, y: 0, z: 0, w: 0), "inputGVector": CIVector(x: 0, y: gain, z: 0, w: 0), "inputBVector": CIVector(x: 0, y: 0, z: gain, w: 0), "inputBiasVector": CIVector(x: bias, y: bias, z: bias, w: 0)])
            try filter("CIColorClamp", ["inputMinComponents": CIVector(x: 0, y: 0, z: 0, w: 0), "inputMaxComponents": CIVector(x: 1, y: 1, z: 1, w: 1)])
        }
        if value.levelsGamma != 1 { try filter("CIGammaAdjust", ["inputPower": 1 / value.levelsGamma]) }
        image = try MediaAssemblyAssetCatalog.apply(value.filter, to: image)
        if includeStickers {
            for sticker in value.stickers {
                let dimension = min(outputRect.width, outputRect.height) * sticker.size
                guard let source = MediaAssemblyAssetCatalog.stickerImage(sticker.kind) else { throw MediaAssemblyError.unavailable }
                let scale = dimension / CGFloat(max(source.width, source.height))
                var artwork = CIImage(cgImage: source).transformed(by: CGAffineTransform(scaleX: scale, y: scale))
                artwork = artwork.transformed(by: CGAffineTransform(translationX: -CGFloat(source.width) * scale / 2, y: -CGFloat(source.height) * scale / 2))
                    .transformed(by: CGAffineTransform(rotationAngle: -sticker.rotation * .pi / 180))
                    .transformed(by: CGAffineTransform(translationX: outputRect.width * sticker.x, y: outputRect.height * (1 - sticker.y)))
                image = artwork.composited(over: image).cropped(to: outputRect)
            }
        }
        return image.cropped(to: outputRect)
    }
    func makePlaybackItem(_ document: MediaAssemblyDocument, adjustments: MediaAssemblyAdjustments) throws -> (AVPlayerItem, NotesMediaAccess) {
        let access = try NotesMediaFactory.resolve(document.reference)
        do {
            guard try MediaAssemblyFileIdentity.read(access.url) == document.identity else { throw MediaAssemblyError.changedOnDisk }
            let asset = AVURLAsset(url: access.url)
            let item = AVPlayerItem(asset: asset)
            item.videoComposition = try Self.composition(asset: asset, adjustments: adjustments, maximumDimension: 1024)
            item.forwardPlaybackEndTime = try adjustments.timeRange(duration: document.duration).end
            return (item, access)
        } catch { access.close(); throw error }
    }
    static func composition(asset: AVAsset, adjustments: MediaAssemblyAdjustments, maximumDimension: CGFloat? = nil) throws -> AVMutableVideoComposition {
        guard adjustments.isValid, adjustments.stickers.isEmpty, let track = asset.tracks(withMediaType: .video).first else { throw MediaAssemblyError.unsupported }
        let oriented = track.naturalSize.applying(track.preferredTransform)
        let size = outputSize(CGSize(width: abs(oriented.width), height: abs(oriented.height)), adjustments: adjustments, maximumDimension: maximumDimension, even: true)
        guard size.width <= 16384 && size.height <= 16384 else { throw MediaAssemblyError.unsupportedExport }
        let instruction = MediaAssemblyVideoInstruction(trackID: track.trackID, transform: track.preferredTransform,
            range: CMTimeRange(start: .zero, duration: asset.duration), adjustments: adjustments)
        let result = AVMutableVideoComposition(); result.customVideoCompositorClass = MediaAssemblyVideoCompositor.self
        result.renderSize = size
        let fps = max(1, min(120, Double(track.nominalFrameRate > 0 ? track.nominalFrameRate : 30)))
        result.frameDuration = CMTime(seconds: 1 / fps, preferredTimescale: 60_000)
        result.instructions = [instruction]; result.sourceTrackIDForFrameTiming = track.trackID
        return result
    }
    /// Caller runs preparation on its serial utility queue; AVFoundation invokes
    /// completion asynchronously. No poller is needed for export progress.
    func export(_ document: MediaAssemblyDocument, adjustments: MediaAssemblyAdjustments, to destination: URL, overwrite: Bool,
                ticket: MediaAssemblyExportTicket, completion: @escaping (Result<URL, Error>) -> Void) {
        var temporary: URL?
        do {
            guard !ticket.isCancelled else { throw MediaAssemblyError.cancelled }
            guard adjustments.isValid, destination.isFileURL else { throw MediaAssemblyError.invalidAdjustment }
            let access = try NotesMediaFactory.resolve(document.reference)
            guard try MediaAssemblyFileIdentity.read(access.url) == document.identity else { access.close(); throw MediaAssemblyError.changedOnDisk }
            let sourceURL = access.url
            let target = destination.standardizedFileURL.resolvingSymlinksInPath()
            let scoped = destination.startAccessingSecurityScopedResource()
            let existing: MediaAssemblyFileIdentity?
            do { existing = FileManager.default.fileExists(atPath: target.path) ? try MediaAssemblyFileIdentity.read(target) : nil }
            catch { access.close(); if scoped { destination.stopAccessingSecurityScopedResource() }; throw error }
            guard overwrite || existing == nil else { access.close(); if scoped { destination.stopAccessingSecurityScopedResource() }; throw MediaAssemblyError.exists }
            let temp = target.deletingLastPathComponent().appendingPathComponent(".endfield-export-\(UUID()).\(target.pathExtension)"); temporary = temp
            let finish: (Result<Void, Error>) -> Void = { result in
                defer { access.close(); if scoped { destination.stopAccessingSecurityScopedResource() }; try? FileManager.default.removeItem(at: temp); ticket.finish() }
                do {
                    try result.get()
                    self.beforeCommit?()
                    try ticket.commit {
                        guard try MediaAssemblyFileIdentity.read(sourceURL) == document.identity else { throw MediaAssemblyError.changedOnDisk }
                        let now = FileManager.default.fileExists(atPath: target.path) ? try MediaAssemblyFileIdentity.read(target) : nil
                        guard now == existing else { throw MediaAssemblyError.changedOnDisk }
                        let status = temp.path.withCString { from in target.path.withCString { to in overwrite ? rename(from, to) : renamex_np(from, to, UInt32(RENAME_EXCL)) } }
                        guard status == 0 else { throw MediaAssemblyError.exportFailed }
                    }
                    completion(.success(target))
                } catch { completion(.failure(error)) }
            }
            if document.isVideo {
                let asset = AVURLAsset(url: sourceURL)
                let fileType: AVFileType
                switch target.pathExtension.lowercased() { case "mov": fileType = .mov; case "mp4", "m4v": fileType = .mp4; default: finish(.failure(MediaAssemblyError.unsupportedExport)); return }
                guard let session = AVAssetExportSession(asset: asset, presetName: AVAssetExportPresetHighestQuality), session.supportedFileTypes.contains(fileType) else { finish(.failure(MediaAssemblyError.unsupportedExport)); return }
                session.outputURL = temp; session.outputFileType = fileType
                do { session.videoComposition = try Self.composition(asset: asset, adjustments: adjustments); session.timeRange = try adjustments.timeRange(duration: document.duration) }
                catch { finish(.failure(error)); return }
                session.shouldOptimizeForNetworkUse = false
                ticket.observeCancellation { [weak session] in session?.cancelExport() }
                ticket.observeProgress { [weak session] in Double(session?.progress ?? 0) }
                session.exportAsynchronously {
                    if session.status == .completed { finish(.success(())) }
                    else { finish(.failure(ticket.isCancelled ? MediaAssemblyError.cancelled : session.error ?? MediaAssemblyError.exportFailed)) }
                }
            } else {
                do {
                    let type: String
                    switch target.pathExtension.lowercased() {
                    case "png": type = "public.png"
                    case "jpg", "jpeg": type = "public.jpeg"
                    case "tif", "tiff": type = "public.tiff"
                    case "heic", "heif": type = "public.heic"
                    default: throw MediaAssemblyError.unsupportedExport
                    }
                    guard (CGImageDestinationCopyTypeIdentifiers() as! [String]).contains(type),
                          let source = CGImageSourceCreateWithURL(sourceURL as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
                          let decoded = CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { throw MediaAssemblyError.unsupportedExport }
                    if target == sourceURL.standardizedFileURL.resolvingSymlinksInPath() {
                        let originalType = CGImageSourceGetType(source) as String?
                        guard originalType == type || (type == "public.heic" && originalType == "public.heif") else { throw MediaAssemblyError.unsupportedExport }
                    }
                    let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any]
                    let orientation = (properties?[kCGImagePropertyOrientation] as? NSNumber)?.int32Value ?? 1
                    let image = CIImage(cgImage: decoded).oriented(forExifOrientation: orientation)
                    ticket.setProgress(0.25)
                    let output = try Self.apply(adjustments, to: image, includeStickers: true)
                    guard !ticket.isCancelled else { throw MediaAssemblyError.cancelled }
                    guard let rendered = context.createCGImage(output, from: output.extent, format: .RGBA8, colorSpace: CGColorSpace(name: CGColorSpace.sRGB)),
                          let sink = CGImageDestinationCreateWithURL(temp as CFURL, type as CFString, 1, nil) else { throw MediaAssemblyError.exportFailed }
                    ticket.setProgress(0.7)
                    CGImageDestinationAddImage(sink, rendered, [kCGImageDestinationLossyCompressionQuality: 0.95] as CFDictionary)
                    guard CGImageDestinationFinalize(sink) else { throw MediaAssemblyError.exportFailed }
                    finish(.success(()))
                } catch { finish(.failure(error)) }
            }
        } catch { if let temporary { try? FileManager.default.removeItem(at: temporary) }; ticket.finish(); completion(.failure(error)) }
    }
}

private final class MediaAssemblyVideoInstruction: NSObject, AVVideoCompositionInstructionProtocol {
    let timeRange: CMTimeRange
    let enablePostProcessing = false, containsTweening = false
    let requiredSourceTrackIDs: [NSValue]?
    let passthroughTrackID = kCMPersistentTrackID_Invalid
    let trackID: CMPersistentTrackID, transform: CGAffineTransform, adjustments: MediaAssemblyAdjustments
    init(trackID: CMPersistentTrackID, transform: CGAffineTransform, range: CMTimeRange, adjustments: MediaAssemblyAdjustments) {
        self.trackID = trackID; self.transform = transform; timeRange = range; self.adjustments = adjustments
        requiredSourceTrackIDs = [NSNumber(value: trackID)]
    }
}
/// One serial renderer per AVFoundation player/export compositor. Requests are
/// finished on cancellation, and no frame is requested when playback is stopped.
final class MediaAssemblyVideoCompositor: NSObject, AVVideoCompositing {
    let sourcePixelBufferAttributes: [String: Any]? = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA]
    let requiredPixelBufferAttributesForRenderContext: [String: Any] = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32BGRA,
        kCVPixelBufferIOSurfacePropertiesKey as String: [:]]
    private let queue = DispatchQueue(label: "EndfieldHUD.MediaAssembly.Video", qos: .userInitiated)
    private let context = CIContext(options: [.cacheIntermediates: false])
    private let lock = NSLock(); private var generation = 0
    func renderContextChanged(_ newRenderContext: AVVideoCompositionRenderContext) {}
    func startRequest(_ request: AVAsynchronousVideoCompositionRequest) {
        lock.lock(); let token = generation; lock.unlock()
        queue.async { [self] in
            lock.lock(); let cancelled = token != generation; lock.unlock()
            guard !cancelled else { request.finishCancelledRequest(); return }
            do {
                guard let instruction = request.videoCompositionInstruction as? MediaAssemblyVideoInstruction,
                      let source = request.sourceFrame(byTrackID: instruction.trackID), let output = request.renderContext.newPixelBuffer() else { throw MediaAssemblyError.exportFailed }
                var image = CIImage(cvPixelBuffer: source).transformed(by: instruction.transform)
                image = try MediaAssemblyEngine.apply(instruction.adjustments, to: image, includeStickers: false)
                let size = request.renderContext.size
                image = image.transformed(by: CGAffineTransform(scaleX: size.width / image.extent.width, y: size.height / image.extent.height))
                context.render(image, to: output, bounds: CGRect(origin: .zero, size: size), colorSpace: CGColorSpace(name: CGColorSpace.sRGB))
                lock.lock(); let stale = token != generation; lock.unlock()
                if stale { request.finishCancelledRequest() } else { request.finish(withComposedVideoFrame: output) }
            } catch { request.finish(with: error) }
        }
    }
    func cancelAllPendingVideoCompositionRequests() { lock.lock(); generation += 1; lock.unlock() }
}
