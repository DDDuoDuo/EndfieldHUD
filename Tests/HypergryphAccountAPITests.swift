import Foundation

/// Every URLSession request is intercepted. No real account, network, cookie store, or Keychain.
enum HypergryphAccountAPITests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) { count += 1; if !condition { fatalError(message) } }
        let date = Date(timeIntervalSince1970: 1_800_000_000)
        let credentials = HypergryphCredentials(cred: "fixture-cred", signingToken: "test-signing-token")
        check(HypergryphAccountAPI.signature(path: "/api/v1/game/player/binding", query: "", timestamp: "1800000000", signingToken: credentials.signingToken)
              == "9bc850f4a4cd807579a3fa7b31a004d0", "Official ordered header/HMAC-hex-MD5 vector matches independent Python implementation")
        check(HypergryphAccountAPI.signature(path: "/web/v1/game/endfield/card/detail", query: "roleId=123456&serverId=1&userId=community-9", timestamp: "1800000000", signingToken: credentials.signingToken)
              == "8dd17612820eeb685c17dd435035602a", "GET query bytes participate in the signature, without a question mark")
        let sessionCredentials = HypergryphCredentials(cred: credentials.cred, signingToken: credentials.signingToken, deviceID: "fixture-device-id")
        check(HypergryphAccountAPI.signature(path: "/api/v1/game/player/binding", query: "", timestamp: "1800000000",
              signingToken: credentials.signingToken, deviceID: sessionCredentials.deviceID) == "9aff9f3d1ebeefcd9caff4df9f1d01a4",
              "Issued device ID participates in the independently calculated official signing vector")
        check(HypergryphAccountAPI.signature(path: "/api/v1/game/player/binding", query: "", timestamp: "1800000000",
              signingToken: credentials.signingToken, deviceID: "device-\"quoted\"\\slash/end") == "c515fde93db4157416abd6a367978509",
              "Quoted, backslash and slash device bytes use JSON.stringify-compatible escaping")
        let legacyCredentials = try! JSONDecoder().decode(HypergryphCredentials.self,
            from: Data("{\"cred\":\"fixture-cred\",\"signingToken\":\"test-signing-token\"}".utf8))
        check(legacyCredentials == credentials && legacyCredentials.deviceID == nil, "Existing Keychain payloads without device context still decode")
        check((try! JSONDecoder().decode(HypergryphCredentials.self, from: JSONEncoder().encode(sessionCredentials))) == sessionCredentials,
              "A community session retains its issued device context across Keychain encoding")
        let role = HypergryphRole(region: .mainland, game: .endfield, bindingUID: "account-7", roleID: "123456",
            serverID: "1", communityUserID: "community-9")
        let card: [String: Any] = ["detail": ["base": ["roleId": "123456", "name": "Endministrator", "level": 23,
            "worldLevel": "4", "exp": 91, "createTime": "1700000000", "charNum": 20, "weaponNum": "12", "docNum": 0,
            "avatarUrl": "https://bbs.hycdn.cn/fixture/avatar.png"], "dungeon": ["curStamina": "245", "maxStamina": "240", "maxTs": "0"],
            "currentTs": "1800000000"]]
        let snapshot = try! HypergryphAccountAPI.decodeProfile(card, role: role, observedAt: date)
        check(snapshot.name == "Endministrator" && snapshot.level == 23 && snapshot.worldLevel == 4, "Actual Endfield base fields decode across numeric representations")
        check(snapshot.operatorCount == 20 && snapshot.weaponCount == 12 && snapshot.documentCount == 0, "Explicit zero is retained while count fields map independently")
        check(snapshot.createdAt?.timeIntervalSince1970 == 1_700_000_000 && snapshot.avatarURL?.host == "bbs.hycdn.cn", "Wake timestamp and server avatar are retained")
        check(snapshot.stamina?.current == 245 && snapshot.stamina?.maximum == 240 && snapshot.stamina?.fullRecoveryAt == nil,
              "Over-cap stamina is not clamped and zero recovery sentinel does not become a real date")
        let sparse = try! HypergryphAccountAPI.decodeProfile(["detail": ["base": ["roleId": "123456"],
            "dungeon": ["curStamina": -1, "maxStamina": 240], "dailyMission": ["dailyActivation": 100]]], role: role, observedAt: date)
        check(sparse.name == nil && sparse.level == nil && sparse.operatorCount == nil && sparse.weaponCount == nil
              && sparse.documentCount == nil && sparse.stamina == nil, "Missing data and provider unavailable sentinels never become fabricated zero/activity stamina")
        let malformed = try! HypergryphAccountAPI.decodeProfile(["detail": ["base": ["level": true, "charNum": 1.5,
            "weaponNum": "bad", "docNum": -1, "createTime": "1800000000000", "avatarUrl": "file:///private/fixture"]]], role: role, observedAt: date)
        check(malformed.level == nil && malformed.operatorCount == nil && malformed.weaponCount == nil && malformed.documentCount == nil
              && malformed.createdAt == nil && malformed.avatarURL == nil, "Boolean/fractional/negative/malformed fields and invalid image schemes are unavailable")
        do { _ = try HypergryphAccountAPI.decodeProfile(["detail": ["base": ["roleId": "wrong"]]], role: role, observedAt: date); check(false, "Wrong role must fail") }
        catch { check(error as? HypergryphAPIError == .invalidResponse, "A mismatched game UID cannot overwrite the selected profile") }
        let ark = HypergryphRole(region: .mainland, game: .arknights, bindingUID: "9001", roleID: "9001")
        let arkSnapshot = try! HypergryphAccountAPI.decodeProfile(["status": ["uid": "9001", "name": "Doctor", "charCnt": 77,
            "registerTs": 1_600_000_000, "ap": ["current": 80, "max": 135, "completeRecoveryTime": 1_800_019_800]]], role: ark, observedAt: date)
        check(arkSnapshot.stamina?.current == 80 && arkSnapshot.stamina?.maximum == 135 && arkSnapshot.operatorCount == 77,
              "Arknights maps status.ap and charCnt independently from Endfield")
        check(arkSnapshot.worldLevel == nil && arkSnapshot.weaponCount == nil && arkSnapshot.documentCount == nil,
              "Arknights never invents Endfield-only profile counts")
        func normalizedArkAP(_ current: Int = 80, maximum: Int = 135, full: Any? = nil,
                             last: Any? = nil, server: Any? = nil, observed: Date? = nil) -> Int? {
            var ap: [String: Any] = ["current": current, "max": maximum]
            ap["completeRecoveryTime"] = full; ap["lastApAddTime"] = last
            var data: [String: Any] = ["status": ["uid": "9001", "ap": ap]]
            data["currentTs"] = server
            return try! HypergryphAccountAPI.decodeProfile(data, role: ark, observedAt: observed ?? date).stamina?.current
        }
        for (elapsed, expected) in [(359, 80), (360, 81), (361, 81)] {
            check(normalizedArkAP(full: 1_800_019_800, last: 1_800_000_000 - elapsed, server: 1_800_000_000) == expected,
                  "Official Arknights tick recovery changes exactly at each six-minute boundary")
        }
        for (remaining, expected) in [(359, 134), (360, 134), (361, 133)] {
            check(normalizedArkAP(full: 1_800_000_000 + remaining, server: 1_800_000_000) == expected,
                  "Without a stored tick, the full-recovery ceiling formula preserves partial recovery intervals")
        }
        check(normalizedArkAP(full: 1_800_000_000, last: 1_800_000_001, server: 1_800_000_000) == 135 &&
              normalizedArkAP(full: 1_799_999_999, server: 1_800_000_000) == 135,
              "A valid completed recovery deadline returns full AP even when the last-tick field is inconsistent")
        check(normalizedArkAP(160, full: 1_799_999_999, last: 1_700_000_000, server: 1_800_000_000) == 160,
              "Arknights over-cap AP is preserved rather than clamped to the natural maximum")
        for invalidFull: Any? in [nil, 0, -1, "invalid", 1_800_000_000_000] {
            check(normalizedArkAP(full: invalidFull, last: 1_700_000_000, server: 1_800_000_000) == 80,
                  "Missing, zero, disabled, malformed or millisecond recovery sentinels preserve reported AP")
        }
        check(normalizedArkAP(full: 1_800_019_800, last: 1_800_000_001, server: 1_800_000_000) == 80,
              "A future last-recovery timestamp cannot subtract AP")
        check(normalizedArkAP(full: 1_800_019_800, last: 1_799_999_640, server: 1_800_000_000,
                              observed: date.addingTimeInterval(7_200)) == 81,
              "Valid server currentTs takes precedence over a skewed local observation clock")
        check(normalizedArkAP(full: 1_800_019_800, last: 1_799_999_640) == 81 &&
              normalizedArkAP(full: 1_800_019_800, last: 1_799_999_640, server: "invalid") == 81,
              "Missing or invalid server currentTs falls back to the provided observation date")
        check(normalizedArkAP(full: 1_800_019_800, last: 0, server: 1_800_000_000) == 80 &&
              normalizedArkAP(full: 1_800_100_000, server: 1_800_000_000) == 80,
              "Invalid stored tick uses the deadline fallback without lowering raw AP")
        check(normalizedArkAP(Int.max - 1, maximum: Int.max, full: 1_800_019_800, last: 1, server: 1_800_000_000) == Int.max &&
              normalizedArkAP(0, maximum: Int.max, full: 1_800_000_360, server: 1_800_000_000) == Int.max - 1,
              "Tick and deadline normalization bound arithmetic before integer conversion or addition")
        check(normalizedArkAP(0, full: 32_503_679_999, observed: Date(timeIntervalSince1970: -Double.greatestFiniteMagnitude)) == 0 &&
              normalizedArkAP(full: 1_800_019_800, observed: Date(timeIntervalSince1970: .infinity)) == 80,
              "Extreme or nonfinite observation dates do not overflow or fabricate recovered AP")
        // Runtime projection reuses timestamps without network activity or a new timer.
        for game in HypergryphGame.allCases {
            let seconds: TimeInterval = game == .endfield ? 432 : 360
            let projectionRole = HypergryphRole(region: .global, game: game, bindingUID: "projection", roleID: "projection")
            var projected = HypergryphProfileSnapshot(role: projectionRole, observedAt: date)
            let serverTime = date.addingTimeInterval(7_200)
            projected.stamina = HypergryphStamina(current: 42, maximum: 360,
                fullRecoveryAt: serverTime.addingTimeInterval(317 * seconds + 49), serverObservedAt: serverTime)
            let atStart = projected.sanityPresentation(at: date)!
            check(atStart.current == 42 && atStart.nextRecoveryAt == date.addingTimeInterval(49)
                  && atStart.fullRecoveryAt == date.addingTimeInterval(317 * seconds + 49),
                  "Server timestamps become correctly offset local next/full recovery dates")
            check(projected.sanityPresentation(at: date.addingTimeInterval(48))?.current == 42,
                  "Projection never grants a point before its actual recovery boundary")
            check(projected.sanityPresentation(at: date.addingTimeInterval(49))?.current == 43
                  && projected.sanityPresentation(at: date.addingTimeInterval(49))?.nextRecoveryAt == date.addingTimeInterval(49 + seconds),
                  "Each game's exact recovery boundary advances sanity and the next countdown")
            check(projected.sanityPresentation(at: date.addingTimeInterval(49 + seconds))?.current == 44,
                  "Endfield and Arknights retain their separate 432/360 second recovery intervals")
            let full = projected.sanityPresentation(at: date.addingTimeInterval(317 * seconds + 49))!
            check(full.current == 360 && full.nextRecoveryAt == nil && full.fullRecoveryAt == nil,
                  "Natural recovery stops exactly at the cap and hides both countdowns")
            check(projected.sanityPresentation(at: date.addingTimeInterval(172_800))?.current == 360,
                  "Sleep or closed-HUD elapsed time catches up in a single local projection")
            check(projected.sanityPresentation(at: date.addingTimeInterval(-1))?.current == 42,
                  "A clock moved behind the snapshot cannot invent recovered sanity")
            let busyProjection = projected.sanityPresentation(at: date, isRefreshing: true, refreshAvailable: false)!
            check(busyProjection.isRefreshing && !busyProjection.refreshAvailable && busyProjection.observedAt == date,
                  "Dropdown state shares the exact sanity snapshot and its last synchronization time")
            let stored = try! JSONEncoder().encode(projected)
            let restoredProjection = try! JSONDecoder().decode(HypergryphProfileSnapshot.self, from: stored)
            check(restoredProjection.sanityPresentation(at: date.addingTimeInterval(49))?.current == 43,
                  "Existing snapshot timestamps are sufficient for projection after an app restart")
            projected.stamina = HypergryphStamina(current: 400, maximum: 360,
                fullRecoveryAt: date.addingTimeInterval(-1), serverObservedAt: date)
            let over = projected.sanityPresentation(at: date.addingTimeInterval(10_000))!
            check(over.current == 400 && over.nextRecoveryAt == nil && over.fullRecoveryAt == nil,
                  "Purchased sanity above the natural cap is never discarded")
            projected.stamina = HypergryphStamina(current: 0, maximum: 360)
            let unknown = projected.sanityPresentation(at: date.addingTimeInterval(10_000))!
            check(unknown.current == 0 && unknown.nextRecoveryAt == nil && unknown.fullRecoveryAt == nil,
                  "A missing or legacy recovery deadline remains unknown, including zero sanity")
        }
        let bindingData: [String: Any] = ["list": [
            ["appCode": "unknown-future-game", "bindingList": []],
            ["appCode": "arknights", "defaultUid": "9001", "bindingList": [["uid": "9001", "nickName": "Doctor", "channelMasterId": "1", "channelName": "Official"]]],
            ["appCode": "endfield", "bindingList": [["uid": "account-7", "defaultRole": ["roleId": "123456", "serverId": "1"],
                "roles": [["roleId": "123456", "serverId": "1", "nickname": "Main"], ["roleId": "123456", "serverId": "2", "isBanned": true]]]]]]]
        let parsed = try! HypergryphAccountAPI.decodeBindings(bindingData, region: .mainland)
        check(parsed.count == 3 && parsed[0].isDefault && parsed[1].isDefault && !parsed[2].isAvailable,
              "Known games retain nested server roles and mark bans unavailable")
        check(parsed[1].roleID == "123456" && parsed[1].bindingUID == "account-7" && parsed[1].id != parsed[2].id,
              "Binding account ID and game UID are distinct; server participates in stable identity")
        let globalParsed = try! HypergryphAccountAPI.decodeBindings(bindingData, region: .global)
        check(globalParsed[1].id != parsed[1].id, "Identical CN/Global role numbers do not collide")
        let fallback = try! HypergryphAccountAPI.decodeBindings(["list": [["appCode": "endfield", "bindingList": [["uid": "binding",
            "isDelete": true, "defaultRole": ["roleId": "789", "serverId": "1"]]]]]], region: .global)
        check(fallback.count == 1 && !fallback[0].isAvailable, "A default-only role is retained, including deleted-account unavailability")
        check((try? HypergryphAccountAPI.decodeBindings(["list": []], region: .global)) == [], "No supported roles is a valid empty response")
        check((try? HypergryphAccountAPI.decodeBindings([:], region: .global)) == nil, "Missing binding schema is an error, not a fabricated empty account")
        check((try! JSONDecoder().decode(HypergryphRole.self, from: JSONEncoder().encode(role))) == role, "Role cache round trips without credentials")
        let largeBindingJSON = Data(#"{"list":[{"appCode":"endfield","bindingList":[{"uid":9007199254740993,"roles":[{"roleId":9007199254740995,"serverId":18446744073709551615}]}]}]}"#.utf8)
        let largeBindings = try! HypergryphAccountAPI.decodeBindings(JSONSerialization.jsonObject(with: largeBindingJSON) as! [String: Any], region: .mainland)
        check(largeBindings[0].bindingUID == "9007199254740993" && largeBindings[0].roleID == "9007199254740995"
              && largeBindings[0].serverID == "18446744073709551615", "Numeric JSON identities retain exact signed/unsigned 64-bit digits above Double precision")
        check((try! HypergryphAccountAPI.decodeProfile(["detail": ["base": ["exp": NSNumber(value: Int.max)]]], role: role, observedAt: date)).experience == Int.max,
              "Exact signed integers remain representable at the model boundary")

        var reasonRequest = URLRequest(url: URL(string: "https://zonai.skland.com/web/v1/game/endfield/card/detail?roleId=123456&serverId=1&userId=7654321")!)
        reasonRequest.setValue("fixture-private-credential", forHTTPHeaderField: "cred")
        let reason = HypergryphAccountAPI.redactedServiceMessage(
            "签名校验失败；cred=fixture-private-credential token=fixture-signing-key role=123456 account=7654321 mail=user@example.com https://example.com/private?token=foo opaque=abcdefghijklmnopqrstuv\n", request: reasonRequest, secrets: ["fixture-signing-key"])
        check(reason.contains("签名校验失败") && !reason.contains("fixture-") && !reason.contains("123456") && !reason.contains("7654321")
              && !reason.contains("user@example.com") && !reason.contains("https://") && !reason.contains("abcdefghijklmnopqrstuv"),
              "Visible service reasons preserve the explanation while removing session material, IDs and addresses")
        check(HypergryphAccountAPI.redactedServiceMessage(["message": "nested body must not be exposed"], request: reasonRequest, secrets: []) == "No service reason was supplied.",
              "Diagnostic display never serializes response dictionaries or nested account payloads")
        check(HypergryphAccountAPI.redactedServiceMessage(String(repeating: "x", count: 4_097), request: reasonRequest, secrets: []).hasPrefix("The service reason was too long"),
              "Oversized provider messages are rejected instead of partially exposing data")
        let plainReason = HypergryphAccountAPI.redactedServiceMessage("<b>失败</b>\u{202e}\n请稍后重试", request: reasonRequest, secrets: [])
        check(!plainReason.contains("<b>") && !plainReason.contains("\u{202e}") && !plainReason.contains("\n"),
              "Diagnostic UI receives plain text without markup, bidi or multiline controls")
        check(HypergryphAPIDiagnostic.Reason.classify("签名校验失败") == .signature
              && HypergryphAPIDiagnostic.Reason.classify("当前用户未经授权") == .permission,
              "Signature and authorization errors no longer collapse into the other category")

        let configuration = URLSessionConfiguration.ephemeral; configuration.protocolClasses = [HypergryphFixtureProtocol.self]
        let api = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        defer { HypergryphFixtureProtocol.reset() }
        func awaitResult<T>(_ start: (@escaping (Result<T, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest?) -> Result<T, HypergryphAPIError> {
            var result: Result<T, HypergryphAPIError>?
            let handle = start { value in check(Thread.isMainThread, "All API completions use the main queue"); result = value }
            let deadline = Date().addingTimeInterval(4)
            while result == nil && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
            withExtendedLifetime(handle) {}
            guard let result = result else { fatalError("Fixture API timed out") }; return result
        }
        for region in HypergryphAccountRegion.allCases {
            HypergryphFixtureProtocol.install { request in
                switch request.url!.path {
                case "/web/v1/auth/refresh": return .json(["code": 0, "timestamp": 1_800_000_000, "data": ["token": "test-signing-token"]])
                case "/api/v1/game/player/binding": return .json(["code": 0, "data": bindingData])
                case "/web/v1/user": return .json(["code": 0, "data": ["user": ["id": "community-9"]]])
                case "/web/v1/game/endfield/card/detail": return .json(["code": 0, "data": card])
                default: return .json(["code": 999, "data": [:]])
                }
            }
            let refreshed = try! awaitResult { api.refreshCredentials(cred: credentials.cred, region: region, completion: $0) }.get()
            let roles = try! awaitResult { api.bindings(credentials: refreshed, region: region, completion: $0) }.get()
            let profile = try! awaitResult { api.profile(role: roles[1], credentials: refreshed, completion: $0) }.get()
            check(refreshed == credentials && profile.role.region == region && profile.stamina?.current == 245, "Both regions complete scoped refresh → bindings → selected profile")
            let requests = HypergryphFixtureProtocol.requests
            check(requests.count == 4 && requests.allSatisfy { $0.url?.host == region.apiHost && $0.httpMethod == "GET" }, "Only four read-only calls go to the selected official host")
            check(requests.allSatisfy { $0.value(forHTTPHeaderField: "Cookie") == nil && $0.value(forHTTPHeaderField: "Authorization") == nil }, "No browser/passport cookies or bearer token are sent")
            check(requests.allSatisfy { $0.value(forHTTPHeaderField: "sk-language") == (region == .global ? "en" : nil) },
                  "Mainland omits the unsupported passport locale; global retains its supported game-data language")
            check(requests[0].value(forHTTPHeaderField: "sign") == nil && requests[1].value(forHTTPHeaderField: "sign") == "9bc850f4a4cd807579a3fa7b31a004d0", "Legacy cred-only adapter remains compatible; subsequent requests use refreshed signing token")
            check(requests[3].url?.query == "roleId=123456&serverId=1&userId=community-9"
                  && requests[3].value(forHTTPHeaderField: "sk-game-role") == nil
                  && requests[3].value(forHTTPHeaderField: "sign") == "8dd17612820eeb685c17dd435035602a", "The card client signs the selected role/community query without an unrelated shared-client header")
        }
        for region in HypergryphAccountRegion.allCases {
            let sessionAPI: HypergryphAccountServing = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
            HypergryphFixtureProtocol.install { request in
                guard request.value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID else { return .json(["code": 10001]) }
                switch request.url!.path {
                case "/web/v1/auth/refresh": return .json(["code": 0, "data": ["token": "context-refreshed-token"]])
                case "/api/v1/game/player/binding": return .json(["code": 0, "data": bindingData])
                case "/web/v1/user": return .json(["code": 0, "data": ["user": ["id": "community-9"]]])
                default: return .json(["code": 0, "data": card])
                }
            }
            let roles = try! awaitResult { sessionAPI.bindings(credentials: sessionCredentials, region: region, completion: $0) }.get()
            let refreshed = try! awaitResult { sessionAPI.refreshCredentials(credentials: sessionCredentials, region: region, completion: $0) }.get()
            _ = try! awaitResult { sessionAPI.profile(role: roles[1], credentials: refreshed, completion: $0) }.get()
            let requests = HypergryphFixtureProtocol.requests
            check(requests.map { $0.url!.path } == ["/api/v1/game/player/binding", "/web/v1/user", "/web/v1/auth/refresh", "/web/v1/game/endfield/card/detail"],
                  "Imported official session begins with bindings without an unnecessary unsigned token bootstrap")
            check(requests.allSatisfy { $0.url?.host == region.apiHost && $0.httpMethod == "GET" && $0.value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID },
                  "Both regions retain exact issued device header on bindings, community, refresh and card requests")
            check(requests[0].value(forHTTPHeaderField: "sign") == "9aff9f3d1ebeefcd9caff4df9f1d01a4"
                  && requests[2].value(forHTTPHeaderField: "sign") == "adabffa91801a9f2ca3505a114a4a0cd",
                  "Protocol dispatch uses context-aware signed refresh with the original device bytes")
            check(refreshed.deviceID == sessionCredentials.deviceID && refreshed.signingToken == "context-refreshed-token",
                  "A rotated signing token preserves the stored community device context")
        }
        let globalSelf = HypergryphAccountAPI(configuration: configuration, now: { date })
        let globalContext = HypergryphCredentials(cred: "global-issued-cred", signingToken: "global-issued-signing", deviceID: "global-issued-device")
        let globalBindings: [String: Any] = ["list": [["appCode": "endfield", "bindingList": [["uid": "global-binding",
            "defaultRole": ["roleId": "9007199254740995", "serverId": "3"],
            "roles": [["roleId": "9007199254740995", "serverId": "3", "nickname": "Global fixture"],
                      ["roleId": "9007199254740995", "serverId": "2", "isBanned": true]]]]]]]
        HypergryphFixtureProtocol.install { request in
            guard request.url?.host == "zonai.skport.com", request.value(forHTTPHeaderField: "cred") == globalContext.cred,
                  request.value(forHTTPHeaderField: "dId") == globalContext.deviceID else { return .json(["code": 10001]) }
            if request.url?.path == "/api/v1/game/player/binding" { return .json(["code": 0, "data": globalBindings]) }
            return .json(["code": 0, "data": ["detail": ["base": ["roleId": "9007199254740995", "level": 60],
                "currentTs": "1800000120", "dungeon": ["curStamina": "42", "maxStamina": "360", "maxTs": "1800137113"]]]])
        }
        let skportRoles = try! awaitResult { globalSelf.bindings(credentials: globalContext, region: .global, completion: $0) }.get()
        let skportSnapshot = try! awaitResult { globalSelf.profile(role: skportRoles[0], credentials: globalContext, completion: $0) }.get()
        let skportRequests = HypergryphFixtureProtocol.requests
        check(skportRoles.count == 2 && skportRoles[0].serverID == "3" && !skportRoles[1].isAvailable,
              "Global bindings retain actual non-default server IDs and disabled roles")
        check(skportRequests.count == 2 && skportRequests[1].url?.query == "roleId=9007199254740995&serverId=3",
              "Fresh SKPORT context reaches its selected self-profile without userId or a Mainland server assumption")
        check(skportRequests.allSatisfy { $0.value(forHTTPHeaderField: "Origin") == "https://game.skport.com"
            && $0.value(forHTTPHeaderField: "Referer") == "https://game.skport.com/"
            && $0.value(forHTTPHeaderField: "sk-language") == "en" && $0.value(forHTTPHeaderField: "Cookie") == nil },
              "Global reads use their own game origin/language and never browser cookies")
        check(skportSnapshot.stamina?.serverObservedAt == date.addingTimeInterval(120)
              && skportSnapshot.sanityPresentation(at: date)?.nextRecoveryAt == date.addingTimeInterval(49),
              "SKPORT response time anchors recovery despite a skewed local clock")
        check(skportRequests[1].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(
            path: "/api/v1/game/endfield/card/detail", query: "roleId=9007199254740995&serverId=3", timestamp: "1800000000",
            signingToken: globalContext.signingToken, deviceID: globalContext.deviceID),
              "Complete SKPORT context signs its actual server and exact long role ID")
        let refreshClockAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        HypergryphFixtureProtocol.install { request in
            if request.value(forHTTPHeaderField: "timestamp") == "1800000120" {
                return .json(["code": 0, "data": ["token": "clock-refreshed-token"]])
            }
            return .json(["code": 10003, "timestamp": "1800000120"], status: 401)
        }
        let clockRefreshed = try! awaitResult { refreshClockAPI.refreshCredentials(credentials: sessionCredentials, region: .mainland, completion: $0) }.get()
        check(clockRefreshed.deviceID == sessionCredentials.deviceID && HypergryphFixtureProtocol.requests.count == 2,
              "A context refresh corrects provider clock skew once without dropping the issued device")
        check(HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(
              path: "/web/v1/auth/refresh", query: "", timestamp: "1800000120", signingToken: sessionCredentials.signingToken, deviceID: sessionCredentials.deviceID),
              "Clock retry re-signs the refresh with corrected time and the same community context")
        for (timestamp, expectedCount) in [("1800000120", 2), ("", 1), ("invalid", 1)] {
            let failedRefreshAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
            HypergryphFixtureProtocol.install { _ in
                .json(timestamp.isEmpty ? ["code": 10003] : ["code": 10003, "timestamp": timestamp], status: 401)
            }
            check(error(awaitResult { failedRefreshAPI.refreshCredentials(credentials: sessionCredentials, region: .global, completion: $0) }) == .service(code: 10003),
                  "Unresolved refresh clock errors preserve the sanitized provider code")
            check(HypergryphFixtureProtocol.requests.count == expectedCount,
                  "Repeated clock errors stop after one retry; missing or invalid clocks never retry")
        }
        HypergryphFixtureProtocol.install { request in
            request.value(forHTTPHeaderField: "timestamp") == "1800000120"
                ? .json(["code": 0, "data": ["token": "late-refresh-token"]], delay: 0.2)
                : .json(["code": 10003, "timestamp": "1800000120"], status: 401)
        }
        let cancelledClockAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        var cancelledClockResults: [HypergryphAPIError] = []
        let cancelledClock = cancelledClockAPI.refreshCredentials(credentials: sessionCredentials, region: .mainland) {
            if let failure = error($0) { cancelledClockResults.append(failure) }
        }
        let clockDeadline = Date().addingTimeInterval(2)
        while HypergryphFixtureProtocol.requests.count < 2 && Date() < clockDeadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
        check(HypergryphFixtureProtocol.requests.count == 2, "Refresh cancellation fixture reached its bounded clock retry")
        cancelledClock?.cancel(); cancelledClock?.cancel()
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        check(cancelledClockResults == [.cancelled] && HypergryphFixtureProtocol.requests.count == 2,
              "Cancelling a refresh clock retry completes once and suppresses its late successful result")
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": ["list": [["appCode": "arknights", "bindingList": [["uid": "9001"]]]]]]) }
        _ = try! awaitResult { api.bindings(credentials: credentials, region: .mainland, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests.count == 1, "Arknights-only binding does not request unnecessary community profile")
        let arkChannelAPI = HypergryphAccountAPI(configuration: configuration, now: { date })
        let arkChannels: [String: Any] = ["list": [["appCode": "arknights", "bindingList": [
            ["uid": "9001", "channelMasterId": "1", "channelName": "Official", "isDefault": true],
            ["uid": "9001", "channelMasterId": 2, "channelName": "Bilibili"]]]]]
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": arkChannels]) }
        let channelRoles = try! awaitResult { arkChannelAPI.bindings(credentials: sessionCredentials, region: .mainland, completion: $0) }.get()
        check(channelRoles.map { $0.serverID } == ["1", "2"] && channelRoles[0].id != channelRoles[1].id,
              "CN official and Bilibili bindings preserve their returned channels even when fixture UIDs match")
        check(HypergryphFixtureProtocol.requests.map { $0.url!.path } == ["/api/v1/game/player/binding"],
              "The production default loads Arknights channels without any community-user lookup")
        let arkExpectedSigns = ["1": "896f8b6fa1e9c0538d22e3205a6eafff", "2": "f065d63f685115b9601f17137ee943b7"]
        for channelRole in channelRoles {
            let channel = channelRole.serverID!, expectedAP = channel == "1" ? 80 : 12, expectedMax = channel == "1" ? 135 : 130
            let expectedFullTime = 1_800_000_000 + (expectedMax - expectedAP) * 360
            HypergryphFixtureProtocol.install { request in
                guard request.url?.path == "/api/v1/game/player/info",
                      request.url?.query == "uid=9001&channelMasterId=\(channel)" else { return .json(["code": 10001]) }
                return .json(["code": 0, "data": ["currentTs": "1800000000", "status": [
                    "uid": "9001", "level": 120, "ap": ["current": expectedAP, "max": expectedMax,
                    "completeRecoveryTime": String(expectedFullTime)]]]])
            }
            let channelSnapshot = try! awaitResult { arkChannelAPI.profile(role: channelRole, credentials: sessionCredentials, completion: $0) }.get()
            let channelRequests = HypergryphFixtureProtocol.requests
            check(channelRequests.count == 1 && channelRequests[0].url?.host == HypergryphAccountRegion.mainland.apiHost &&
                  channelRequests[0].url?.query == "uid=9001&channelMasterId=\(channel)",
                  "Arknights sends the selected binding's actual channel with its UID, without a fallback or rebind")
            check(channelRequests[0].value(forHTTPHeaderField: "sign") == arkExpectedSigns[channel] &&
                  channelRequests[0].value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID,
                  "Official/Bilibili query bytes and issued device context match independent signature vectors")
            check(channelSnapshot.role == channelRole && channelSnapshot.stamina?.current == expectedAP &&
                  channelSnapshot.stamina?.maximum == expectedMax && channelSnapshot.stamina?.fullRecoveryAt == Date(timeIntervalSince1970: TimeInterval(expectedFullTime)),
                  "Each selected Arknights channel maps status.ap.current/max/recovery independently")
        }
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": ["status": ["uid": "9001", "ap": ["current": 0, "max": 135]]]]) }
        let legacyArkSnapshot = try! awaitResult { arkChannelAPI.profile(role: ark, credentials: sessionCredentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests[0].url?.query == "uid=9001" &&
              HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "sign") == "5e33e14a464dac088954786a2001706b" &&
              legacyArkSnapshot.stamina?.current == 0,
              "Legacy Arknights roles without a channel retain the UID-only request and explicit zero AP")
        var invalidChannel = channelRoles[0]; invalidChannel.serverID = "1&uid=other"
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": [:]]) }
        check(error(awaitResult { arkChannelAPI.profile(role: invalidChannel, credentials: sessionCredentials, completion: $0) }) == .invalidRole &&
              HypergryphFixtureProtocol.requests.isEmpty,
              "A malformed stored Arknights channel is rejected before constructing a request")
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": ["status": ["uid": "another-doctor", "ap": ["current": 1, "max": 135]]]]) }
        check(error(awaitResult { arkChannelAPI.profile(role: channelRoles[0], credentials: sessionCredentials, completion: $0) }) == .invalidResponse,
              "Channel-aware Arknights profiles still reject a response belonging to another UID")
        HypergryphFixtureProtocol.install { _ in .json(["code": 10001, "message": "Untrusted server text with secrets", "data": [:]]) }
        var diagnostics: [HypergryphAPIDiagnostic] = []
        api.onDiagnostic = { diagnostics.append($0) }
        var visibleReasons: [String] = []
        api.onServiceMessage = { _, _, message in visibleReasons.append(message) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .service(code: 10001),
              "Business or identity restrictions are not mislabeled as an expired community session")
        check(diagnostics == [HypergryphAPIDiagnostic(endpoint: .endfieldProfile, code: 10001, reason: .other)],
              "Diagnostics preserve the failed endpoint and numeric code without passing untrusted response text")
        check(visibleReasons == ["Untrusted server text with secrets"], "Explicit diagnostic UI can inspect a bounded service explanation")
        check(HypergryphFixtureProtocol.requests.count == 1
              && HypergryphFixtureProtocol.requests[0].url?.path == "/web/v1/game/endfield/card/detail",
              "A business failure never silently switches routes or retries the normal HUD request")
        for (message, reason) in [("设备信息无效", HypergryphAPIDiagnostic.Reason.device), ("Invalid parameter", .parameters),
                                  ("角色不存在", .role), ("没有权限", .permission), ("请先完成实名认证", .identity),
                                  ("请先登录", .authentication), ("arbitrary private account data", .permission)] {
            HypergryphFixtureProtocol.install { _ in .json(["code": 10001, "message": message]) }
            _ = awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }
            check(diagnostics.last?.reason == reason && diagnostics.last?.code == 10001,
                  "Provider text is reduced to a fixed diagnostic category while preserving the service error")
        }
        check(HypergryphAPIDiagnostic.Reason.classify(String(repeating: "设备", count: 600)) == .other
              && HypergryphAPIDiagnostic.Reason.classify(["message": "设备"]) == .other,
              "Diagnostic classification rejects oversized or non-string messages")
        let diagnosticCount = diagnostics.count
        let visibleReasonCount = visibleReasons.count
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
        _ = awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }
        check(diagnostics.count == diagnosticCount, "Successful profile contents never enter diagnostics")
        check(visibleReasons.count == visibleReasonCount, "Successful profile data never enters the service-reason callback")
        api.onDiagnostic = nil
        api.onServiceMessage = nil
        for region in HypergryphAccountRegion.allCases {
            let appRouteAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .app)
            let appRole = HypergryphRole(region: region, game: .endfield, bindingUID: "account-7", roleID: "123456",
                                        serverID: "1", communityUserID: "community-9")
            HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
            _ = try! awaitResult { appRouteAPI.profile(role: appRole, credentials: sessionCredentials, completion: $0) }.get()
            let appRequest = HypergryphFixtureProtocol.requests[0]
            check(appRequest.url?.host == region.apiHost && appRequest.url?.path == "/api/v1/game/endfield/card/detail"
                  && appRequest.url?.query == "roleId=123456&serverId=1&userId=community-9"
                  && appRequest.value(forHTTPHeaderField: "sk-game-role") == nil,
                  "Both card routes keep the official regional query identity without the shared game-client header")
            check(appRequest.value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(
                    path: "/api/v1/game/endfield/card/detail", query: "roleId=123456&serverId=1&userId=community-9",
                    timestamp: "1800000000", signingToken: sessionCredentials.signingToken, deviceID: sessionCredentials.deviceID),
                  "Alternate official route is signed with its own path and the same issued session context")
        }
        for region in HypergryphAccountRegion.allCases {
            let selfAPI = HypergryphAccountAPI(configuration: configuration, now: { date })
            HypergryphFixtureProtocol.install { request in
                request.url?.path == "/api/v1/game/player/binding"
                    ? .json(["code": 0, "data": bindingData]) : .json(["code": 10001])
            }
            let selfRoles = try! awaitResult { selfAPI.bindings(credentials: sessionCredentials, region: region, completion: $0) }.get()
            check(HypergryphFixtureProtocol.requests.map { $0.url!.path } == ["/api/v1/game/player/binding"],
                  "Default authenticated-self bindings skip the unnecessary community-user lookup in either region")
            check(selfRoles.count == parsed.count && selfRoles.allSatisfy { $0.region == region && $0.communityUserID == nil },
                  "Authenticated-self preserves the scoped binding roles without fabricating a community identity")
            let selfRole = selfRoles.first { $0.game == .endfield }!
            HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
            let selfSnapshot = try! awaitResult { selfAPI.profile(role: selfRole, credentials: sessionCredentials, completion: $0) }.get()
            check(selfSnapshot.role == selfRole && selfSnapshot.stamina?.current == 245,
                  "Default self-profile succeeds without community ID and preserves the selected role")
            let selfRequest = HypergryphFixtureProtocol.requests[0]
            check(HypergryphFixtureProtocol.requests.count == 1 && selfRequest.url?.host == region.apiHost &&
                  selfRequest.url?.path == "/api/v1/game/endfield/card/detail" &&
                  selfRequest.url?.query == "roleId=123456&serverId=1",
                  "Authenticated-self sends exactly the two published role/server parameters to the selected regional host")
            check(URLComponents(url: selfRequest.url!, resolvingAgainstBaseURL: false)?.queryItems?.count == 2 &&
                  selfRequest.value(forHTTPHeaderField: "sk-game-role") == nil,
                  "Authenticated-self sends neither userId nor the shared game-client role header")
            check(selfRequest.value(forHTTPHeaderField: "sign") == "4f7266212f17bf56d0ae6a7a2120da3f" &&
                  selfRequest.value(forHTTPHeaderField: "cred") == sessionCredentials.cred &&
                  selfRequest.value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID,
                  "The exact two-parameter self query matches the independent HMAC/MD5 signing vector with issued device context")
            var mismatched = card
            mismatched["detail"] = ["base": ["roleId": "another-role"]]
            let wrongCard = mismatched
            HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": wrongCard]) }
            check(error(awaitResult { selfAPI.profile(role: selfRole, credentials: sessionCredentials, completion: $0) }) == .invalidResponse,
                  "Self-profile still rejects another role's returned profile identity")
            HypergryphFixtureProtocol.install { _ in .json(["code": 10001, "message": "Operation failed"]) }
            check(error(awaitResult { selfAPI.profile(role: selfRole, credentials: sessionCredentials, completion: $0) }) == .service(code: 10001),
                  "Self-profile preserves the provider business error")
            check(HypergryphFixtureProtocol.requests.count == 1 &&
                  HypergryphFixtureProtocol.requests[0].url?.query == "roleId=123456&serverId=1",
                  "Self-profile never retries or falls back to a different contract after code 10001")
            var missingServer = selfRole; missingServer.serverID = nil
            HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
            check(error(awaitResult { selfAPI.profile(role: missingServer, credentials: sessionCredentials, completion: $0) }) == .invalidRole &&
                  HypergryphFixtureProtocol.requests.isEmpty,
                  "Self-profile still requires the selected Endfield server before sending any request")
            for route in [HypergryphAccountAPI.EndfieldCardRoute.web, .app] {
                let normalAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: route)
                check(error(awaitResult { normalAPI.profile(role: selfRole, credentials: sessionCredentials, completion: $0) }) == .invalidRole &&
                      HypergryphFixtureProtocol.requests.isEmpty,
                      "Explicit web/app diagnostics retain their original community-ID validation")
            }
        }
        HypergryphFixtureProtocol.install { _ in .json(["code": 10002, "message": "Untrusted server text with secrets", "data": [:]], status: 401) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .authenticationExpired,
              "The actual expired credential code remains a closed error, never raw server text")
        check(HypergryphFixtureProtocol.requests.count == 1, "Expired community credentials never trigger a signing retry")

        let repairedAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        let correctedTime = "1800000120", newToken = "fixture-rotated-signing-token"
        let cardPath = "/web/v1/game/endfield/card/detail", cardQuery = "roleId=123456&serverId=1&userId=community-9"
        let repairedSign = HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: correctedTime, signingToken: newToken)
        HypergryphFixtureProtocol.install { request in
            if request.url?.path == "/web/v1/auth/refresh" {
                return .json(["code": 0, "timestamp": 1_800_000_120, "data": ["token": newToken]])
            }
            if request.value(forHTTPHeaderField: "sign") == repairedSign { return .json(["code": 0, "data": card]) }
            return .json(["code": 10000, "timestamp": 1_800_000_120], status: 401)
        }
        let recoveredProfile = try! awaitResult { repairedAPI.profile(role: role, credentials: credentials, completion: $0) }.get()
        check(recoveredProfile.stamina?.current == 245, "A 401 signing-token expiry recovers without asking the user to sign in again")
        let repairedRequests = HypergryphFixtureProtocol.requests
        check(repairedRequests.map { $0.url!.path } == [cardPath, "/web/v1/auth/refresh", cardPath], "Stale signing token permits one refresh and one retry only")
        check(repairedRequests[1].value(forHTTPHeaderField: "cred") == credentials.cred && repairedRequests[1].value(forHTTPHeaderField: "sign") ==
              HypergryphAccountAPI.signature(path: "/web/v1/auth/refresh", query: "", timestamp: correctedTime, signingToken: ""),
              "Recovery refresh clears the stale signing key while keeping the official signed request format")
        check(repairedRequests[2].value(forHTTPHeaderField: "timestamp") == correctedTime
              && repairedRequests[2].value(forHTTPHeaderField: "sign") == repairedSign, "Retried request applies fresh token and the provider's clock")
        let priorRequestCount = HypergryphFixtureProtocol.requests.count
        _ = try! awaitResult { repairedAPI.profile(role: role, credentials: credentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests.count == priorRequestCount + 1, "The same controller credential reuses its recovered token without an extra refresh each sync")

        let deviceRecoveryAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        let recoveredDeviceSign = HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: "1800000000",
            signingToken: newToken, deviceID: sessionCredentials.deviceID)
        HypergryphFixtureProtocol.install { request in
            guard request.value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID else { return .json(["code": 10001]) }
            if request.url?.path == "/web/v1/auth/refresh" { return .json(["code": 0, "data": ["token": newToken]]) }
            return request.value(forHTTPHeaderField: "sign") == recoveredDeviceSign ? .json(["code": 0, "data": card]) : .json(["code": 10000], status: 401)
        }
        _ = try! awaitResult { deviceRecoveryAPI.profile(role: role, credentials: sessionCredentials, completion: $0) }.get()
        _ = try! awaitResult { deviceRecoveryAPI.profile(role: role, credentials: sessionCredentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests.count == 4 && HypergryphFixtureProtocol.requests.allSatisfy { $0.value(forHTTPHeaderField: "dId") == sessionCredentials.deviceID },
              "Device context survives signing recovery, retry and transient recovered-token reuse")
        check(HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "sign") == "9efee052bd2cb5c0f97ff4ee1ca42d4e",
              "Signing expiry refresh uses the official empty-key signature with the original issued device ID")
        let otherDevice = HypergryphCredentials(cred: sessionCredentials.cred, signingToken: sessionCredentials.signingToken, deviceID: "second-issued-device")
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
        _ = try! awaitResult { deviceRecoveryAPI.profile(role: role, credentials: otherDevice, completion: $0) }.get()
        _ = try! awaitResult { deviceRecoveryAPI.profile(role: role, credentials: sessionCredentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(
              path: cardPath, query: cardQuery, timestamp: "1800000000", signingToken: otherDevice.signingToken, deviceID: otherDevice.deviceID),
              "Same scoped cred and supplied token on another device cannot reuse a recovered signing token")
        check(HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(
              path: cardPath, query: cardQuery, timestamp: "1800000000", signingToken: sessionCredentials.signingToken, deviceID: sessionCredentials.deviceID),
              "Switching device context discards the previous context's transient token instead of accumulating session copies")

        let bindingRecoveryAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        HypergryphFixtureProtocol.install { request in
            if request.url?.path == "/web/v1/auth/refresh" { return .json(["code": 0, "data": ["token": newToken]]) }
            let expected = HypergryphAccountAPI.signature(path: request.url!.path, query: request.url!.query ?? "",
                timestamp: "1800000000", signingToken: newToken)
            guard request.value(forHTTPHeaderField: "sign") == expected else { return .json(["code": 10000], status: 401) }
            if request.url?.path == "/api/v1/game/player/binding" { return .json(["code": 0, "data": bindingData]) }
            if request.url?.path == "/web/v1/user" { return .json(["code": 0, "data": ["user": ["id": "community-9"]]]) }
            return .json(["code": 0, "data": card])
        }
        let recoveredRoles = try! awaitResult { bindingRecoveryAPI.bindings(credentials: credentials, region: .mainland, completion: $0) }.get()
        _ = try! awaitResult { bindingRecoveryAPI.profile(role: recoveredRoles[1], credentials: credentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests.map { $0.url!.path } == ["/api/v1/game/player/binding", "/web/v1/auth/refresh",
            "/api/v1/game/player/binding", "/web/v1/user", cardPath], "Login bindings recover once; community and game data reuse the same recovered signing token")
        check(recoveredRoles[1].communityUserID == "community-9", "Recovery preserves the complete role-selection and community identity chain")

        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
        let otherIdentity = HypergryphCredentials(cred: "second-account", signingToken: "second-token")
        _ = try! awaitResult { repairedAPI.profile(role: role, credentials: otherIdentity, completion: $0) }.get()
        _ = try! awaitResult { repairedAPI.profile(role: role, credentials: credentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "cred") == "second-account"
              && HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: correctedTime, signingToken: "second-token"),
              "Selecting a different identity cannot reuse the prior account's recovered signing token")
        check(HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: correctedTime, signingToken: credentials.signingToken),
              "Each region retains only its current identity, not an accumulating credential cache")

        // Deliberately share the supplied signing token between distinct scoped
        // credentials: cache identity must be based on the credential digest,
        // not merely on the signing-token string.
        let sharedTokenOtherIdentity = HypergryphCredentials(cred: "another-scoped-identity", signingToken: credentials.signingToken)
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
        _ = try! awaitResult { bindingRecoveryAPI.profile(role: role, credentials: sharedTokenOtherIdentity, completion: $0) }.get()
        _ = try! awaitResult { bindingRecoveryAPI.profile(role: role, credentials: credentials, completion: $0) }.get()
        let originalSign = HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: "1800000000", signingToken: credentials.signingToken)
        check(HypergryphFixtureProtocol.requests.count == 2 && HypergryphFixtureProtocol.requests.allSatisfy { $0.value(forHTTPHeaderField: "sign") == originalSign },
              "Credential digests distinguish accounts sharing a supplied token and discard the replaced account's recovered key")
        check(HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "cred") == sharedTokenOtherIdentity.cred
              && HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "cred") == credentials.cred,
              "Digest-based transient lookup never substitutes the credential sent to the official server")

        let clockAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
        var globalRole = role; globalRole = HypergryphRole(region: .global, game: globalRole.game,
            bindingUID: globalRole.bindingUID, roleID: globalRole.roleID, serverID: globalRole.serverID, communityUserID: globalRole.communityUserID)
        HypergryphFixtureProtocol.install { request in
            if request.value(forHTTPHeaderField: "timestamp") == correctedTime { return .json(["code": 0, "data": card]) }
            return .json(["code": "10003", "timestamp": "1800000120"], status: 401)
        }
        _ = try! awaitResult { clockAPI.profile(role: globalRole, credentials: credentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests.count == 2 && HypergryphFixtureProtocol.requests.allSatisfy { $0.url?.host == HypergryphAccountRegion.global.apiHost && $0.url?.path == cardPath },
              "Global clock skew retries the same read once without fetching a new token")
        check(HypergryphFixtureProtocol.requests[1].value(forHTTPHeaderField: "sign") == HypergryphAccountAPI.signature(path: cardPath, query: cardQuery, timestamp: correctedTime, signingToken: credentials.signingToken),
              "Clock recovery re-signs with the existing token and corrected server timestamp")
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card]) }
        _ = try! awaitResult { clockAPI.profile(role: role, credentials: credentials, completion: $0) }.get()
        check(HypergryphFixtureProtocol.requests[0].value(forHTTPHeaderField: "timestamp") == "1800000000", "Clock correction is isolated by server region")

        for repeatedCode in [10000, 10003] {
            let retryAPI = HypergryphAccountAPI(configuration: configuration, now: { date }, endfieldCardRoute: .web)
            HypergryphFixtureProtocol.install { request in
                if request.url?.path == "/web/v1/auth/refresh" { return .json(["code": 0, "data": ["token": newToken]]) }
                return .json(["code": repeatedCode, "timestamp": 1_800_000_120], status: 401)
            }
            check(error(awaitResult { retryAPI.profile(role: role, credentials: credentials, completion: $0) }) == .service(code: repeatedCode),
                  "An unrepaired signing or clock failure remains recoverable and cannot invalidate the community session")
            check(HypergryphFixtureProtocol.requests.count == (repeatedCode == 10000 ? 3 : 2), "Repeated signing failures stop after exactly one retry")
        }
        HypergryphFixtureProtocol.install { _ in
            .json(["code": HypergryphFixtureProtocol.requests.count == 1 ? 10003 : 10000, "timestamp": 1_800_000_120], status: 401)
        }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .service(code: 10000)
              && HypergryphFixtureProtocol.requests.count == 2, "A different signing failure on the retry cannot reset the recovery budget")
        HypergryphFixtureProtocol.install { _ in .json(["code": 10003], status: 401) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .service(code: 10003)
              && HypergryphFixtureProtocol.requests.count == 1, "Clock failures without a valid timestamp do not fabricate a correction or retry")
        HypergryphFixtureProtocol.install { request in
            .json(["code": request.url?.path == "/web/v1/auth/refresh" ? 10002 : 10000], status: 401)
        }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .authenticationExpired
              && HypergryphFixtureProtocol.requests.count == 2, "A genuinely expired credential discovered during recovery is reported without retrying the profile")
        HypergryphFixtureProtocol.install { _ in .json(["code": 10001, "message": "private provider details"], status: 403) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .service(code: 10001), "A bounded 403 business response preserves its sanitized error code")
        for status in [401, 403] {
            HypergryphFixtureProtocol.install { _ in .response(status: status, body: Data("not JSON".utf8), length: nil, delay: 0) }
            check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .http(status: status), "An unstructured auth HTTP failure does not invent an expired credential")
        }
        HypergryphFixtureProtocol.install { _ in .json(["code": 0, "data": card], status: 401) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .http(status: 401), "An HTTP auth failure cannot masquerade as successful game data")

        HypergryphFixtureProtocol.install { request in
            if request.url?.path == "/web/v1/auth/refresh" {
                return .json(["code": 0, "data": ["token": newToken]], delay: 0.2)
            }
            return .json(["code": 10000], status: 401)
        }
        var recoveryCancellation: [HypergryphAPIError] = []
        let recovering = api.profile(role: role, credentials: credentials) { if let value = error($0) { recoveryCancellation.append(value) } }
        let recoveryDeadline = Date().addingTimeInterval(2)
        while HypergryphFixtureProtocol.requests.count < 2 && Date() < recoveryDeadline { RunLoop.main.run(until: Date().addingTimeInterval(0.005)) }
        check(HypergryphFixtureProtocol.requests.count == 2, "Cancellation fixture reached the signing refresh")
        recovering?.cancel(); recovering?.cancel()
        RunLoop.main.run(until: Date().addingTimeInterval(0.3))
        check(recoveryCancellation == [.cancelled] && HypergryphFixtureProtocol.requests.count == 2,
              "Cancelling during recovery completes once and prevents a late profile retry")
        HypergryphFixtureProtocol.install { _ in .response(status: 503, body: Data(), length: nil, delay: 0) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .http(status: 503), "HTTP service failure remains recoverable")
        HypergryphFixtureProtocol.install { _ in .response(status: 200, body: Data("[]".utf8), length: nil, delay: 0) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .invalidResponse, "A malformed successful payload cannot clear real data")
        HypergryphFixtureProtocol.install { _ in .response(status: 200, body: Data(), length: HypergryphAccountAPI.maximumResponseBytes + 1, delay: 0) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .responseTooLarge, "Advertised oversized responses stop before buffering")
        HypergryphFixtureProtocol.install { _ in .response(status: 200, body: Data(repeating: 32, count: HypergryphAccountAPI.maximumResponseBytes + 1), length: nil, delay: 0) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .responseTooLarge, "Streaming size is bounded even without Content-Length")
        HypergryphFixtureProtocol.install { _ in .redirect(URL(string: "https://untrusted.invalid/fixture")!) }
        check(error(awaitResult { api.profile(role: role, credentials: credentials, completion: $0) }) == .unsafeRedirect, "Redirects cannot forward scoped credentials to another endpoint")
        check(HypergryphFixtureProtocol.requests.count == 1, "Rejected redirect does not issue a follow-up network request")
        HypergryphFixtureProtocol.install { _ in .response(status: 200, body: Data("{\"code\":0,\"data\":{\"list\":[]}}".utf8), length: nil, delay: 0.1) }
        var cancelledResults: [HypergryphAPIError] = []
        let cancellation = api.bindings(credentials: credentials, region: .mainland) { if let value = error($0) { cancelledResults.append(value) } }
        cancellation?.cancel(); cancellation?.cancel()
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
        check(cancelledResults == [.cancelled], "Cancelling a multi-request chain reports once and suppresses late completion")
        HypergryphFixtureProtocol.install { _ in .json(["code": 10001, "message": "设备信息无效"], delay: 0.1) }
        diagnostics.removeAll(); api.onDiagnostic = { diagnostics.append($0) }
        var cancelledDiagnosticResults: [HypergryphAPIError] = []
        let diagnosticRequest = api.profile(role: role, credentials: credentials) {
            if let value = error($0) { cancelledDiagnosticResults.append(value) }
        }
        diagnosticRequest?.cancel()
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
        check(cancelledDiagnosticResults == [.cancelled] && diagnostics.isEmpty,
              "Cancelling before a delayed provider response suppresses its diagnostic and extra completion")
        api.onDiagnostic = nil
        let before = HypergryphFixtureProtocol.requests.count
        check(error(awaitResult { api.refreshCredentials(cred: "bad\r\ncredential", region: .mainland, completion: $0) }) == .invalidCredentials,
              "Control characters cannot enter HTTP credential headers")
        for badDevice in ["", "device\r\nInjected:header", String(repeating: "x", count: 4_097)] {
            let invalid = HypergryphCredentials(cred: credentials.cred, signingToken: credentials.signingToken, deviceID: badDevice)
            check(error(awaitResult { api.refreshCredentials(credentials: invalid, region: .mainland, completion: $0) }) == .invalidCredentials,
                  "Invalid issued device context is rejected before signed refresh")
            check(error(awaitResult { api.profile(role: role, credentials: invalid, completion: $0) }) == .invalidCredentials,
                  "Invalid device context cannot enter profile headers or signing bytes")
        }
        var invalidRole = role; invalidRole.communityUserID = nil
        check(error(awaitResult { api.profile(role: invalidRole, credentials: credentials, completion: $0) }) == .invalidRole,
              "Missing Endfield community identity does not issue an ambiguous card request")
        check(HypergryphFixtureProtocol.requests.count == before, "Invalid input is rejected without any network activity")
        return count
    }
    private static func error<T>(_ result: Result<T, HypergryphAPIError>) -> HypergryphAPIError? {
        if case .failure(let value) = result { return value }; return nil
    }
}

private final class HypergryphFixtureProtocol: URLProtocol {
    enum Reply {
        case response(status: Int, body: Data, length: Int?, delay: TimeInterval)
        case redirect(URL)
        static func json(_ value: [String: Any], status: Int = 200, delay: TimeInterval = 0) -> Reply {
            .response(status: status, body: try! JSONSerialization.data(withJSONObject: value), length: nil, delay: delay)
        }
    }
    private static let lock = NSLock()
    private static var handler: ((URLRequest) -> Reply)?
    private static var captured: [URLRequest] = []
    private let stateLock = NSLock()
    private var stopped = false
    static var requests: [URLRequest] { lock.lock(); defer { lock.unlock() }; return captured }
    static func install(_ handler: @escaping (URLRequest) -> Reply) { lock.lock(); self.handler = handler; captured = []; lock.unlock() }
    static func reset() { lock.lock(); handler = nil; captured = []; lock.unlock() }
    override class func canInit(with request: URLRequest) -> Bool { true }
    override class func canonicalRequest(for request: URLRequest) -> URLRequest { request }
    override func startLoading() {
        Self.lock.lock(); let callback = Self.handler; Self.captured.append(request); Self.lock.unlock()
        guard let callback = callback else { fatalError("Unexpected fixture network request") }
        let reply = callback(request)
        let emit = { [self] in
            stateLock.lock(); let stopped = self.stopped; stateLock.unlock(); guard !stopped else { return }
            switch reply {
            case .redirect(let url):
                let response = HTTPURLResponse(url: request.url!, statusCode: 302, httpVersion: "HTTP/1.1", headerFields: ["Location": url.absoluteString])!
                client?.urlProtocol(self, wasRedirectedTo: URLRequest(url: url), redirectResponse: response)
            case .response(let status, let body, let length, _):
                var headers = ["Content-Type": "application/json"]
                if let length = length { headers["Content-Length"] = String(length) }
                client?.urlProtocol(self, didReceive: HTTPURLResponse(url: request.url!, statusCode: status, httpVersion: "HTTP/1.1", headerFields: headers)!, cacheStoragePolicy: .notAllowed)
                if !body.isEmpty { client?.urlProtocol(self, didLoad: body) }
                client?.urlProtocolDidFinishLoading(self)
            }
        }
        if case .response(_, _, _, let delay) = reply, delay > 0 { DispatchQueue.global().asyncAfter(deadline: .now() + delay, execute: emit) }
        else { emit() }
    }
    override func stopLoading() { stateLock.lock(); stopped = true; stateLock.unlock() }
}
