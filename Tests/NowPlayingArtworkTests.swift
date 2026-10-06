import AppKit
import ImageIO

/// No URLs are fetched and no real player is queried by these tests.
enum NowPlayingArtworkTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !value { fatalError(message, file: file, line: line) }
        }
        let app = NowPlayingApplication(source: .spotify, pid: 123, bundleURL: URL(fileURLWithPath: "/fixture/Spotify.app"))
        func track(_ id: String, position: Double = 10) -> NowPlayingTrack {
            NowPlayingTrack(title: id, artist: "Artist", album: "Album", duration: 100,
                            position: position, isPlaying: true, sampledAt: position, identifier: id)
        }
        let png = NowPlayingArtworkLoader.fixturePNG()
        let decoded = NowPlayingArtworkLoader.decode(png)
        check(decoded?.width == 96 && decoded?.height == 96, "A valid synthetic cover decodes within a bounded thumbnail")
        check(NowPlayingArtworkLoader.decode(Data()) == nil && NowPlayingArtworkLoader.decode(Data("not artwork".utf8)) == nil,
              "Empty and malformed player data never becomes a cover")
        check(NowPlayingArtworkLoader.decode(Data(repeating: 0, count: NowPlayingArtworkLoader.maximumBytes + 1)) == nil,
              "Compressed artwork larger than 8 MiB is rejected before decoding")
        let dimensions = [(UInt32(8_193), UInt32(1)), (UInt32(8_192), UInt32(8_192)), (UInt32(0), UInt32(96))]
        for (width, height) in dimensions {
            let oversized = replacingPNGDimensions(png, width: width, height: height)
            check(NowPlayingArtworkLoader.decode(oversized) == nil, "Declared image dimensions and decoded pixel budgets are checked before decompression")
        }
        let allowed = URL(string: "https://i.scdn.co/image/fixture")!
        check(NowPlayingArtworkLoader.isAllowedRemoteURL(allowed), "Documented Spotify image CDN is accepted")
        for raw in ["http://i.scdn.co/image/a", "https://user:password@i.scdn.co/image/a", "https://localhost/a",
                    "https://127.0.0.1/a", "https://[::1]/a", "file:///tmp/a.png", "https://i.scdn.co.evil.example/a",
                    "https://i.scdn.co:8443/a", "https://i.scdn.co./a", "https://example.com/a", "https://i.scdn.co/a#fragment"] {
            check(!NowPlayingArtworkLoader.isAllowedRemoteURL(URL(string: raw)!), "Unsafe or unrelated artwork destinations are rejected")
        }
        let goodResponse = HTTPURLResponse(url: allowed, statusCode: 200, httpVersion: "HTTP/1.1", headerFields: ["Content-Type": "image/png", "Content-Length": "128"])!
        check(NowPlayingArtworkDownload.acceptsResponse(goodResponse), "Valid bounded image responses can stream")
        for headers in [["Content-Type": "text/html"], ["Content-Type": "image/png", "Content-Length": "8388609"]] {
            let response = HTTPURLResponse(url: allowed, statusCode: 200, httpVersion: nil, headerFields: headers)!
            check(!NowPlayingArtworkDownload.acceptsResponse(response), "Non-images and declared oversized responses are rejected before data delivery")
        }
        check(!NowPlayingArtworkDownload.acceptsResponse(HTTPURLResponse(url: allowed, statusCode: 302, httpVersion: nil, headerFields: ["Content-Type": "image/png"])!),
              "Redirect responses cannot supply image data")
        var buffer = Data(repeating: 0, count: NowPlayingArtworkLoader.maximumBytes - 3)
        check(NowPlayingArtworkDownload.append(Data([1, 2, 3]), to: &buffer) && buffer.count == NowPlayingArtworkLoader.maximumBytes,
              "Streaming collector accepts exactly its byte limit")
        check(!NowPlayingArtworkDownload.append(Data([4]), to: &buffer) && buffer.count == NowPlayingArtworkLoader.maximumBytes,
              "Chunked responses cannot exceed the byte limit even without Content-Length")

        let harness = Harness(); var resolutions: [String] = []
        let loader = NowPlayingArtworkLoader(resolve: { _, track, cancelled in
            guard !cancelled() else { throw NowPlayingFailure.cancelled }
            resolutions.append(track.title); return .remote(allowed)
        }, work: harness.schedule, deliver: { $0() }, fetch: harness.fetch)
        loader.request(application: app, track: track("A")); harness.drain()
        check(resolutions == ["A"] && harness.replies.count == 1 && loader.image == nil, "Artwork source query starts one asynchronous fetch")
        for value in 1...1_000 { loader.request(application: app, track: track("A", position: Double(value))) }
        check(harness.replies.count == 1 && harness.work.isEmpty, "Same-track playback notifications never repeat a source query or download")
        loader.request(application: app, track: track("B")); loader.request(application: app, track: track("C"))
        check(harness.cancellations == 1 && resolutions == ["A"] && harness.work.isEmpty,
              "Track bursts cancel current work and retain only the latest desired cover")
        harness.replies[0](png); harness.drain()
        check(resolutions == ["A", "C"] && harness.replies.count == 2 && loader.image == nil,
              "Late cancelled artwork cannot appear and an intermediate track is never queried")
        harness.replies[1](png); harness.drain()
        check(loader.image?.width == 96 && loader.cachedImageCountForVerification == 1, "Only current-track artwork publishes")
        harness.replies[0](Data()); harness.drain()
        check(loader.image?.width == 96, "An out-of-order old completion cannot erase the new cover")
        loader.clear(); loader.request(application: app, track: track("C"))
        check(loader.image?.width == 96 && harness.replies.count == 2 && harness.work.isEmpty,
              "Reopening the same track uses its decoded thumbnail without image IO or network")
        loader.request(application: app, track: track("D")); harness.drain(); loader.clear()
        let resolutionCount = resolutions.count
        harness.replies[2](png); harness.drain()
        check(loader.image == nil && !loader.inFlightForVerification && harness.work.isEmpty && resolutions.count == resolutionCount,
              "Hiding cancels work, drops late data and cannot resume loading or polling")

        let embeddedHarness = Harness(); var embeddedReads = 0; var remoteReads = 0
        let embedded = NowPlayingArtworkLoader(resolve: { _, track, _ in
            embeddedReads += 1; return track.title == "missing" ? nil : .embedded(png)
        }, work: embeddedHarness.schedule, deliver: { $0() }, fetch: { _, _ in remoteReads += 1; return {} })
        for title in ["one", "two", "three"] { embedded.request(application: app, track: track(title)); embeddedHarness.drain() }
        check(embedded.cachedImageCountForVerification == 2 && embeddedReads == 3 && remoteReads == 0,
              "Music embedded covers retain at most two decoded images and never use network")
        embedded.request(application: app, track: track("three", position: 80)); embeddedHarness.drain()
        check(embeddedReads == 3, "Metadata-only updates reuse embedded art")
        embedded.request(application: app, track: track("missing")); embeddedHarness.drain()
        embedded.clear(); embedded.request(application: app, track: track("missing")); embeddedHarness.drain()
        check(embedded.image == nil && embeddedReads == 4, "Missing artwork has a bounded negative cache instead of repeated failed reads")
        embedded.clear()

        let retryHarness = Harness()
        let retryBackend = RemoteBackend(track: track("retry"), url: allowed)
        let retryController = NowPlayingController(backend: retryBackend, applications: { [app] }, subscribe: { _ in {} },
            work: retryHarness.schedule, deliver: { $0() }, artworkFetch: retryHarness.fetch)
        retryController.activate(); retryHarness.drain(); retryHarness.replies[0](nil); retryHarness.drain()
        check(retryController.artworkImage == nil && retryHarness.replies.count == 1,
              "A transient transport failure produces a cached missing cover")
        for _ in 0..<20 { retryController.refresh(); retryHarness.drain() }
        check(retryHarness.replies.count == 1 && retryBackend.artworkReads == 1,
              "Normal playback notifications do not retry a failed image download")
        retryController.deactivate(); retryController.activate(); retryHarness.drain()
        check(retryHarness.replies.count == 1, "Reopening still avoids an automatic retry loop for unavailable artwork")
        retryController.refreshManually(); retryHarness.drain()
        check(retryHarness.replies.count == 2 && retryBackend.artworkReads == 2,
              "Explicit Refresh invalidates only the current missing entry and retries its source")
        retryHarness.replies[1](png); retryHarness.drain()
        check(retryController.artworkImage?.width == 96, "Manual Refresh recovers artwork after a transient failure")
        retryController.refreshManually(); retryHarness.drain()
        check(retryHarness.replies.count == 2 && retryBackend.artworkReads == 2,
              "Manual Refresh preserves successfully decoded current artwork without another download")
        retryController.deactivate(); let readsBeforeHidden = retryBackend.reads
        retryController.refreshManually(); retryHarness.drain()
        check(retryHarness.replies.count == 2 && retryBackend.reads == readsBeforeHidden && retryHarness.work.isEmpty,
              "Hidden manual Refresh cannot query the player or artwork transport")

        let queuedHarness = Harness(); var forbiddenReads = 0
        let queued = NowPlayingArtworkLoader(resolve: { _, _, _ in forbiddenReads += 1; return .embedded(png) },
            work: queuedHarness.schedule, deliver: { $0() }, fetch: { _, _ in fatalError("No network") })
        queued.request(application: app, track: track("queued")); queued.clear(); queuedHarness.drain()
        check(forbiddenReads == 0 && queued.image == nil, "Closing before worker delivery prevents all player artwork queries")
        let fixture = NowPlayingController.fixture(); fixture.activate()
        check(fixture.artworkImage?.width == 96, "Native diagnostic provider supplies synthetic album artwork without a real player")
        fixture.deactivate(); check(fixture.artworkImage == nil, "Controller visibility teardown also releases its displayed artwork")
        return count
    }

    private final class RemoteBackend: NowPlayingBackend {
        let track: NowPlayingTrack
        let url: URL
        var reads = 0
        var artworkReads = 0
        init(track: NowPlayingTrack, url: URL) { self.track = track; self.url = url }
        func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
            if cancelled() { throw NowPlayingFailure.cancelled }; reads += 1; return track
        }
        func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload? {
            if cancelled() { throw NowPlayingFailure.cancelled }; artworkReads += 1; return .remote(url)
        }
        func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {}
        func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {}
    }
    private final class Harness {
        var work: [() -> Void] = []
        var replies: [(Data?) -> Void] = []
        var cancellations = 0
        func schedule(_ action: @escaping () -> Void) { work.append(action) }
        func fetch(_ url: URL, completion: @escaping (Data?) -> Void) -> (() -> Void) {
            replies.append(completion); return { [weak self] in self?.cancellations += 1 }
        }
        func drain() {
            var count = 0
            while !work.isEmpty { count += 1; precondition(count < 30); work.removeFirst()() }
        }
    }
    private static func replacingPNGDimensions(_ input: Data, width: UInt32, height: UInt32) -> Data {
        var bytes = [UInt8](input)
        for (offset, value) in [(16, width), (20, height)] {
            for index in 0..<4 { bytes[offset + index] = UInt8((value >> UInt32(24 - index * 8)) & 255) }
        }
        var crc: UInt32 = 0xffff_ffff
        for byte in bytes[12..<29] {
            crc ^= UInt32(byte)
            for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) != 0 ? 0xedb8_8320 : 0) }
        }
        crc ^= 0xffff_ffff
        for index in 0..<4 { bytes[29 + index] = UInt8((crc >> UInt32(24 - index * 8)) & 255) }
        return Data(bytes)
    }
}
