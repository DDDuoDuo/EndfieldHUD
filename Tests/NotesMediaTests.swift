import Foundation
import AppKit
import AVFoundation
import ImageIO
import CoreVideo

/// Synthetic assets only: these tests never read the user's media or open a
/// window, and the movie has no audio track.
enum NotesMediaTests {
    static func run() -> Int {
        precondition(Thread.isMainThread)
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        let directory = FileManager.default.temporaryDirectory.appendingPathComponent("NotesMediaTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: directory) }
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            let stillURL = directory.appendingPathComponent("Synthetic image.png")
            let gifURL = directory.appendingPathComponent("Synthetic animation.gif")
            try imageData(width: 1800, height: 900, frames: 1).write(to: stillURL)
            try imageData(width: 80, height: 40, frames: 5).write(to: gifURL)
            let names = try FileManager.default.contentsOfDirectory(atPath: directory.path)
            let still = try NotesMediaFactory.makeReference(from: stillURL)
            let animation = try NotesMediaFactory.makeReference(from: gifURL)
            check(still.isValid && still.kind == .image && still.pixelWidth == 1800 && still.pixelHeight == 900,
                  "Image references retain dimensions without copying full-resolution source bytes")
            check(still.frameCount == 1 && still.duration == nil && still.displayName == stillURL.lastPathComponent,
                  "Still-image metadata is compact and has no fabricated movie duration")
            check(animation.isValid && animation.kind == .gif && animation.frameCount == 5,
                  "A GIF is distinguished from a still image and keeps its actual frame count")
            check(animation.duration.map { $0.isFinite && $0 >= 0.2 } == true,
                  "GIF duration is finite and derived from bounded per-frame delays")
            check(try FileManager.default.contentsOfDirectory(atPath: directory.path) == names,
                  "Reference creation does not duplicate assets or create sidecar files")
            let encoded = try JSONEncoder().encode(animation)
            check(try JSONDecoder().decode(NotesMediaReference.self, from: encoded) == animation && encoded.count < 64 * 1024,
                  "Bookmark metadata round-trips as a small Codable descriptor")
            let access = try NotesMediaFactory.resolve(still)
            check(access.url.standardizedFileURL == stillURL.standardizedFileURL,
                  "Bookmarks resolve the original file rather than a managed duplicate")
            access.close(); access.close()
            let thumbnail = try NotesMediaFactory.thumbnail(for: still, maximumDimension: 150)
            check(thumbnail.width == 150 && thumbnail.height == 75,
                  "Thumbnail decoding respects its maximum dimension and source aspect ratio")
            let boundedThumbnail = try NotesMediaFactory.thumbnail(for: still, maximumDimension: Int.max)
            check(max(boundedThumbnail.width, boundedThumbnail.height) <= 1600,
                  "Callers cannot bypass the thumbnail dimension limit")
            let tinyThumbnail = try NotesMediaFactory.thumbnail(for: animation, maximumDimension: -100)
            check(max(tinyThumbnail.width, tinyThumbnail.height) <= 1,
                  "Invalid thumbnail size inputs are clamped before decoding")

            for delay in [Optional<Double>.none, .some(.nan), .some(.infinity), .some(-1), .some(0)] {
                check(NotesMediaFactory.normalizedFrameDelay(delay) == 0.1,
                      "Missing or malformed GIF timing cannot create a zero-delay loop")
            }
            check(NotesMediaFactory.normalizedFrameDelay(0.001) == 0.04,
                  "GIF scheduling is capped at 25 frame deadlines per second")
            check(NotesMediaFactory.normalizedFrameDelay(2.75) == 2.75,
                  "Intentional slower GIF frames retain their duration")
            check(NotesMediaFactory.normalizedFrameDelay(Double.greatestFiniteMagnitude) == 600,
                  "A corrupt large GIF delay stays finite and bounded")

            var invalid = still
            invalid.version = 2
            check(!invalid.isValid, "Unknown descriptor versions are rejected before file I/O")
            invalid = still; invalid.bookmark = Data()
            check(!invalid.isValid, "Empty bookmarks are invalid")
            invalid = still; invalid.bookmark = Data(repeating: 0, count: 1024 * 1024 + 1)
            check(!invalid.isValid, "Descriptors cannot smuggle unbounded data into the store")
            invalid = still; invalid.lastKnownPath = "../relative.png"
            check(!invalid.isValid, "References require an absolute last-known path")
            invalid = still; invalid.pixelWidth = Int.max
            check(!invalid.isValid, "Malformed dimensions fail validation without overflow")
            invalid = still; invalid.frameCount = 2
            check(!invalid.isValid, "Still images cannot claim multiple animated frames")
            invalid = animation; invalid.frameCount = NotesMediaFactory.maximumGIFFrames + 1
            check(!invalid.isValid, "GIF descriptor frame counts are bounded")
            invalid = animation; invalid.duration = .nan
            check(!invalid.isValid, "Nonfinite media duration is rejected")
            invalid = still; invalid.kind = .video
            check(!invalid.isValid, "Video descriptors require a finite positive duration")
            check(rejected { _ = try NotesMediaFactory.resolve(invalid) },
                  "Resolution enforces descriptor validation")
            check(rejected { _ = try NotesMediaFactory.makeReference(from: URL(string: "https://example.invalid/movie.mp4")!) },
                  "Media import cannot silently become a network stream")
            check(rejected { _ = try NotesMediaFactory.makeReference(from: directory) },
                  "Folders are rejected as media")
            let fake = directory.appendingPathComponent("not-a-movie.mp4")
            try Data("Synthetic invalid movie".utf8).write(to: fake)
            check(rejected { _ = try NotesMediaFactory.makeReference(from: fake) },
                  "A familiar extension cannot disguise unsupported contents")
            check(rejected { _ = try NotesMediaFactory.makeReference(from: directory.appendingPathComponent("missing.png")) },
                  "Missing files produce a clean error")

            let imagePresentation = NotesMediaPresentation(reference: still)
            check(imagePresentation.state == .hidden && imagePresentation.layer.contents == nil
                    && !imagePresentation.hasActiveDecoder && !imagePresentation.hasScheduledFrame,
                  "Constructing hidden notes performs no media decoding or scheduling")
            var callbacksOnMain = true
            imagePresentation.onStateChange = { callbacksOnMain = callbacksOnMain && Thread.isMainThread }
            imagePresentation.setVisible(true)
            check(eventually { imagePresentation.state == .ready }, "Visible image notes decode a bounded poster asynchronously")
            check(imagePresentation.layer.contents != nil && !imagePresentation.hasScheduledFrame,
                  "Still images require no continuous frame timer")
            imagePresentation.play()
            check(!imagePresentation.isPlaying, "Still images cannot enter a fictitious playback state")
            imagePresentation.setVisible(false)
            check(imagePresentation.layer.contents == nil && imagePresentation.state == .hidden,
                  "Hidden still notes release their decoded contents")
            imagePresentation.dispose(); imagePresentation.setVisible(true)
            check(!imagePresentation.isVisible && imagePresentation.state == .hidden,
                  "Disposed presentations cannot start another background load")
            check(callbacksOnMain, "Media state callbacks stay on the main thread")

            let outgoing = NotesMediaPresentation(reference: still)
            outgoing.setVisible(true)
            check(eventually { outgoing.state == .ready }, "The transition fixture loads only its synthetic still poster")
            outgoing.setVisible(false, preserveArtworkOnHide: true)
            check(outgoing.layer.contents != nil && !outgoing.hasActiveDecoder && !outgoing.hasScheduledFrame,
                  "A closing note retains its bounded artwork without keeping decoders or frame clocks alive")
            check(eventually(timeout: 1) { outgoing.layer.contents == nil },
                  "The outgoing artwork is released after the finite close transition")
            outgoing.setVisible(true)
            check(eventually { outgoing.state == .ready }, "The transition fixture can be reopened")
            outgoing.setVisible(false, preserveArtworkOnHide: true); outgoing.setVisible(true)
            check(eventually { outgoing.state == .ready }, "Reopening cancels a pending artwork release")
            spin(for: 0.65)
            check(outgoing.layer.contents != nil, "A stale close completion never erases a reopened poster")
            outgoing.setVisible(false, preserveArtworkOnHide: true); outgoing.dispose()
            check(outgoing.layer.contents == nil, "Disposal always releases transition artwork immediately")

            let gif = NotesMediaPresentation(reference: animation, maximumDimension: 64)
            gif.setVisible(true)
            check(eventually { gif.isPlaying && gif.hasScheduledFrame }, "Visible GIFs autoplay using a finite frame deadline")
            check(eventually { gif.cachedFrameCount == 3 }, "Animated playback fills only the bounded three-frame cache")
            check(gif.cachedFrameCount <= 3 && gif.hasActiveDecoder,
                  "GIFs never retain all frames as a full-resolution animated image")
            gif.pause()
            check(gif.state == .paused && !gif.hasScheduledFrame,
                  "Pausing a GIF cancels its next frame deadline")
            gif.play(); gif.play(); gif.play()
            check(gif.isPlaying, "Repeated play is idempotent and keeps one scheduling chain")
            gif.setVisible(false)
            check(!gif.hasActiveDecoder && !gif.hasScheduledFrame && gif.cachedFrameCount == 0 && gif.layer.contents == nil,
                  "Hiding GIFs releases the decoder, frame cache, contents, and deadline")
            spin(for: 0.15)
            check(!gif.hasActiveDecoder && !gif.hasScheduledFrame && gif.state == .hidden,
                  "A stale frame completion cannot reactivate a hidden note")
            gif.setVisible(true)
            check(eventually { gif.isPlaying }, "A previously playing GIF resumes only after becoming visible again")
            gif.pause(); gif.setVisible(false); gif.setVisible(true)
            check(eventually { gif.state == .paused }, "Explicit user pause survives hide and reveal")
            gif.setVisible(false, preserveArtworkOnHide: true)
            check(gif.layer.contents != nil && !gif.hasActiveDecoder && !gif.hasScheduledFrame && gif.cachedFrameCount == 0,
                  "Animated outgoing notes retain one frame while their GIF cache and decoder stop immediately")
            gif.dispose()

            // Hide before the async poster arrives, exercising generation guards.
            let cancelled = NotesMediaPresentation(reference: animation)
            cancelled.setVisible(true); cancelled.setVisible(false)
            spin(for: 0.1)
            check(cancelled.layer.contents == nil && !cancelled.hasActiveDecoder && !cancelled.hasScheduledFrame,
                  "Pending decode completions do not attach resources after the overlay closes")
            cancelled.dispose()

            let movieURL = directory.appendingPathComponent("Silent synthetic video.mov")
            try makeSilentMovie(at: movieURL)
            let movieBytes = try Data(contentsOf: movieURL)
            let video = try NotesMediaFactory.makeReference(from: movieURL)
            check(video.kind == .video && video.isValid && video.duration.map { $0 > 0 } == true,
                  "Public AVFoundation validates a local playable video with a real video track")
            let imageWithVideoExtension = directory.appendingPathComponent("Synthetic image named movie.mov")
            try Data(contentsOf: stillURL).write(to: imageWithVideoExtension)
            check(try NotesMediaFactory.makeReference(from: imageWithVideoExtension).kind == .image,
                  "Recognized image contents still use image validation even with a movie extension")
            check(video.pixelWidth == 32 && video.pixelHeight == 24 && video.frameCount == 1,
                  "Video descriptors keep display dimensions without scanning every frame")
            let videoPoster = try NotesMediaFactory.thumbnail(for: video, maximumDimension: 16)
            check(max(videoPoster.width, videoPoster.height) <= 16,
                  "Video posters are decoded at bounded dimensions")
            let movie = NotesMediaPresentation(reference: video)
            movie.setVisible(true)
            check(eventually { movie.state == .paused && movie.hasActiveDecoder && !movie.hasProgressObserver },
                  "Visible video notes create one paused native AVPlayerItem")
            check((movie.layer.sublayers ?? []).contains { $0 is AVPlayerLayer },
                  "Native video uses AVPlayerLayer inside the existing card")
            movie.seek(to: (video.duration ?? 1) * 0.7)
            check(abs(movie.currentTime - (video.duration ?? 1) * 0.7) < 0.001,
                  "Seeking keeps the requested thumb position while AVPlayer finishes its asynchronous seek")
            movie.seek(to: .nan)
            check(movie.currentTime.isFinite, "Invalid seek values never reach Core Media")
            movie.play()
            check(movie.isPlaying && movie.hasProgressObserver, "The video play control activates native playback and one progress observer")
            movie.pause()
            check(movie.state == .paused && !movie.isPlaying && !movie.hasProgressObserver, "The video pause control stops playback")
            movie.setVisible(false)
            check(!movie.hasActiveDecoder && !movie.hasProgressObserver && movie.layer.sublayers?.isEmpty != false && movie.layer.contents == nil,
                  "Hidden movies detach their native item, player layer, poster, and scoped file access")
            movie.setVisible(true)
            check(eventually { movie.state == .paused }, "Paused native video remains paused when reopened")
            movie.dispose()
            check(!movie.hasActiveDecoder && !movie.hasScheduledFrame && !movie.hasProgressObserver, "Disposal releases video resources without a polling timer")
            check(try Data(contentsOf: movieURL) == movieBytes, "Media import and playback never alter the source movie")

            let seekStore = try NotesStore(directory: directory.appendingPathComponent("seek-canvas"))
            let seekCanvas = NotesCanvas(store: seekStore, reduceMotion: { true })
            seekCanvas.setWorkspaceBounds(CGRect(x: 0, y: 0, width: 800, height: 600))
            check(seekCanvas.importMedia(reference: video, at: CGPoint(x: 80, y: 70)), "Video seek fixture creates a reference-only canvas note")
            let seekAction = seekCanvas.mediaSeekActions.first!
            let seekID = UUID(uuidString: seekAction.id)!
            let seekDatabase = directory.appendingPathComponent("seek-canvas/notes.sqlite3")
            let seekBytes = try Data(contentsOf: seekDatabase)
            check(seekCanvas.mouseDownInWorkspace(at: CGPoint(x: seekAction.rect.midX, y: seekAction.rect.midY), clickCount: 1)
                  && seekCanvas.isDragging, "The full video rail starts a seek gesture")
            seekCanvas.mouseDragged(to: CGPoint(x: seekAction.rect.maxX + 20, y: seekAction.rect.midY))
            check(seekCanvas.mediaPosition(for: seekID) == 0, "Scrubbing previews progress without repeatedly seeking the decoder")
            seekCanvas.mouseUp()
            check(!seekCanvas.isDragging && abs(seekCanvas.mediaPosition(for: seekID) - (video.duration ?? 1)) < 0.001,
                  "Seek release clamps to the movie duration and commits one final position")
            check(try Data(contentsOf: seekDatabase) == seekBytes, "Video scrubbing never writes playback position into Notes storage")

            try FileManager.default.removeItem(at: stillURL)
            check(rejected { _ = try NotesMediaFactory.resolve(still) },
                  "A missing original stays unavailable rather than using a guessed fallback path")
        } catch { fatalError("Notes media test failed: \(error)") }
        return count
    }

    private static func eventually(timeout: TimeInterval = 5, _ condition: () -> Bool) -> Bool {
        let end = Date().addingTimeInterval(timeout)
        while !condition(), Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.01)) }
        return condition()
    }

    private static func spin(for duration: TimeInterval) {
        let end = Date().addingTimeInterval(duration)
        while Date() < end { RunLoop.current.run(until: min(end, Date().addingTimeInterval(0.01))) }
    }

    private static func imageData(width: Int, height: Int, frames: Int) throws -> Data {
        let data = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(data, (frames > 1 ? "com.compuserve.gif" : "public.png") as CFString, frames, nil),
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { throw NotesMediaError.decodingFailed }
        if frames > 1 { CGImageDestinationSetProperties(destination, [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFLoopCount: 0]] as CFDictionary) }
        for index in 0..<frames {
            context.setFillColor(CGColor(red: CGFloat(index) / CGFloat(frames), green: 0.3, blue: 0.8, alpha: 1))
            context.fill(CGRect(x: 0, y: 0, width: width, height: height))
            guard let image = context.makeImage() else { throw NotesMediaError.decodingFailed }
            CGImageDestinationAddImage(destination, image, frames > 1 ? [kCGImagePropertyGIFDictionary: [kCGImagePropertyGIFDelayTime: 0.06]] as CFDictionary : nil)
        }
        guard CGImageDestinationFinalize(destination) else { throw NotesMediaError.decodingFailed }
        return data as Data
    }

    private static func makeSilentMovie(at url: URL) throws {
        let writer = try AVAssetWriter(outputURL: url, fileType: .mov)
        let input = AVAssetWriterInput(mediaType: .video, outputSettings: [AVVideoCodecKey: AVVideoCodecType.h264,
                                                                         AVVideoWidthKey: 32, AVVideoHeightKey: 24])
        let attributes: [String: Any] = [kCVPixelBufferPixelFormatTypeKey as String: kCVPixelFormatType_32ARGB,
                                        kCVPixelBufferWidthKey as String: 32, kCVPixelBufferHeightKey as String: 24]
        let adaptor = AVAssetWriterInputPixelBufferAdaptor(assetWriterInput: input, sourcePixelBufferAttributes: attributes)
        guard writer.canAdd(input) else { throw NotesMediaError.decodingFailed }
        writer.add(input)
        guard writer.startWriting() else { throw writer.error ?? NotesMediaError.decodingFailed }
        writer.startSession(atSourceTime: .zero)
        for index in 0..<3 {
            guard eventually(timeout: 3, { input.isReadyForMoreMediaData }) else { throw NotesMediaError.decodingFailed }
            var buffer: CVPixelBuffer?
            guard CVPixelBufferCreate(kCFAllocatorDefault, 32, 24, kCVPixelFormatType_32ARGB, nil, &buffer) == kCVReturnSuccess,
                  let buffer else { throw NotesMediaError.decodingFailed }
            CVPixelBufferLockBaseAddress(buffer, [])
            if let bytes = CVPixelBufferGetBaseAddress(buffer) {
                memset(bytes, Int32(60 + index * 50), CVPixelBufferGetBytesPerRow(buffer) * CVPixelBufferGetHeight(buffer))
            }
            CVPixelBufferUnlockBaseAddress(buffer, [])
            guard adaptor.append(buffer, withPresentationTime: CMTime(value: Int64(index), timescale: 3)) else {
                throw writer.error ?? NotesMediaError.decodingFailed
            }
        }
        input.markAsFinished()
        let finished = DispatchSemaphore(value: 0)
        writer.finishWriting { finished.signal() }
        guard finished.wait(timeout: .now() + 10) == .success, writer.status == .completed else {
            writer.cancelWriting(); throw writer.error ?? NotesMediaError.decodingFailed
        }
    }
}
