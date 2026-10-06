import Foundation
import CryptoKit
import CoreFoundation

/// A read-only adapter for the public community clients' observed endpoints. No polling,
/// account-token access, check-in, or game/account mutation lives in this client.
final class HypergryphAccountAPI: HypergryphAccountServing {
    /// The HUD reads the authenticated owner's profile. Web/app variants are
    /// explicit diagnostic contracts; errors never switch routes automatically.
    enum EndfieldCardRoute {
        case web, app, authenticatedSelf
        var requiresCommunityUser: Bool { self != .authenticatedSelf }
        var path: String {
            self == .web ? "/web/v1/game/endfield/card/detail" : "/api/v1/game/endfield/card/detail"
        }
    }
    private let configuration: URLSessionConfiguration
    private let now: () -> Date
    private let endfieldCardRoute: EndfieldCardRoute
    /// Nil in the HUD. Diagnostic tools may observe closed error categories;
    /// no response text, request headers or identifiers are exposed.
    var onDiagnostic: ((HypergryphAPIDiagnostic) -> Void)?
    /// Explicit diagnostic UI only, never wired by the HUD or written to logs.
    /// The bounded service reason is redacted before crossing this boundary.
    var onServiceMessage: ((HypergryphAPIDiagnostic.Endpoint, Int, String) -> Void)?
    private let lock = NSLock()
    private var clockOffsets: [HypergryphAccountRegion: TimeInterval] = [:]
    // At most one active identity per region. This transient replacement keeps a
    // recovered signing token usable while the controller still holds its old
    // token; it never stores a passport token or creates another polling service.
    private struct SigningState {
        let identityDigest: Data
        let suppliedToken: String
        var activeToken: String
    }
    private var signingStates: [HypergryphAccountRegion: SigningState] = [:]
    static let maximumResponseBytes = 8 * 1_024 * 1_024

    init(configuration: URLSessionConfiguration = .ephemeral, now: @escaping () -> Date = Date.init,
         endfieldCardRoute: EndfieldCardRoute = .authenticatedSelf) {
        self.configuration = configuration.copy() as! URLSessionConfiguration
        self.configuration.httpCookieStorage = nil
        self.configuration.httpShouldSetCookies = false
        self.configuration.urlCredentialStorage = nil
        self.configuration.urlCache = nil
        self.configuration.requestCachePolicy = .reloadIgnoringLocalCacheData
        self.configuration.timeoutIntervalForRequest = 15
        self.configuration.timeoutIntervalForResource = 20
        self.now = now
        self.endfieldCardRoute = endfieldCardRoute
    }

    @discardableResult
    func refreshCredentials(cred: String, region: HypergryphAccountRegion,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        guard Self.validSecret(cred) else { Self.deliver(.failure(.invalidCredentials), completion); return nil }
        prepareIdentity(cred, region: region)
        return refreshSigningCredential(cred: cred, region: region, completion: completion)
    }

    @discardableResult
    func refreshCredentials(credentials: HypergryphCredentials, region: HypergryphAccountRegion,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        guard valid(credentials) else { Self.deliver(.failure(.invalidCredentials), completion); return nil }
        let effective = effectiveCredentials(credentials, region: region)
        return refreshSigningCredential(cred: effective.cred, credentials: effective, region: region, completion: completion)
    }

    private func refreshSigningCredential(cred: String, credentials: HypergryphCredentials? = nil, region: HypergryphAccountRegion,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest {
        let chain = HypergryphRequestChain { completion(.failure(.cancelled)) }
        performRefresh(cred: cred, credentials: credentials, region: region, mayCorrectClock: true, chain: chain, completion: completion)
        return chain
    }

    private func performRefresh(cred: String, credentials: HypergryphCredentials?, region: HypergryphAccountRegion,
        mayCorrectClock: Bool, chain: HypergryphRequestChain,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) {
        guard !chain.isCancelled else { return }
        let request = makeRequest(region: region, path: "/web/v1/auth/refresh", cred: cred, credentials: credentials)
        chain.replace(fetch(request, redacting: [cred, credentials?.signingToken ?? "", credentials?.deviceID ?? ""]) { [self, chain] result in
            guard !chain.isCancelled else { return }
            if mayCorrectClock, credentials != nil, case .success(let envelope) = result,
               Self.integer(envelope["code"]) == 10003, calibrateClock(envelope, region: region) {
                performRefresh(cred: cred, credentials: credentials, region: region,
                    mayCorrectClock: false, chain: chain, completion: completion)
                return
            }
            do {
                let envelope = try result.get(), data = try Self.payload(envelope)
                guard let token = data["token"] as? String, Self.validSecret(token) else { throw HypergryphAPIError.invalidResponse }
                calibrateClock(envelope, region: region)
                chain.finish { completion(.success(HypergryphCredentials(cred: cred, signingToken: token, deviceID: credentials?.deviceID))) }
            } catch { chain.finish { completion(.failure(Self.apiError(error))) } }
        })
    }

    @discardableResult
    func bindings(credentials: HypergryphCredentials, region: HypergryphAccountRegion,
        completion: @escaping (Result<[HypergryphRole], HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        guard valid(credentials) else { Self.deliver(.failure(.invalidCredentials), completion); return nil }
        let chain = HypergryphRequestChain { completion(.failure(.cancelled)) }
        chain.replace(fetchSigned(region: region, path: "/api/v1/game/player/binding", credentials: credentials) { [self, chain] result in
            guard !chain.isCancelled else { return }
            do {
                let roles = try Self.decodeBindings(try Self.payload(result.get()), region: region)
                guard endfieldCardRoute.requiresCommunityUser, roles.contains(where: { $0.game == .endfield }) else { chain.finish { completion(.success(roles)) }; return }
                chain.replace(self.fetchSigned(region: region, path: "/web/v1/user", credentials: credentials) { result in
                    guard !chain.isCancelled else { return }
                    do {
                        let data = try Self.payload(result.get()), user = data["user"] as? [String: Any]
                        guard let id = Self.identifier(user?["id"] ?? user?["userId"]) else { throw HypergryphAPIError.invalidResponse }
                        chain.finish { completion(.success(roles.map { var role = $0; role.communityUserID = id; return role })) }
                    } catch { chain.finish { completion(.failure(Self.apiError(error))) } }
                })
            } catch { chain.finish { completion(.failure(Self.apiError(error))) } }
        })
        return chain
    }

    @discardableResult
    func profile(role: HypergryphRole, credentials: HypergryphCredentials,
        completion: @escaping (Result<HypergryphProfileSnapshot, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        guard valid(credentials) else { Self.deliver(.failure(.invalidCredentials), completion); return nil }
        guard role.isAvailable, Self.identifier(role.roleID) != nil,
              role.game != .arknights || role.serverID == nil || Self.identifier(role.serverID) != nil,
              role.game != .endfield || (Self.identifier(role.serverID) != nil &&
                  (!endfieldCardRoute.requiresCommunityUser || Self.identifier(role.communityUserID) != nil)) else {
            Self.deliver(.failure(.invalidRole), completion); return nil
        }
        let path: String, query: [(String, String)]
        if role.game == .endfield {
            // The official card service owns a separate OneFetch instance.
            // Its identity is the query below; the shared game client's
            // sk-game-role header is not configured on that card client.
            path = endfieldCardRoute.path
            query = [("roleId", role.roleID), ("serverId", role.serverID!)] +
                (endfieldCardRoute.requiresCommunityUser ? [("userId", role.communityUserID!)] : [])
        } else {
            path = "/api/v1/game/player/info"
            query = [("uid", role.roleID)] + (role.serverID.map { [("channelMasterId", $0)] } ?? [])
        }
        return fetchSigned(region: role.region, path: path, query: query, credentials: credentials) { [now] result in
            do { completion(.success(try Self.decodeProfile(try Self.payload(result.get()), role: role, observedAt: now()))) }
            catch { completion(.failure(Self.apiError(error))) }
        }
    }

    private func valid(_ credentials: HypergryphCredentials) -> Bool {
        Self.validSecret(credentials.cred) && Self.validSecret(credentials.signingToken) &&
            (credentials.deviceID.map(Self.validSecret) ?? true)
    }

    private func prepareIdentity(_ cred: String, region: HypergryphAccountRegion) {
        let identity = Self.identityDigest(cred)
        lock.lock(); defer { lock.unlock() }
        if signingStates[region]?.identityDigest != identity {
            signingStates[region] = SigningState(identityDigest: identity, suppliedToken: "", activeToken: "")
        }
    }

    private static func identityDigest(_ cred: String, deviceID: String? = nil) -> Data {
        // Credential/device validators exclude NUL, so this boundary cannot
        // collide. Keep only the digest, not another copy of session context.
        Data(SHA256.hash(data: Data((cred + "\u{0}" + (deviceID ?? "")).utf8)))
    }

    private func effectiveCredentials(_ supplied: HypergryphCredentials, region: HypergryphAccountRegion) -> HypergryphCredentials {
        let identity = Self.identityDigest(supplied.cred, deviceID: supplied.deviceID)
        lock.lock(); defer { lock.unlock() }
        if let state = signingStates[region], state.identityDigest == identity,
           supplied.signingToken == state.suppliedToken || supplied.signingToken == state.activeToken {
            return HypergryphCredentials(cred: supplied.cred, signingToken: state.activeToken, deviceID: supplied.deviceID)
        }
        signingStates[region] = SigningState(identityDigest: identity, suppliedToken: supplied.signingToken, activeToken: supplied.signingToken)
        return supplied
    }

    @discardableResult
    private func calibrateClock(_ envelope: [String: Any], region: HypergryphAccountRegion) -> Bool {
        guard let timestamp = Self.timestamp(envelope["timestamp"]) else { return false }
        lock.lock(); clockOffsets[region] = timestamp.timeIntervalSince(now()); lock.unlock()
        return true
    }

    private func fetchSigned(region: HypergryphAccountRegion, path: String, query: [(String, String)] = [],
        credentials: HypergryphCredentials,
        completion: @escaping (Result<[String: Any], HypergryphAPIError>) -> Void) -> HypergryphAccountRequest {
        let chain = HypergryphRequestChain { completion(.failure(.cancelled)) }
        performSigned(region: region, path: path, query: query, credentials: effectiveCredentials(credentials, region: region),
            mayRecover: true, chain: chain, completion: completion)
        return chain
    }

    private func performSigned(region: HypergryphAccountRegion, path: String, query: [(String, String)],
        credentials: HypergryphCredentials, mayRecover: Bool, chain: HypergryphRequestChain,
        completion: @escaping (Result<[String: Any], HypergryphAPIError>) -> Void) {
        guard !chain.isCancelled else { return }
        chain.replace(fetch(makeRequest(region: region, path: path, query: query, credentials: credentials),
                            redacting: [credentials.cred, credentials.signingToken, credentials.deviceID ?? ""]) { [self, chain] result in
            guard !chain.isCancelled else { return }
            guard mayRecover, case .success(let envelope) = result,
                  let code = Self.integer(envelope["code"]), code == 10000 || code == 10003 else {
                chain.finish { completion(result) }; return
            }
            // The provider distinguishes a stale signing token (10000) and
            // clock skew (10003) from an expired community credential (10002).
            // Exactly one retry is allowed, including when the retry fails in a
            // different way. A missing timestamp cannot fabricate a clock fix.
            let clockUpdated = calibrateClock(envelope, region: region)
            if code == 10003 {
                guard clockUpdated else { chain.finish { completion(result) }; return }
                performSigned(region: region, path: path, query: query, credentials: credentials,
                    mayRecover: false, chain: chain, completion: completion)
                return
            }
            // The official client clears only its stale signing key before
            // refresh. It still signs the request and retains its issued dId.
            let refreshContext = HypergryphCredentials(cred: credentials.cred, signingToken: "", deviceID: credentials.deviceID)
            let refresh = refreshSigningCredential(cred: credentials.cred, credentials: refreshContext, region: region, completion: { [self, chain] refreshed in
                guard !chain.isCancelled else { return }
                switch refreshed {
                case .failure(let error): chain.finish { completion(.failure(error)) }
                case .success(let value):
                    lock.lock()
                    // A late request from a previously selected account cannot
                    // replace the current identity's transient signing state.
                    if var state = signingStates[region], state.identityDigest == Self.identityDigest(value.cred, deviceID: value.deviceID), state.activeToken == credentials.signingToken {
                        state.activeToken = value.signingToken; signingStates[region] = state
                    }
                    lock.unlock()
                    performSigned(region: region, path: path, query: query, credentials: value,
                        mayRecover: false, chain: chain, completion: completion)
                }
            })
            chain.replace(refresh)
        })
    }

    private func makeRequest(region: HypergryphAccountRegion, path: String, query: [(String, String)] = [],
        cred: String? = nil, credentials: HypergryphCredentials? = nil) -> URLRequest {
        // Only these internal, fixed read endpoints are reachable; arbitrary caller URLs are never accepted.
        precondition(["/web/v1/auth/refresh", "/api/v1/game/player/binding", "/web/v1/user",
            "/web/v1/game/endfield/card/detail", "/api/v1/game/endfield/card/detail", "/api/v1/game/player/info"].contains(path))
        var components = URLComponents(); components.scheme = "https"; components.host = region.apiHost; components.path = path
        if !query.isEmpty { components.percentEncodedQuery = query.map { Self.queryEscape($0.0) + "=" + Self.queryEscape($0.1) }.joined(separator: "&") }
        var request = URLRequest(url: components.url!); request.httpMethod = "GET"
        request.setValue("application/json", forHTTPHeaderField: "Accept")
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")
        request.setValue(cred ?? credentials?.cred, forHTTPHeaderField: "cred")
        request.setValue("3", forHTTPHeaderField: "platform")
        request.setValue("1.0.0", forHTTPHeaderField: "vName")
        request.setValue(region.gameOrigin, forHTTPHeaderField: "Origin")
        request.setValue(region.gameOrigin + "/", forHTTPHeaderField: "Referer")
        // The mainland game-tools client omits this header. "zh-cn" is a
        // passport SDK locale, not a valid game-data language enum. SKPORT's
        // game-tools client uses en / zh_Hans / zh_Hant / ja / ko instead.
        if region == .global { request.setValue("en", forHTTPHeaderField: "sk-language") }
        if let credentials = credentials {
            lock.lock(); let offset = clockOffsets[region] ?? 0; lock.unlock()
            let timestamp = String(Int64(now().timeIntervalSince1970 + offset))
            request.setValue(timestamp, forHTTPHeaderField: "timestamp")
            request.setValue(credentials.deviceID, forHTTPHeaderField: "dId")
            request.setValue(Self.signature(path: path, query: components.percentEncodedQuery ?? "", timestamp: timestamp,
                signingToken: credentials.signingToken, deviceID: credentials.deviceID), forHTTPHeaderField: "sign")
        }
        return request
    }

    static func signature(path: String, query: String, timestamp: String, signingToken: String, deviceID: String? = nil) -> String {
        // Preserve the official field order and JSON escaping. The exact issued
        // dId must match both its HTTP header and these signed bytes.
        let escapedDevice = (deviceID ?? "").replacingOccurrences(of: "\\", with: "\\\\").replacingOccurrences(of: "\"", with: "\\\"")
        let headers = "{\"platform\":\"3\",\"timestamp\":\"\(timestamp)\",\"dId\":\"\(escapedDevice)\",\"vName\":\"1.0.0\"}"
        let message = Data((path + query + timestamp + headers).utf8)
        let hmac = HMAC<SHA256>.authenticationCode(for: message, using: SymmetricKey(data: Data(signingToken.utf8)))
        let hex = hmac.map { String(format: "%02x", $0) }.joined()
        return Insecure.MD5.hash(data: Data(hex.utf8)).map { String(format: "%02x", $0) }.joined()
    }

    private static func queryEscape(_ value: String) -> String {
        value.addingPercentEncoding(withAllowedCharacters: CharacterSet(charactersIn: "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~"))!
    }
    private func fetch(_ request: URLRequest, redacting secrets: [String] = [],
                       completion: @escaping (Result<[String: Any], HypergryphAPIError>) -> Void) -> HypergryphAccountRequest {
        let diagnostic = onDiagnostic
        let serviceMessage = onServiceMessage
        let redactions = serviceMessage == nil ? [] : secrets
        let endpoint: HypergryphAPIDiagnostic.Endpoint?
        switch request.url?.path {
        case "/web/v1/auth/refresh": endpoint = .refresh
        case "/api/v1/game/player/binding": endpoint = .bindings
        case "/web/v1/user": endpoint = .communityUser
        case "/web/v1/game/endfield/card/detail", "/api/v1/game/endfield/card/detail": endpoint = .endfieldProfile
        case "/api/v1/game/player/info": endpoint = .arknightsProfile
        default: endpoint = nil
        }
        let operation = HypergryphBoundedRequest(configuration: configuration, maximumBytes: Self.maximumResponseBytes) { result in
            if let endpoint = endpoint, case .success(let envelope) = result,
               let code = Self.integer(envelope["code"]), code != 0 {
                diagnostic?(HypergryphAPIDiagnostic(endpoint: endpoint, code: code,
                    reason: .classify(envelope["message"] ?? envelope["msg"])))
                if let serviceMessage = serviceMessage {
                    serviceMessage(endpoint, code, Self.redactedServiceMessage(envelope["message"] ?? envelope["msg"],
                                                                               request: request, secrets: redactions))
                }
            }
            completion(result)
        }
        operation.start(request); return operation
    }

    /// This is a diagnostic display aid, not a general response serializer.
    /// Extract only the service's error sentence; never inspect nested data.
    static func redactedServiceMessage(_ value: Any?, request: URLRequest, secrets: [String]) -> String {
        guard let message = value as? String, !message.isEmpty else { return "No service reason was supplied." }
        guard message.utf8.count <= 4_096 else { return "The service reason was too long to display safely." }
        var text = message
        let headers = ["cred", "dId", "sign", "sk-game-role"].compactMap { request.value(forHTTPHeaderField: $0) }
        let identifiers = request.url.flatMap { URLComponents(url: $0, resolvingAgainstBaseURL: false)?.queryItems }?.compactMap(\.value) ?? []
        for value in Set(secrets + headers + identifiers).filter({ !$0.isEmpty }).sorted(by: { $0.count > $1.count }) {
            text = text.replacingOccurrences(of: value, with: "[redacted]")
        }
        // Keep plain readable text only. Strip markup and control/bidi escapes;
        // redact addresses, long opaque strings and numeric identifiers too.
        for pattern in [#"(?i)https?://[^\s<>\"']+"#, #"[A-Za-z0-9._%+-]+@[A-Za-z0-9.-]+\.[A-Za-z]{2,}"#,
                        #"[A-Za-z0-9_+/=.-]{20,}"#, #"\d{3,}"#, #"<[^>]*>"#] {
            if let expression = try? NSRegularExpression(pattern: pattern) {
                text = expression.stringByReplacingMatches(in: text, range: NSRange(text.startIndex..., in: text), withTemplate: "[redacted]")
            }
        }
        text = text.components(separatedBy: .whitespacesAndNewlines).filter { !$0.isEmpty }.joined(separator: " ")
        text = String(text.unicodeScalars.filter {
            !CharacterSet.controlCharacters.contains($0) && $0.properties.generalCategory != .format
        })
        guard !text.isEmpty else { return "No readable service reason was supplied." }
        return String(text.prefix(512))
    }
    fileprivate static func deliver<T>(_ result: Result<T, HypergryphAPIError>, _ completion: @escaping (Result<T, HypergryphAPIError>) -> Void) {
        DispatchQueue.main.async { completion(result) }
    }
    private static func apiError(_ error: Error) -> HypergryphAPIError { error as? HypergryphAPIError ?? .invalidResponse }
    private static func validSecret(_ value: String) -> Bool {
        !value.isEmpty && value.utf8.count <= 4_096 && value.utf8.allSatisfy { $0 >= 33 && $0 <= 126 }
    }
    static func payload(_ envelope: [String: Any]) throws -> [String: Any] {
        guard let code = integer(envelope["code"]) else { throw HypergryphAPIError.invalidResponse }
        if code == 10002 { throw HypergryphAPIError.authenticationExpired }
        guard code == 0 else { throw HypergryphAPIError.service(code: code) }
        guard let data = envelope["data"] as? [String: Any] else { throw HypergryphAPIError.invalidResponse }
        return data
    }
    static func decodeBindings(_ data: [String: Any], region: HypergryphAccountRegion) throws -> [HypergryphRole] {
        guard let apps = data["list"] as? [[String: Any]], apps.count <= 64 else { throw HypergryphAPIError.invalidResponse }
        var roles: [HypergryphRole] = [], seen = Set<String>()
        for app in apps {
            guard let code = app["appCode"] as? String, let game = HypergryphGame(rawValue: code) else { continue }
            guard let bindings = app["bindingList"] as? [[String: Any]], bindings.count <= 256 else { throw HypergryphAPIError.invalidResponse }
            for binding in bindings {
                guard let uid = identifier(binding["uid"]) else { continue }
                let available = !(binding["isDelete"] as? Bool ?? false)
                let isDefault = (binding["isDefault"] as? Bool ?? false) || identifier(app["defaultUid"]) == uid
                if game == .arknights {
                    let role = HypergryphRole(region: region, game: game, bindingUID: uid, roleID: uid,
                        serverID: identifier(binding["channelMasterId"]), name: text(binding["nickName"] ?? binding["nickname"]),
                        serverName: text(binding["channelName"]), isDefault: isDefault, isAvailable: available)
                    if seen.insert(role.id).inserted { roles.append(role) }
                } else {
                    var items = binding["roles"] as? [[String: Any]] ?? []
                    if items.isEmpty, let defaultRole = binding["defaultRole"] as? [String: Any] { items = [defaultRole] }
                    guard items.count <= 256 else { throw HypergryphAPIError.invalidResponse }
                    let defaultID = (binding["defaultRole"] as? [String: Any]).flatMap { identifier($0["roleId"]) }
                    for item in items {
                        guard let id = identifier(item["roleId"]), let server = identifier(item["serverId"]) else { continue }
                        let role = HypergryphRole(region: region, game: game, bindingUID: uid, roleID: id, serverID: server,
                            name: text(item["nickname"] ?? item["nickName"]), serverName: text(item["serverName"]),
                            isDefault: (item["isDefault"] as? Bool ?? false) || id == defaultID || (isDefault && items.count == 1),
                            isAvailable: available && !(item["isBanned"] as? Bool ?? false))
                        if seen.insert(role.id).inserted { roles.append(role) }
                    }
                }
                guard roles.count <= 512 else { throw HypergryphAPIError.invalidResponse }
            }
        }
        return roles
    }
    static func decodeProfile(_ data: [String: Any], role: HypergryphRole, observedAt: Date) throws -> HypergryphProfileSnapshot {
        var snapshot = HypergryphProfileSnapshot(role: role, observedAt: observedAt)
        if role.game == .endfield {
            guard let detail = data["detail"] as? [String: Any], let base = detail["base"] as? [String: Any] else { throw HypergryphAPIError.invalidResponse }
            if let id = identifier(base["roleId"]), id != role.roleID { throw HypergryphAPIError.invalidResponse }
            snapshot.name = text(base["name"]); snapshot.avatarURL = imageURL(base["avatarUrl"])
            snapshot.level = nonnegative(base["level"]); snapshot.worldLevel = nonnegative(base["worldLevel"])
            snapshot.experience = nonnegative(base["exp"]); snapshot.createdAt = timestamp(base["createTime"])
            snapshot.operatorCount = nonnegative(base["charNum"]); snapshot.weaponCount = nonnegative(base["weaponNum"])
            snapshot.documentCount = nonnegative(base["docNum"])
            if let dungeon = detail["dungeon"] as? [String: Any], let current = nonnegative(dungeon["curStamina"]),
               let maximum = nonnegative(dungeon["maxStamina"]), maximum > 0 {
                snapshot.stamina = HypergryphStamina(current: current, maximum: maximum,
                    fullRecoveryAt: timestamp(dungeon["maxTs"]), serverObservedAt: timestamp(detail["currentTs"] ?? data["currentTs"]))
            }
        } else {
            guard let status = data["status"] as? [String: Any] else { throw HypergryphAPIError.invalidResponse }
            if let id = identifier(status["uid"]), id != role.roleID { throw HypergryphAPIError.invalidResponse }
            snapshot.name = text(status["name"]); snapshot.avatarURL = imageURL((status["avatar"] as? [String: Any])?["url"])
            snapshot.level = nonnegative(status["level"])
            snapshot.experience = nonnegative((status["exp"] as? [String: Any])?["current"])
            snapshot.createdAt = timestamp(status["registerTs"]); snapshot.operatorCount = nonnegative(status["charCnt"])
            if let ap = status["ap"] as? [String: Any], let current = nonnegative(ap["current"]),
               let maximum = nonnegative(ap["max"]), maximum > 0 {
                let fullRecovery = timestamp(ap["completeRecoveryTime"]), serverTime = timestamp(data["currentTs"])
                let normalized = arknightsAP(current: current, maximum: maximum, fullRecovery: fullRecovery,
                    lastRecovery: timestamp(ap["lastApAddTime"]), reference: serverTime ?? observedAt)
                snapshot.stamina = HypergryphStamina(current: normalized, maximum: maximum,
                    fullRecoveryAt: fullRecovery, serverObservedAt: serverTime)
            }
        }
        return snapshot
    }
    private static func arknightsAP(current: Int, maximum: Int, fullRecovery: Date?,
                                    lastRecovery: Date?, reference: Date) -> Int {
        guard current < maximum, let fullRecovery,
              reference.timeIntervalSince1970.isFinite else { return current }
        if fullRecovery <= reference { return maximum }
        let capacity = maximum - current
        if let lastRecovery {
            // Official Arknights card codec: one AP per six minutes since the
            // last stored tick, evaluated once at the response's server time.
            let recovered = floor(max(0, reference.timeIntervalSince(lastRecovery)) / 360)
            guard recovered.isFinite else { return current }
            return recovered >= Double(capacity) ? maximum : current + Int(recovered)
        }
        // Older payloads omit the tick. The published SKLAND integrations use
        // the full-recovery deadline, retaining at least the reported raw AP.
        let missing = ceil(fullRecovery.timeIntervalSince(reference) / 360)
        guard missing.isFinite else { return current }
        let estimated = missing >= Double(maximum) ? 0 : maximum - Int(missing)
        return max(current, min(maximum, estimated))
    }
    private static func text(_ value: Any?) -> String? {
        guard let string = value as? String, !string.isEmpty, string.utf8.count <= 4_096 else { return nil }; return string
    }
    private static func identifier(_ value: Any?) -> String? {
        let string: String
        if let value = value as? String { string = value }
        else if let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID(),
                !number.stringValue.isEmpty, number.stringValue.utf8.allSatisfy({ (48...57).contains($0) }) {
            // JSON integer IDs must not round-trip through Double: it changes
            // distinct 64-bit IDs above 2^53 into another account/role identity.
            string = number.stringValue
        }
        else if let number = integer(value), number >= 0 { string = String(number) }
        else { return nil }
        guard !string.isEmpty, string.utf8.count <= 256,
              string.unicodeScalars.allSatisfy({ CharacterSet.alphanumerics.union(CharacterSet(charactersIn: "-_.")).contains($0) }) else { return nil }
        return string
    }
    fileprivate static func integer(_ value: Any?) -> Int? {
        if let string = value as? String { return Int(string) }
        guard let number = value as? NSNumber, CFGetTypeID(number) != CFBooleanGetTypeID() else { return nil }
        if let exact = Int(number.stringValue) { return exact }
        let d = number.doubleValue
        guard d.isFinite, d.rounded(.towardZero) == d, d >= Double(Int.min), d < Double(Int.max) else { return nil }
        return Int(d)
    }
    private static func nonnegative(_ value: Any?) -> Int? { guard let value = integer(value), value >= 0 else { return nil }; return value }
    private static func timestamp(_ value: Any?) -> Date? {
        guard let value = nonnegative(value), value > 0, value < 32_503_680_000 else { return nil }
        return Date(timeIntervalSince1970: TimeInterval(value))
    }
    private static func imageURL(_ value: Any?) -> URL? {
        guard let value = text(value), let url = URL(string: value), url.scheme == "https", url.host != nil,
              url.user == nil, url.password == nil, url.port == nil || url.port == 443 else { return nil }; return url
    }
}

private final class HypergryphRequestChain: HypergryphAccountRequest {
    private let lock = NSLock()
    private var request: HypergryphAccountRequest?
    private var cancelled = false
    private var completed = false
    private let onCancel: () -> Void
    init(onCancel: @escaping () -> Void) { self.onCancel = onCancel }
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    func replace(_ request: HypergryphAccountRequest) {
        lock.lock(); let cancelled = self.cancelled; if !cancelled { self.request = request }; lock.unlock()
        if cancelled { request.cancel() }
    }
    func finish(_ action: () -> Void) {
        lock.lock(); let deliver = !cancelled && !completed; completed = true; request = nil; lock.unlock()
        if deliver { action() }
    }
    func cancel() {
        lock.lock(); let deliver = !cancelled && !completed; cancelled = true; let request = self.request; self.request = nil; lock.unlock()
        request?.cancel(); if deliver { DispatchQueue.main.async(execute: onCancel) }
    }
}

/// Streams a bounded payload, rejects every redirect (never forwards a credential), then
/// invalidates its ephemeral session. URLSession retains the delegate only until completion.
private final class HypergryphBoundedRequest: NSObject, HypergryphAccountRequest, URLSessionDataDelegate {
    private let configuration: URLSessionConfiguration
    private let maximumBytes: Int
    private let completion: (Result<[String: Any], HypergryphAPIError>) -> Void
    private let lock = NSLock()
    private var finished = false
    private var bytes = Data()
    private var responseStatus = 200
    private var session: URLSession?
    private var task: URLSessionDataTask?
    init(configuration: URLSessionConfiguration, maximumBytes: Int,
        completion: @escaping (Result<[String: Any], HypergryphAPIError>) -> Void) {
        self.configuration = configuration; self.maximumBytes = maximumBytes; self.completion = completion
    }
    func start(_ request: URLRequest) {
        let queue = OperationQueue(); queue.maxConcurrentOperationCount = 1; queue.qualityOfService = .utility
        let session = URLSession(configuration: configuration, delegate: self, delegateQueue: queue)
        self.session = session; task = session.dataTask(with: request); task?.resume()
    }
    func cancel() { finish(.failure(.cancelled)) }
    private func finish(_ result: Result<[String: Any], HypergryphAPIError>) {
        lock.lock(); guard !finished else { lock.unlock(); return }; finished = true
        let session = self.session; self.session = nil; task = nil; lock.unlock()
        session?.invalidateAndCancel()
        HypergryphAccountAPI.deliver(result, completion)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
        completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let response = response as? HTTPURLResponse else { completionHandler(.cancel); finish(.failure(.invalidResponse)); return }
        // Authentication-related HTTP responses carry the actionable provider
        // code and clock. Read that bounded JSON before deciding whether the
        // community session has expired; a signing error is recoverable.
        responseStatus = response.statusCode
        guard (200...299).contains(responseStatus) || responseStatus == 401 || responseStatus == 403 else {
            completionHandler(.cancel); finish(.failure(.http(status: responseStatus))); return
        }
        guard response.expectedContentLength <= Int64(maximumBytes) else { completionHandler(.cancel); finish(.failure(.responseTooLarge)); return }
        completionHandler(.allow)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        lock.lock(); let done = finished; lock.unlock(); guard !done else { return }
        guard data.count <= maximumBytes - bytes.count else { finish(.failure(.responseTooLarge)); return }
        bytes.append(data)
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        lock.lock(); let done = finished; lock.unlock(); guard !done else { return }
        if let error = error { finish(.failure((error as NSError).code == NSURLErrorCancelled ? .cancelled : .transport)); return }
        do {
            guard let value = try JSONSerialization.jsonObject(with: bytes) as? [String: Any] else {
                finish(.failure(responseStatus == 401 || responseStatus == 403 ? .http(status: responseStatus) : .invalidResponse)); return
            }
            if responseStatus == 401 || responseStatus == 403 {
                guard let code = HypergryphAccountAPI.integer(value["code"]), code != 0 else {
                    finish(.failure(.http(status: responseStatus))); return
                }
            }
            finish(.success(value))
        } catch { finish(.failure(responseStatus == 401 || responseStatus == 403 ? .http(status: responseStatus) : .invalidResponse)) }
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
        newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        completionHandler(nil); finish(.failure(.unsafeRedirect))
    }
    func urlSession(_ session: URLSession, didReceive challenge: URLAuthenticationChallenge,
        completionHandler: @escaping (URLSession.AuthChallengeDisposition, URLCredential?) -> Void) {
        // Default TLS validation; no trust exceptions or persisted HTTP credentials.
        completionHandler(.performDefaultHandling, nil)
    }
}
