import Foundation

// Oracle for the Windows release checker (windows/packaging/update_check.*).
// Compiled beside the UNCHANGED Sources/HUDGitHubRelease.swift: every row is
// produced by the Mac HUDReleaseVersion, HUDGitHubRelease.validatedURL and
// HUDGitHubReleaseClient (with an injected synchronous transport, a fixed
// clock and synchronous delivery). Synthetic metadata only; no network.
@main enum UpdateReleaseReference {
    static let endpoint = HUDGitHubReleaseClient.endpoint.absoluteString
    static let base = "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/"

    static func entry(_ tag: Any, name: Any? = nil, url: Any? = nil, draft: Any = false, prerelease: Any = false,
                      published: Any? = "2026-10-01T00:00:00Z", extra: [String: Any] = [:]) -> [String: Any] {
        var value: [String: Any] = ["tag_name": tag, "draft": draft, "prerelease": prerelease]
        value["html_url"] = url ?? (tag as? String).map { base + $0 } ?? NSNull()
        if let name { value["name"] = name } else { value["name"] = NSNull() }
        if let published { value["published_at"] = published } else { value["published_at"] = NSNull() }
        for (k, v) in extra { value[k] = v }
        return value
    }
    static func json(_ value: Any) -> String {
        String(data: try! JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .fragmentsAllowed]), encoding: .utf8)!
    }
    // One transport step: a response (status/headers/body/url) or a failure.
    struct Step { var now: Double; var status = 200; var headers: [String: String] = [:]; var body = "[]"; var url: String? = nil; var failure: String? = nil }
    struct Scenario { var name: String; var current: String?; var steps: [Step] }

    // Values of an ignored field: number grammar, literals and string escapes.
    static let tokenProbes = ["0", "-0", "01", "-01", "00", "1.", "1.5", ".5", "-.5", "+1", "-", "1e", "1e+", "1e5", "1E-5", "1.e5", "1e400",
        "-1e400", "123456789012345678901234567890", "0x10", "Infinity", "-Infinity", "NaN", "true", "false", "null", "tru", "nul", "TRUE",
        "\"a\\x\"", "\"\\u00zz\"", "\"\\u00e9\"", "\"\\uD83D\\uDE00\"", "\"\\uDE00\"", "\"\\'\"", "\"\\/\"", "\"\u{7F}\"", "\"\u{1F}\"",
        "\"\\U0041\"", "[]", "{}", "[[[]]]", "{\"a\":[1,2,{\"b\":null}]}", "1 2", "\"a\" \"b\"", "[1 2]", "{\"a\" 1}", "{\"a\":}", "{a:1}", "{\"a\":1 \"b\":2}"]

    static func errorRow(_ error: HUDGitHubReleaseError) -> [String: Any] {
        switch error {
        case .invalidCurrentVersion: return ["error": "invalidCurrentVersion"]
        case .invalidResponse: return ["error": "invalidResponse"]
        case .responseTooLarge: return ["error": "responseTooLarge"]
        case .noPublishedReleases: return ["error": "noPublishedReleases"]
        case .noMatchingReleases: return ["error": "noMatchingReleases"]
        case .unexpectedNotModified: return ["error": "unexpectedNotModified"]
        case .httpStatus(let code): return ["error": "httpStatus", "status": code]
        case .rateLimited(let until): return ["error": "rateLimited", "until": until.map { $0.timeIntervalSince1970 } as Any? ?? NSNull()]
        case .network: return ["error": "network"]
        case .cancelled: return ["error": "cancelled"]
        }
    }
    static func releaseRow(_ kind: String, _ r: HUDGitHubRelease) -> [String: Any] {
        ["result": kind, "tag": r.tag, "title": r.title, "url": r.url.absoluteString, "prerelease": r.isPrerelease]
    }

    static func run(_ scenario: Scenario) -> [String: Any] {
        var now = Date(timeIntervalSince1970: 0)
        var pending: Step?
        var sent: [String: String] = [:]
        var requested = false
        let client = HUDGitHubReleaseClient(transport: { request, limit, completion in
            requested = true
            sent = request.allHTTPHeaderFields ?? [:]
            precondition(request.url == HUDGitHubReleaseClient.endpoint && limit == HUDGitHubReleaseClient.maximumResponseBytes)
            let step = pending!
            if let failure = step.failure {
                completion(.failure(failure == "cancelled" ? .cancelled : .network("synthetic")))
            } else {
                completion(.success(.init(statusCode: step.status, headers: step.headers, body: Data(step.body.utf8),
                                          url: URL(string: step.url ?? endpoint)!)))
            }
            return {}
        }, now: { now }, deliver: { $0() })
        var rows: [[String: Any]] = []
        for step in scenario.steps {
            now = Date(timeIntervalSince1970: step.now); pending = step; requested = false; sent = [:]
            var outcome: [String: Any] = [:]
            client.check(currentTag: scenario.current) { status in
                switch status {
                case .disabled: outcome = ["result": "disabled"]
                case .current(let r): outcome = releaseRow("current", r)
                case .available(let r): outcome = releaseRow("available", r)
                case .failed(let e): outcome = ["result": "failed"].merging(errorRow(e)) { a, _ in a }
                }
            }
            var row: [String: Any] = ["now": step.now, "requested": requested, "expect": outcome]
            if requested {
                row["request"] = ["accept": sent["Accept"] as Any? ?? NSNull(), "apiVersion": sent["X-GitHub-Api-Version"] as Any? ?? NSNull(),
                                  "userAgent": sent["User-Agent"] as Any? ?? NSNull(), "ifNoneMatch": sent["If-None-Match"] as Any? ?? NSNull()]
                if let failure = step.failure { row["failure"] = failure }
                else { row["response"] = ["status": step.status, "headers": step.headers, "body": step.body, "url": step.url ?? endpoint] }
            }
            rows.append(row)
        }
        return ["name": scenario.name, "current": scenario.current as Any? ?? NSNull(), "steps": rows]
    }

    static func main() throws {
        guard CommandLine.arguments.count == 2 else { throw NSError(domain: "UpdateReference", code: 1) }
        // --- HUDReleaseVersion: acceptance and precedence --------------------------
        let long = "v1.2.3-" + String(repeating: "a", count: 121)
        let tags = ["v1.2.0", "1.2.0", "v1.2.1", "v1.10.0", "v1.9.9", "v2.0.0", "v0.0.0", "v01.2.0", "v1.02.0", "v1.2.00",
            "v1.2", "v1.2.0.0", "V1.2.0", " v1.2.0", "v1.2.0 ", "vv1.2.0", "v1.2.0-", "v1.2.0-rc", "v1.2.0-rc.1", "v1.2.0-rc.2",
            "v1.2.0-rc.10", "v1.2.0-rc.01", "v1.2.0-0", "v1.2.0-00", "v1.2.0-alpha", "v1.2.0-alpha.1", "v1.2.0-alpha.beta",
            "v1.2.0-beta", "v1.2.0-beta.2", "v1.2.0-beta.11", "v1.2.0-alpha..1", "v1.2.0-alpha_1", "v1.2.0-x-y-z.--",
            "v1.2.0+build", "v1.2.0+build.01", "v1.2.0+", "v1.2.0+a+b", "v1.2.0-rc.1+exp.sha.5114f85", "v1.2.0+build.5",
            "v99999999999999999999.0.0", "v99999999999999999998.0.0", "v1.2.0-1", "v1.2.0-2", "v1.2.0-10", "v1.2.0-a1",
            "windows-v1.2.0", "windows-v1.3.0", "windows-1.2.0", "v1.2.0-windows", "v1.2.0+windows", "", "v", "v-1.2.0",
            "v1.-2.0", "v1.2.0\u{0}", "v１.2.0", long, long + "a", "v1.2.3-\u{e9}"]
        var versions: [[String: Any]] = []
        for tag in tags { versions.append(["tag": tag, "valid": HUDReleaseVersion(tag) != nil]) }
        let valid = tags.filter { HUDReleaseVersion($0) != nil }
        var less: [[Bool]] = []
        for a in valid { less.append(valid.map { HUDReleaseVersion(a)! < HUDReleaseVersion($0)! }) }

        // --- HUDGitHubRelease.validatedURL --------------------------------------------
        let urlCases: [(String, String)] = [
            (base + "v1.2.0", "v1.2.0"), ("https://GitHub.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("http://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("HTTPS://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("https://github.com:443/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("https://user@github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("https://user:pass@github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), (base + "v1.2.0?x=1", "v1.2.0"), (base + "v1.2.0?", "v1.2.0"),
            (base + "v1.2.0#top", "v1.2.0"), (base + "v1.2.0#", "v1.2.0"), (base + "v1.2.1", "v1.2.0"), (base + "v1.2.0/", "v1.2.0"),
            ("https://github.com/dddoudou/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("https://api.github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("https://github.com.evil.example/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), (base + "v1%2E2%2E0", "v1.2.0"),
            (base + "v1.2.0", "1.2"), (base + "v1.2.0-rc.1", "v1.2.0-rc.1"), (base + "v1.2.0+build.5", "v1.2.0+build.5"),
            ("https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/windows-v1.2.0", "windows-v1.2.0"), (base + " v1.2.0", "v1.2.0"),
            ("https://github.com//DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("https://github.com/DDDuoDuo/EndfieldHUD/releases/download/v1.2.0", "v1.2.0"),
            ("", "v1.2.0"), ("github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("https:///DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("https://github.com:/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), (base + "v1.2.0%", "v1.2.0"), (base + "v1.2.0%zz", "v1.2.0"),
            ("https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0%2B1", "v1.2.0+1"), ("https://github.com/DDDuoDuo/EndfieldHUD/releases/tag%2Fv1.2.0", "v1.2.0"),
            ("https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0\\", "v1.2.0"), ("https://GITHUB.COM/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"),
            ("https://github.com./DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0"), ("https://[::1]/DDDuoDuo/EndfieldHUD/releases/tag/v1.2.0", "v1.2.0")]
        var urls: [[String: Any]] = []
        for (value, tag) in urlCases {
            urls.append(["value": value, "tag": tag, "url": HUDGitHubRelease.validatedURL(value, tag: tag)?.absoluteString as Any? ?? NSNull()])
        }

        // --- Foundation CharacterSet.controlCharacters (titles, ETags) ---------------
        var control: [[UInt32]] = []
        var scalar: UInt32 = 0
        while scalar <= 0x10FFFF {
            if let s = Unicode.Scalar(scalar), CharacterSet.controlCharacters.contains(s) {
                if let last = control.last, last[1] + 1 == scalar { control[control.count - 1][1] = scalar } else { control.append([scalar, scalar]) }
            }
            scalar += 1
        }

        // --- HUDGitHubReleaseClient scenarios ---------------------------------------
        let longName = String(repeating: "名", count: 150) + String(repeating: "x", count: 100)
        let many = (0..<101).map { entry("v0.0.\($0)") }
        let s: [Scenario] = [
            Scenario(name: "stable update", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v1.2.1", name: "EndfieldHUD 1.2.1"), entry("v1.2.0"), entry("v1.3.0-beta.1", prerelease: true)]))]),
            Scenario(name: "current release", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v1.2.0"), entry("v1.1.9")]))]),
            Scenario(name: "newer installed", current: "v1.3.0", steps: [Step(now: 1000, body: json([entry("v1.2.0")]))]),
            Scenario(name: "prerelease channel", current: "v1.3.0-beta.1", steps: [Step(now: 1000, body: json([entry("v1.2.1"), entry("v1.3.0-beta.2", prerelease: true)]))]),
            Scenario(name: "prerelease channel stable newest", current: "v1.3.0-beta.1", steps: [Step(now: 1000, body: json([entry("v1.3.0"), entry("v1.3.0-beta.2", prerelease: true)]))]),
            Scenario(name: "prerelease flag on stable version", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v1.2.5", prerelease: true), entry("v1.2.1")]))]),
            Scenario(name: "semver prerelease without flag", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v1.2.5-rc.1"), entry("v1.2.1")]))]),
            Scenario(name: "ignored entries", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v9.0.0", draft: true), entry("v8.0.0", published: nil),
                entry("v7.0.0", url: base + "v6.0.0"), entry("windows-v6.0.0"), entry("6.0.0", url: base + "6.0.0"), entry("v5.0.0", url: "http://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v5.0.0"), entry("v1.2.0")]))]),
            Scenario(name: "no published releases", current: "v1.2.0", steps: [Step(now: 1000, body: "[]"), Step(now: 1001, body: json([entry("v2.0.0", draft: true)]))]),
            Scenario(name: "windows tags only", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("windows-v1.3.0"), entry("windows-v1.2.0")]))]),
            Scenario(name: "no matching channel", current: "v1.2.0", steps: [Step(now: 1000, body: json([entry("v1.3.0-beta.1", prerelease: true), entry("v1.2.5", prerelease: true)]))]),
            Scenario(name: "invalid current", current: "1.2", steps: [Step(now: 1000)]),
            Scenario(name: "missing current", current: nil, steps: [Step(now: 1000)]),
            Scenario(name: "windows current tag", current: "windows-v1.2.0", steps: [Step(now: 1000)]),
            Scenario(name: "etag flow", current: "v1.2.0", steps: [
                Step(now: 1000, headers: ["ETag": "W/\"abc\""], body: json([entry("v1.2.1")])),
                Step(now: 1001, status: 304, headers: ["ETag": "W/\"abc\""], body: ""),
                Step(now: 1002, body: json([entry("v1.2.0")])),
                Step(now: 1003, status: 304, body: "")]),
            Scenario(name: "etag header case", current: "v1.2.0", steps: [
                Step(now: 1000, headers: ["etag": "\"lower\""], body: json([entry("v1.2.0")])),
                Step(now: 1001, status: 304, body: "")]),
            Scenario(name: "not modified without etag", current: "v1.2.0", steps: [Step(now: 1000, status: 304, body: "")]),
            Scenario(name: "invalid etags", current: "v1.2.0", steps: [
                Step(now: 1000, headers: ["ETag": "\"a\u{7}b\""], body: json([entry("v1.2.1")])),
                Step(now: 1001, status: 304, body: ""),
                Step(now: 1002, headers: ["ETag": String(repeating: "e", count: 513)], body: json([entry("v1.2.1")])),
                Step(now: 1003, status: 304, body: ""),
                Step(now: 1004, headers: ["ETag": ""], body: json([entry("v1.2.1")])),
                Step(now: 1005, status: 304, body: ""),
                Step(now: 1006, headers: ["ETag": String(repeating: "e", count: 512)], body: json([entry("v1.2.1")])),
                Step(now: 1007, status: 304, body: "")]),
            Scenario(name: "retry after", current: "v1.2.0", steps: [
                Step(now: 1000, status: 429, headers: ["Retry-After": "120"], body: ""),
                Step(now: 1060), Step(now: 1120),
                Step(now: 1121, body: json([entry("v1.2.0")])),
                Step(now: 1122, body: json([entry("v1.2.0")]))]),
            Scenario(name: "retry after cap and fraction", current: "v1.2.0", steps: [
                Step(now: 1000, status: 429, headers: ["retry-after": "999999"], body: ""),
                Step(now: 1000 + 86399), Step(now: 1000 + 86401, body: json([entry("v1.2.1")])),
                Step(now: 200000, status: 429, headers: ["Retry-After": "1.5"], body: ""), Step(now: 200001.4), Step(now: 200001.6, body: "[]")]),
            Scenario(name: "rate limit reset", current: "v1.2.0", steps: [
                Step(now: 1000, status: 403, headers: ["X-RateLimit-Remaining": "0", "X-RateLimit-Reset": "5000"], body: "{}"),
                Step(now: 4999), Step(now: 5000, body: json([entry("v1.2.0")]))]),
            Scenario(name: "rate limit forms", current: "v1.2.0", steps: [
                Step(now: 1000, status: 403, headers: ["Retry-After": "soon"], body: ""),
                Step(now: 1001, status: 403, headers: ["Retry-After": "-5", "X-RateLimit-Reset": "3000"], body: ""),
                Step(now: 3001, status: 429, headers: ["X-RateLimit-Reset": "nan"], body: ""),
                Step(now: 3002, status: 429, headers: ["Retry-After": "inf", "X-RateLimit-Reset": "0"], body: ""),
                Step(now: 3003, status: 429, headers: ["Retry-After": "1e2"], body: ""),
                Step(now: 3200, status: 403, headers: ["X-RateLimit-Remaining": "1"], body: ""),
                Step(now: 3201, status: 403, headers: ["X-RateLimit-Remaining": "00"], body: ""),
                Step(now: 3202, status: 403, headers: ["Retry-After": ""], body: ""),
                Step(now: 3203, body: json([entry("v1.2.0")]))]),
            Scenario(name: "http statuses", current: "v1.2.0", steps: [
                Step(now: 1000, status: 500, body: ""), Step(now: 1001, status: 201, body: json([entry("v1.2.0")])),
                Step(now: 1002, status: 404, body: "{}"), Step(now: 1003, status: 302, headers: ["Location": "https://example.com/"], body: "")]),
            Scenario(name: "invalid bodies", current: "v1.2.0", steps: [
                Step(now: 1000, body: "not json"), Step(now: 1001, body: "{}"), Step(now: 1002, body: json([["tag_name": "v1.2.1", "html_url": base + "v1.2.1", "prerelease": false]])),
                Step(now: 1003, body: json([entry("v1.2.1", prerelease: "yes")])), Step(now: 1004, body: json([entry("v1.2.1", published: 5)])),
                Step(now: 1005, body: json([entry(12)])), Step(now: 1006, body: json([entry("v1.2.1", name: 7)])),
                Step(now: 1007, body: json([entry("v1.2.1", draft: 0)])), Step(now: 1008, body: json([1])),
                Step(now: 1009, body: json([entry("v1.2.1", extra: ["assets": [["name": "x"]], "unknown": NSNull()])])),
                Step(now: 1010, body: json([entry("v1.2.1", url: 5)])), Step(now: 1011, body: "[" + json(entry("v1.2.1")) + ",]"),
                Step(now: 1012, body: json(many)), Step(now: 1013, body: json(Array(many.prefix(100)))),
                Step(now: 1014, body: "\u{FEFF}" + json([entry("v1.2.1")])), Step(now: 1015, body: " " + json([entry("v1.2.1")]) + "\n")]),
            // Foundation parser leniency probes (the Windows decoder must agree).
            Scenario(name: "json syntax", current: "v1.2.0", steps: [
                Step(now: 1000, body: "[" + String(json(entry("v1.2.1")).dropLast()) + ",}]"),
                Step(now: 1001, body: "[,]"), Step(now: 1002, body: "[" + json(entry("v1.2.1")) + ",,]"),
                Step(now: 1003, body: "[," + json(entry("v1.2.1")) + "]"), Step(now: 1004, body: "[ ]"), Step(now: 1005, body: "[\n" + json(entry("v1.2.1")) + "\n,\n]"),
                Step(now: 1006, body: "[{\"tag_name\":\"v1.2.1\",\"tag_name\":\"v1.2.2\",\"html_url\":\"" + base + "v1.2.2\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\"}]"),
                Step(now: 1007, body: "/* c */" + json([entry("v1.2.1")])), Step(now: 1008, body: "[{'tag_name':'v1.2.1'}]"),
                Step(now: 1009, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":NaN}]"),
                Step(now: 1010, body: "[{\"tag_name\":\"v1.2.1\",\"name\":\"a\tb\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\"}]"),
                Step(now: 1011, body: "[{\"tag_name\":\"v1.2.1\",\"name\":\"a\\ud800b\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\"}]"),
                Step(now: 1012, body: "[{\"tag_name\":\"v1.2.1\",\"name\":\"\\u0041\\u00e9\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"\"}]"),
                Step(now: 1013, body: json([entry("v1.2.1")]) + "x"), Step(now: 1014, body: json([entry("v1.2.1")]) + "[]"),
                Step(now: 1015, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":01}]"),
                Step(now: 1016, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":1.}]"),
                Step(now: 1017, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":[1,]}]"),
                Step(now: 1018, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":{\"a\":1,}}]"),
                Step(now: 1019, body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":null,\"prerelease\":false,\"published_at\":\"x\"}]"),
                Step(now: 1020, body: "\u{FEFF}\u{FEFF}" + json([entry("v1.2.1")])), Step(now: 1021, body: "")]),
            Scenario(name: "json tokens", current: "v1.2.0", steps: tokenProbes.enumerated().map { index, token in
                Step(now: 2000 + Double(index), body: "[{\"tag_name\":\"v1.2.1\",\"html_url\":\"" + base + "v1.2.1\",\"draft\":false,\"prerelease\":false,\"published_at\":\"x\",\"n\":" + token + "}]")
            }),
            Scenario(name: "foreign response url", current: "v1.2.0", steps: [
                Step(now: 1000, body: json([entry("v1.2.1")]), url: "https://api.github.com/repos/DDDuoDuo/EndfieldHUD/releases?per_page=50"),
                Step(now: 1001, body: json([entry("v1.2.1")]), url: "https://api.github.com/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100")]),
            Scenario(name: "transport failures", current: "v1.2.0", steps: [Step(now: 1000, failure: "network"), Step(now: 1001, failure: "cancelled")]),
            Scenario(name: "titles", current: "v1.2.0", steps: [
                Step(now: 1000, body: json([entry("v1.2.1", name: "\u{7}EndfieldHUD\u{200B} 1.2.1\u{FEFF}\n")])),
                Step(now: 1001, body: json([entry("v1.2.1", name: longName)])),
                Step(now: 1002, body: json([entry("v1.2.1", name: "\u{200B}\u{0}\u{AD}")])),
                Step(now: 1003, body: json([entry("v1.2.1", name: "")])),
                Step(now: 1004, body: json([entry("v1.2.1", name: "e\u{301}\u{1F468}\u{200D}\u{1F469}")]))]),
            Scenario(name: "equal versions keep first", current: "v1.2.0", steps: [
                Step(now: 1000, body: json([entry("v1.3.0+a", name: "first"), entry("v1.3.0+b", name: "second"), entry("v1.3.0", name: "third")])),
                Step(now: 1001, body: json([entry("v1.2.0+build.9")]))]),
            Scenario(name: "channel cache after not modified", current: "v1.2.0-beta.1", steps: [
                Step(now: 1000, headers: ["ETag": "\"x\""], body: json([entry("v1.2.0-beta.2", prerelease: true), entry("v1.1.0")])),
                Step(now: 1001, status: 304, body: "")])]
        let scenarios = s.map(run)
        let output: [String: Any] = ["schemaVersion": 1, "endpoint": endpoint, "maximumResponseBytes": HUDGitHubReleaseClient.maximumResponseBytes,
            "versions": versions, "validTags": valid, "less": less, "urls": urls, "controlCharacters": control, "scenarios": scenarios]
        try JSONSerialization.data(withJSONObject: output, options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
        print("PASS \(versions.count) tags, \(valid.count * valid.count) orderings, \(urls.count) URLs, \(scenarios.count) client scenarios")
    }
}
