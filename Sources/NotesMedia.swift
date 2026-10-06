import Foundation
import AppKit
import AVFoundation
import ImageIO
import QuartzCore
import Security

enum NotesMediaKind: String, Codable { case image, gif, video }

/// A small reference to the user's original file. No movie or source image bytes
/// belong in the Notes database. Legacy managed PNG notes remain independent.
struct NotesMediaReference: Codable, Equatable {
    var version = 1
    var kind: NotesMediaKind
    var bookmark: Data
    var isSecurityScoped: Bool
    var lastKnownPath: String
    var displayName: String
    var pixelWidth: Int
    var pixelHeight: Int
    var duration: TimeInterval?
    var frameCount: Int

    /// Persistence can reject malformed descriptors without opening user files.
    var isValid: Bool {
        version == 1 && !bookmark.isEmpty && bookmark.count <= 1024 * 1024
            && lastKnownPath.hasPrefix("/") && !lastKnownPath.contains("\0")
            && lastKnownPath.utf8.count <= 32_768 && !displayName.isEmpty
            && !displayName.contains("\0") && displayName.utf8.count <= 4096
            && pixelWidth > 0 && pixelHeight > 0
            && pixelWidth <= 65_536 && pixelHeight <= 65_536
            && frameCount > 0 && frameCount <= NotesMediaFactory.maximumGIFFrames
            && (duration == nil || (duration!.isFinite && duration! > 0 && duration! <= 31_536_000))
            && (kind != .video || duration != nil)
            && (kind != .image || frameCount == 1)
    }
}

enum NotesMediaError: LocalizedError {
    case invalidReference, unavailable, unsupportedFormat, imageTooLarge, tooManyFrames, decodingFailed
    var errorDescription: String? {
        switch self {
        case .invalidReference: return L10n.text("The saved media reference is invalid.", "保存的媒体引用无效。")
        case .unavailable: return L10n.text("The original media file is unavailable. Choose it again to restore access.", "原始媒体文件不可用，请重新选择以恢复访问。")
        case .unsupportedFormat: return L10n.text("This file is not a supported image or playable video.", "此文件不是受支持的图片或可播放的视频。")
        case .imageTooLarge: return L10n.text("Choose an image below 128 MB and 64 megapixels.", "请选择小于 128 MB 和 6400 万像素的图片。")
        case .tooManyFrames: return L10n.text("Choose a GIF with no more than 2,000 frames.", "请选择不超过 2000 帧的 GIF。")
        case .decodingFailed: return L10n.text("This media could not be decoded.", "无法解码此媒体。")
        }
    }
}

final class NotesMediaAccess {
    let url: URL
    private var scoped: Bool
    fileprivate init(url: URL, scoped: Bool) {
        self.url = url
        self.scoped = scoped && url.startAccessingSecurityScopedResource()
    }
    func close() {
        if scoped { scoped = false; url.stopAccessingSecurityScopedResource() }
    }
    deinit { close() }
}

enum NotesMediaFactory {
    static let maximumGIFFrames = 2000
    static let maximumImageBytes = 128 * 1024 * 1024
    static let maximumImagePixels = 64_000_000
    /// The open panel is a convenience filter. The decoder, not an extension,
    /// decides whether the selected contents are actually supported on this OS.
    static let supportedFileExtensions = ["png", "jpg", "jpeg", "gif", "heic", "heif", "tif", "tiff",
                                          "bmp", "webp", "jp2", "mov", "mp4", "m4v", "avi", "mpeg", "mpg"]

    /// Run this on an import queue: AVFoundation may read container metadata.
    /// Movie sizes are unrestricted because they are referenced, never copied.
    static func makeReference(from url: URL) throws -> NotesMediaReference {
        guard url.isFileURL else { throw NotesMediaError.unsupportedFormat }
        let access = NotesMediaAccess(url: url, scoped: true)
        defer { access.close() }
        let values: URLResourceValues
        do { values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey, .nameKey]) }
        catch { throw NotesMediaError.unavailable }
        guard values.isRegularFile == true else { throw NotesMediaError.unsupportedFormat }
        var kind: NotesMediaKind
        var width: Int, height: Int, frameCount = 1
        var duration: TimeInterval?
        if let source = imageSource(at: url) {
            let dimensions = try validateImage(source, byteCount: values.fileSize)
            width = dimensions.width; height = dimensions.height
            kind = CGImageSourceGetType(source).map { ($0 as String) == "com.compuserve.gif" } == true ? .gif : .image
            if kind == .gif {
                frameCount = CGImageSourceGetCount(source)
                guard frameCount <= maximumGIFFrames else { throw NotesMediaError.tooManyFrames }
                if frameCount > 1 { duration = (0..<frameCount).reduce(0) { $0 + frameDelay(source, index: $1) } }
            }
            // Validate one bounded frame, not the full-resolution asset.
            _ = try imageFrame(source, index: 0, maximumDimension: 64)
        } else {
            let asset = AVURLAsset(url: url, options: [AVURLAssetPreferPreciseDurationAndTimingKey: false])
            guard asset.isPlayable, let track = asset.tracks(withMediaType: .video).first else {
                throw NotesMediaError.unsupportedFormat
            }
            let size = track.naturalSize.applying(track.preferredTransform)
            let seconds = CMTimeGetSeconds(asset.duration)
            guard size.width.isFinite, size.height.isFinite, abs(size.width) >= 1, abs(size.height) >= 1,
                  abs(size.width) <= 65_536, abs(size.height) <= 65_536,
                  seconds.isFinite, seconds > 0, seconds <= 31_536_000 else { throw NotesMediaError.unsupportedFormat }
            kind = .video; width = Int(abs(size.width).rounded()); height = Int(abs(size.height).rounded())
            duration = seconds
        }
        let bookmark = try bookmark(for: url)
        let result = NotesMediaReference(kind: kind, bookmark: bookmark.data, isSecurityScoped: bookmark.scoped,
                                        lastKnownPath: url.path, displayName: values.name ?? url.lastPathComponent,
                                        pixelWidth: width, pixelHeight: height, duration: duration, frameCount: frameCount)
        guard result.isValid else { throw NotesMediaError.invalidReference }
        return result
    }

    static func resolve(_ reference: NotesMediaReference) throws -> NotesMediaAccess {
        guard reference.isValid else { throw NotesMediaError.invalidReference }
        var stale = false
        var options: URL.BookmarkResolutionOptions = [.withoutUI, .withoutMounting]
        if reference.isSecurityScoped { options.insert(.withSecurityScope) }
        do {
            let url = try URL(resolvingBookmarkData: reference.bookmark, options: options,
                              relativeTo: nil, bookmarkDataIsStale: &stale)
            guard url.isFileURL else { throw NotesMediaError.unavailable }
            let access = NotesMediaAccess(url: url, scoped: reference.isSecurityScoped)
            guard (try url.resourceValues(forKeys: [.isRegularFileKey])).isRegularFile == true else {
                access.close(); throw NotesMediaError.unavailable
            }
            return access
        } catch { throw NotesMediaError.unavailable }
    }

    /// A single bounded poster. Video extraction is also a background operation.
    static func thumbnail(for reference: NotesMediaReference, maximumDimension: Int = 512) throws -> CGImage {
        let access = try resolve(reference)
        defer { access.close() }
        if reference.kind == .video { return try videoPoster(at: access.url, maximumDimension: maximumDimension) }
        guard let source = imageSource(at: access.url) else { throw NotesMediaError.decodingFailed }
        _ = try validateImage(source, byteCount: try access.url.resourceValues(forKeys: [.fileSizeKey]).fileSize)
        return try imageFrame(source, index: 0, maximumDimension: maximumDimension)
    }

    fileprivate static func imageSource(at url: URL) -> CGImageSource? {
        // ImageIO can return an unrecognized probe source for a movie. A
        // non-nil object alone must not divert valid video from AVFoundation.
        guard let source = CGImageSourceCreateWithURL(url as CFURL,
                [kCGImageSourceShouldCache: false] as CFDictionary),
              CGImageSourceGetType(source) != nil else { return nil }
        return source
    }

    fileprivate static func validateImage(_ source: CGImageSource, byteCount: Int?) throws -> (width: Int, height: Int) {
        guard byteCount.map({ $0 <= maximumImageBytes }) != false else { throw NotesMediaError.imageTooLarge }
        guard CGImageSourceGetCount(source) > 0,
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int,
              width > 0, height > 0, width <= 65_536, height <= 65_536 else { throw NotesMediaError.decodingFailed }
        guard width * height <= maximumImagePixels else { throw NotesMediaError.imageTooLarge }
        return (width, height)
    }

    fileprivate static func imageFrame(_ source: CGImageSource, index: Int, maximumDimension: Int) throws -> CGImage {
        guard index >= 0, index < CGImageSourceGetCount(source),
              let image = CGImageSourceCreateThumbnailAtIndex(source, index, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: max(1, min(1600, maximumDimension)),
                kCGImageSourceShouldCacheImmediately: true
              ] as CFDictionary) else { throw NotesMediaError.decodingFailed }
        return image
    }

    static func normalizedFrameDelay(_ delay: TimeInterval?) -> TimeInterval {
        // Broken zero-delay GIFs cannot create a busy loop. Slow frames retain
        // their timing; 25 fps is sufficient for a small persistent HUD object.
        guard let delay, delay.isFinite, delay > 0 else { return 0.1 }
        return min(600, max(0.04, delay))
    }

    fileprivate static func frameDelay(_ source: CGImageSource, index: Int) -> TimeInterval {
        let properties = CGImageSourceCopyPropertiesAtIndex(source, index, nil) as? [CFString: Any]
        let gif = properties?[kCGImagePropertyGIFDictionary] as? [CFString: Any]
        return normalizedFrameDelay((gif?[kCGImagePropertyGIFUnclampedDelayTime] as? NSNumber)?.doubleValue
            ?? (gif?[kCGImagePropertyGIFDelayTime] as? NSNumber)?.doubleValue)
    }

    fileprivate static func videoPoster(at url: URL, maximumDimension: Int) throws -> CGImage {
        let generator = AVAssetImageGenerator(asset: AVURLAsset(url: url))
        generator.appliesPreferredTrackTransform = true
        let size = max(1, min(1600, maximumDimension))
        generator.maximumSize = CGSize(width: size, height: size)
        do { return try generator.copyCGImage(at: .zero, actualTime: nil) }
        catch { throw NotesMediaError.decodingFailed }
    }

    private static func bookmark(for url: URL) throws -> (data: Data, scoped: Bool) {
        do {
            return (try url.bookmarkData(options: [.withSecurityScope, .securityScopeAllowOnlyReadAccess],
                                         includingResourceValuesForKeys: [.nameKey], relativeTo: nil), true)
        } catch {
            // A development build may lack security-scoped bookmark support.
            // A sandboxed build must never silently lose the access requirement.
            guard let task = SecTaskCreateFromSelf(nil),
                  (SecTaskCopyValueForEntitlement(task, "com.apple.security.app-sandbox" as CFString, nil) as? Bool) != true else {
                throw NotesMediaError.unavailable
            }
            do { return (try url.bookmarkData(options: [], includingResourceValuesForKeys: [.nameKey], relativeTo: nil), false) }
            catch { throw NotesMediaError.unavailable }
        }
    }
}

/// Used on the serial decode queue only. At most three bounded bitmaps are
/// retained even when a GIF contains thousands of source frames.
private final class NotesGIFDecoder {
    let access: NotesMediaAccess
    let source: CGImageSource
    let delays: [TimeInterval]
    let maximumDimension: Int
    private var frames: [Int: CGImage] = [:]
    private var order: [Int] = []
    var cachedFrameCount: Int { frames.count }
    init(access: NotesMediaAccess, maximumDimension: Int) throws {
        self.access = access; self.maximumDimension = maximumDimension
        guard let source = NotesMediaFactory.imageSource(at: access.url) else { throw NotesMediaError.decodingFailed }
        _ = try NotesMediaFactory.validateImage(source, byteCount: try access.url.resourceValues(forKeys: [.fileSizeKey]).fileSize)
        let count = CGImageSourceGetCount(source)
        guard count > 0, count <= NotesMediaFactory.maximumGIFFrames else { throw NotesMediaError.tooManyFrames }
        self.source = source
        delays = (0..<count).map { NotesMediaFactory.frameDelay(source, index: $0) }
    }
    func frame(_ index: Int) throws -> CGImage {
        if let cached = frames[index] {
            order.removeAll { $0 == index }; order.append(index)
            return cached
        }
        let image = try NotesMediaFactory.imageFrame(source, index: index, maximumDimension: maximumDimension)
        frames[index] = image; order.append(index)
        while order.count > 3 { frames.removeValue(forKey: order.removeFirst()) }
        return image
    }
}

enum NotesMediaPlaybackState: Equatable { case hidden, loading, ready, playing, paused, failed }

private final class NotesMediaContainerLayer: HUDDecorativeContentLayer {
    override func layoutSublayers() {
        super.layoutSublayers()
        CATransaction.begin(); CATransaction.setDisableActions(true)
        for layer in sublayers ?? [] { layer.frame = bounds }
        CATransaction.commit()
    }
}

private final class NotesMediaCancellation {
    private let lock = NSLock()
    private var cancelled = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func cancel() { lock.lock(); cancelled = true; lock.unlock() }
}

/// Main-thread presentation owner. Creating a note does no decoding; the host
/// must explicitly supply visibility (including overlay closure and clipping).
/// Hiding cancels the only GIF deadline and discards the native player item.
final class NotesMediaPresentation {
    let layer: CALayer = NotesMediaContainerLayer()
    let reference: NotesMediaReference
    private(set) var state: NotesMediaPlaybackState = .hidden
    private(set) var error: NotesMediaError?
    private(set) var isVisible = false
    var isPlaying: Bool { state == .playing }
    var onStateChange: (() -> Void)?
    var onProgress: (() -> Void)?
    var hasProgressObserver: Bool { progressObserver != nil }
    private(set) var cachedFrameCount = 0
    var hasActiveDecoder: Bool { gif != nil || player?.currentItem != nil }
    var hasScheduledFrame: Bool { deadline != nil }
    var currentTime: TimeInterval {
        if let pendingSeekTime { return pendingSeekTime }
        if let player { let t = CMTimeGetSeconds(player.currentTime()); return t.isFinite ? max(0, t) : savedTime }
        return savedTime
    }

    private static let decodeQueue = DispatchQueue(label: "EndfieldCharge.NotesMedia", qos: .utility)
    private let maximumDimension: Int
    private var generation: UInt = 0
    private var gif: NotesGIFDecoder?
    private var frameIndex = 0
    private var deadline: DispatchWorkItem?
    private var artworkRelease: DispatchWorkItem?
    private var frameDecodePending = false
    private var cancellation = NotesMediaCancellation()
    private var player: AVPlayer?
    private var playerLayer: AVPlayerLayer?
    private var access: NotesMediaAccess?
    private var statusObserver: NSKeyValueObservation?
    private var endObserver: NSObjectProtocol?
    private var progressObserver: Any?
    private var savedTime: TimeInterval = 0
    private var pendingSeekTime: TimeInterval?
    private var seekSerial: UInt = 0
    private var wantsPlayback = false
    private var hasLoaded = false
    private var disposed = false

    init(reference: NotesMediaReference, maximumDimension: Int = 512) {
        self.reference = reference
        self.maximumDimension = max(32, min(768, maximumDimension))
        layer.name = "notes.media"
        layer.contentsGravity = .resizeAspect
        layer.masksToBounds = true
        layer.actions = ["contents": NSNull(), "bounds": NSNull(), "position": NSNull()]
    }

    func setVisible(_ visible: Bool, preserveArtworkOnHide: Bool = false) {
        precondition(Thread.isMainThread)
        guard !disposed, isVisible != visible else { return }
        isVisible = visible; generation &+= 1
        artworkRelease?.cancel(); artworkRelease = nil
        cancellation.cancel(); cancellation = NotesMediaCancellation()
        if !visible {
            releasePlayback(preserveArtwork: preserveArtworkOnHide)
            if preserveArtworkOnHide, layer.contents != nil {
                // Only the existing bounded poster/current GIF frame survives
                // the finite close transition. No decoder or bookmark survives.
                let expected = generation
                let work = DispatchWorkItem { [weak self] in
                    guard let self, !self.isVisible, self.generation == expected else { return }
                    self.layer.contents = nil; self.artworkRelease = nil
                }
                artworkRelease = work
                DispatchQueue.main.asyncAfter(deadline: .now() + 0.6, execute: work)
            }
            changeState(.hidden); return
        }
        error = nil
        if !hasLoaded { wantsPlayback = reference.kind == .gif; hasLoaded = true }
        changeState(.loading)
        let expected = generation, reference = reference, dimension = maximumDimension, cancellation = cancellation
        Self.decodeQueue.async { [weak self] in
            guard !cancellation.isCancelled else { return }
            do {
                let access = try NotesMediaFactory.resolve(reference)
                guard !cancellation.isCancelled else { return }
                switch reference.kind {
                case .image:
                    defer { access.close() }
                    guard let source = NotesMediaFactory.imageSource(at: access.url) else { throw NotesMediaError.decodingFailed }
                    _ = try NotesMediaFactory.validateImage(source, byteCount: try access.url.resourceValues(forKeys: [.fileSizeKey]).fileSize)
                    let image = try NotesMediaFactory.imageFrame(source, index: 0, maximumDimension: dimension)
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.accepts(expected) else { return }
                        self.layer.contents = image; self.changeState(.ready)
                    }
                case .gif:
                    let decoder = try NotesGIFDecoder(access: access, maximumDimension: dimension)
                    let image = try decoder.frame(0)
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.accepts(expected) else { return }
                        self.gif = decoder; self.frameIndex = 0; self.cachedFrameCount = 1; self.layer.contents = image
                        self.changeState(self.wantsPlayback && decoder.delays.count > 1 ? .playing : .paused)
                        if self.isPlaying { self.scheduleFrame() }
                    }
                case .video:
                    // Poster failure should not prevent a valid native movie
                    // from opening. AVPlayerItem reports playback failures.
                    let poster = try? NotesMediaFactory.videoPoster(at: access.url, maximumDimension: dimension)
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.accepts(expected) else { return }
                        self.access = access; self.layer.contents = poster; self.installPlayer(url: access.url)
                    }
                }
            } catch {
                let failure = (error as? NotesMediaError) ?? .decodingFailed
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.accepts(expected) else { return }
                    self.error = failure; self.changeState(.failed)
                }
            }
        }
    }

    func play() {
        precondition(Thread.isMainThread)
        guard !disposed, isVisible, reference.kind != .image, state != .failed else { return }
        wantsPlayback = true
        if let player {
            if let duration = reference.duration, currentTime >= duration - 0.05 {
                savedTime = 0; player.seek(to: .zero)
            }
            player.play(); changeState(.playing); installProgressObserver()
        } else if let gif, gif.delays.count > 1 {
            changeState(.playing); scheduleFrame()
        }
    }

    func pause() {
        precondition(Thread.isMainThread)
        wantsPlayback = false; deadline?.cancel(); deadline = nil
        player?.pause(); removeProgressObserver()
        if isVisible, state == .playing { changeState(.paused) }
    }

    func togglePlayback() { isPlaying || wantsPlayback ? pause() : play() }

    func seek(to seconds: TimeInterval) {
        precondition(Thread.isMainThread)
        guard !disposed, reference.kind == .video, seconds.isFinite, let duration = reference.duration else { return }
        savedTime = min(duration, max(0, seconds))
        let target = savedTime, expected = generation
        guard let player else { onProgress?(); return }
        pendingSeekTime = target; seekSerial &+= 1
        let serial = seekSerial
        onProgress?()
        player.seek(to: CMTime(seconds: target, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero) { [weak self] finished in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.accepts(expected), self.seekSerial == serial else { return }
                self.pendingSeekTime = nil
                if finished { self.savedTime = target }
                self.onProgress?()
            }
        }
    }

    func dispose() {
        precondition(Thread.isMainThread)
        guard !disposed else { return }
        disposed = true; isVisible = false; generation &+= 1; cancellation.cancel()
        artworkRelease?.cancel(); artworkRelease = nil
        releasePlayback(); changeState(.hidden); onStateChange = nil; onProgress = nil
    }

    private func accepts(_ expected: UInt) -> Bool { !disposed && isVisible && generation == expected }

    private func changeState(_ newState: NotesMediaPlaybackState) {
        guard state != newState else { return }
        state = newState; onStateChange?()
    }

    private func installPlayer(url: URL) {
        let item = AVPlayerItem(url: url)
        let player = AVPlayer(playerItem: item)
        self.player = player
        let video = AVPlayerLayer(player: player)
        video.videoGravity = .resizeAspect; video.frame = layer.bounds
        layer.addSublayer(video); playerLayer = video
        let expected = generation
        statusObserver = item.observe(\.status, options: [.initial, .new]) { [weak self] item, _ in
            DispatchQueue.main.async { [weak self] in
                guard let self, self.accepts(expected) else { return }
                if item.status == .failed {
                    self.error = .decodingFailed; self.releasePlayback(); self.changeState(.failed)
                } else if item.status == .readyToPlay {
                    if self.savedTime > 0 { self.seek(to: self.savedTime) }
                    if self.wantsPlayback { self.play() } else { self.changeState(.paused) }
                }
            }
        }
        endObserver = NotificationCenter.default.addObserver(forName: .AVPlayerItemDidPlayToEndTime,
                                                             object: item, queue: .main) { [weak self] _ in
            guard let self, self.accepts(expected) else { return }
            self.wantsPlayback = false; self.player?.pause(); self.removeProgressObserver(); self.changeState(.paused)
        }
        if wantsPlayback { player.play(); changeState(.playing); installProgressObserver() }
        else { changeState(.paused) }
    }

    private func scheduleFrame() {
        guard isVisible, isPlaying, deadline == nil, !frameDecodePending, let gif else { return }
        let expected = generation
        let cancellation = cancellation
        let delay = gif.delays[frameIndex]
        let work = DispatchWorkItem { [weak self] in
            guard let self, self.accepts(expected), self.isPlaying else { return }
            self.deadline = nil
            self.frameDecodePending = true
            let index = (self.frameIndex + 1) % gif.delays.count
            Self.decodeQueue.async { [weak self] in
                guard !cancellation.isCancelled else { return }
                do {
                    let image = try gif.frame(index), count = gif.cachedFrameCount
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.accepts(expected) else { return }
                        self.frameDecodePending = false
                        guard self.isPlaying else { return }
                        self.frameIndex = index; self.cachedFrameCount = count; self.layer.contents = image
                        self.scheduleFrame()
                    }
                } catch {
                    DispatchQueue.main.async { [weak self] in
                        guard let self, self.accepts(expected) else { return }
                        self.frameDecodePending = false
                        self.error = .decodingFailed; self.releasePlayback(); self.changeState(.failed)
                    }
                }
            }
        }
        deadline = work
        DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: work)
    }

    private func installProgressObserver() {
        guard isVisible, isPlaying, progressObserver == nil, let player else { return }
        let expected = generation
        progressObserver = player.addPeriodicTimeObserver(forInterval: CMTime(seconds: 1, preferredTimescale: 600), queue: .main) { [weak self] _ in
            guard let self, self.accepts(expected), self.isPlaying else { return }
            self.onProgress?()
        }
    }
    private func removeProgressObserver() {
        if let progressObserver { player?.removeTimeObserver(progressObserver) }
        progressObserver = nil
    }

    private func releasePlayback(preserveArtwork: Bool = false) {
        deadline?.cancel(); deadline = nil
        removeProgressObserver()
        frameDecodePending = false
        if let player {
            let time = pendingSeekTime ?? CMTimeGetSeconds(player.currentTime())
            if time.isFinite { savedTime = max(0, time) }
            player.pause(); player.replaceCurrentItem(with: nil)
        }
        pendingSeekTime = nil; seekSerial &+= 1
        statusObserver?.invalidate(); statusObserver = nil
        if let endObserver { NotificationCenter.default.removeObserver(endObserver) }
        endObserver = nil
        playerLayer?.player = nil; playerLayer?.removeFromSuperlayer(); playerLayer = nil; player = nil
        gif = nil; cachedFrameCount = 0
        if !preserveArtwork { layer.contents = nil }
        access?.close(); access = nil
    }

    deinit {
        cancellation.cancel(); deadline?.cancel(); artworkRelease?.cancel(); statusObserver?.invalidate()
        if let endObserver { NotificationCenter.default.removeObserver(endObserver) }
        if let progressObserver { player?.removeTimeObserver(progressObserver) }
        player?.pause(); player?.replaceCurrentItem(with: nil); playerLayer?.player = nil
        access?.close()
    }
}
