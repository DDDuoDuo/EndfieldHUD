import Foundation

struct NowPlayingLyricLine: Equatable {
    let time: Double
    let text: String
}

struct NowPlayingLyrics: Equatable {
    let lines: [NowPlayingLyricLine]
    var hasContent: Bool { lines.contains { !$0.text.isEmpty } }
    init?(lrc: String) {
        guard lrc.utf8.count <= 512 * 1024 else { return nil }
        let stamp = try! NSRegularExpression(pattern: "\\[(?:(\\d{1,2}):)?(\\d{1,3}):(\\d{1,2}(?:[.,]\\d{1,3})?)\\]")
        let offsetPattern = try! NSRegularExpression(pattern: "(?i)\\[offset:([+-]?\\d{1,7})\\]")
        let full = lrc as NSString
        var offset = 0.0
        if let match = offsetPattern.firstMatch(in: lrc, range: NSRange(location: 0, length: full.length)),
           let milliseconds = Double(full.substring(with: match.range(at: 1))) { offset = min(3600, max(-3600, milliseconds / 1000)) }
        var parsed: [(time: Double, text: String, order: Int)] = []
        for line in lrc.components(separatedBy: .newlines).prefix(4096) {
            let raw = line as NSString
            guard raw.length <= 8192 else { continue }
            let matches = stamp.matches(in: line, range: NSRange(location: 0, length: raw.length))
            guard let last = matches.last, matches.first?.range.location == 0 else { continue }
            let text = String(raw.substring(from: NSMaxRange(last.range)).trimmingCharacters(in: .whitespacesAndNewlines).prefix(1024))
            for match in matches.prefix(32) {
                let hours = match.range(at: 1).location == NSNotFound ? 0 : Double(raw.substring(with: match.range(at: 1))) ?? 0
                let minutes = Double(raw.substring(with: match.range(at: 2))) ?? -1
                let seconds = Double(raw.substring(with: match.range(at: 3)).replacingOccurrences(of: ",", with: ".")) ?? -1
                let time = hours * 3600 + minutes * 60 + seconds + offset
                guard minutes >= 0, seconds >= 0, seconds < 60, time.isFinite, time <= 604_800 else { continue }
                parsed.append((max(0, time), text, parsed.count))
                if parsed.count >= 4096 { break }
            }
            if parsed.count >= 4096 { break }
        }
        parsed.sort { $0.time == $1.time ? $0.order < $1.order : $0.time < $1.time }
        var result: [NowPlayingLyricLine] = []
        for line in parsed {
            if let previous = result.last, previous.time == line.time {
                if !line.text.isEmpty, previous.text != line.text {
                    result[result.count - 1] = NowPlayingLyricLine(time: line.time,
                        text: String((previous.text.isEmpty ? line.text : previous.text + " / " + line.text).prefix(1024)))
                }
            } else { result.append(NowPlayingLyricLine(time: line.time, text: line.text)) }
        }
        guard result.contains(where: { !$0.text.isEmpty }) else { return nil }
        lines = result
    }

    /// Three stable rows, with the current line in the middle. No timer, player
    /// read or allocation of the whole document is needed for a display tick.
    func window(at elapsed: Double) -> [String] {
        guard elapsed.isFinite else { return ["", "", lines.first?.text ?? ""] }
        let current = firstIndex(after: elapsed) - 1
        return [current - 1, current, current + 1].map { lines.indices.contains($0) ? lines[$0].text : "" }
    }

    /// Schedule a local display update at the exact next cue, including blank
    /// instrumental cues. This never reads a player or scans the lyric document.
    func nextBoundary(after elapsed: Double) -> Double? {
        guard elapsed.isFinite else { return nil }
        let index = firstIndex(after: elapsed)
        return lines.indices.contains(index) ? lines[index].time : nil
    }
    private func firstIndex(after elapsed: Double) -> Int {
        var low = 0, high = lines.count
        while low < high {
            let middle = (low + high) / 2
            if lines[middle].time <= elapsed { low = middle + 1 } else { high = middle }
        }
        return low
    }
}

/// At most two requests per distinct track, an eight-entry memory cache, and no
/// retry/polling loop. Embedded timed lyrics take precedence over web lookups.
final class NowPlayingLyricsLoader {
    typealias Fetch = (URL, @escaping (Data?) -> Void) -> (() -> Void)
    private(set) var lyrics: NowPlayingLyrics?
    var onChange: (() -> Void)?
    private struct Entry { let key: NowPlayingArtworkKey; let lyrics: NowPlayingLyrics? }
    private var cache: [Entry] = []
    private var desired: NowPlayingArtworkKey?
    private var embeddedSource: String?
    private var cancel: (() -> Void)?
    private var generation = 0
    private let fetch: Fetch
    init(fetch: @escaping Fetch) { self.fetch = fetch }
    deinit { cancel?() }

    func request(application: NowPlayingApplication, track: NowPlayingTrack) {
        precondition(Thread.isMainThread)
        let key = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
        // Players may attach or correct timed lyrics after their first track
        // notification. Artwork identity deliberately omits lyric text, so this
        // authoritative content must be considered before the same-key cache.
        if let text = track.timedLyrics, text != embeddedSource || desired != key {
            embeddedSource = text
            if let embedded = NowPlayingLyrics(lrc: text) {
                generation += 1; cancel?(); cancel = nil; desired = key
                save(embedded, key: key); return
            }
        }
        guard desired != key else { return }
        generation += 1; let generation = generation
        cancel?(); cancel = nil; desired = key; embeddedSource = track.timedLyrics
        if let index = cache.firstIndex(where: { $0.key == key }) {
            let value = cache.remove(at: index); cache.append(value); assign(value.lyrics); return
        }
        assign(nil)
        guard let url = Self.requestURL(for: track) else { save(nil, key: key); return }
        var completed = false
        let cancellation = fetch(url) { [weak self] data in
            let complete = { [weak self] in
                completed = true
                guard let self, self.generation == generation, self.desired == key else { return }
                self.cancel = nil
                if let result = data.flatMap(Self.decodeResponse) { self.save(result, key: key) }
                else { self.searchFallback(track: track, key: key, expected: generation) }
            }
            if Thread.isMainThread { complete() } else { DispatchQueue.main.async(execute: complete) }
        }
        if !completed, self.generation == generation { cancel = cancellation }
    }
    private func searchFallback(track: NowPlayingTrack, key: NowPlayingArtworkKey, expected: Int) {
        guard let url = Self.searchURL(for: track) else { save(nil, key: key); return }
        var completed = false
        let cancellation = fetch(url) { [weak self] data in
            let apply = { [weak self] in
                completed = true
                guard let self, self.generation == expected, self.desired == key else { return }
                self.cancel = nil; self.save(data.flatMap { Self.decodeSearch($0, track: track) }, key: key)
            }
            if Thread.isMainThread { apply() } else { DispatchQueue.main.async(execute: apply) }
        }
        if !completed, generation == expected { cancel = cancellation }
    }
    func invalidateMissing(application: NowPlayingApplication, track: NowPlayingTrack) {
        let key = NowPlayingArtworkKey(application: application, track: track, includeArtworkRevision: false)
        if cache.contains(where: { $0.key == key && $0.lyrics == nil }) {
            cache.removeAll { $0.key == key }; if desired == key { clear() }
        }
    }
    static func searchURL(for track: NowPlayingTrack) -> URL? {
        guard !track.title.isEmpty, !track.artist.isEmpty else { return nil }
        var parts = URLComponents(string: "https://lrclib.net/api/search")!
        parts.queryItems = [.init(name: "track_name", value: track.title),
            .init(name: "artist_name", value: track.artist.components(separatedBy: CharacterSet(charactersIn: "/,、;&")).first ?? track.artist)]
        return parts.url
    }
    static func decodeSearch(_ data: Data, track: NowPlayingTrack) -> NowPlayingLyrics? {
        guard data.count <= NowPlayingLyricsDownload.maximumBytes,
              let rows = try? JSONSerialization.jsonObject(with: data) as? [[String: Any]] else { return nil }
        let candidates: [(Int, NowPlayingLyrics)] = rows.prefix(100).compactMap { row in
            guard let lrc = row["syncedLyrics"] as? String, let lyrics = NowPlayingLyrics(lrc: lrc),
                  let score = NowPlayingTrackMatcher.score(title: row["trackName"] as? String ?? "",
                    artists: [row["artistName"] as? String ?? ""], album: row["albumName"] as? String ?? "",
                    duration: (row["duration"] as? NSNumber)?.doubleValue, target: track) else { return nil }
            return (score, lyrics)
        }.sorted { $0.0 > $1.0 }
        guard let first = candidates.first else { return nil }
        if candidates.count > 1, candidates[1].0 == first.0, candidates[1].1 != first.1 { return nil }
        return first.1
    }
    func clear() {
        generation += 1; cancel?(); cancel = nil; desired = nil; embeddedSource = nil
        assign(nil)
    }
    private func save(_ result: NowPlayingLyrics?, key: NowPlayingArtworkKey) {
        cache.removeAll { $0.key == key }; cache.append(Entry(key: key, lyrics: result))
        if cache.count > 8 { cache.removeFirst(cache.count - 8) }
        assign(result)
    }
    private func assign(_ value: NowPlayingLyrics?) {
        guard lyrics != value else { return }
        lyrics = value; onChange?()
    }
    static func requestURL(for track: NowPlayingTrack) -> URL? {
        guard !track.title.isEmpty, !track.artist.isEmpty else { return nil }
        var parts = URLComponents(string: "https://lrclib.net/api/get")!
        var query = [URLQueryItem(name: "track_name", value: track.title), URLQueryItem(name: "artist_name", value: track.artist)]
        if !track.album.isEmpty { query.append(URLQueryItem(name: "album_name", value: track.album)) }
        if let duration = track.duration { query.append(URLQueryItem(name: "duration", value: String(Int(duration.rounded())))) }
        parts.queryItems = query
        return parts.url
    }
    static func decodeResponse(_ data: Data) -> NowPlayingLyrics? {
        guard data.count <= NowPlayingLyricsDownload.maximumBytes,
              let object = try? JSONSerialization.jsonObject(with: data) as? [String: Any],
              let lrc = object["syncedLyrics"] as? String else { return nil }
        return NowPlayingLyrics(lrc: lrc)
    }
}

/// Public LRCLIB metadata lookup. No cookies, accounts, library reads or audio
/// uploads. Only title/artist/album/duration are sent when the current song changes.
final class NowPlayingLyricsDownload: NSObject, URLSessionDataDelegate {
    static let maximumBytes = 1024 * 1024
    private var session: URLSession?
    private var task: URLSessionDataTask?
    private var bytes = Data()
    private var accepted = false
    private var completion: ((Data?) -> Void)?
    static func isAllowedURL(_ url: URL) -> Bool {
        guard url.scheme == "https", url.user == nil, url.password == nil, url.port == nil,
              url.fragment == nil, url.absoluteString.utf8.count <= 8192 else { return false }
        if url.host == "lrclib.net" { return ["/api/get", "/api/search"].contains(url.path) }
        return url.host == "music.163.com" && ["/api/cloudsearch/pc", "/api/song/lyric"].contains(url.path)
    }
    static func fetch(_ url: URL, completion: @escaping (Data?) -> Void) -> (() -> Void) {
        guard isAllowedURL(url) else { completion(nil); return {} }
        let value = NowPlayingLyricsDownload(); value.completion = completion
        let config = URLSessionConfiguration.ephemeral
        config.urlCache = nil; config.httpCookieStorage = nil; config.httpShouldSetCookies = false
        config.timeoutIntervalForRequest = 8; config.timeoutIntervalForResource = 10
        let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1; queue.qualityOfService = .utility
        let session = URLSession(configuration: config, delegate: value, delegateQueue: queue); value.session = session
        var request = URLRequest(url: url)
        request.setValue("EndfieldHUD/1.0", forHTTPHeaderField: "User-Agent")
        if url.host == "music.163.com" { request.setValue("https://music.163.com/", forHTTPHeaderField: "Referer") }
        let task = session.dataTask(with: request); value.task = task; task.resume()
        return { task.cancel() }
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let response = response as? HTTPURLResponse, response.statusCode == 200,
              response.expectedContentLength <= Int64(Self.maximumBytes),
              ["application/json", "text/plain"].contains(response.mimeType?.lowercased() ?? "") else { completionHandler(.cancel); return }
        accepted = true; completionHandler(.allow)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        guard accepted, data.count <= Self.maximumBytes - bytes.count else { dataTask.cancel(); return }
        bytes.append(data)
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let callback = completion; completion = nil
        callback?(error == nil && accepted ? bytes : nil)
        self.task = nil; self.session = nil; session.finishTasksAndInvalidate()
    }
}
