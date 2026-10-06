import Foundation

enum NowPlayingCatalogTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; if !value { fatalError(message) } }
        let track = NowPlayingTrack(title: "Song!", artist: "Artist/Guest", album: "Album", duration: 100,
            position: 30, isPlaying: true, sampledAt: 0)
        let app = NowPlayingApplication(source: .netease, pid: 12, bundleURL: URL(fileURLWithPath: "/Fixture.app"))
        func song(_ id: Int, name: String = "Song!", artist: String = "Artist", duration: Int = 100000) -> [String: Any] {
            ["id": id, "name": name, "ar": [["name": artist], ["name": "Guest"]], "dt": duration,
             "al": ["name": "Album", "picUrl": "http://p1.music.126.net/genuine-cover.jpg"]]
        }
        func response(_ songs: [[String: Any]]) -> Data { try! JSONSerialization.data(withJSONObject: ["code": 200, "result": ["songs": songs]]) }
        let matched = NowPlayingCatalog.match(data: response([song(1)]), track: track)
        check(matched?.identifier == 1 && matched?.artwork?.scheme == "https", "Exact recording metadata resolves a real catalog ID and HTTPS cover")
        check(URLComponents(url: matched!.artwork!, resolvingAgainstBaseURL: false)?.queryItems?.first?.value == "600y600", "NetEase cover requests ask for a small thumbnail at source")
        check(NowPlayingCatalog.match(data: response([song(2, name: "Song! Remix")]), track: track) == nil, "A remix is not substituted for the current recording")
        check(NowPlayingCatalog.match(data: response([song(2, duration: 125000)]), track: track) == nil, "A duration mismatch rejects a look-alike recording")
        check(NowPlayingCatalog.match(data: response([song(1), song(2)]), track: track) == nil, "Equally strong different recording IDs are not guessed")
        check(NowPlayingCatalog.match(data: Data("{}".utf8), track: track) == nil, "Invalid catalog responses fail without fabricated metadata")
        check(NowPlayingCatalog.coverURL("https://p1.music.126.net.evil.invalid/image.jpg") == nil && NowPlayingCatalog.coverURL("file:///tmp/cover.jpg") == nil, "Only real NetEase CDN hosts can supply catalog covers")
        check(NowPlayingArtworkLoader.isAllowedRemoteURL(matched!.artwork!), "The artwork loader accepts the verified NetEase CDN URL")
        let lrc = try! JSONSerialization.data(withJSONObject: ["code": 200, "lrc": ["lyric": "[00:00]First\n[00:30]Current\n[01:00]Next"]])
        check(NowPlayingCatalog.decodeLyrics(lrc)?.window(at: 31) == ["First", "Current", "Next"], "NetEase's own timed lyrics use the shared three-line parser")
        var replies: [(Data?) -> Void] = [], requests: [URL] = [], cancellations = 0
        let loader = NowPlayingCatalog { url, completion in requests.append(url); replies.append(completion); return { cancellations += 1 } }
        loader.request(application: app, track: track)
        for _ in 0..<20 { loader.request(application: app, track: track) }
        check(requests.count == 1 && loader.isLoading, "One distinct track makes one shared cover/lyrics catalog search")
        replies[0](response([song(1)]))
        check(loader.artwork != nil && loader.lyrics == nil && requests.count == 2, "The cover becomes available before timed lyrics finish")
        check(requests[1].path == "/api/song/lyric", "Only the matched catalog song requests lyrics")
        replies[1](lrc)
        check(loader.lyrics?.hasContent == true && !loader.isLoading, "Completed timed lyrics and cover share one cached result")
        loader.clear(); loader.request(application: app, track: track)
        check(requests.count == 2 && loader.artwork != nil && loader.lyrics != nil, "Reopening the same song reuses cached cover and lyrics without requests")
        let other = NowPlayingTrack(title: "Other", artist: "Artist", album: "", duration: 100, position: 0, isPlaying: false, sampledAt: 0)
        loader.request(application: app, track: other); let old = replies.last!
        loader.clear(); old(response([song(1)]))
        check(loader.artwork == nil && loader.lyrics == nil && cancellations == 1, "Hiding cancels lookup and ignores stale catalog completions")
        let search = try! JSONSerialization.data(withJSONObject: [["trackName": "Song!", "artistName": "Artist, Guest", "albumName": "Translated album", "duration": 100, "syncedLyrics": "[00:00]Found"]])
        check(NowPlayingLyricsLoader.decodeSearch(search, track: track)?.hasContent == true, "LRCLIB search matches artist and duration despite an album translation")
        let wrong = try! JSONSerialization.data(withJSONObject: [["trackName": "Song!", "artistName": "Other", "duration": 100, "syncedLyrics": "[00:00]Wrong"]])
        check(NowPlayingLyricsLoader.decodeSearch(wrong, track: track) == nil, "LRCLIB fallback rejects another artist's same-title song")
        let urls = [NowPlayingCatalog.searchURL(track)!, NowPlayingCatalog.lyricsURL(identifier: 1), NowPlayingLyricsLoader.searchURL(for: track)!]
        check(urls.allSatisfy(NowPlayingLyricsDownload.isAllowedURL), "Only the exact public search and timed-lyric API paths are enabled")
        check(!NowPlayingLyricsDownload.isAllowedURL(URL(string: "https://music.163.com/api/user/account")!), "Account and unrelated NetEase endpoints remain inaccessible")
        var coverLoads = 0
        let coverLoader = NowPlayingArtworkLoader(resolve: { _, _, _ in nil }, work: { $0() }, deliver: { $0() }, fetch: { _, completion in
            coverLoads += 1; completion(NowPlayingArtworkLoader.fixturePNG()); return {}
        })
        coverLoader.request(application: app, track: track)
        check(coverLoader.image == nil, "Native missing artwork is initially cached without a false cover")
        coverLoader.provideArtworkURL(matched!.artwork!, application: app, track: track)
        check(coverLoader.image != nil && coverLoads == 1, "A later exact catalog match recovers the native missing-art cache immediately")
        coverLoader.provideArtworkURL(matched!.artwork!, application: app, track: track)
        coverLoader.clear(); coverLoader.request(application: app, track: track)
        check(coverLoads == 1 && coverLoader.image != nil, "Repeated catalog callbacks and reopen reuse the decoded thumbnail")
        return count
    }
}
