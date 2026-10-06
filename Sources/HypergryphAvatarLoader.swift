import Foundation
import ImageIO

enum HypergryphAvatarError: Error, Equatable {
    case invalidURL, transport, responseTooLarge, invalidImage, redirect
    case http(Int)
}

/// Public avatar images only; credentials, cookies and authorization headers are
/// never sent to image hosts. One transient request, no cache or background poll.
final class HypergryphAvatarLoader {
    static let maximumBytes = 4 * 1_024 * 1_024
    static let maximumPixels = 16_000_000
    static let thumbnailSize = 512
    private var job: HypergryphAvatarDownload?
    private var generation = 0
    private let configuration: () -> URLSessionConfiguration

    init(configuration: @escaping () -> URLSessionConfiguration = { .ephemeral }) {
        self.configuration = configuration
    }

    func load(url: URL, completion: @escaping (Result<Data, Error>) -> Void) {
        precondition(Thread.isMainThread)
        cancel()
        guard Self.isAllowedURL(url) else { completion(.failure(HypergryphAvatarError.invalidURL)); return }
        let generation = self.generation
        let task = HypergryphAvatarDownload(configuration: configuration()) { [weak self] result in
            guard let self, self.generation == generation else { return }
            self.job = nil; completion(result)
        }
        job = task; task.start(url: url)
    }

    func cancel() {
        precondition(Thread.isMainThread)
        generation &+= 1; job?.cancel(); job = nil
    }
    deinit { job?.cancel() }

    static func isAllowedURL(_ url: URL) -> Bool {
        // Hosts referenced by current official CN/global game-tools frontend;
        // unknown future avatar hosts deliberately remain unavailable.
        let hosts: Set<String> = ["bbs.hycdn.cn", "assets.skland.com", "assets.skport.com", "static.skport.com", "web-static.hg-cdn.com"]
        return url.scheme?.lowercased() == "https" && url.user == nil && url.password == nil &&
            (url.port == nil || url.port == 443) && hosts.contains(url.host?.lowercased() ?? "")
    }

    static func downsample(_ data: Data) throws -> Data {
        guard !data.isEmpty, data.count <= maximumBytes,
              let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
              let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
              width > 0, height > 0, width <= 8_192, height <= 8_192,
              width <= maximumPixels / height,
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true,
                kCGImageSourceThumbnailMaxPixelSize: thumbnailSize,
                kCGImageSourceShouldCacheImmediately: true
              ] as CFDictionary) else { throw HypergryphAvatarError.invalidImage }
        let output = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(output, "public.png" as CFString, 1, nil) else {
            throw HypergryphAvatarError.invalidImage
        }
        // No source metadata or additional animation frames enter the profile.
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination), output.length <= maximumBytes else {
            throw HypergryphAvatarError.invalidImage
        }
        return output as Data
    }
}

private final class HypergryphAvatarDownload: NSObject, URLSessionDataDelegate, @unchecked Sendable {
    private static let decodeQueue = DispatchQueue(label: "EndfieldHUD.account-avatar", qos: .utility)
    private let lock = NSLock()
    private let configuration: URLSessionConfiguration
    private var completion: ((Result<Data, Error>) -> Void)?
    private var session: URLSession?
    private var buffer = Data()
    private var cancelled = false
    private var completed = false

    init(configuration: URLSessionConfiguration, completion: @escaping (Result<Data, Error>) -> Void) {
        self.configuration = configuration; self.completion = completion
    }

    func start(url: URL) {
        configuration.urlCache = nil; configuration.httpCookieStorage = nil; configuration.urlCredentialStorage = nil
        configuration.httpShouldSetCookies = false; configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        configuration.httpAdditionalHeaders = [:]
        configuration.timeoutIntervalForRequest = 10; configuration.timeoutIntervalForResource = 10
        configuration.waitsForConnectivity = false
        let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1; queue.qualityOfService = .utility
        let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
        self.session = session
        var request = URLRequest(url: url); request.httpMethod = "GET"
        request.setValue("image/*", forHTTPHeaderField: "Accept")
        session.dataTask(with: request).resume()
    }

    func cancel() {
        lock.lock(); cancelled = true; completion = nil; buffer.removeAll(keepingCapacity: false)
        let session = self.session; self.session = nil; lock.unlock()
        session?.invalidateAndCancel()
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let response = response as? HTTPURLResponse, (200...299).contains(response.statusCode) else {
            completionHandler(.cancel); complete(.failure(HypergryphAvatarError.http((response as? HTTPURLResponse)?.statusCode ?? 0))); return
        }
        guard response.expectedContentLength <= HypergryphAvatarLoader.maximumBytes else {
            completionHandler(.cancel); complete(.failure(HypergryphAvatarError.responseTooLarge)); return
        }
        let mime = response.mimeType?.lowercased() ?? ""
        guard mime.isEmpty || mime.hasPrefix("image/") || mime == "application/octet-stream" else {
            completionHandler(.cancel); complete(.failure(HypergryphAvatarError.invalidImage)); return
        }
        lock.lock(); let active = !cancelled && !completed; lock.unlock()
        completionHandler(active ? .allow : .cancel)
    }

    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        lock.lock()
        guard !cancelled && !completed else { lock.unlock(); return }
        guard data.count <= HypergryphAvatarLoader.maximumBytes - buffer.count else {
            lock.unlock(); complete(.failure(HypergryphAvatarError.responseTooLarge)); return
        }
        buffer.append(data); lock.unlock()
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        lock.lock()
        guard !cancelled && !completed else { lock.unlock(); return }
        let data = buffer; buffer = Data(); self.session = nil; lock.unlock()
        session.finishTasksAndInvalidate()
        guard error == nil else { complete(.failure(HypergryphAvatarError.transport)); return }
        Self.decodeQueue.async { [self] in
            lock.lock(); let active = !cancelled && !completed; lock.unlock()
            guard active else { return }
            complete(Result { try HypergryphAvatarLoader.downsample(data) })
        }
    }

    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil); complete(.failure(HypergryphAvatarError.redirect))
    }

    func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
                    completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        completionHandler(challenge.protectionSpace.authenticationMethod == NSURLAuthenticationMethodServerTrust
                          ? .performDefaultHandling : .cancelAuthenticationChallenge, nil)
    }

    private func complete(_ result: Result<Data, Error>) {
        lock.lock()
        guard !cancelled && !completed else { lock.unlock(); return }
        completed = true; buffer.removeAll(keepingCapacity: false)
        let callback = completion; completion = nil; let session = self.session; self.session = nil
        lock.unlock(); session?.invalidateAndCancel()
        DispatchQueue.main.async { callback?(result) }
    }
}
