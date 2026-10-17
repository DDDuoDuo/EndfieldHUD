import AppKit
import QuartzCore
import CryptoKit
#if ACCOUNT_REFERENCE_SPLIT
@testable import EndfieldAccountCore
#endif

// Oracle for the Windows Account Linking port. The original build-18 account
// sources (models, API, login policy, controller, canvas and gauge) execute
// unchanged inside this detached harness. No window, login page, Keychain item,
// network connection, real credential or personal data is used: HTTP is served
// by an in-process URLProtocol from synthetic envelopes, credentials are
// synthetic strings, the profile store lives in a temporary directory and the
// clock is fixed. Doubles are emitted as Swift round-trip strings.
@main enum HypergryphAccountReference {
    static func write(_ value: Any, _ file: URL) throws {
        try JSONSerialization.data(withJSONObject: value, options: [.sortedKeys, .withoutEscapingSlashes]).write(to: file, options: .atomic)
    }
    static func d(_ value: Double) -> String { "\(value)" }
    static func d(_ value: Date?) -> Any { value.map { "\($0.timeIntervalSinceReferenceDate)" } ?? NSNull() }
    static func ref(_ unix: Double) -> Date { Date(timeIntervalSince1970: unix) }

    static func main() throws {
        guard CommandLine.arguments.count == 2, ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil,
              ProcessInfo.processInfo.environment["TZ"] == "UTC" else { fatalError("usage: CFFIXED_USER_HOME=... TZ=UTC reference OUTPUT") }
        let output = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        NSApplication.shared.setActivationPolicy(.prohibited)
        var config = AppConfiguration.defaults
        config.language = .english; config.theme = .dark; config.ambientAnimation = false; config.reduceMotion = true; config.launchAtLogin = false
        HUDRuntimeAppearance.configuration = config; L10n.language = .english
        try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
        var root: [String: Any] = [:]
        root["unicode"] = unicodeSets()
        root["identity"] = identityRows()
        root["sanity"] = sanityRows()
        root["signature"] = signatureRows()
        root["decode"] = decodeRows()
        root["api"] = try apiScenarios()
        root["redaction"] = redactionRows()
        root["login"] = loginRows()
        root["avatar"] = avatarRows()
        root["controller"] = try controllerTrace()
        root["canvas"] = try canvasTrace(output: output)
        root["gauge"] = try gaugeRows()
        try write(root, output.appendingPathComponent("reference.json"))
    }

    // MARK: Unicode sets used by Foundation/ICU in the ported parsing paths.
    static func ranges(_ test: (UInt32) -> Bool) -> [[UInt32]] {
        var result: [[UInt32]] = []
        var start: UInt32? = nil
        for value in UInt32(0)...UInt32(0x10FFFF) {
            let inside = (value < 0xD800 || value > 0xDFFF) && test(value)
            if inside, start == nil { start = value }
            if !inside, let s = start { result.append([s, value - 1]); start = nil }
        }
        if let s = start { result.append([s, 0x10FFFF]) }
        return result
    }
    static func regexRanges(_ pattern: String) -> [[UInt32]] {
        var scalars = String.UnicodeScalarView()
        var offsets: [Int: UInt32] = [:]
        var utf16 = 0
        for value in UInt32(0)...UInt32(0x10FFFF) where value < 0xD800 || value > 0xDFFF {
            let scalar = Unicode.Scalar(value)!
            offsets[utf16] = value; scalars.append(scalar); utf16 += scalar.utf16.count
        }
        let text = String(scalars)
        let expression = try! NSRegularExpression(pattern: pattern)
        var matched = Set<UInt32>()
        expression.enumerateMatches(in: text, range: NSRange(location: 0, length: (text as NSString).length)) { match, _, _ in
            guard let match, match.range.length > 0, let value = offsets[match.range.location] else { return }
            matched.insert(value)
        }
        return ranges { matched.contains($0) }
    }
    static func unicodeSets() -> [String: Any] {
        let alnum = CharacterSet.alphanumerics, ws = CharacterSet.whitespaces, wsnl = CharacterSet.whitespacesAndNewlines
        let control = CharacterSet.controlCharacters
        // String.replacingOccurrences(of:with:) (secret redaction) treats every
        // digit with the same numeric value as equal to an ASCII digit needle.
        var digits: [String: Any] = [:]
        for digit in 0...9 {
            let needle = String(digit)
            digits["digitEquivalent\(digit)"] = ranges { String(Character(Unicode.Scalar($0)!)).replacingOccurrences(of: needle, with: "#") == "#" }
        }
        return digits.merging([
            "alphanumerics": ranges { alnum.contains(Unicode.Scalar($0)!) },
            "whitespaces": ranges { ws.contains(Unicode.Scalar($0)!) },
            "whitespacesAndNewlines": ranges { wsnl.contains(Unicode.Scalar($0)!) },
            "controlCharacters": ranges { control.contains(Unicode.Scalar($0)!) },
            "format": ranges { Unicode.Scalar($0)!.properties.generalCategory == .format },
            "icuDigit": regexRanges(#"\d"#),
            "icuSpace": regexRanges(#"\s"#),
            // A preceding scalar that joins "#" into one grapheme cluster
            // (Prepend), so String.lastIndex(of: "#") cannot select that "#".
            "prependBeforeHash": ranges { (String(Character(Unicode.Scalar($0)!)) + "#").count == 1 },
            // Extend/SpacingMark/ZWJ: joins a preceding base letter (String.prefix).
            "joinsAfterBase": ranges { ("a" + String(Character(Unicode.Scalar($0)!))).count == 1 },
        ]) { a, _ in a }
    }

    // MARK: Models
    static func role(_ game: HypergryphGame = .endfield, region: HypergryphAccountRegion = .mainland, uid: String = "100200300",
                     id: String = "4000500060", server: String? = "1", name: String? = nil) -> HypergryphRole {
        HypergryphRole(region: region, game: game, bindingUID: uid, roleID: id, serverID: server, name: name)
    }
    static func identityRows() -> [[String: Any]] {
        let names: [String?] = [nil, "", "   ", "Endmin#1234", "Endmin #1234", "Endmin#12345678901", "Endmin#1234567890", "#1234",
            " # 12", "End#min#77", "Endmin#", "Endmin#12a", "Endmin#１２", "管理员#0420", "\nName\r#55\n", "Name\r\n#9",
            "Na\u{0600}#1234", "Na#\u{20E3}12", "\u{3000}Endmin\u{3000}#88", "Endmin\u{00A0}#77", "\u{200B}Zero#1",
            "Tab\t#3", "A#0", "e\u{301}#42", "Endmin#٣٤"]
        var rows: [[String: Any]] = []
        for card in names {
            for binding in [nil, "Binding#9999", "Binding", "   "] as [String?] {
                var snapshot = HypergryphProfileSnapshot(role: role(name: binding), observedAt: Date(timeIntervalSinceReferenceDate: 0))
                snapshot.name = card
                let identity = snapshot.personalProfileIdentity
                rows.append(["card": card as Any? ?? NSNull(), "binding": binding as Any? ?? NSNull(),
                             "name": identity.name as Any? ?? NSNull(), "tag": identity.tag as Any? ?? NSNull()])
            }
        }
        return rows
    }
    static func sanityRows() -> [[String: Any]] {
        var rows: [[String: Any]] = []
        let base = 781_000_000.25
        let cases: [(HypergryphGame, Int, Int, Double?, Double?)] = [
            (.endfield, 42, 360, 7_000, 0), (.endfield, 42, 360, 7_000, nil), (.endfield, 359, 360, 432, -12),
            (.endfield, 0, 360, 155_520, 3), (.endfield, 0, 360, 200_000, 0), (.endfield, 400, 360, 9_000, 0),
            (.endfield, 360, 360, 0, 0), (.endfield, 10, 360, nil, 0), (.endfield, 10, 360, -5, 0),
            (.arknights, 10, 135, 7_000, 0), (.arknights, 134, 135, 359.5, 1.25), (.arknights, 0, 135, 50_000, 0),
            (.arknights, 5, 0, 100, 0), (.arknights, -1, 135, 100, 0), (.endfield, 100, 360, 1, 0)]
        for (index, item) in cases.enumerated() {
            let (game, current, maximum, fullOffset, serverOffset) = item
            let observed = Date(timeIntervalSinceReferenceDate: base + Double(index) * 0.125)
            var snapshot = HypergryphProfileSnapshot(role: role(game), observedAt: observed)
            let server = serverOffset.map { observed.addingTimeInterval($0) }
            let full: Date? = fullOffset.map { offset in
                offset < 0 ? Date(timeIntervalSince1970: -1) : (server ?? observed).addingTimeInterval(offset)
            }
            snapshot.stamina = HypergryphStamina(current: current, maximum: maximum, fullRecoveryAt: full, serverObservedAt: server)
            for elapsed in [-10.0, 0, 0.5, 49, 431.75, 432, 3_600, 7_000, 200_000] {
                let date = observed.addingTimeInterval(elapsed)
                let value = snapshot.sanityPresentation(at: date, isRefreshing: elapsed == 49, refreshAvailable: elapsed != 0)
                var row: [String: Any] = ["game": game.rawValue, "current": current, "maximum": maximum,
                    "fullRecoveryAt": d(full), "serverObservedAt": d(server), "observedAt": d(observed), "date": d(date),
                    "refreshing": elapsed == 49, "available": elapsed != 0]
                if let value {
                    row["out"] = ["current": value.current, "maximum": value.maximum, "observedAt": d(value.observedAt),
                                  "next": d(value.nextRecoveryAt), "full": d(value.fullRecoveryAt),
                                  "refreshing": value.isRefreshing, "available": value.refreshAvailable, "game": value.game.rawValue]
                } else { row["out"] = NSNull() }
                rows.append(row)
            }
        }
        return rows
    }
    static func signatureRows() -> [[String: Any]] {
        var rows: [[String: Any]] = []
        let paths = ["/web/v1/auth/refresh", "/api/v1/game/player/binding", "/api/v1/game/endfield/card/detail"]
        let queries = ["", "roleId=4000500060&serverId=1", "uid=12%20a&channelMasterId=%E8%A7%92"]
        let tokens = ["", "synthetic-token-a", String(repeating: "k", count: 64), String(repeating: "Q", count: 65), String(repeating: "z", count: 300)]
        let devices: [String?] = [nil, "", "synthetic-device-1", "dev\"quote\\slash"]
        var n = 0
        for path in paths { for query in queries { for token in tokens { for device in devices {
            n += 1; let timestamp = String(1_791_000_000 + n)
            rows.append(["path": path, "query": query, "timestamp": timestamp, "token": token, "device": device as Any? ?? NSNull(),
                "sign": HypergryphAccountAPI.signature(path: path, query: query, timestamp: timestamp, signingToken: token, deviceID: device)])
        } } } }
        return rows
    }

    // MARK: Response decoding (static adapters only)
    static func encodeRole(_ r: HypergryphRole) -> [String: Any] {
        var v: [String: Any] = ["region": r.region.rawValue, "game": r.game.rawValue, "bindingUID": r.bindingUID, "roleID": r.roleID,
                                "isDefault": r.isDefault, "isAvailable": r.isAvailable, "id": r.id]
        if let x = r.serverID { v["serverID"] = x }; if let x = r.name { v["name"] = x }
        if let x = r.serverName { v["serverName"] = x }; if let x = r.communityUserID { v["communityUserID"] = x }
        return v
    }
    static func encodeSnapshot(_ s: HypergryphProfileSnapshot) -> [String: Any] {
        var v: [String: Any] = ["role": encodeRole(s.role), "observedAt": d(s.observedAt)]
        if let x = s.name { v["name"] = x }; if let x = s.avatarURL { v["avatarURL"] = x.absoluteString }
        if let x = s.level { v["level"] = x }; if let x = s.worldLevel { v["worldLevel"] = x }
        if let x = s.experience { v["experience"] = x }; if let x = s.createdAt { v["createdAt"] = d(x) }
        if let x = s.operatorCount { v["operatorCount"] = x }; if let x = s.weaponCount { v["weaponCount"] = x }
        if let x = s.documentCount { v["documentCount"] = x }
        if let x = s.stamina { v["stamina"] = ["current": x.current, "maximum": x.maximum, "fullRecoveryAt": d(x.fullRecoveryAt), "serverObservedAt": d(x.serverObservedAt)] }
        return v
    }
    static func encodeError(_ e: Error) -> [String: Any] {
        guard let e = e as? HypergryphAPIError else { return ["error": "other"] }
        switch e {
        case .invalidCredentials: return ["error": "invalidCredentials"]
        case .invalidRole: return ["error": "invalidRole"]
        case .authenticationExpired: return ["error": "authenticationExpired"]
        case .service(let code): return ["error": "service", "code": code]
        case .http(let status): return ["error": "http", "code": status]
        case .transport: return ["error": "transport"]
        case .cancelled: return ["error": "cancelled"]
        case .responseTooLarge: return ["error": "responseTooLarge"]
        case .invalidResponse: return ["error": "invalidResponse"]
        case .unsafeRedirect: return ["error": "unsafeRedirect"]
        }
    }
    static func object(_ text: String) -> [String: Any]? {
        (try? JSONSerialization.jsonObject(with: Data(text.utf8))) as? [String: Any]
    }
    static func decodeRows() -> [String: Any] {
        let ids = ["\"4000500060\"", "4000500060", "9007199254740993", "18446744073709551615", "123456789012345678901234567890",
                   "1.0", "1.5", "1e3", "-5", "0", "true", "\"007\"", "\"abc-_.Z\"", "\"a b\"", "\"\"", "\"ü1\"", "\"角色7\"",
                   "\"１２\"", "\"a/b\"", "null", "\"" + String(repeating: "9", count: 257) + "\"", "12.000", "1E2", "-0", "0.0", "1e30", "1.5e1", "2.50e1", "1e-2", "100000000000000000000.0", "4000500060.0", "12345678901234567.0", "0.1e1", "9223372036854775807", "9223372036854775808", "-9223372036854775808", "1e19", "9.223372036854776e18", "123456789012345678901234567890123456789012345", "1.00000000000000000001", "-0.0"]
        var payloads: [[String: Any]] = []
        for envelope in ["{\"code\":0,\"data\":{\"a\":1}}", "{\"code\":\"0\",\"data\":{}}", "{\"code\":10002}", "{\"code\":10001,\"message\":\"x\"}",
                         "{\"code\":0}", "{\"code\":0,\"data\":[]}", "{\"data\":{}}", "{\"code\":true,\"data\":{}}", "{\"code\":0.0,\"data\":{}}",
                         "{\"code\":\"+0\",\"data\":{}}", "{\"code\":\" 0\",\"data\":{}}", "{\"code\":1e0,\"data\":{}}", "{\"code\":-1,\"data\":{}}"] {
            var row: [String: Any] = ["envelope": envelope]
            do { row["ok"] = try HypergryphAccountAPI.payload(object(envelope)!).keys.sorted() } catch { row.merge(encodeError(error)) { a, _ in a } }
            payloads.append(row)
        }
        var bindings: [[String: Any]] = []
        func binding(_ body: String, _ region: HypergryphAccountRegion = .mainland) {
            var row: [String: Any] = ["data": body, "region": region.rawValue]
            do { row["ok"] = try HypergryphAccountAPI.decodeBindings(object(body)!, region: region).map(encodeRole) } catch { row.merge(encodeError(error)) { a, _ in a } }
            bindings.append(row)
        }
        for id in ids {
            binding("{\"list\":[{\"appCode\":\"arknights\",\"defaultUid\":\(id),\"bindingList\":[{\"uid\":\(id),\"channelMasterId\":\(id),\"nickName\":\"Doctor\",\"channelName\":\"官服\"}]}]}")
            binding("{\"list\":[{\"appCode\":\"endfield\",\"bindingList\":[{\"uid\":\"77\",\"roles\":[{\"roleId\":\(id),\"serverId\":\(id),\"nickname\":\"Admin#1\",\"serverName\":\"Asia\"}]}]}]}", .global)
        }
        let rich = """
        {"list":[{"appCode":"arknights","defaultUid":"222","bindingList":[{"uid":"111","isDelete":true,"nickName":"Old","channelMasterId":"1"},{"uid":"222","isDefault":false,"nickname":"Doc","channelMasterId":"2","channelName":"B服"},{"uid":"222","nickName":"Dup","channelMasterId":"2"},{"uid":"333","isDefault":true}]},
        {"appCode":"endfield","defaultUid":"9","bindingList":[{"uid":"9","isDefault":true,"defaultRole":{"roleId":"5","serverId":"2","nickname":"Default"}},
        {"uid":"10","roles":[{"roleId":"6","serverId":"2","isBanned":true,"nickName":"Ban"},{"roleId":"7","serverId":"3","isDefault":true},{"roleId":"8"},{"roleId":"9","serverId":"4","nickname":""}],"defaultRole":{"roleId":"9"}},
        {"uid":"11","isDelete":true,"roles":[{"roleId":"12","serverId":"1"}]},{"uid":"12","isDefault":true,"roles":[{"roleId":"13","serverId":"1"}]}]},
        {"appCode":"unknownGame","bindingList":"ignored"},{"appCode":7,"bindingList":[]},{"bindingList":[]}]}
        """
        binding(rich); binding(rich, .global)
        for body in ["{}", "{\"list\":{}}", "{\"list\":[{\"appCode\":\"endfield\"}]}", "{\"list\":[{\"appCode\":\"endfield\",\"bindingList\":[{\"uid\":\"1\",\"roles\":{}}]}]}",
                     "{\"list\":[]}", "{\"list\":[{\"appCode\":\"arknights\",\"bindingList\":[{\"uid\":\"1\",\"isDelete\":1,\"isDefault\":\"true\"}]}]}"] { binding(body) }
        binding("{\"list\":[" + Array(repeating: "{}", count: 65).joined(separator: ",") + "]}")
        binding("{\"list\":[{\"appCode\":\"endfield\",\"bindingList\":[{\"uid\":\"1\",\"roles\":[" + (0..<257).map { "{\"roleId\":\"\($0)\",\"serverId\":\"1\"}" }.joined(separator: ",") + "]}]}]}")
        binding("{\"list\":[{\"appCode\":\"endfield\",\"bindingList\":[" + (0..<3).map { b in "{\"uid\":\"\(b)\",\"roles\":[" + (0..<200).map { "{\"roleId\":\"\(b)-\($0)\",\"serverId\":\"1\"}" }.joined(separator: ",") + "]}" }.joined(separator: ",") + "]}]}")
        var profiles: [[String: Any]] = []
        let observed = Date(timeIntervalSinceReferenceDate: 781_000_000.5)
        func profile(_ body: String, _ r: HypergryphRole) {
            var row: [String: Any] = ["data": body, "role": encodeRole(r)]
            do { row["ok"] = encodeSnapshot(try HypergryphAccountAPI.decodeProfile(object(body)!, role: r, observedAt: observed)) } catch { row.merge(encodeError(error)) { a, _ in a } }
            profiles.append(row)
        }
        let endfield = role(.endfield), ark = role(.arknights, id: "88001", server: "1")
        profile("""
        {"detail":{"base":{"roleId":"4000500060","name":"Endmin#1234","avatarUrl":"https://assets.skland.com/a/b.png?x=1","level":41,"worldLevel":"4","exp":12345,"createTime":1700000000,"charNum":31,"weaponNum":"40","docNum":125},"dungeon":{"curStamina":"42","maxStamina":360,"maxTs":1791001000},"currentTs":1791000000}}
        """, endfield)
        for variant in ["{\"detail\":{\"base\":{}}}", "{\"detail\":{}}", "{}", "{\"detail\":{\"base\":{\"roleId\":\"999\"}}}",
                        "{\"detail\":{\"base\":{\"roleId\":4000500060,\"level\":-1,\"createTime\":0,\"avatarUrl\":\"http://assets.skland.com/a.png\"},\"dungeon\":{\"curStamina\":5,\"maxStamina\":0}},\"currentTs\":\"1791000000\"}",
                        "{\"detail\":{\"base\":{\"avatarUrl\":\"https://u:p@assets.skland.com/a.png\",\"createTime\":32503680000,\"exp\":1.5}},\"currentTs\":1791000000}",
                        "{\"detail\":{\"base\":{\"avatarUrl\":\"https://assets.skland.com:8443/a.png\",\"createTime\":32503679999},\"dungeon\":{\"curStamina\":-3,\"maxStamina\":10}}}",
                        "{\"detail\":{\"base\":{\"avatarUrl\":\"https://assets.skland.com:443/a.png\",\"name\":\"\"},\"dungeon\":{\"curStamina\":3,\"maxStamina\":10,\"maxTs\":\"x\"}},\"currentTs\":5}",
                        "{\"detail\":{\"base\":{\"avatarUrl\":\"HTTPS://assets.skland.com/a.png\"}}}",
                        "{\"detail\":{\"base\":{\"avatarUrl\":\"https:///a.png\"}}}"] { profile(variant, endfield) }
        for url in ["https://assets.skland.com/a b.png", "https://assets.skland.com/é.png", "https://assets.skland.com/a|b", "https://assets.skland.com/a#frag",
                    "https://assets.skland.com/%zz", "https://assets.skland.com", "https://assets.skland.com?x", "https://ASSETS.skland.com/A.png",
                    "https://assets.skland.com:/a", "https://assets.skland.com:0443/a", "https://assets.skland.com:abc/a", "https://[::1]/a",
                    "https://资产.com/a", "https:assets.skland.com/a", "https://a.com\\b", "https://@assets.skland.com/a", "https://:@assets.skland.com/a",
                    " https://assets.skland.com/a", "https://assets.skland.com/a\u{0}", "https://assets.skland.com/a%20b", "https://a..b/c",
                    "https://assets.skland.com/" + String(repeating: "x", count: 5000), "https://assets.skland.com/[x]", "https://assets.skland.com/{x}",
                    "https://assets.skland.com/a\"b", "https://assets.skland.com/a<b>", "https://assets.skland.com/a^b`c", "https://assets.skland.com:65536/a",
                    "https://assets.skland.com:443443/a", "https://a_b.skland.com/a", "https://a b.com/a"] {
            let body = String(data: try! JSONSerialization.data(withJSONObject: ["detail": ["base": ["avatarUrl": url]]]), encoding: .utf8)!
            profile(body, endfield)
        }
        profile("""
        {"status":{"uid":"88001","name":"Doctor","avatar":{"url":"https://web-static.hg-cdn.com/x.png"},"level":120,"exp":{"current":"5"},"registerTs":1600000000,"charCnt":300,"ap":{"current":10,"max":135,"completeRecoveryTime":1791003000,"lastApAddTime":1790999000}},"currentTs":1791000000}
        """, ark)
        for variant in ["{\"status\":{\"ap\":{\"current\":10,\"max\":135,\"completeRecoveryTime\":1791003000}},\"currentTs\":1791000000}",
                        "{\"status\":{\"ap\":{\"current\":10,\"max\":135,\"completeRecoveryTime\":1790000000}},\"currentTs\":1791000000}",
                        "{\"status\":{\"ap\":{\"current\":10,\"max\":135,\"completeRecoveryTime\":1791003000,\"lastApAddTime\":1700000000}},\"currentTs\":1791000000}",
                        "{\"status\":{\"ap\":{\"current\":10,\"max\":135,\"completeRecoveryTime\":1791003000,\"lastApAddTime\":1791000500}},\"currentTs\":1791000000}",
                        "{\"status\":{\"ap\":{\"current\":140,\"max\":135,\"completeRecoveryTime\":1791003000}},\"currentTs\":1791000000}",
                        "{\"status\":{\"ap\":{\"current\":10,\"max\":135}}}",
                        "{\"status\":{\"ap\":{\"current\":10,\"max\":135,\"completeRecoveryTime\":1791900000}},\"currentTs\":1791000000}",
                        "{\"status\":{\"uid\":\"1\"}}", "{\"status\":[]}", "{}",
                        "{\"status\":{\"ap\":{\"current\":0,\"max\":135,\"completeRecoveryTime\":1791048600}}}"] { profile(variant, ark) }
        return ["payload": payloads, "bindings": bindings, "profiles": profiles]
    }

    // MARK: HTTP oracle
    enum Stub { case http(Int, [String: String], String), failure(Int), redirect(String) }
    final class StubProtocol: URLProtocol {
        static let lock = NSLock()
        static var script: [Stub] = []
        static var captured: [[String: Any]] = []
        override class func canInit(with request: URLRequest) -> Bool { true }
        override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
        override func startLoading() {
            Self.lock.lock()
            Self.captured.append(["url": request.url?.absoluteString ?? "", "method": request.httpMethod ?? "",
                                  "headers": request.allHTTPHeaderFields ?? [:]])
            let next = Self.script.isEmpty ? Stub.failure(NSURLErrorCannotConnectToHost) : Self.script.removeFirst()
            Self.lock.unlock()
            switch next {
            case .http(let status, var headers, let body):
                let data = Data(body.utf8)
                if headers["Content-Length"] == nil { headers["Content-Length"] = String(data.count) }
                let response = HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers)!
                client?.urlProtocol(self, didReceive: response, cacheStoragePolicy: .notAllowed)
                client?.urlProtocol(self, didLoad: data)
                client?.urlProtocolDidFinishLoading(self)
            case .failure(let code):
                client?.urlProtocol(self, didFailWithError: NSError(domain: NSURLErrorDomain, code: code))
            case .redirect(let location):
                let response = HTTPURLResponse(url: request.url!, statusCode: 302, httpVersion: "HTTP/1.1", headerFields: ["Location": location])!
                client?.urlProtocol(self, wasRedirectedTo: URLRequest(url: URL(string: location)!), redirectResponse: response)
            }
        }
        override func stopLoading() {}
    }
    static var apiNow = Date(timeIntervalSince1970: 1_791_000_000.75)
    static func pump(until done: () -> Bool) {
        let deadline = Date().addingTimeInterval(20)
        while !done() {
            guard Date() < deadline else { fatalError("oracle timed out") }
            RunLoop.main.run(until: Date().addingTimeInterval(0.002))
        }
    }
    static func json(_ body: [String: Any]) -> String { String(data: try! JSONSerialization.data(withJSONObject: body, options: [.sortedKeys]), encoding: .utf8)! }
    static func apiScenarios() throws -> [[String: Any]] {
        let configuration = URLSessionConfiguration.ephemeral
        configuration.protocolClasses = [StubProtocol.self]
        var scenarios: [[String: Any]] = []
        let a = HypergryphCredentials(cred: "SYNTHETIC-CRED-0001", signingToken: "synthetic-token-a", deviceID: "synthetic-device-1")
        let b = HypergryphCredentials(cred: "SYNTHETIC-CRED-0002", signingToken: "synthetic-token-b", deviceID: "synthetic-device-2")
        let legacy = HypergryphCredentials(cred: "SYNTHETIC-CRED-0003", signingToken: "synthetic-token-c", deviceID: nil)
        let ok: (Any) -> Stub = { .http(200, ["Content-Type": "application/json"], json(["code": 0, "message": "OK", "timestamp": "1791000100", "data": $0])) }
        let code: (Int, Any?) -> Stub = { c, t in var body: [String: Any] = ["code": c, "message": "synthetic"]; if let t { body["timestamp"] = t }; return .http(200, [:], json(body)) }
        let bindingsBody: [String: Any] = ["list": [
            ["appCode": "endfield", "defaultUid": "9", "bindingList": [["uid": "9", "isDefault": true, "roles": [["roleId": "4000500060", "serverId": "1", "nickname": "Endmin#1234", "serverName": "Asia", "isDefault": true]]]]],
            ["appCode": "arknights", "bindingList": [["uid": "88001", "channelMasterId": "1", "nickName": "Doctor", "channelName": "官服"]]]]]
        let endfieldRole = HypergryphRole(region: .mainland, game: .endfield, bindingUID: "9", roleID: "4000500060", serverID: "1", name: "Endmin#1234")
        let arkRole = HypergryphRole(region: .mainland, game: .arknights, bindingUID: "88001", roleID: "88001", serverID: "1")
        let unicodeRole = HypergryphRole(region: .global, game: .endfield, bindingUID: "9", roleID: "角色7", serverID: "1")
        let card: [String: Any] = ["detail": ["base": ["roleId": "4000500060", "name": "Endmin#1234", "level": 41], "dungeon": ["curStamina": 42, "maxStamina": 360, "maxTs": 1791007000]], "currentTs": 1791000050]
        enum Op { case refreshCred(String, HypergryphAccountRegion), refresh(HypergryphCredentials, HypergryphAccountRegion)
                  case bindings(HypergryphCredentials, HypergryphAccountRegion), profile(HypergryphRole, HypergryphCredentials), advance(Double) }
        func run(_ name: String, route: HypergryphAccountAPI.EndfieldCardRoute = .authenticatedSelf, _ steps: [(Op, [Stub])]) {
            apiNow = Date(timeIntervalSince1970: 1_791_000_000.75)
            let api = HypergryphAccountAPI(configuration: configuration, now: { apiNow }, endfieldCardRoute: route)
            var rows: [[String: Any]] = []
            for (op, stubs) in steps {
                StubProtocol.lock.lock(); StubProtocol.script = stubs; StubProtocol.captured = []; StubProtocol.lock.unlock()
                var result: [String: Any]? = nil
                var opName = ""
                var input: [String: Any] = [:]
                switch op {
                case .advance(let seconds): apiNow = apiNow.addingTimeInterval(seconds); rows.append(["op": "advance", "seconds": seconds]); continue
                case .refreshCred(let cred, let region):
                    opName = "refreshCred"; input = ["cred": cred, "region": region.rawValue]
                    api.refreshCredentials(cred: cred, region: region) { r in result = (try? r.get()).map { ["ok": ["cred": $0.cred, "signingToken": $0.signingToken, "deviceID": $0.deviceID as Any? ?? NSNull()]] } ?? encodeError(r.failureValue!) }
                case .refresh(let c, let region):
                    opName = "refresh"; input = ["cred": c.cred, "signingToken": c.signingToken, "deviceID": c.deviceID as Any? ?? NSNull(), "region": region.rawValue]
                    api.refreshCredentials(credentials: c, region: region) { r in result = (try? r.get()).map { ["ok": ["cred": $0.cred, "signingToken": $0.signingToken, "deviceID": $0.deviceID as Any? ?? NSNull()]] } ?? encodeError(r.failureValue!) }
                case .bindings(let c, let region):
                    opName = "bindings"; input = ["cred": c.cred, "signingToken": c.signingToken, "deviceID": c.deviceID as Any? ?? NSNull(), "region": region.rawValue]
                    api.bindings(credentials: c, region: region) { r in result = (try? r.get()).map { ["ok": $0.map(encodeRole)] } ?? encodeError(r.failureValue!) }
                case .profile(let r, let c):
                    opName = "profile"; input = ["role": encodeRole(r), "cred": c.cred, "signingToken": c.signingToken, "deviceID": c.deviceID as Any? ?? NSNull()]
                    api.profile(role: r, credentials: c) { x in result = (try? x.get()).map { ["ok": encodeSnapshot($0)] } ?? encodeError(x.failureValue!) }
                }
                pump { result != nil }
                StubProtocol.lock.lock(); let captured = StubProtocol.captured; let left = StubProtocol.script.count; StubProtocol.lock.unlock()
                rows.append(["op": opName, "input": input, "now": d(apiNow), "responses": stubs.map { stub -> [String: Any] in
                    switch stub {
                    case .http(let s, let h, let b): return ["status": s, "headers": h, "body": b]
                    case .failure(let c): return ["failure": c]
                    case .redirect(let l): return ["redirect": l]
                    }
                }, "requests": captured, "unused": left, "result": result!])
            }
            scenarios.append(["name": name, "route": route == .web ? "web" : route == .app ? "app" : "self", "steps": rows])
        }
        run("refresh-cred", [(.refreshCred(a.cred, .mainland), [ok(["token": "issued-token-1"])]),
                             (.refreshCred("bad cred", .mainland), []),
                             (.refreshCred(a.cred, .global), [ok(["token": 5])]),
                             (.refreshCred(a.cred, .global), [code(10002, nil)])])
        run("refresh-signed-clock", [(.refresh(a, .mainland), [code(10003, "1791000300"), ok(["token": "issued-token-2"])]),
                                     (.bindings(a, .mainland), [ok(bindingsBody)]),
                                     (.advance(12.5), []),
                                     (.refresh(a, .mainland), [code(10003, nil)]),
                                     (.refresh(a, .mainland), [code(10003, 1791000900), code(10003, 1791000950)]),
                                     (.refresh(legacy, .global), [ok(["token": "issued-token-3"])]),
                                     (.refresh(HypergryphCredentials(cred: a.cred, signingToken: "bad token", deviceID: a.deviceID), .mainland), [])])
        run("signing-recovery", [(.bindings(a, .mainland), [code(10000, "1791000010"), ok(["token": "recovered-token"]), ok(bindingsBody)]),
                                 (.bindings(a, .mainland), [ok(bindingsBody)]),
                                 (.profile(endfieldRole, a), [ok(card)]),
                                 (.bindings(b, .mainland), [ok(bindingsBody)]),
                                 (.bindings(a, .mainland), [ok(bindingsBody)]),
                                 (.bindings(a, .global), [code(10000, nil), code(10002, nil)]),
                                 (.bindings(a, .global), [code(10000, nil), ok(["token": "recovered-2"]), code(10000, nil)]),
                                 (.bindings(a, .global), [code(10003, "1791000005"), code(10003, "1791000006")])])
        run("errors", [(.bindings(a, .mainland), [code(10002, nil)]),
                       (.bindings(a, .mainland), [code(10001, nil)]),
                       (.bindings(a, .mainland), [.http(500, [:], "{\"code\":0}")]),
                       (.bindings(a, .mainland), [.http(401, [:], "{\"code\":10002,\"message\":\"login\"}")]),
                       (.bindings(a, .mainland), [.http(403, [:], "{\"code\":0,\"data\":{}}")]),
                       (.bindings(a, .mainland), [.http(401, [:], "not json")]),
                       (.bindings(a, .mainland), [.http(403, [:], "[1]")]),
                       (.bindings(a, .mainland), [.http(200, [:], "not json")]),
                       (.bindings(a, .mainland), [.http(200, [:], "[]")]),
                       (.bindings(a, .mainland), [.http(204, [:], "")]),
                       (.bindings(a, .mainland), [.failure(NSURLErrorTimedOut)]),
                       (.bindings(a, .mainland), [.redirect("https://zonai.skland.com/elsewhere")]),
                       (.bindings(a, .mainland), [.http(200, ["Content-Length": "8388609"], "{}")]),
                       (.bindings(a, .mainland), [.http(401, [:], "{\"code\":10000}"), ok(["token": "t2"]), .http(200, [:], "{\"code\":0,\"data\":{\"list\":[]}}")]),
                       (.bindings(HypergryphCredentials(cred: "", signingToken: "x", deviceID: nil), .mainland), []),
                       (.bindings(HypergryphCredentials(cred: "c", signingToken: "x", deviceID: "dev ice"), .mainland), [])])
        run("profiles", [(.profile(endfieldRole, a), [ok(card)]),
                         (.profile(arkRole, a), [ok(["status": ["uid": "88001", "ap": ["current": 1, "max": 135, "completeRecoveryTime": 1791010000]], "currentTs": 1791000000])]),
                         (.profile(HypergryphRole(region: .mainland, game: .arknights, bindingUID: "1", roleID: "1"), a), [ok(["status": [:]])]),
                         (.profile(unicodeRole, legacy), [ok(["detail": ["base": [:]]])]),
                         (.profile(HypergryphRole(region: .mainland, game: .endfield, bindingUID: "9", roleID: "1"), a), []),
                         (.profile(HypergryphRole(region: .mainland, game: .endfield, bindingUID: "9", roleID: "1", serverID: "1", isAvailable: false), a), []),
                         (.profile(HypergryphRole(region: .mainland, game: .arknights, bindingUID: "9", roleID: "1", serverID: "a b"), a), []),
                         (.profile(HypergryphRole(region: .mainland, game: .endfield, bindingUID: "9", roleID: "x/y", serverID: "1"), a), []),
                         (.profile(endfieldRole, a), [ok(["detail": ["base": ["roleId": "1"]]])])])
        var webRole = endfieldRole; webRole.communityUserID = "55501"
        run("web-route", route: .web, [(.bindings(a, .mainland), [ok(bindingsBody), ok(["user": ["id": 55501]])]),
                                       (.bindings(a, .mainland), [ok(bindingsBody), ok(["user": ["userId": "u-1"]])]),
                                       (.bindings(a, .mainland), [ok(bindingsBody), ok(["user": [:]])]),
                                       (.bindings(a, .mainland), [ok(["list": []])]),
                                       (.profile(webRole, a), [ok(card)]),
                                       (.profile(endfieldRole, a), [])])
        run("app-route", route: .app, [(.profile(webRole, a), [ok(card)])])
        return scenarios
    }

    // MARK: Diagnostics redaction/classification
    static func redactionRows() -> [String: Any] {
        var request = URLRequest(url: URL(string: "https://zonai.skland.com/api/v1/game/endfield/card/detail?roleId=4000500060&serverId=1")!)
        request.setValue("SYNTHETIC-CRED-0001", forHTTPHeaderField: "cred"); request.setValue("synthetic-device-1", forHTTPHeaderField: "dId")
        request.setValue("0123456789abcdef0123456789abcdef", forHTTPHeaderField: "sign")
        let messages: [Any?] = [nil, "", 42, "Role 4000500060 is not bound to SYNTHETIC-CRED-0001", "设备 synthetic-device-1 校验失败",
            "See https://example.com/path?q=1 or mail a.b@example.org now", "token abcdefghijklmnopqrstuvwxyz0123 leaked",
            "<b>bold</b>   spaced\n\ttext", "id ١٢٣٤ and １２３ and 12", "bidi \u{202E}evil\u{202C} zero\u{200B}width \u{0007}bell",
            String(repeating: "x", count: 4097), String(repeating: "长", count: 600), "only <tag>", "   ",
            "e\u{301}\u{301}" + String(repeating: "é", count: 520), "secret-xyz and SECRET-XYZ", "\u{3000}wide\u{3000}space\u{00A0}nb"]
        var rows: [[String: Any]] = []
        for message in messages {
            rows.append(["message": message ?? NSNull(), "secrets": ["secret-xyz", ""],
                         "out": HypergryphAccountAPI.redactedServiceMessage(message, request: request, secrets: ["secret-xyz", ""])])
        }
        var classify: [[String: Any]] = []
        for message in [nil, 5, "", "Device check failed", "设备异常", "INVALID SIGN", "签名错误", "Parameter missing", "参数错误", "Real-Name required",
                        "实名认证", "Forbidden", "权限不足", "授權失敗", "private profile", "UNAUTHORIZED", "角色不存在", "Role missing", "绑定失败",
                        "Login expired", "请登录", "credential bad", "something else", String(repeating: "设", count: 342), String(repeating: "a", count: 1025) + "device"] as [Any?] {
            classify.append(["message": message ?? NSNull(), "reason": HypergryphAPIDiagnostic.Reason.classify(message).rawValue])
        }
        return ["request": ["url": request.url!.absoluteString, "headers": request.allHTTPHeaderFields!], "redacted": rows, "classify": classify]
    }

    // MARK: Login policy and scripts
    static func loginRows() -> [String: Any] {
        let urls: [String?] = [nil, "https://www.skland.com/", "https://www.skland.com/x?y", "https://WWW.SKLAND.COM/", "HTTPS://www.skland.com/",
            "http://www.skland.com/", "https://www.skland.com:443/", "https://www.skland.com:8443/", "https://u:p@www.skland.com/",
            "https://u@www.skland.com/", "https://web-api.skland.com/a", "https://user.hypergryph.com/login", "https://as.hypergryph.com/",
            "https://web-api.hypergryph.com/", "https://assets.skland.com/a.png", "https://www.skport.com/", "https://web-api.skport.com/",
            "https://user.gryphline.com/", "https://as.gryphline.com/", "https://web-api.gryphline.com/", "https://web-api.gryphline.net/",
            "https://assets.skport.com/", "https://gcaptcha4.geetest.com/x", "https://gcaptcha4.geevisit.com/", "https://gcaptcha4.gsensebot.com/",
            "https://static.geetest.com/", "https://static.geevisit.com/", "https://dn-staticdown.qbox.me/", "about:blank", "about:srcdoc",
            "https://evil.example/", "https://www.skland.com.evil.example/", "https://skland.com/", "javascript:alert(1)", "data:text/html,x",
            "https://www.skland.com./", "https://[::1]/", "https://zonai.skland.com/"]
        var origin: [[String: Any]] = [], navigation: [[String: Any]] = [], popup: [[String: Any]] = []
        for region in HypergryphAccountRegion.allCases {
            for url in urls {
                let parsed = url.flatMap(URL.init(string:))
                origin.append(["url": url as Any? ?? NSNull(), "region": region.rawValue, "parsed": parsed != nil,
                               "accepts": HypergryphAccountLoginPolicy.acceptsCredentialOrigin(parsed, region: region)])
                for main in [true, false] {
                    navigation.append(["url": url as Any? ?? NSNull(), "region": region.rawValue, "mainFrame": main, "parsed": parsed != nil,
                                       "permits": HypergryphAccountLoginPolicy.permitsNavigation(parsed, region: region, mainFrame: main)])
                }
                for source in [nil, "https://www.skland.com/", "https://www.skport.com/", "https://user.hypergryph.com/", "https://gcaptcha4.geetest.com/", "about:blank"] as [String?] {
                    popup.append(["url": url as Any? ?? NSNull(), "source": source as Any? ?? NSNull(), "region": region.rawValue,
                                  "permits": HypergryphAccountLoginPolicy.permitsPopup(parsed, from: source.flatMap(URL.init(string:)), region: region)])
                }
            }
        }
        let zoom = [0, 1, 639.5, 640, 860, 1279, 1280, 2000, -10, Double.infinity, -Double.infinity, Double.nan].map { width -> [String: Any] in
            ["width": d(width), "zoom": d(Double(HypergryphAccountLoginPolicy.pageZoom(for: CGFloat(width))))] }
        let accepts = ["", "a", "a b", "!~", "\u{7F}", "é", String(repeating: "x", count: 4096), String(repeating: "x", count: 4097), "\t"].map {
            ["value": $0, "accepts": HypergryphAccountLoginPolicy.acceptsCredential($0)] }
        let nonce = "00000000-0000-4000-8000-000000000001"
        let bodies = ["{\"nonce\":\"\(nonce)\",\"cred\":\"C1\",\"signingToken\":\"T1\",\"deviceID\":\"D1\"}",
                      "{\"nonce\":\"\(nonce)\",\"cred\":\"C1\",\"signingToken\":\"T1\"}",
                      "{\"nonce\":\"\(nonce)\",\"cred\":\"C1\",\"signingToken\":\"T1\",\"deviceID\":\"D1\",\"extra\":1}",
                      "{\"nonce\":\"other\",\"cred\":\"C1\",\"signingToken\":\"T1\",\"deviceID\":\"D1\"}",
                      "{\"nonce\":\"\(nonce)\",\"cred\":\"C 1\",\"signingToken\":\"T1\",\"deviceID\":\"D1\"}",
                      "{\"nonce\":\"\(nonce)\",\"cred\":\"C1\",\"signingToken\":7,\"deviceID\":\"D1\"}",
                      "{\"nonce\":\"\(nonce)\",\"cred\":\"C1\",\"signingToken\":\"T1\",\"deviceID\":null}",
                      "[\"\(nonce)\"]", "\"text\"", "{\"nonce\":\"\(nonce)\",\"cred\":\"C1\"}"]
        var sessions: [[String: Any]] = []
        for body in bodies {
            let value = try! JSONSerialization.jsonObject(with: Data(body.utf8), options: [.fragmentsAllowed])
            for (region, mainFrame, frame, page, expected) in [(HypergryphAccountRegion.mainland, true, "https://www.skland.com", "https://www.skland.com/user", nonce),
                                                               (.mainland, false, "https://www.skland.com", "https://www.skland.com/", nonce),
                                                               (.mainland, true, "https://user.hypergryph.com", "https://www.skland.com/", nonce),
                                                               (.mainland, true, "https://www.skland.com", "https://user.hypergryph.com/", nonce),
                                                               (.global, true, "https://www.skport.com", "https://www.skport.com/", nonce),
                                                               (.global, true, "https://www.skland.com", "https://www.skland.com/", nonce),
                                                               (.mainland, true, "https://www.skland.com", "https://www.skland.com/", "")] {
                let session = HypergryphAccountLoginPolicy.decodeSession(value, expectedNonce: expected, region: region, mainFrame: mainFrame,
                    frameOrigin: URL(string: frame), pageURL: URL(string: page))
                let legacy = HypergryphAccountLoginPolicy.decodeCredential(value, expectedNonce: expected, region: region, mainFrame: mainFrame,
                    frameOrigin: URL(string: frame), pageURL: URL(string: page))
                sessions.append(["body": body, "region": region.rawValue, "mainFrame": mainFrame, "frame": frame, "page": page, "nonce": expected,
                    "session": session.map { ["cred": $0.cred, "signingToken": $0.signingToken as Any? ?? NSNull(), "deviceID": $0.deviceID as Any? ?? NSNull(), "region": $0.region.rawValue] } ?? NSNull(),
                    "legacy": legacy ?? NSNull()])
            }
        }
        var scripts: [String: Any] = [:]
        for region in HypergryphAccountRegion.allCases {
            scripts[region.rawValue] = ["bridge": HypergryphAccountLoginPolicy.bridgeScript(region: region, nonce: nonce),
                                        "read": HypergryphAccountLoginPolicy.credentialReadScript(region: region, nonce: nonce),
                                        "direct": HypergryphAccountLoginPolicy.directLoginScript(region: region, nonce: nonce),
                                        "site": HypergryphAccountLoginPolicy.siteURL(region).absoluteString]
        }
        let tags: [HypergryphAccountLoginDiagnostic] = [.consented, .pageStarted, .pageLoaded, .pageRetried, .pageTimeout, .pageFailed(-1001),
            .webProcessTerminated, .navigationBlocked, .popupOpened, .popupClosed, .credentialAccepted, .credentialRejected,
            .loginFormRequested, .loginFormUnavailable, .credentialContextMissing, .completed, .cancelled, .expired]
        return ["origin": origin, "navigation": navigation, "popup": popup, "zoom": zoom, "accepts": accepts, "sessions": sessions,
                "scripts": scripts, "nonce": nonce, "tags": tags.map(\.tag),
                "keys": [HypergryphAccountLoginPolicy.credentialKey, HypergryphAccountLoginPolicy.signingTokenKey, HypergryphAccountLoginPolicy.deviceIDKey,
                         HypergryphAccountLoginPolicy.messageName, HypergryphAccountLoginPolicy.stateMessageName]]
    }
    static func avatarRows() -> [[String: Any]] {
        ["https://bbs.hycdn.cn/a.png", "https://BBS.HYCDN.CN/a.png", "HTTPS://assets.skland.com/a.png", "https://assets.skport.com/x",
         "https://static.skport.com/x", "https://web-static.hg-cdn.com/x", "https://web-static.hg-cdn.com:443/x", "https://web-static.hg-cdn.com:444/x",
         "http://assets.skland.com/a.png", "https://u@assets.skland.com/a.png", "https://evil.example/a.png", "https://assets.skland.com.evil/a",
         "https://assets.skland.com./a"].map { ["url": $0, "allowed": URL(string: $0).map(HypergryphAvatarLoader.isAllowedURL) ?? false] }
    }
}
