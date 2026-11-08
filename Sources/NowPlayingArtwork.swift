import AppKit
import CoreImage
import ImageIO

/// Transient public player metadata; never persisted or included in Event Log.
enum NowPlayingArtworkPayload { case embedded(Data), remote(URL) }

struct NowPlayingArtworkKey: Hashable {
    let source: String
    let pid: Int32
    let identity: String?
    let title: String
    let artist: String
    let album: String
    let duration: Double?
    let revision: String?
    init(application: NowPlayingApplication, track: NowPlayingTrack, includeArtworkRevision: Bool = true) {
        source = application.bundleIdentifier; pid = application.pid; identity = track.identifier
        title = track.title; artist = track.artist; album = track.album; duration = track.duration
        revision = includeArtworkRevision ? track.artworkRevision : nil
    }
}

/// A decoded cover. `native`: the source's own upright pixel size. A cover
/// that fills its slot without being enlarged more than
/// `NowPlayingArtworkLoader.fillLimit` (Music, Spotify) is drawn filling it as
/// before, with no `backdrop`. A smaller one (a browser's 150 px Media Session
/// thumbnail) is drawn whole, Lanczos-resampled to at most
/// `maximumEnlargement` times its own size, over `backdrop`: a small blurred
/// and darkened fill of the same picture, made once here rather than per frame.
struct NowPlayingArtworkImage {
    let image: CGImage
    let native: CGSize
    let backdrop: CGImage?
}

private final class NowPlayingArtworkToken {
    private let lock = NSLock()
    private var value = false
    func cancel() { lock.lock(); value = true; lock.unlock() }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
}

/// Main-thread coordinator: one job plus one replaceable desired track, two
/// decoded covers (including negative results), and no timer or disk cache.
/// Covers are decoded at the size they are drawn (`pixelSize`, the slot's
/// pixels), never larger; a cover much smaller than its slot is enlarged at
/// most twice, over a backdrop (`NowPlayingArtworkImage`).
final class NowPlayingArtworkLoader {
    typealias Resolve = (NowPlayingApplication, NowPlayingTrack, @escaping () -> Bool) throws -> NowPlayingArtworkPayload?
    typealias Fetch = (URL, @escaping (Data?) -> Void) -> (() -> Void)
    /// Downloads and a player's scripting reply.
    static let maximumBytes = 8 * 1_024 * 1_024
    /// Cover bytes a player hands over directly (MediaRemote, the adapter):
    /// bounded by their pixels (`maximumPixels`, checked from the header) and
    /// by a ceiling that fits an uncompressed RGBA cover of the largest decode
    /// size, so a browser's uncompressed TIFF of up to 2048 px still shows.
    /// The adapter's line limit is derived from it; a larger cover drops only
    /// its art, never the track or the stream.
    static let maximumEmbeddedBytes = maximumPixelSize * maximumPixelSize * 4 + 1_024 * 1_024
    static let maximumPixels = 32_000_000
    /// The decode size until the UI passes its slot's: about the 480-point
    /// slot on the reference display (735 px) without decoding far beyond it.
    static let defaultPixelSize = 720
    /// A cover enlarged more than this to fill its slot is drawn whole over a
    /// backdrop instead, enlarged at most `maximumEnlargement` times.
    static let fillLimit: CGFloat = 1.25
    static let maximumEnlargement: CGFloat = 2
    /// The backdrop's side in pixels, and its blur (in those pixels) and
    /// darkening (EV): heavy and dim enough to sit under the cover and title.
    static let backdropPixels = 96
    static let backdropBlur: CGFloat = 6
    static let backdropExposure: CGFloat = -1.2
    /// Bounds of a requested decode side; 2048 px is 16 MiB of decoded pixels.
    static let minimumPixelSize = 64
    static let maximumPixelSize = 2_048
    /// A drawn slot's longer side in pixels: points times the layer's backing
    /// scale, rounded up, within the decode bounds.
    static func pixelSize(points: CGFloat, scale: CGFloat) -> Int {
        let pixels = Double(points * scale)
        guard pixels.isFinite, pixels > 0 else { return defaultPixelSize }
        return boundedPixelSize(Int(min(Double(maximumPixelSize), (pixels - 1e-6).rounded(.up))))
    }
    static func boundedPixelSize(_ pixels: Int) -> Int { min(maximumPixelSize, max(minimumPixelSize, pixels)) }
    private(set) var artwork: NowPlayingArtworkImage?
    var image: CGImage? { artwork?.image }
    /// The longest decoded side requested for the drawn cover. Change it with
    /// `setPixelSize`.
    private(set) var pixelSize: Int
    var onChange: (() -> Void)?
    /// `trackKey`: the revisionless key, the same track whatever its artwork
    /// revision (the identity Catalog and Lyrics use).
    private struct Request {
        let key: NowPlayingArtworkKey; let trackKey: NowPlayingArtworkKey; let app: NowPlayingApplication; let track: NowPlayingTrack
        init(application: NowPlayingApplication, track: NowPlayingTrack) {
            key = NowPlayingArtworkKey(application: application, track: track)
            trackKey = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
            app = application; self.track = track
        }
    }
    /// `pixels`: the decode size this entry stands for.
    private struct Entry {
        let key: NowPlayingArtworkKey; let trackKey: NowPlayingArtworkKey; let artwork: NowPlayingArtworkImage?; let pixels: Int
        var image: CGImage? { artwork?.image }
    }
    private var cache: [Entry] = []
    private var desired: Request?
    private var token: NowPlayingArtworkToken?
    private var cancelFetch: (() -> Void)?
    private var supplements: [(NowPlayingArtworkKey, URL)] = []
    private let resolve: Resolve
    private let work: (@escaping () -> Void) -> Void
    private let deliver: (@escaping () -> Void) -> Void
    private let fetch: Fetch

    init(resolve: @escaping Resolve, work: @escaping (@escaping () -> Void) -> Void,
         deliver: @escaping (@escaping () -> Void) -> Void,
         fetch: @escaping Fetch = NowPlayingArtworkDownload.fetch, pixelSize: Int = NowPlayingArtworkLoader.defaultPixelSize) {
        self.resolve = resolve; self.work = work; self.deliver = deliver; self.fetch = fetch
        self.pixelSize = Self.boundedPixelSize(pixelSize)
    }
    deinit { token?.cancel(); cancelFetch?() }
    var cachedImageCountForVerification: Int { cache.filter { $0.image != nil }.count }
    var inFlightForVerification: Bool { token != nil }

    func request(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        let request = Request(application: application, track: track)
        guard desired?.key != request.key else { return }
        desired = request; token?.cancel()
        let cancellation = cancelFetch; cancelFetch = nil
        artwork = nil
        if let index = cache.firstIndex(where: { $0.key == request.key }) {
            let entry = cache.remove(at: index); cache.append(entry); artwork = entry.artwork
        }
        // Only the artwork revision changed (a reopened player stream restarts
        // it, or sends the cover later): keep the same track's newest decoded
        // cover on screen, and keep it cached, while this key resolves.
        if artwork == nil, let index = cache.lastIndex(where: { $0.image != nil && $0.trackKey == request.trackKey }) {
            let entry = cache.remove(at: index); cache.append(entry); artwork = entry.artwork
        }
        onChange?(); cancellation?(); startLatestIfNeeded()
    }
    /// The drawn slot's size in pixels (see `pixelSize(points:scale:)`). The shown
    /// cover, if decoded for another size, is decoded again for this one and
    /// stays visible until its replacement is ready; other cached covers of
    /// another size are dropped rather than kept at a size nothing draws.
    func setPixelSize(_ value: Int) {
        precondition(Thread.isMainThread)
        let value = Self.boundedPixelSize(value)
        guard value != pixelSize else { return }
        pixelSize = value
        cache.removeAll { $0.image != nil && $0.image !== image && !fits($0) }
        startLatestIfNeeded()
    }
    func provideArtworkURL(_ url: URL, application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        guard Self.isAllowedRemoteURL(url) else { return }
        let key = NowPlayingArtworkKey(application: application, track: track)
        guard !supplements.contains(where: { $0.0 == key && $0.1 == url }) else { return }
        supplements.removeAll { $0.0 == key }; supplements.append((key, url))
        if supplements.count > 8 { supplements.removeFirst(supplements.count - 8) }
        guard !cache.contains(where: { $0.key == key && $0.image != nil }) else { return }
        cache.removeAll { $0.key == key }
        if desired?.key == key {
            desired = nil; token?.cancel()
            let cancellation = cancelFetch; cancelFetch = nil; cancellation?()
            request(application: application, track: track)
        }
    }
    /// Only an explicit user refresh or a new Automation grant may retire a
    /// missing-art result. Normal metadata notifications and reopenings
    /// retain the negative cache.
    func invalidateMissing(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        let key = NowPlayingArtworkKey(application: application, track: track)
        guard let index = cache.firstIndex(where: { $0.key == key && $0.image == nil }) else { return }
        cache.remove(at: index)
        if desired?.key == key { desired = nil }
    }
    func clear() {
        precondition(Thread.isMainThread)
        desired = nil; token?.cancel()
        let cancellation = cancelFetch; cancelFetch = nil; artwork = nil
        cancellation?()
    }
    /// Whether an entry serves the current size: a missing cover, one decoded
    /// for this size, or one a new decode would draw the same (a cover at its
    /// own full size within both, or a small one at twice its size).
    private func fits(_ entry: Entry) -> Bool {
        guard let artwork = entry.artwork, entry.pixels != pixelSize else { return true }
        return Self.layout(native: artwork.native, pixels: entry.pixels) == Self.layout(native: artwork.native, pixels: pixelSize)
    }
    private func startLatestIfNeeded() {
        guard token == nil, let request = desired, !cache.contains(where: { $0.key == request.key && fits($0) }) else { return }
        let token = NowPlayingArtworkToken(); self.token = token
        let resolve = self.resolve, deliver = self.deliver, pixels = pixelSize
        let supplement = supplements.first { $0.0 == request.key }?.1
        work { [weak self] in
            guard !token.cancelled else { deliver { [weak self] in self?.finish(request, token: token, artwork: nil, pixels: pixels) }; return }
            let payload = (try? resolve(request.app, request.track, { token.cancelled })) ?? supplement.map(NowPlayingArtworkPayload.remote)
            if case .embedded(let data) = payload {
                let artwork = token.cancelled ? nil : Self.decode(data, pixelSize: pixels)
                deliver { [weak self] in self?.finish(request, token: token, artwork: artwork, pixels: pixels) }
            } else if case .remote(let url) = payload, Self.isAllowedRemoteURL(url), !token.cancelled {
                deliver { [weak self] in self?.download(Self.remoteURL(url, pixels: pixels), request: request, token: token, pixels: pixels) }
            } else { deliver { [weak self] in self?.finish(request, token: token, artwork: nil, pixels: pixels) } }
        }
    }
    private func download(_ url: URL, request: Request, token: NowPlayingArtworkToken, pixels: Int) {
        guard self.token === token, !token.cancelled, desired?.key == request.key else { finish(request, token: token, artwork: nil, pixels: pixels); return }
        let deliver = self.deliver, work = self.work
        let cancellation = fetch(url) { [weak self] data in
            guard !token.cancelled else { deliver { [weak self] in self?.finish(request, token: token, artwork: nil, pixels: pixels) }; return }
            work { [weak self] in
                let artwork = token.cancelled ? nil : data.flatMap { Self.decode($0, pixelSize: pixels) }
                deliver { [weak self] in self?.finish(request, token: token, artwork: artwork, pixels: pixels) }
            }
        }
        // An injected transport can complete synchronously. Do not retain its
        // cancellation callback if that completion already retired the job.
        if self.token === token { cancelFetch = cancellation }
    }
    private func finish(_ request: Request, token: NowPlayingArtworkToken, artwork: NowPlayingArtworkImage?, pixels: Int) {
        guard self.token === token else { return }
        self.token = nil; cancelFetch = nil
        if !token.cancelled, desired?.key == request.key {
            let shown = cache.first { $0.key == request.key }?.artwork
            cache.removeAll { $0.key == request.key }
            if artwork == nil, let shown {
                // A failed decode for a new size keeps the cover already shown,
                // which then stands for this size: no gap and no repeated attempt.
                cache.append(Entry(key: request.key, trackKey: request.trackKey, artwork: shown, pixels: pixelSize))
            } else if artwork == nil, self.artwork != nil {
                // No cover under this revision, but the same track's earlier
                // cover is shown: record the miss and keep that cover. It is not
                // copied in under this key, so a later cover URL can still fill it.
                cache.append(Entry(key: request.key, trackKey: request.trackKey, artwork: nil, pixels: pixels))
            } else {
                // A new cover replaces the same track's covers of other revisions.
                if artwork != nil { cache.removeAll { $0.image != nil && $0.trackKey == request.trackKey } }
                cache.append(Entry(key: request.key, trackKey: request.trackKey, artwork: artwork, pixels: pixels))
                self.artwork = artwork; onChange?()
            }
            if cache.count > 2 { cache.removeFirst(cache.count - 2) }
        }
        startLatestIfNeeded()
    }

    /// First frame scaled down so its longer side is at most `pixelSize`; a
    /// smaller image keeps its own size, or, if it would be enlarged more than
    /// `fillLimit` to fill the slot, is drawn whole at most `maximumEnlargement`
    /// times its size over a backdrop (`layout`). Runs on the worker.
    static func decode(_ data: Data, pixelSize: Int = defaultPixelSize) -> NowPlayingArtworkImage? {
        let limit = boundedPixelSize(pixelSize)
        guard !data.isEmpty, data.count <= maximumEmbeddedBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
              let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
              width > 0, height > 0, width <= 8_192, height <= 8_192, width * height <= maximumPixels else { return nil }
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: limit,
            kCGImageSourceShouldCacheImmediately: true]
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary),
              image.width <= limit, image.height <= limit else { return nil }
        let turned = (5...8).contains((properties[kCGImagePropertyOrientation] as? NSNumber)?.intValue ?? 1)
        let native = CGSize(width: turned ? height : width, height: turned ? width : height)
        let plan = layout(native: native, pixels: limit)
        guard plan.small else { return NowPlayingArtworkImage(image: image, native: native, backdrop: nil) }
        let picture = trimmingLetterbox(image)
        let canvas = CIImage(cgImage: picture), extent = canvas.extent, clamped = canvas.clampedToExtent()
        let factor = CGFloat(plan.side) / max(extent.width, extent.height)
        let square = min(extent.width, extent.height)
        guard let foreground = factor > 1 ? HUDPortraitArtwork.scaledImage(clamped, region: extent, factor: factor) : picture,
              let backdrop = HUDPortraitArtwork.scaledImage(clamped,
                  region: CGRect(x: extent.midX - square / 2, y: extent.midY - square / 2, width: square, height: square),
                  factor: CGFloat(backdropPixels) / square, finish: {
                      $0.applyingFilter("CIGaussianBlur", parameters: [kCIInputRadiusKey: backdropBlur])
                        .applyingFilter("CIExposureAdjust", parameters: [kCIInputEVKey: backdropExposure])
                  }) else { return NowPlayingArtworkImage(image: image, native: native, backdrop: nil) }
        return NowPlayingArtworkImage(image: foreground, native: native, backdrop: backdrop)
    }

    /// How a cover of `native` pixels is drawn in a slot of `pixels`: its
    /// drawn longer side, and whether it is small (filling the slot would
    /// enlarge it more than `fillLimit`, so it is drawn whole over a backdrop,
    /// enlarged at most `maximumEnlargement` times).
    static func layout(native: CGSize, pixels: Int) -> (small: Bool, side: Int) {
        let long = max(native.width, native.height), short = min(native.width, native.height), slot = CGFloat(pixels)
        guard short > 0, slot / short > fillLimit else { return (false, Int(min(long, slot).rounded())) }
        return (true, Int(min(slot, long * maximumEnlargement).rounded()))
    }

    /// A video still with near-black bars above and below a 16:9 picture
    /// (YouTube's 4:3 thumbnails) loses the bars; anything else is returned as
    /// it is. Only opaque, near-black, nearly equal bars that leave a 16:9
    /// picture count, so a dark album cover keeps its edges.
    static func trimmingLetterbox(_ image: CGImage) -> CGImage {
        let width = image.width, height = image.height
        guard width > height, width <= maximumPixelSize,
              let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                  space: CGColorSpace(name: CGColorSpace.sRGB)!,
                  bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return image }
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        guard let data = context.data else { return image }
        let pixels = data.assumingMemoryBound(to: UInt8.self), stride = context.bytesPerRow
        func dark(_ row: Int) -> Bool {
            (0..<width).allSatisfy { x in
                let i = row * stride + x * 4
                return pixels[i + 3] >= 250 && max(pixels[i], pixels[i + 1], pixels[i + 2]) <= 24
            }
        }
        var top = 0, bottom = 0
        while top < height / 2, dark(top) { top += 1 }
        while bottom < height / 2, dark(height - 1 - bottom) { bottom += 1 }
        let kept = height - top - bottom
        guard top >= 2, bottom >= 2, abs(top - bottom) <= max(2, height / 40), kept > 0,
              (1.70...1.86).contains(CGFloat(width) / CGFloat(kept)),
              let cropped = image.cropping(to: CGRect(x: 0, y: top, width: width, height: kept)) else { return image }
        return cropped
    }

    /// NetEase's image CDN serves any requested square: ask for at least the
    /// drawn size (`NowPlayingCatalog.coverPixels(forDrawn:)`) instead of a small
    /// player thumbnail. Other CDNs' URLs name a fixed image and stay unchanged.
    static func remoteURL(_ url: URL, pixels: Int) -> URL {
        guard let host = url.host?.lowercased(), netEaseHosts.contains(host) else { return url }
        return NowPlayingCatalog.coverURL(url.absoluteString, pixels: NowPlayingCatalog.coverPixels(forDrawn: pixels)) ?? url
    }
    private static let netEaseHosts: Set<String> = ["p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net"]

    /// Known Spotify/NetEase image CDNs only; arbitrary web/local URLs from a
    /// scripting reply are never fetched. Redirects are disallowed separately.
    static func isAllowedRemoteURL(_ url: URL) -> Bool {
        guard url.absoluteString.utf8.count <= 2_048, let parts = URLComponents(url: url, resolvingAgainstBaseURL: false),
              parts.scheme?.lowercased() == "https", parts.user == nil, parts.password == nil,
              parts.port == nil || parts.port == 443, parts.fragment == nil,
              let host = parts.host?.lowercased(), !host.hasSuffix(".") else { return false }
        return host == "i.scdn.co" || host == "mosaic.scdn.co"
            || host == "image-cdn.spotifycdn.com"
            || netEaseHosts.contains(host)
    }

    /// Synthetic art for isolated diagnostics only, never a fallback album cover.
    static func fixturePNG() -> Data {
        let color = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: nil, width: 96, height: 96, bitsPerComponent: 8, bytesPerRow: 96 * 4,
                                      space: color, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return Data() }
        context.setFillColor(CGColor(red: 0.09, green: 0.16, blue: 0.22, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: 96, height: 96))
        context.setFillColor(CGColor(red: 0.85, green: 0.70, blue: 0.22, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: 24, height: 96))
        context.setStrokeColor(CGColor(gray: 0.95, alpha: 1)); context.setLineWidth(5); context.strokeEllipse(in: CGRect(x: 33, y: 21, width: 47, height: 47))
        guard let image = context.makeImage() else { return Data() }
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(output, "public.png" as CFString, 1, nil) else { return Data() }
        CGImageDestinationAddImage(destination, image, nil)
        return CGImageDestinationFinalize(destination) ? output as Data : Data()
    }
}

/// URLSession streams into this bounded buffer rather than allocating an entire
/// untrusted response through a data-task convenience completion handler.
final class NowPlayingArtworkDownload: NSObject, URLSessionDataDelegate {
    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var bytes = Data()
    private var acceptedResponse = false
    private var completion: ((Data?) -> Void)?
    static func fetch(_ url: URL, completion: @escaping (Data?) -> Void) -> (() -> Void) {
        guard NowPlayingArtworkLoader.isAllowedRemoteURL(url) else { completion(nil); return {} }
        let download = NowPlayingArtworkDownload(); download.completion = completion
        let configuration = URLSessionConfiguration.ephemeral
        configuration.urlCache = nil; configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.httpCookieStorage = nil; configuration.httpShouldSetCookies = false
        configuration.urlCredentialStorage = nil; configuration.timeoutIntervalForRequest = 8
        configuration.timeoutIntervalForResource = 10; configuration.httpMaximumConnectionsPerHost = 1
        let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1; queue.qualityOfService = .utility
        let session = URLSession(configuration: configuration, delegate: download, delegateQueue: queue)
        let task = session.dataTask(with: url)
        download.session = session; download.task = task; task.resume()
        return { task.cancel() }
    }
    static func acceptsResponse(_ response: URLResponse) -> Bool {
        guard let response = response as? HTTPURLResponse, (200...299).contains(response.statusCode),
              let url = response.url, NowPlayingArtworkLoader.isAllowedRemoteURL(url),
              response.mimeType?.lowercased().hasPrefix("image/") == true else { return false }
        return response.expectedContentLength < 0 || response.expectedContentLength <= NowPlayingArtworkLoader.maximumBytes
    }
    static func append(_ chunk: Data, to bytes: inout Data) -> Bool {
        guard chunk.count <= NowPlayingArtworkLoader.maximumBytes - bytes.count else { return false }
        bytes.append(chunk); return true
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        acceptedResponse = Self.acceptsResponse(response); completionHandler(acceptedResponse ? .allow : .cancel)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        if !acceptedResponse || !Self.append(data, to: &bytes) { acceptedResponse = false; dataTask.cancel() }
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let completion = self.completion; self.completion = nil
        let result = error == nil && acceptedResponse && !bytes.isEmpty ? bytes : nil
        bytes = Data(); self.task = nil; self.session = nil; session.finishTasksAndInvalidate()
        completion?(result)
    }
}
