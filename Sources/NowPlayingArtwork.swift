import AppKit
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

private final class NowPlayingArtworkToken {
    private let lock = NSLock()
    private var value = false
    func cancel() { lock.lock(); value = true; lock.unlock() }
    var cancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
}

/// Main-thread coordinator: one job plus one replaceable desired track, two
/// decoded thumbnails (including negative results), and no timer or disk cache.
final class NowPlayingArtworkLoader {
    typealias Resolve = (NowPlayingApplication, NowPlayingTrack, @escaping () -> Bool) throws -> NowPlayingArtworkPayload?
    typealias Fetch = (URL, @escaping (Data?) -> Void) -> (() -> Void)
    static let maximumBytes = 8 * 1_024 * 1_024
    static let maximumPixels = 32_000_000
    static let thumbnailSize = 512
    private(set) var image: CGImage?
    var onChange: (() -> Void)?
    private struct Request { let key: NowPlayingArtworkKey; let app: NowPlayingApplication; let track: NowPlayingTrack }
    private struct Entry { let key: NowPlayingArtworkKey; let image: CGImage? }
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
         fetch: @escaping Fetch = NowPlayingArtworkDownload.fetch) {
        self.resolve = resolve; self.work = work; self.deliver = deliver; self.fetch = fetch
    }
    deinit { token?.cancel(); cancelFetch?() }
    var cachedImageCountForVerification: Int { cache.filter { $0.image != nil }.count }
    var inFlightForVerification: Bool { token != nil }

    func request(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        let request = Request(key: NowPlayingArtworkKey(application: application, track: track), app: application, track: track)
        guard desired?.key != request.key else { return }
        desired = request; token?.cancel()
        let cancellation = cancelFetch; cancelFetch = nil
        image = nil
        if let index = cache.firstIndex(where: { $0.key == request.key }) {
            let entry = cache.remove(at: index); cache.append(entry); image = entry.image
        }
        onChange?(); cancellation?(); startLatestIfNeeded()
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
    /// Only an explicit user refresh may retire a missing-art result. Normal
    /// metadata notifications and reopenings retain the negative cache.
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
        let cancellation = cancelFetch; cancelFetch = nil; image = nil
        cancellation?()
    }
    private func startLatestIfNeeded() {
        guard token == nil, let request = desired, !cache.contains(where: { $0.key == request.key }) else { return }
        let token = NowPlayingArtworkToken(); self.token = token
        let resolve = self.resolve, deliver = self.deliver
        let supplement = supplements.first { $0.0 == request.key }?.1
        work { [weak self] in
            guard !token.cancelled else { deliver { [weak self] in self?.finish(request, token: token, image: nil) }; return }
            let payload = (try? resolve(request.app, request.track, { token.cancelled })) ?? supplement.map(NowPlayingArtworkPayload.remote)
            if case .embedded(let data) = payload {
                let image = token.cancelled ? nil : Self.decode(data)
                deliver { [weak self] in self?.finish(request, token: token, image: image) }
            } else if case .remote(let url) = payload, Self.isAllowedRemoteURL(url), !token.cancelled {
                deliver { [weak self] in self?.download(url, request: request, token: token) }
            } else { deliver { [weak self] in self?.finish(request, token: token, image: nil) } }
        }
    }
    private func download(_ url: URL, request: Request, token: NowPlayingArtworkToken) {
        guard self.token === token, !token.cancelled, desired?.key == request.key else { finish(request, token: token, image: nil); return }
        let deliver = self.deliver, work = self.work
        let cancellation = fetch(url) { [weak self] data in
            guard !token.cancelled else { deliver { [weak self] in self?.finish(request, token: token, image: nil) }; return }
            work { [weak self] in
                let image = token.cancelled ? nil : data.flatMap(Self.decode)
                deliver { [weak self] in self?.finish(request, token: token, image: image) }
            }
        }
        // An injected transport can complete synchronously. Do not retain its
        // cancellation callback if that completion already retired the job.
        if self.token === token { cancelFetch = cancellation }
    }
    private func finish(_ request: Request, token: NowPlayingArtworkToken, image: CGImage?) {
        guard self.token === token else { return }
        self.token = nil; cancelFetch = nil
        if !token.cancelled, desired?.key == request.key {
            cache.removeAll { $0.key == request.key }; cache.append(Entry(key: request.key, image: image))
            if cache.count > 2 { cache.removeFirst(cache.count - 2) }
            self.image = image; onChange?()
        }
        startLatestIfNeeded()
    }

    static func decode(_ data: Data) -> CGImage? {
        guard !data.isEmpty, data.count <= maximumBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
              let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
              width > 0, height > 0, width <= 8_192, height <= 8_192, width * height <= maximumPixels else { return nil }
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: thumbnailSize,
            kCGImageSourceShouldCacheImmediately: true]
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary),
              image.width <= thumbnailSize, image.height <= thumbnailSize else { return nil }
        return image
    }

    /// Known Spotify/NetEase image CDNs only; arbitrary web/local URLs from a
    /// scripting reply are never fetched. Redirects are disallowed separately.
    static func isAllowedRemoteURL(_ url: URL) -> Bool {
        guard url.absoluteString.utf8.count <= 2_048, let parts = URLComponents(url: url, resolvingAgainstBaseURL: false),
              parts.scheme?.lowercased() == "https", parts.user == nil, parts.password == nil,
              parts.port == nil || parts.port == 443, parts.fragment == nil,
              let host = parts.host?.lowercased(), !host.hasSuffix(".") else { return false }
        return host == "i.scdn.co" || host == "mosaic.scdn.co"
            || host == "image-cdn.spotifycdn.com"
            || ["p1.music.126.net", "p2.music.126.net", "p3.music.126.net", "p4.music.126.net"].contains(host)
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
