import Foundation

enum HUDGitHubReleaseTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        let ordered = ["1.0.0-alpha", "1.0.0-alpha.1", "1.0.0-alpha.beta", "1.0.0-beta",
                       "1.0.0-beta.2", "1.0.0-beta.11", "1.0.0-rc.1", "1.0.0", "1.0.1", "1.1.0", "2.0.0", "10.0.0"]
        for pair in zip(ordered, ordered.dropFirst()) {
            check(HUDReleaseVersion(pair.0)! < HUDReleaseVersion(pair.1)!, "SemVer precedence follows the specification, not lexical version strings")
        }
        for tag in ["", "latest", "1", "1.2", "1.2.3.4", "v01.2.3", "1.02.3", "1.2.03", "1.2.3-01", "1.2.3-alpha..1",
                    "1.2.3-", "1.2.3+", "1.2.3+a+b", "v1.2.3/other", "1.2.3-pre_1", "1.2.3-你好", " 1.2.3", "1.2.3\n"] {
            check(HUDReleaseVersion(tag) == nil, "Malformed version tags cannot become update candidates")
        }
        check(HUDReleaseVersion("v0.4.0-preview.1")! < HUDReleaseVersion("v0.4.0-preview.10")!, "Preview numbers sort numerically")
        check(HUDReleaseVersion("1.0.0-99999999999999999999")! < HUDReleaseVersion("1.0.0-100000000000000000000")!,
              "Large numeric identifiers compare without integer overflow")
        check(HUDReleaseVersion("1.0.0+001") == HUDReleaseVersion("v1.0.0+other"), "Build metadata does not affect SemVer precedence")
        check(HUDGitHubRelease.currentTag(info: ["HUDReleaseTag": "v0.4.0-preview.1", "CFBundleShortVersionString": "0.4.0"]) == "v0.4.0-preview.1",
              "The packaged exact preview tag takes precedence over the marketing version")
        check(HUDGitHubRelease.currentTag(info: ["HUDReleaseTag": "invalid", "CFBundleShortVersionString": "0.4.0"]) == "0.4.0"
              && HUDGitHubRelease.currentTag(info: [:]) == nil, "Current version fallback is validated and missing versions stay unknown")
        let validURL = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0"
        check(HUDGitHubRelease.validatedURL(validURL, tag: "v1.0.0") != nil, "Only the intended release-page location is accepted")
        for value in ["http://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0",
                      "https://github.com.evil.test/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0",
                      "https://github.com@evil.test/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0",
                      "https://user@github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0", validURL + "?redirect=evil", validURL + "#download",
                      "https://github.com:443/DDDuoDuo/EndfieldHUD/releases/tag/v1.0.0",
                      "https://github.com/DDDuoDuo/Other/releases/tag/v1.0.0",
                      "https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.0.0/file.dmg",
                      "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v2.0.0", validURL + "/.."] {
            check(HUDGitHubRelease.validatedURL(value, tag: "v1.0.0") == nil, "Spoofed hosts, credentials, assets and mismatched tags are rejected")
        }

        func entry(_ tag: String, draft: Bool = false, published: Bool = true, url: String? = nil, prerelease: Bool? = nil) -> [String: Any] {
            ["tag_name": tag, "name": "Release \(tag)", "draft": draft,
             "prerelease": prerelease ?? !(HUDReleaseVersion(tag)?.prerelease.isEmpty ?? true),
             "published_at": published ? "2026-09-28T12:00:00Z" as Any : NSNull(),
             "html_url": url ?? "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/\(tag)"]
        }
        func response(_ entries: [[String: Any]], status: Int = 200, headers: [String: String] = [:]) -> HUDGitHubReleaseClient.Response {
            HUDGitHubReleaseClient.Response(statusCode: status, headers: headers,
                body: try! JSONSerialization.data(withJSONObject: entries), url: HUDGitHubReleaseClient.endpoint)
        }
        var requests: [URLRequest] = []
        var completions: [(Result<HUDGitHubReleaseClient.Response, HUDGitHubReleaseError>) -> Void] = []
        var cancelCount = 0
        var clock = Date(timeIntervalSince1970: 10_000)
        let client = HUDGitHubReleaseClient(transport: { request, limit, completion in
            check(limit == HUDGitHubReleaseClient.maximumResponseBytes, "Transport receives the bounded response cap")
            requests.append(request); completions.append(completion)
            return { cancelCount += 1 }
        }, now: { clock }, deliver: { $0() })
        var last: HUDGitHubReleaseStatus?
        func begin(_ current: String? = "v0.4.0-preview.1", enabled: Bool = true) {
            client.check(currentTag: current, enabled: enabled) { last = $0 }
        }
        begin(enabled: false)
        check(last == .disabled && requests.isEmpty, "Disabled checks perform no network work")
        begin("bad")
        check(last == .failed(.invalidCurrentVersion) && requests.isEmpty, "An unknown local version is never reported up to date")
        begin()
        let request = requests.last!
        check(request.url == HUDGitHubReleaseClient.endpoint && request.url?.path.hasSuffix("/releases") == true
              && request.value(forHTTPHeaderField: "Authorization") == nil && request.timeoutInterval == 12,
              "Checks enumerate public releases including previews, without credentials and with a finite timeout")
        completions.last!(.success(response([entry("v0.4.0-preview.1")], headers: ["ETag": "\"first\""])))
        if case .current(let release) = last { check(release.tag == "v0.4.0-preview.1", "The installed preview is current when it matches the release") }
        else { check(false, "Matching preview must be current") }
        begin("0.3.0-preview.1")
        check(requests.last!.value(forHTTPHeaderField: "If-None-Match") == "\"first\"", "A verified ETag is sent on the next check")
        completions.last!(.success(response([], status: 304)))
        if case .available(let release) = last { check(release.tag == "v0.4.0-preview.1", "A 304 reuses decoded metadata and reevaluates against the current version") }
        else { check(false, "Cached preview must still be available to an older version") }
        begin("0.3.0")
        completions.last!(.success(response([], status: 304)))
        check(last == .failed(.noMatchingReleases), "A stable client never advertises a cached preview as a compatible update")
        begin()
        completions.last!(.success(response([entry("v0.4.0-preview.10"), entry("v0.4.0-preview.2"), entry("v0.4.0"),
                                            entry("v9.0.0", draft: true), entry("v8.0.0", published: false), entry("not-a-version"),
                                            entry("v7.0.0", url: "https://evil.test/latest") ])))
        if case .available(let release) = last { check(release.tag == "v0.4.0" && !release.isPrerelease, "Stable outranks its previews; drafts, unpublished and invalid entries are excluded") }
        else { check(false, "The stable release is newer than its installed preview") }
        begin("0.4.0-preview.1")
        completions.last!(.success(response([entry("v0.5.0-preview.2"), entry("v0.5.0-preview.10")])))
        if case .available(let release) = last { check(release.tag == "v0.5.0-preview.10" && release.isPrerelease, "Preview discovery includes the highest numerical prerelease") }
        else { check(false, "The next minor preview is a newer release") }
        begin("0.4.0")
        completions.last!(.success(response([entry("v0.4.1"), entry("v0.5.0-preview.10"), entry("v2.0.0", prerelease: true),
                                            entry("v3.0.0-preview.1", prerelease: false)], headers: ["ETag": "\"channels\""])))
        if case .available(let release) = last { check(release.tag == "v0.4.1" && !release.isPrerelease, "Stable clients exclude API-marked previews and prerelease tags regardless of the API flag") }
        else { check(false, "Stable client must select the newest stable release") }
        begin("0.4.0-preview.1")
        completions.last!(.success(response([], status: 304)))
        if case .available(let release) = last { check(release.tag == "v3.0.0-preview.1", "A 304 after a stable check still retains the newest preview candidate") }
        else { check(false, "Preview channel must reuse its candidate from the same response cache") }
        begin("0.4.0+build-a")
        completions.last!(.success(response([], status: 304)))
        if case .available(let release) = last { check(release.tag == "v0.4.1", "A hyphen in build metadata does not opt a stable client into previews") }
        else { check(false, "Build metadata must not change the stable update channel") }
        begin("0.4.1")
        completions.last!(.success(response([], status: 304)))
        if case .current(let release) = last { check(release.tag == "v0.4.1", "A stable installation is current even when a newer preview exists") }
        else { check(false, "Matching stable release must be current") }
        begin("1.0.0")
        completions.last!(.success(response([entry("0.9.0")])))
        if case .current = last { check(true, "A locally newer build never prompts a downgrade") } else { check(false, "Older remote version must not prompt an update") }
        for status in [301, 401, 403, 404, 500, 503] {
            begin(); completions.last!(.success(response([], status: status)))
            check(last == .failed(.httpStatus(status)), "HTTP failures are not interpreted as up-to-date")
        }
        begin(); completions.last!(.failure(.network("Offline")))
        check(last == .failed(.network("Offline")), "Network errors retain explicit unknown status")
        begin(); completions.last!(.success(response([])))
        check(last == .failed(.noPublishedReleases), "An empty usable release list is distinct from being up-to-date")
        begin(); completions.last!(.success(HUDGitHubReleaseClient.Response(statusCode: 200, headers: [:], body: Data("not json".utf8), url: HUDGitHubReleaseClient.endpoint)))
        check(last == .failed(.invalidResponse), "Malformed JSON cannot produce a successful status")
        begin(); completions.last!(.success(HUDGitHubReleaseClient.Response(statusCode: 200, headers: [:], body: Data(repeating: 0, count: HUDGitHubReleaseClient.maximumResponseBytes + 1), url: HUDGitHubReleaseClient.endpoint)))
        check(last == .failed(.responseTooLarge), "Injected oversized responses also honor the transport bound")
        begin(); completions.last!(.success(HUDGitHubReleaseClient.Response(statusCode: 200, headers: [:], body: Data("[]".utf8), url: URL(string: "https://evil.test")!)))
        check(last == .failed(.invalidResponse), "Responses from unexpected destinations are rejected")
        begin(); completions.last!(.success(response([], status: 403, headers: ["x-ratelimit-remaining": "0", "X-RateLimit-Reset": "10100"])))
        check(last == .failed(.rateLimited(until: Date(timeIntervalSince1970: 10_100))), "Rate-limit reset headers become an explicit retry time")
        let rateLimitedRequests = requests.count
        begin()
        check(requests.count == rateLimitedRequests && last == .failed(.rateLimited(until: Date(timeIntervalSince1970: 10_100))), "Checks respect the server's backoff without another request")
        clock = Date(timeIntervalSince1970: 10_101)
        begin(); completions.last!(.success(response([], status: 429, headers: ["Retry-After": "30"])))
        check(last == .failed(.rateLimited(until: Date(timeIntervalSince1970: 10_131))), "Secondary rate limits honor Retry-After")
        clock = Date(timeIntervalSince1970: 10_132)
        begin(); let oldCompletion = completions.last!
        begin(); oldCompletion(.success(response([entry("99.0.0")])))
        completions.last!(.success(response([entry("0.4.0")])))
        if case .available(let release) = last { check(release.tag == "0.4.0" && cancelCount > 0, "Superseded requests cannot publish stale status or metadata") }
        else { check(false, "Only newest request status must be delivered") }
        let beforeCancellation = last
        let cancel = client.check(currentTag: "0.3.0") { last = $0 }
        let cancelledCompletion = completions.last!; cancel()
        cancelledCompletion(.success(response([entry("99.0.0")])))
        check(last == beforeCancellation, "Cancellation prevents delayed callbacks from updating UI state")
        var synchronousCancellations = 0
        let uncached = HUDGitHubReleaseClient(transport: { _, _, callback in
            callback(.success(response([], status: 304))); return { synchronousCancellations += 1 }
        }, deliver: { $0() })
        uncached.check(currentTag: "1.0.0") { last = $0 }
        check(last == .failed(.unexpectedNotModified), "A 304 without validated cached metadata is an error")
        uncached.check(currentTag: "1.0.0", enabled: false) { last = $0 }
        check(synchronousCancellations == 0, "Synchronously completed transports do not retain stale cancellation handles")
        return count
    }
}
