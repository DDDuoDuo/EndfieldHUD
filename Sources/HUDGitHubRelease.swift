import Foundation

/// SemVer precedence, including numeric prerelease identifiers. Build metadata
/// does not affect ordering/equality; a release retains its exact tag separately.
struct HUDReleaseVersion: Comparable {
    let major: String
    let minor: String
    let patch: String
    let prerelease: [String]

    init?(_ tag: String) {
        guard !tag.isEmpty, tag.utf8.count <= 128 else { return nil }
        let value = tag.first == "v" ? String(tag.dropFirst()) : tag
        let build = value.split(separator: "+", omittingEmptySubsequences: false)
        guard build.count <= 2, build.count != 2 || Self.validIdentifiers(String(build[1]), numericZeroAllowed: true) else { return nil }
        let release = build[0].split(separator: "-", maxSplits: 1, omittingEmptySubsequences: false)
        let core = release[0].split(separator: ".", omittingEmptySubsequences: false).map(String.init)
        guard core.count == 3, core.allSatisfy(Self.validNumber) else { return nil }
        if release.count == 2, !Self.validIdentifiers(String(release[1]), numericZeroAllowed: false) { return nil }
        major = core[0]; minor = core[1]; patch = core[2]
        prerelease = release.count == 2 ? release[1].split(separator: ".").map(String.init) : []
    }

    private static func numeric(_ value: String) -> Bool {
        !value.isEmpty && value.utf8.allSatisfy { (48...57).contains($0) }
    }
    private static func validNumber(_ value: String) -> Bool {
        numeric(value) && (value.count == 1 || value.first != "0")
    }
    private static func validIdentifiers(_ value: String, numericZeroAllowed: Bool) -> Bool {
        value.split(separator: ".", omittingEmptySubsequences: false).allSatisfy { part in
            !part.isEmpty && part.utf8.allSatisfy { byte in
                (48...57).contains(byte) || (65...90).contains(byte) || (97...122).contains(byte) || byte == 45
            } && (numericZeroAllowed || !numeric(String(part)) || validNumber(String(part)))
        }
    }
    private static func numberLess(_ lhs: String, _ rhs: String) -> Bool {
        lhs.count == rhs.count ? lhs < rhs : lhs.count < rhs.count
    }
    static func < (lhs: Self, rhs: Self) -> Bool {
        for (a, b) in [(lhs.major, rhs.major), (lhs.minor, rhs.minor), (lhs.patch, rhs.patch)] where a != b {
            return numberLess(a, b)
        }
        if lhs.prerelease.isEmpty || rhs.prerelease.isEmpty { return !lhs.prerelease.isEmpty && rhs.prerelease.isEmpty }
        for (a, b) in zip(lhs.prerelease, rhs.prerelease) where a != b {
            let an = numeric(a), bn = numeric(b)
            if an && bn { return numberLess(a, b) }
            if an != bn { return an }
            return a < b
        }
        return lhs.prerelease.count < rhs.prerelease.count
    }
}

struct HUDGitHubRelease: Equatable {
    let tag: String
    let version: HUDReleaseVersion
    let title: String
    let url: URL
    let isPrerelease: Bool

    static func currentTag(in bundle: Bundle = .main) -> String? {
        currentTag(info: bundle.infoDictionary ?? [:])
    }
    static func currentTag(info: [String: Any]) -> String? {
        for key in ["HUDReleaseTag", "CFBundleShortVersionString"] {
            if let tag = info[key] as? String, HUDReleaseVersion(tag) != nil { return tag }
        }
        return nil
    }

    static func validatedURL(_ value: String, tag: String) -> URL? {
        guard HUDReleaseVersion(tag) != nil, let parts = URLComponents(string: value),
              parts.scheme == "https", parts.host?.lowercased() == "github.com",
              parts.user == nil, parts.password == nil, parts.port == nil,
              parts.query == nil, parts.fragment == nil,
              parts.path == "/DDDuoDuo/EndfieldHUD/releases/tag/" + tag else { return nil }
        return parts.url
    }
}

enum HUDGitHubReleaseError: Error, Equatable {
    case invalidCurrentVersion, invalidResponse, responseTooLarge, noPublishedReleases, noMatchingReleases
    case unexpectedNotModified, httpStatus(Int), rateLimited(until: Date?)
    case network(String), cancelled
}

enum HUDGitHubReleaseStatus: Equatable {
    case disabled
    case current(HUDGitHubRelease)
    case available(HUDGitHubRelease)
    case failed(HUDGitHubReleaseError)
}

/// Release discovery only. Installation and signed update feeds belong to the
/// updater; this client never requests or executes a release asset.
final class HUDGitHubReleaseClient {
    struct Response {
        let statusCode: Int
        let headers: [String: String]
        let body: Data
        let url: URL
        func header(_ name: String) -> String? { headers.first { $0.key.caseInsensitiveCompare(name) == .orderedSame }?.value }
    }
    typealias Cancel = () -> Void
    typealias Transport = (URLRequest, Int, @escaping (Result<Response, HUDGitHubReleaseError>) -> Void) -> Cancel
    typealias Delivery = (@escaping () -> Void) -> Void
    static let endpoint = URL(string: "https://api.github.com/repos/DDDuoDuo/EndfieldHUD/releases?per_page=100")!
    static let maximumResponseBytes = 1_048_576
    private let transport: Transport
    private let deliver: Delivery
    private let now: () -> Date
    private var etag: String?
    // Cache only the best candidate in each channel, not the response body. A
    // 304 must be usable even when the caller changes from preview to stable.
    private struct Candidates {
        let newest: HUDGitHubRelease
        let stable: HUDGitHubRelease?
        func release(for current: HUDReleaseVersion) -> HUDGitHubRelease? {
            current.prerelease.isEmpty ? stable : newest
        }
    }
    private var cachedCandidates: Candidates?
    private var retryAfter: Date?
    private var generation = 0
    private var completedGeneration = 0
    private var cancellation: Cancel?

    init(transport: @escaping Transport = HUDGitHubReleaseClient.load,
         now: @escaping () -> Date = Date.init,
         deliver: @escaping Delivery = { callback in DispatchQueue.main.async(execute: callback) }) {
        self.transport = transport; self.now = now; self.deliver = deliver
    }
    deinit { cancellation?() }

    /// Call and cancel on the main thread. Each new check supersedes the old
    /// one; late callbacks cannot replace newer status or mutate its ETag cache.
    @discardableResult
    func check(currentTag: String?, enabled: Bool = true, completion: @escaping (HUDGitHubReleaseStatus) -> Void) -> Cancel {
        precondition(Thread.isMainThread)
        generation += 1; let token = generation
        cancellation?(); cancellation = nil
        guard enabled else { completion(.disabled); return {} }
        guard let tag = currentTag, let current = HUDReleaseVersion(tag) else {
            completion(.failed(.invalidCurrentVersion)); return {}
        }
        if let retryAfter, retryAfter > now() { completion(.failed(.rateLimited(until: retryAfter))); return {} }
        var request = URLRequest(url: Self.endpoint, cachePolicy: .reloadIgnoringLocalCacheData, timeoutInterval: 12)
        request.setValue("application/vnd.github+json", forHTTPHeaderField: "Accept")
        request.setValue("2022-11-28", forHTTPHeaderField: "X-GitHub-Api-Version")
        request.setValue("EndfieldHUD-release-check", forHTTPHeaderField: "User-Agent")
        if let etag { request.setValue(etag, forHTTPHeaderField: "If-None-Match") }
        let cancelRequest = transport(request, Self.maximumResponseBytes) { [weak self] result in
            guard let self else { return }
            self.deliver { [weak self] in
                guard let self, self.generation == token else { return }
                self.completedGeneration = token
                self.cancellation = nil
                completion(self.status(for: result, current: current))
            }
        }
        // An injected transport may complete synchronously. Do not retain its
        // cancellation handle after completion or overwrite a reentrant check.
        if generation == token, completedGeneration != token { cancellation = cancelRequest }
        return { [weak self] in
            precondition(Thread.isMainThread)
            guard let self, self.generation == token else { return }
            self.generation += 1; self.cancellation?(); self.cancellation = nil
        }
    }

    private func status(for result: Result<Response, HUDGitHubReleaseError>, current: HUDReleaseVersion) -> HUDGitHubReleaseStatus {
        let response: Response
        switch result { case .failure(let error): return .failed(error); case .success(let value): response = value }
        guard response.url == Self.endpoint else { return .failed(.invalidResponse) }
        if response.statusCode == 429 || (response.statusCode == 403 && (response.header("X-RateLimit-Remaining") == "0" || response.header("Retry-After") != nil)) {
            let interval = response.header("Retry-After").flatMap(Double.init).flatMap { $0.isFinite && $0 >= 0 ? $0 : nil }
            let reset = response.header("X-RateLimit-Reset").flatMap(Double.init).flatMap { $0.isFinite && $0 > 0 ? Date(timeIntervalSince1970: $0) : nil }
            retryAfter = interval.map { now().addingTimeInterval(min($0, 86_400)) } ?? reset
            return .failed(.rateLimited(until: retryAfter))
        }
        let candidates: Candidates
        if response.statusCode == 304 {
            guard etag != nil, let cachedCandidates else { return .failed(.unexpectedNotModified) }
            candidates = cachedCandidates
        } else {
            guard response.statusCode == 200 else { return .failed(.httpStatus(response.statusCode)) }
            guard response.body.count <= Self.maximumResponseBytes else { return .failed(.responseTooLarge) }
            let entries: [Entry]
            do { entries = try JSONDecoder().decode([Entry].self, from: response.body) }
            catch { return .failed(.invalidResponse) }
            guard entries.count <= 100 else { return .failed(.invalidResponse) }
            let releases = entries.compactMap { entry -> HUDGitHubRelease? in
                guard !entry.draft, entry.publishedAt != nil, let version = HUDReleaseVersion(entry.tag),
                      let url = HUDGitHubRelease.validatedURL(entry.url, tag: entry.tag) else { return nil }
                let title = String(String.UnicodeScalarView((entry.name ?? entry.tag).unicodeScalars
                    .filter { !CharacterSet.controlCharacters.contains($0) }.prefix(200)))
                return HUDGitHubRelease(tag: entry.tag, version: version, title: title.isEmpty ? entry.tag : title,
                    url: url, isPrerelease: entry.prerelease || !version.prerelease.isEmpty)
            }
            guard let latest = releases.max(by: { $0.version < $1.version }) else { return .failed(.noPublishedReleases) }
            candidates = Candidates(newest: latest,
                stable: releases.filter { !$0.isPrerelease }.max(by: { $0.version < $1.version }))
            cachedCandidates = candidates
            etag = response.header("ETag").flatMap { value in
                !value.isEmpty && value.utf8.count <= 512 && !value.unicodeScalars.contains(where: CharacterSet.controlCharacters.contains) ? value : nil
            }
        }
        retryAfter = nil
        guard let release = candidates.release(for: current) else { return .failed(.noMatchingReleases) }
        return release.version > current ? .available(release) : .current(release)
    }

    private struct Entry: Decodable {
        let tag: String
        let name: String?
        let url: String
        let draft: Bool
        let prerelease: Bool
        let publishedAt: String?
        enum CodingKeys: String, CodingKey { case tag = "tag_name", name, url = "html_url", draft, prerelease, publishedAt = "published_at" }
    }

    static func load(_ request: URLRequest, limit: Int, completion: @escaping (Result<Response, HUDGitHubReleaseError>) -> Void) -> Cancel {
        let loader = Loader(limit: limit, completion: completion)
        let session = loader.start(request)
        return { session.invalidateAndCancel() }
    }

    /// Streaming enforces the cap before appending another chunk. Ephemeral
    /// requests send no stored credentials/cookies and accept no redirects.
    private final class Loader: NSObject, URLSessionDataDelegate {
        let limit: Int
        private var completion: ((Result<Response, HUDGitHubReleaseError>) -> Void)?
        private var session: URLSession?
        private var response: HTTPURLResponse?
        private var body = Data()
        init(limit: Int, completion: @escaping (Result<Response, HUDGitHubReleaseError>) -> Void) {
            self.limit = limit; self.completion = completion
        }
        func start(_ request: URLRequest) -> URLSession {
            let config = URLSessionConfiguration.ephemeral
            config.timeoutIntervalForRequest = 12; config.timeoutIntervalForResource = 20
            config.urlCache = nil; config.httpCookieStorage = nil; config.urlCredentialStorage = nil
            let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1
            let session = URLSession(configuration: config, delegate: self, delegateQueue: queue)
            self.session = session
            session.dataTask(with: request).resume()
            return session
        }
        func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
            completionHandler(nil)
        }
        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                        completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
            guard let response = response as? HTTPURLResponse, response.url == HUDGitHubReleaseClient.endpoint else {
                completionHandler(.cancel); finish(.failure(.invalidResponse)); return
            }
            guard response.expectedContentLength <= Int64(limit) else {
                completionHandler(.cancel); finish(.failure(.responseTooLarge)); return
            }
            self.response = response; completionHandler(.allow)
        }
        func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
            guard completion != nil else { return }
            guard data.count <= limit - body.count else { finish(.failure(.responseTooLarge)); return }
            body.append(data)
        }
        func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
            if let error {
                finish(.failure((error as NSError).code == NSURLErrorCancelled ? .cancelled : .network(error.localizedDescription))); return
            }
            guard let response, let url = response.url else { finish(.failure(.invalidResponse)); return }
            var headers: [String: String] = [:]
            for (key, value) in response.allHeaderFields { headers[String(describing: key)] = String(describing: value) }
            finish(.success(Response(statusCode: response.statusCode, headers: headers, body: body, url: url)))
        }
        private func finish(_ result: Result<Response, HUDGitHubReleaseError>) {
            guard let completion else { return }; self.completion = nil
            completion(result); body.removeAll(keepingCapacity: false)
            session?.invalidateAndCancel(); session = nil
        }
    }
}
