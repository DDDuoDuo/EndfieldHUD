import Foundation
import ImageIO
import CoreGraphics
import UniformTypeIdentifiers

/// Disposable URLProtocol fixtures only; no network, browser, profile or cache.
enum HypergryphAvatarLoaderTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; precondition(condition, message) }
        let url = URL(string: "https://bbs.hycdn.cn/avatar-fixture.png")!
        for host in ["bbs.hycdn.cn", "assets.skland.com", "assets.skport.com", "static.skport.com", "web-static.hg-cdn.com"] {
            check(HypergryphAvatarLoader.isAllowedURL(URL(string: "https://\(host)/avatar.png")!), "Known official image host is accepted")
        }
        for raw in ["https://bbs.hycdn.cn.example.com/a.png", "http://bbs.hycdn.cn/a.png", "https://user@bbs.hycdn.cn/a.png",
                    "https://bbs.hycdn.cn:9443/a.png", "file:///tmp/avatar.png", "https://localhost/a.png"] {
            check(!HypergryphAvatarLoader.isAllowedURL(URL(string: raw)!), "Unknown host or unsafe transport is refused")
        }
        let input = png(width: 1_536, height: 1_024)
        let output = try! HypergryphAvatarLoader.downsample(input)
        let properties = CGImageSourceCopyPropertiesAtIndex(CGImageSourceCreateWithData(output as CFData, nil)!, 0, nil)! as NSDictionary
        check(properties[kCGImagePropertyPixelWidth] as? Int == 512 && properties[kCGImagePropertyPixelHeight] as? Int == 341,
              "Avatar is downsampled to512px preserving aspect")
        check(CGImageSourceGetType(CGImageSourceCreateWithData(output as CFData, nil)!)! as String == UTType.png.identifier,
              "The ready-to-import output is a PNG")
        check(output.count <= HypergryphAvatarLoader.maximumBytes, "Imported avatar remains bounded")
        for invalid in [Data(), Data("not an image".utf8), Data(repeating: 0, count: HypergryphAvatarLoader.maximumBytes + 1), png(width: 9_000, height: 1)] {
            do { _ = try HypergryphAvatarLoader.downsample(invalid); check(false, "Invalid image must fail") }
            catch { check(error as? HypergryphAvatarError == .invalidImage, "Invalid, oversized or extreme-dimension image is rejected before full decode") }
        }
        func makeLoader() -> HypergryphAvatarLoader {
            HypergryphAvatarLoader(configuration: {
                let config = URLSessionConfiguration.ephemeral; config.protocolClasses = [FixtureProtocol.self]
                return config
            })
        }
        func wait(_ complete: @escaping () -> Bool) {
            let end = Date().addingTimeInterval(3)
            while !complete(), Date() < end { RunLoop.current.run(until: Date().addingTimeInterval(0.005)) }
            check(complete(), "Isolated request finishes within bounded time")
        }
        let loader = makeLoader()
        FixtureProtocol.response = .image(input)
        var result: Result<Data, Error>?
        loader.load(url: url) { check(Thread.isMainThread, "Avatar completion returns on main thread"); result = $0 }
        wait { result != nil }
        check((try? result!.get()) == output, "Official-host response uses the bounded image pipeline")
        check(FixtureProtocol.requests.last?.value(forHTTPHeaderField: "Authorization") == nil &&
              FixtureProtocol.requests.last?.value(forHTTPHeaderField: "cred") == nil &&
              FixtureProtocol.requests.last?.value(forHTTPHeaderField: "Cookie") == nil,
              "Avatar request carries no auth headers or browser cookies")
        for (fixture, expected) in [(FixtureResponse.declaredLarge, HypergryphAvatarError.responseTooLarge),
                                    (.streamedLarge, .responseTooLarge), (.status(403), .http(403)),
                                    (.html, .invalidImage), (.redirect, .redirect)] {
            FixtureProtocol.response = fixture; result = nil
            loader.load(url: url) { result = $0 }; wait { result != nil }
            if case .failure(let error) = result! { check(error as? HypergryphAvatarError == expected, "Transport rejects oversized, invalid and redirected responses") }
            else { check(false, "Unsafe fixture must not import an image") }
        }
        let before = FixtureProtocol.requests.count
        result = nil
        loader.load(url: URL(string: "https://example.com/avatar.png")!) { result = $0 }
        check(FixtureProtocol.requests.count == before, "Unknown image host does not even create a request")
        if case .failure(let error) = result! { check(error as? HypergryphAvatarError == .invalidURL, "Unknown host reports unavailable explicitly") }
        else { check(false, "Untrusted host cannot succeed") }
        FixtureProtocol.response = .pending
        var cancelledCallback = false
        loader.load(url: url) { _ in cancelledCallback = true }
        wait { FixtureProtocol.requests.count > before }
        loader.cancel()
        FixtureProtocol.response = .image(input); result = nil
        loader.load(url: url) { result = $0 }; wait { result != nil }
        check(!cancelledCallback, "Cancelled image request cannot update later profile state")
        loader.cancel()
        FixtureProtocol.requests.removeAll(); FixtureProtocol.response = .pending
        return count
    }

    private static func png(width: Int, height: Int) -> Data {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                                space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(CGColor(red: 0.2, green: 0.4, blue: 0.7, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        let data = NSMutableData()
        let target = CGImageDestinationCreateWithData(data, UTType.png.identifier as CFString, 1, nil)!
        CGImageDestinationAddImage(target, context.makeImage()!, nil); precondition(CGImageDestinationFinalize(target))
        return data as Data
    }

    private enum FixtureResponse { case image(Data), declaredLarge, streamedLarge, status(Int), html, redirect, pending }
    private final class FixtureProtocol: URLProtocol {
        private static let lock = NSLock()
        private static var storedResponse = FixtureResponse.pending
        private static var storedRequests: [URLRequest] = []
        static var response: FixtureResponse {
            get { lock.lock(); defer { lock.unlock() }; return storedResponse }
            set { lock.lock(); storedResponse = newValue; lock.unlock() }
        }
        static var requests: [URLRequest] {
            get { lock.lock(); defer { lock.unlock() }; return storedRequests }
            set { lock.lock(); storedRequests = newValue; lock.unlock() }
        }
        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
        override func startLoading() {
            Self.lock.lock(); Self.storedRequests.append(request); Self.lock.unlock()
            func headers(_ status: Int = 200, _ fields: [String: String] = ["Content-Type": "image/png"]) {
                client?.urlProtocol(self, didReceive: HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: fields)!, cacheStoragePolicy: .notAllowed)
            }
            switch Self.response {
            case .pending: return
            case .image(let data): headers(); client?.urlProtocol(self, didLoad: data)
            case .declaredLarge: headers(200, ["Content-Type": "image/png", "Content-Length": String(HypergryphAvatarLoader.maximumBytes + 1)])
            case .streamedLarge:
                headers(); client?.urlProtocol(self, didLoad: Data(repeating: 0, count: HypergryphAvatarLoader.maximumBytes))
                client?.urlProtocol(self, didLoad: Data([1]))
            case .status(let code): headers(code)
            case .html: headers(200, ["Content-Type": "text/html"])
            case .redirect:
                client?.urlProtocol(self, wasRedirectedTo: URLRequest(url: URL(string: "https://example.com/redirect.png")!),
                    redirectResponse: HTTPURLResponse(url: request.url!, statusCode: 302, httpVersion: "HTTP/1.1", headerFields: ["Location": "https://example.com/redirect.png"])!)
                return
            }
            client?.urlProtocolDidFinishLoading(self)
        }
        override func stopLoading() {}
    }
}
