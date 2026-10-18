import AppKit
import QuartzCore
import CryptoKit
#if ACCOUNT_REFERENCE_SPLIT
@testable import EndfieldAccountCore
#endif

extension Result {
    var failureValue: Failure? { if case .failure(let error) = self { return error }; return nil }
}

// Controller, canvas and gauge sections of the account oracle. The controller
// runs against a scripted in-memory service and vault (no network, Keychain or
// avatar request: scripted snapshots never carry avatar URLs).
final class AccountFakeRequest: HypergryphAccountRequest {
    static var cancels = 0
    func cancel() { Self.cancels += 1 }
}
final class AccountFakeVault: HypergryphAccountCredentialVault {
    private let lock = NSLock()
    private var values: [HypergryphAccountRegion: HypergryphCredentials] = [:]
    var failSave = false, failRemove = false, failLoad = false
    func load(region: HypergryphAccountRegion) throws -> HypergryphCredentials? {
        lock.lock(); defer { lock.unlock() }
        if failLoad { throw HypergryphKeychainError(status: -1) }
        return values[region]
    }
    func save(_ credentials: HypergryphCredentials, region: HypergryphAccountRegion) throws {
        lock.lock(); defer { lock.unlock() }
        if failSave { throw HypergryphKeychainError(status: -2) }
        values[region] = credentials
    }
    func remove(region: HypergryphAccountRegion) throws {
        lock.lock(); defer { lock.unlock() }
        if failRemove { throw HypergryphKeychainError(status: -3) }
        values.removeValue(forKey: region)
    }
    var stored: [String: Any] {
        lock.lock(); defer { lock.unlock() }
        var out: [String: Any] = [:]
        for (region, c) in values { out[region.rawValue] = ["cred": c.cred, "signingToken": c.signingToken, "deviceID": c.deviceID as Any? ?? NSNull()] }
        return out
    }
}
final class AccountFakeAPI: HypergryphAccountServing {
    var pending: [(String, ([String: Any]) -> Void)] = []
    var calls: [[String: Any]] = []
    var now: () -> Date = Date.init
    static func credentials(_ c: HypergryphCredentials) -> [String: Any] {
        ["cred": c.cred, "signingToken": c.signingToken, "deviceID": c.deviceID as Any? ?? NSNull()]
    }
    static func error(_ value: [String: Any]) -> HypergryphAPIError? {
        guard let name = value["error"] as? String else { return nil }
        let code = value["code"] as? Int ?? 0
        switch name {
        case "invalidCredentials": return .invalidCredentials
        case "invalidRole": return .invalidRole
        case "authenticationExpired": return .authenticationExpired
        case "service": return .service(code: code)
        case "http": return .http(status: code)
        case "transport": return .transport
        case "cancelled": return .cancelled
        case "responseTooLarge": return .responseTooLarge
        case "unsafeRedirect": return .unsafeRedirect
        default: return .invalidResponse
        }
    }
    static func role(_ v: [String: Any]) -> HypergryphRole {
        HypergryphRole(region: HypergryphAccountRegion(rawValue: v["region"] as! String)!, game: HypergryphGame(rawValue: v["game"] as! String)!,
                       bindingUID: v["bindingUID"] as! String, roleID: v["roleID"] as! String, serverID: v["serverID"] as? String,
                       name: v["name"] as? String, serverName: v["serverName"] as? String, isDefault: v["isDefault"] as? Bool ?? false,
                       isAvailable: v["isAvailable"] as? Bool ?? true)
    }
    func refreshCredentials(cred: String, region: HypergryphAccountRegion,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        calls.append(["kind": "refreshCred", "cred": cred, "region": region.rawValue])
        pending.append(("refreshCred", { v in
            if let e = Self.error(v) { completion(.failure(e)); return }
            let c = v["ok"] as! [String: Any]
            completion(.success(HypergryphCredentials(cred: c["cred"] as! String, signingToken: c["signingToken"] as! String, deviceID: c["deviceID"] as? String)))
        }))
        return AccountFakeRequest()
    }
    func refreshCredentials(credentials: HypergryphCredentials, region: HypergryphAccountRegion,
        completion: @escaping (Result<HypergryphCredentials, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        calls.append(["kind": "refresh", "region": region.rawValue].merging(Self.credentials(credentials)) { a, _ in a })
        pending.append(("refresh", { v in
            if let e = Self.error(v) { completion(.failure(e)); return }
            let c = v["ok"] as! [String: Any]
            completion(.success(HypergryphCredentials(cred: c["cred"] as! String, signingToken: c["signingToken"] as! String, deviceID: c["deviceID"] as? String)))
        }))
        return AccountFakeRequest()
    }
    func bindings(credentials: HypergryphCredentials, region: HypergryphAccountRegion,
        completion: @escaping (Result<[HypergryphRole], HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        calls.append(["kind": "bindings", "region": region.rawValue].merging(Self.credentials(credentials)) { a, _ in a })
        pending.append(("bindings", { v in
            if let e = Self.error(v) { completion(.failure(e)); return }
            completion(.success((v["ok"] as! [[String: Any]]).map(Self.role)))
        }))
        return AccountFakeRequest()
    }
    func profile(role: HypergryphRole, credentials: HypergryphCredentials,
        completion: @escaping (Result<HypergryphProfileSnapshot, HypergryphAPIError>) -> Void) -> HypergryphAccountRequest? {
        calls.append(["kind": "profile", "roleID": role.roleID, "game": role.game.rawValue, "region": role.region.rawValue]
            .merging(Self.credentials(credentials)) { a, _ in a })
        pending.append(("profile", { [now] v in
            if let e = Self.error(v) { completion(.failure(e)); return }
            let s = v["ok"] as! [String: Any]
            var snapshot = HypergryphProfileSnapshot(role: s["mismatch"] as? Bool == true ? HypergryphRole(region: role.region, game: role.game, bindingUID: role.bindingUID, roleID: "mismatch", serverID: role.serverID) : role, observedAt: now())
            snapshot.name = s["name"] as? String; snapshot.level = s["level"] as? Int; snapshot.worldLevel = s["worldLevel"] as? Int
            snapshot.experience = s["experience"] as? Int
            snapshot.createdAt = (s["createdAt"] as? Double).map { Date(timeIntervalSince1970: $0) }
            snapshot.operatorCount = s["operatorCount"] as? Int; snapshot.weaponCount = s["weaponCount"] as? Int; snapshot.documentCount = s["documentCount"] as? Int
            if let st = s["stamina"] as? [String: Any] {
                snapshot.stamina = HypergryphStamina(current: (st["current"] as? Int) ?? Int(st["current"] as! Double), maximum: (st["maximum"] as? Int) ?? Int(st["maximum"] as! Double),
                    fullRecoveryAt: (st["fullRecoveryAt"] as? Double).map { Date(timeIntervalSince1970: $0) },
                    serverObservedAt: (st["serverObservedAt"] as? Double).map { Date(timeIntervalSince1970: $0) })
            }
            completion(.success(snapshot))
        }))
        return AccountFakeRequest()
    }
}

extension HypergryphAccountReference {
    static func pumpBriefly() { for _ in 0..<20 { RunLoop.main.run(until: Date().addingTimeInterval(0.004)) } }
    static func roleSpec(_ game: String, _ uid: String, _ id: String, server: String?, name: String?, serverName: String? = nil,
                         isDefault: Bool = false, available: Bool = true, region: String = "mainland") -> [String: Any] {
        var v: [String: Any] = ["region": region, "game": game, "bindingUID": uid, "roleID": id, "isDefault": isDefault, "isAvailable": available]
        if let server { v["serverID"] = server }; if let name { v["name"] = name }; if let serverName { v["serverName"] = serverName }
        return v
    }
    static func controllerTrace() throws -> [String: Any] {
        let temp = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldAccountReference-" + UUID().uuidString, isDirectory: true)
        defer { try? FileManager.default.removeItem(at: temp) }
        let start = Date(timeIntervalSince1970: 1_791_000_000)
        var now = start
        let profileStore = try UserProfileStore(directory: temp.appendingPathComponent("Profile", isDirectory: true), now: start)
        try profileStore.update { $0.name = "Local"; $0.tag = "4321"; $0.playerIDOverride = "MANUAL-1"; $0.hasManualAwakeningDate = true; $0.permissionLevel = 12 }
        let originalUID = profileStore.profile.uid
        let cacheURL = temp.appendingPathComponent("Account/profile-cache.json")
        let vault = AccountFakeVault()
        var api = AccountFakeAPI()
        var controller: HypergryphAccountController!
        var events: [String] = []
        let initialProfile = profileFields(profileStore.profile)
        func make() {
            api = AccountFakeAPI(); api.now = { now }
            controller = HypergryphAccountController(profile: profileStore, fileURL: cacheURL, api: api, vault: vault, now: { now })
            controller.onEvent = { events.append($0) }
        }
        let ef1 = roleSpec("endfield", "9", "4000500060", server: "1", name: "Endmin#1234", serverName: "Asia", isDefault: true)
        let ef2 = roleSpec("endfield", "9", "4000500061", server: "2", name: "Alt", serverName: "Europe")
        let efBanned = roleSpec("endfield", "9", "4000500062", server: "1", name: "Banned", available: false)
        let ak = roleSpec("arknights", "88001", "88001", server: "1", name: "Doctor", serverName: "官服")
        let foreign = roleSpec("endfield", "7", "77", server: "1", name: "Foreign", region: "global")
        let roles: [[String: Any]] = [ak, ef2, ef1, efBanned, foreign]
        func endfieldCard(_ current: Int, full: Double?, server: Double?) -> [String: Any] {
            var stamina: [String: Any] = ["current": current, "maximum": 360]
            if let full { stamina["fullRecoveryAt"] = full }; if let server { stamina["serverObservedAt"] = server }
            return ["ok": ["name": "Endmin#1234", "level": 41, "worldLevel": 4, "experience": 999, "createdAt": 1_700_000_000.0,
                           "operatorCount": 31, "weaponCount": 40, "documentCount": 125, "stamina": stamina]]
        }
        func arkCard(_ current: Int, full: Double?) -> [String: Any] {
            var stamina: [String: Any] = ["current": current, "maximum": 135]
            if let full { stamina["fullRecoveryAt"] = full }
            return ["ok": ["name": "Doctor", "level": 120, "createdAt": 1_600_000_000.0, "operatorCount": 300, "stamina": stamina]]
        }
        let t = start.timeIntervalSince1970
        var steps: [[String: Any]] = [
            ["op": "new"], ["op": "visible", "value": true, "module": true], ["op": "tick"],
            ["op": "perform", "action": "selectHeaderMode", "value": "workMode"], ["op": "perform", "action": "selectHeaderMode", "value": "endfield"],
            ["op": "accept", "region": "mainland", "cred": "SYNTHETIC-CRED-A", "token": "synthetic-token-a", "device": "synthetic-device-a"],
            ["op": "resolve", "result": ["ok": roles]],
            ["op": "resolve", "result": endfieldCard(42, full: t + 7_000, server: t + 3)],
            ["op": "perform", "action": "setSyncProfile", "value": true],
            ["op": "advance", "seconds": 300.0], ["op": "tick"], ["op": "advance", "seconds": 299.0], ["op": "tick"],
            ["op": "advance", "seconds": 1.0], ["op": "tick"],
            ["op": "resolve", "result": ["ok": roles]], ["op": "resolve", "result": endfieldCard(50, full: t + 7_100, server: t + 600)],
            ["op": "perform", "action": "selectHeaderMode", "value": "arknights"],
            ["op": "advance", "seconds": 5.0], ["op": "tick"],
            ["op": "resolve", "result": endfieldCard(51, full: t + 7_100, server: t + 601)],
            ["op": "resolve", "result": arkCard(10, full: t + 7_000)],
            ["op": "advance", "seconds": 2.0], ["op": "refresh", "manual": true],
            ["op": "advance", "seconds": 5.0], ["op": "refresh", "manual": true],
            ["op": "resolve", "result": endfieldCard(52, full: t + 7_100, server: t + 607)],
            ["op": "resolve", "result": arkCard(11, full: t + 7_000)],
            ["op": "advance", "seconds": 700.0], ["op": "tick"],
            ["op": "resolve", "result": ["ok": ["cred": "SYNTHETIC-CRED-A", "signingToken": "synthetic-token-a2", "deviceID": "synthetic-device-a"]]],
            ["op": "resolve", "result": ["error": "transport"]],
            ["op": "advance", "seconds": 30.0], ["op": "tick"], ["op": "advance", "seconds": 30.0], ["op": "tick"],
            ["op": "resolve", "result": ["error": "service", "code": 10001]],
            ["op": "advance", "seconds": 119.0], ["op": "tick"], ["op": "advance", "seconds": 1.0], ["op": "tick"],
            ["op": "resolve", "result": ["error": "authenticationExpired"]],
            ["op": "advance", "seconds": 1_000.0], ["op": "tick"],
            ["op": "perform", "action": "selectRole", "value": "ak"],
            ["op": "perform", "action": "setSyncProfile", "value": false],
            ["op": "perform", "action": "selectRole", "value": "ef1"],
            ["op": "perform", "action": "setSyncProfile", "value": true],
            ["op": "visible", "value": false, "module": false], ["op": "tick"], ["op": "visible", "value": true, "module": false],
            ["op": "new"], ["op": "visible", "value": true, "module": false], ["op": "tick"],
            ["op": "accept", "region": "mainland", "cred": "SYNTHETIC-CRED-B", "token": "synthetic-token-b", "device": "synthetic-device-b"],
            ["op": "resolve", "result": ["ok": [ef1, ak]]],
            ["op": "resolve", "result": endfieldCard(300, full: nil, server: nil)],
            ["op": "resolve", "result": ["ok": ["stamina": ["current": 3, "maximum": 135]]]],
            ["op": "advance", "seconds": 600.0], ["op": "tick"],
            ["op": "resolve", "result": ["ok": [ef1, ak]]],
            ["op": "resolve", "result": ["ok": ["mismatch": true]]],
            ["op": "perform", "action": "selectRegion", "value": "global"],
            ["op": "accept", "region": "global", "cred": "SYNTHETIC-CRED-G"],
            ["op": "resolve", "result": ["error": "invalidCredentials"]],
            ["op": "vault", "failSave": true],
            ["op": "accept", "region": "global", "cred": "SYNTHETIC-CRED-H", "token": "synthetic-token-h", "device": "synthetic-device-h"],
            ["op": "vault", "failSave": false],
            ["op": "accept", "region": "global", "cred": "SYNTHETIC-CRED-I", "token": "synthetic-token-i"],
            ["op": "accept", "region": "global", "cred": "SYNTHETIC-CRED-J", "token": "synthetic-token-j", "device": "bad device"],
            ["op": "accept", "region": "global", "cred": "SYNTHETIC-CRED-K"],
            ["op": "resolve", "result": ["ok": ["cred": "SYNTHETIC-CRED-K", "signingToken": "synthetic-token-k", "deviceID": "synthetic-device-k"]]],
            ["op": "resolve", "result": ["ok": [roleSpec("arknights", "5", "5", server: nil, name: nil, region: "global")]]],
            ["op": "perform", "action": "selectHeaderMode", "value": "hidden"],
            ["op": "perform", "action": "selectHeaderMode", "value": "endfield"],
            ["op": "resolve", "result": ["ok": ["stamina": ["current": 1, "maximum": 135, "fullRecoveryAt": t + 9_000.0]]]],
            ["op": "perform", "action": "selectRegion", "value": "china"],
            ["op": "vault", "failRemove": true], ["op": "disconnect"], ["op": "vault", "failRemove": false],
            ["op": "disconnect"], ["op": "disconnect"],
            ["op": "accept", "region": "mainland", "cred": "bad cred", "token": "synthetic-token-x", "device": "synthetic-device-x"],
            ["op": "accept", "region": "mainland", "cred": "SYNTHETIC-CRED-L", "token": "synthetic-token-l", "device": "synthetic-device-l"],
            ["op": "resolve", "result": ["ok": []]],
            ["op": "language", "value": "simplifiedChinese"], ["op": "language", "value": "english"],
            ["op": "new"], ["op": "visible", "value": true, "module": true], ["op": "advance", "seconds": 10.0], ["op": "tick"],
            ["op": "resolve", "result": ["ok": ["cred": "SYNTHETIC-CRED-L", "signingToken": "synthetic-token-l2", "deviceID": "synthetic-device-l"]]],
            ["op": "resolve", "result": ["ok": [ak]]],
            ["op": "resolve", "result": arkCard(100, full: t + 20_000)],
            ["op": "perform", "action": "setSyncAvatar", "value": true], ["op": "perform", "action": "setSyncAvatar", "value": false],
            ["op": "new"], ["op": "vault", "failLoad": true], ["op": "visible", "value": true, "module": false], ["op": "advance", "seconds": 700.0],
            ["op": "tick"], ["op": "resolve-none"], ["op": "vault", "failLoad": false], ["op": "tick"],
        ]
        var trace: [[String: Any]] = []
        for index in steps.indices {
            var step = steps[index]
            switch step["op"] as! String {
            case "new": make()
            case "visible": controller.setVisible(step["value"] as! Bool, accountModule: step["module"] as! Bool)
            case "tick": controller.tick()
            case "advance": now = now.addingTimeInterval(step["seconds"] as! Double)
            case "refresh": controller.refresh(manual: step["manual"] as! Bool)
            case "disconnect": controller.disconnect()
            case "accept":
                controller.acceptLogin(cred: step["cred"] as! String, region: HypergryphAccountRegion(rawValue: step["region"] as! String)!,
                                       signingToken: step["token"] as? String, deviceID: step["device"] as? String)
            case "resolve":
                guard !api.pending.isEmpty else { fatalError("oracle script expected a pending call at step \(index): \(trace.suffix(3))") }
                let call = api.pending.removeFirst()
                step["kind"] = call.0
                call.1(step["result"] as! [String: Any])
            case "resolve-none": step["pending"] = api.pending.count
            case "vault":
                if let v = step["failSave"] as? Bool { vault.failSave = v }
                if let v = step["failRemove"] as? Bool { vault.failRemove = v }
                if let v = step["failLoad"] as? Bool { vault.failLoad = v }
            case "language": L10n.language = AppLanguage(rawValue: step["value"] as! String)!
            case "perform":
                let value = step["value"]
                switch step["action"] as! String {
                case "selectHeaderMode": controller.perform(.selectHeaderMode(HUDAccountPresentation.HeaderMode(rawValue: value as! String)!), window: nil)
                case "selectRegion": controller.perform(.selectRegion(HUDAccountPresentation.Region(rawValue: value as! String)!), window: nil)
                case "selectRole":
                    let spec = (value as! String) == "ak" ? ak : ef1
                    let id = AccountFakeAPI.role(spec).id; step["roleID"] = id
                    controller.perform(.selectRole(id), window: nil)
                case "setSyncProfile": controller.perform(.setSyncProfile(value as! Bool), window: nil)
                case "setSyncAvatar": controller.perform(.setSyncAvatar(value as! Bool), window: nil)
                default: fatalError("unknown action")
                }
            default: fatalError("unknown op")
            }
            pumpBriefly()
            steps[index] = step
            trace.append(["step": step, "after": controllerSnapshot(controller, api: api, vault: vault, cacheURL: cacheURL, profile: profileStore, events: &events)])
            api.calls.removeAll(); AccountFakeRequest.cancels = 0
        }
        precondition(profileStore.profile.uid == originalUID)
        return ["start": d(start), "initialProfile": initialProfile, "trace": trace, "roles": ["ak": AccountFakeAPI.role(ak).id, "ef1": AccountFakeAPI.role(ef1).id],
                "work": workRows()]
    }
    static func workSnapshots() -> [WorkModeSnapshot] {
        [WorkModeSnapshot(kind: .countdown, phase: .running, duration: 1500, elapsed: 61.5),
         WorkModeSnapshot(kind: .stopwatch, phase: .running, duration: 1800, elapsed: 3725.9),
         WorkModeSnapshot(kind: .countdown, phase: .idle, duration: 1799.5, elapsed: 0),
         WorkModeSnapshot(kind: .countdown, phase: .completed, duration: 600, elapsed: 700)]
    }
    static func workRows() -> [[String: Any]] {
        workSnapshots().map { ["kind": $0.kind.rawValue, "duration": d($0.duration), "elapsed": d($0.elapsed)] }
    }
    static func profileFields(_ p: UserProfile) -> [String: Any] {
        ["name": p.name, "tag": p.tag, "gamePlayerID": p.gamePlayerID as Any? ?? NSNull(), "playerIDOverride": p.playerIDOverride as Any? ?? NSNull(),
         "awakeningDate": d(p.awakeningDate), "hasManualAwakeningDate": p.hasManualAwakeningDate, "permissionLevel": p.permissionLevel,
         "explorationLevel": p.explorationLevel, "operatorsCount": p.operatorsCount, "weaponsCount": p.weaponsCount, "archivesCount": p.archivesCount]
    }
    static func controllerSnapshot(_ c: HypergryphAccountController, api: AccountFakeAPI, vault: AccountFakeVault, cacheURL: URL,
                                   profile: UserProfileStore, events: inout [String]) -> [String: Any] {
        let p = c.presentation
        let status: String
        switch p.status { case .disconnected: status = "disconnected"; case .connecting: status = "connecting"; case .connected: status = "connected"
                          case .refreshing: status = "refreshing"; case .failed: status = "failed" }
        var cache: Any = NSNull()
        if let data = try? Data(contentsOf: cacheURL) { cache = (try? JSONSerialization.jsonObject(with: data)) ?? NSNull() }
        let sanity: Any = c.sanityPresentation().map { s in
            ["game": s.game.rawValue, "current": s.current, "maximum": s.maximum, "observedAt": d(s.observedAt), "next": d(s.nextRecoveryAt),
             "full": d(s.fullRecoveryAt), "refreshing": s.isRefreshing, "available": s.refreshAvailable] } ?? NSNull()
        let result: [String: Any] = [
            "presentation": ["region": p.region.rawValue, "status": status, "statusMessage": p.statusMessage, "accountName": p.accountName,
                             "roles": p.roles.map { ["id": $0.id, "title": $0.title, "subtitle": $0.subtitle] },
                             "selectedRoleID": p.selectedRoleID as Any? ?? NSNull(), "headerMode": p.headerMode.rawValue,
                             "syncProfile": p.syncProfile, "syncAvatar": p.syncAvatar, "lastSync": p.lastSync, "isLinked": p.isLinked],
            "gauge": workSnapshots().map { w -> [Any] in let g = c.gaugeValue(work: w); return [g.0, g.1, g.2] },
            "sanity": sanity, "active": c.hasActiveRequest, "panel": c.isPresentingAccountPanel, "gameSync": c.gameSyncActive,
            "calls": api.calls, "cancels": AccountFakeRequest.cancels, "events": events, "pending": api.pending.count,
            "profile": profileFields(profile.profile), "cache": cache, "vault": vault.stored]
        events.removeAll()
        return result
    }

    // MARK: Canvas (detached layers only)
    static func canvasTrace(output: URL) throws -> [String: Any] {
        let directory = output.appendingPathComponent("canvas-layers", isDirectory: true)
        let encoder = try ModuleReferenceLayerEncoder(output: directory)
        let canvas = HUDAccountCanvas()
        var config = HUDRuntimeAppearance.configuration
        _ = canvas.makeContent(for: .account, style: HUDModuleContentStyle(dark: true, accent: config.accentColor, contentsScale: 2))
        canvas.setVisible(true)
        var emitted: [String] = []
        canvas.onAction = { action in
            switch action {
            case .connect(let r): emitted.append("connect:" + r.rawValue)
            case .refresh: emitted.append("refresh")
            case .disconnect: emitted.append("disconnect")
            case .selectRegion(let r): emitted.append("selectRegion:" + r.rawValue)
            case .selectRole(let id): emitted.append("selectRole:" + id)
            case .selectHeaderMode(let m): emitted.append("selectHeaderMode:" + m.rawValue)
            case .setSyncProfile(let v): emitted.append("setSyncProfile:" + String(v))
            case .setSyncAvatar(let v): emitted.append("setSyncAvatar:" + String(v))
            }
        }
        var states: [[String: Any]] = []
        func rect(_ r: CGRect) -> [Double] { [Double(r.origin.x), Double(r.origin.y), Double(r.width), Double(r.height)] }
        func state(_ name: String, layers: Bool = true, ops: [[String: Any]]) throws {
            var row: [String: Any] = ["name": name, "ops": ops,
                "controls": canvas.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": rect($0.rect), "enabled": $0.enabled] },
                "open": canvas.isPopoverOpen, "bounds": canvas.popoverBounds.map(rect) as Any? ?? NSNull(),
                "offset": Double(canvas.menuScrollOffset), "actions": emitted]
            if layers { row["layers"] = try encoder.encode(canvas.layer, id: "account") }
            // Visual review only (scratch output, never the fixture): flat 2x layer rendering.
            if ["role-menu", "unlinked", "header-menu", "chinese-header", "disconnect-menu"].contains(name) {
                let width = 800, height = 668
                var pixels = [UInt8](repeating: 0, count: width * height * 4)
                let context = CGContext(data: &pixels, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                                        space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.setFillColor(NSColor(white: 0.08, alpha: 1).cgColor); context.fill(CGRect(x: 0, y: 0, width: width, height: height))
                context.translateBy(x: 0, y: CGFloat(height)); context.scaleBy(x: 2, y: -2)
                canvas.layer.render(in: context)
                if let image = context.makeImage() {
                    let rep = NSBitmapImageRep(cgImage: image)
                    try rep.representation(using: .png, properties: [:])?.write(to: directory.appendingPathComponent("mac-" + name + ".png"))
                }
            }
            emitted = []
            states.append(row)
        }
        var fixture = HUDAccountPresentation()
        func roles(_ count: Int) -> [HUDAccountPresentation.Role] {
            (0..<count).map { .init(id: "role-\($0)", title: ($0 % 2 == 0 ? "Endfield" : "Arknights") + " · Fixture \($0)", subtitle: $0 % 3 == 0 ? "" : "Server \($0)") }
        }
        try state("unlinked", ops: [])
        canvas.perform("region"); try state("region-menu", ops: [["perform": "region"]])
        canvas.perform("region:china"); try state("region-same", layers: false, ops: [["perform": "region:china"]])
        canvas.perform("region"); canvas.moveMenuSelection(1); canvas.activateMenuSelection()
        try state("region-keyboard", layers: false, ops: [["perform": "region"], ["move": 1], ["activate": true]])
        fixture.isLinked = true; fixture.region = .global; fixture.status = .connected; fixture.accountName = "Fixture Endministrator"
        fixture.roles = roles(2); fixture.selectedRoleID = "role-0"; fixture.headerMode = .endfield; fixture.lastSync = "Updated 10/05 12:00:00"
        canvas.update(fixture); try state("linked", ops: [["update": "linked"]])
        canvas.perform("role"); try state("role-menu", ops: [["perform": "role"]])
        canvas.moveMenuSelection(1); canvas.activateMenuSelection(); try state("role-keyboard", layers: false, ops: [["move": 1], ["activate": true]])
        fixture.roles = roles(9); fixture.selectedRoleID = "role-4"; fixture.syncProfile = true
        canvas.update(fixture); canvas.perform("role"); try state("role-long", ops: [["update": "roles9"], ["perform": "role"]])
        _ = canvas.scroll(at: CGPoint(x: 200, y: 200), delta: 50); try state("role-scrolled", ops: [["scroll": [200, 200, 50]]])
        _ = canvas.scroll(at: CGPoint(x: 200, y: 200), delta: 1000); try state("role-scroll-clamped", layers: false, ops: [["scroll": [200, 200, 1000]]])
        _ = canvas.scroll(at: CGPoint(x: 20, y: 20), delta: -50); try state("role-scroll-outside", layers: false, ops: [["scroll": [20, 20, -50]]])
        canvas.moveMenuSelection(-1); canvas.moveMenuSelection(-1); canvas.moveMenuSelection(-1); canvas.moveMenuSelection(-1); canvas.moveMenuSelection(-1)
        try state("role-keyboard-up", layers: false, ops: [["move": -1], ["move": -1], ["move": -1], ["move": -1], ["move": -1]])
        canvas.moveMenuSelection(1); canvas.moveMenuSelection(1); canvas.moveMenuSelection(1); canvas.moveMenuSelection(1); canvas.moveMenuSelection(1); canvas.moveMenuSelection(1); canvas.moveMenuSelection(1)
        try state("role-keyboard-down", ops: [["move": 1], ["move": 1], ["move": 1], ["move": 1], ["move": 1], ["move": 1], ["move": 1]])
        _ = canvas.mouseDown(at: CGPoint(x: 200, y: 200)); try state("role-click", layers: false, ops: [["mouseDown": [200, 200]]])
        canvas.perform("header"); try state("header-menu", ops: [["perform": "header"]])
        _ = canvas.scroll(at: CGPoint(x: 260, y: 250), delta: 20); try state("header-scroll", layers: false, ops: [["scroll": [260, 250, 20]]])
        canvas.perform("header:hidden"); try state("header-hidden", layers: false, ops: [["perform": "header:hidden"]])
        canvas.perform("header"); canvas.perform("header:endfield"); try state("header-same", layers: false, ops: [["perform": "header"], ["perform": "header:endfield"]])
        canvas.perform("disconnect"); try state("disconnect-menu", ops: [["perform": "disconnect"]])
        _ = canvas.scroll(at: CGPoint(x: 200, y: 120), delta: 20); canvas.moveMenuSelection(1); try state("disconnect-inert", layers: false, ops: [["scroll": [200, 120, 20]], ["move": 1]])
        _ = canvas.mouseDown(at: CGPoint(x: 20, y: 300)); try state("disconnect-outside", layers: false, ops: [["mouseDown": [20, 300]]])
        canvas.perform("disconnect"); canvas.activateMenuSelection(); try state("disconnect-keyboard-cancel", layers: false, ops: [["perform": "disconnect"], ["activate": true]])
        canvas.perform("disconnect"); canvas.perform("menu:disconnect"); try state("disconnect-confirmed", layers: false, ops: [["perform": "disconnect"], ["perform": "menu:disconnect"]])
        _ = canvas.mouseDown(at: CGPoint(x: 370, y: 215)); _ = canvas.mouseDown(at: CGPoint(x: 370, y: 256)); _ = canvas.mouseDown(at: CGPoint(x: 330, y: 20))
        _ = canvas.mouseDown(at: CGPoint(x: 335, y: 60)); _ = canvas.mouseDown(at: CGPoint(x: 500, y: 20)); _ = canvas.mouseDown(at: CGPoint(x: 5, y: 5))
        try state("clicks", layers: false, ops: [["mouseDown": [370, 215]], ["mouseDown": [370, 256]], ["mouseDown": [330, 20]], ["mouseDown": [335, 60]], ["mouseDown": [500, 20]], ["mouseDown": [5, 5]]])
        canvas.perform("refresh"); canvas.perform("syncAvatar"); canvas.perform("connect"); canvas.perform("unknown"); canvas.perform("role:role-1")
        try state("perform-direct", layers: false, ops: [["perform": "refresh"], ["perform": "syncAvatar"], ["perform": "connect"], ["perform": "unknown"], ["perform": "role:role-1"]])
        canvas.perform("role"); fixture.roles = roles(3); canvas.update(fixture); try state("roles-changed-dismiss", layers: false, ops: [["perform": "role"], ["update": "roles3"]])
        canvas.perform("disconnect"); fixture.status = .refreshing; canvas.update(fixture); try state("busy", ops: [["perform": "disconnect"], ["update": "busy"]])
        fixture.status = .failed; fixture.statusMessage = "Sync unavailable. Saved data is preserved."; canvas.update(fixture); try state("failed", ops: [["update": "failed"]])
        fixture.status = .connecting; fixture.statusMessage = ""; fixture.isLinked = false; canvas.update(fixture); try state("connecting", ops: [["update": "connecting"]])
        fixture = HUDAccountPresentation(); fixture.status = .failed; fixture.statusMessage = "Sign in again to continue syncing."; fixture.isLinked = true
        fixture.syncAvatar = true; fixture.headerMode = .workMode; canvas.update(fixture); try state("reconnect", ops: [["update": "reconnect"]])
        _ = canvas.makeContent(for: .account, style: HUDModuleContentStyle(dark: false, accent: NSColor(srgbRed: 0.2, green: 0.55, blue: 0.9, alpha: 1), contentsScale: 2))
        try state("light", ops: [["style": "light"]])
        L10n.language = .simplifiedChinese
        _ = canvas.makeContent(for: .account, style: HUDModuleContentStyle(dark: true, accent: config.accentColor, contentsScale: 2))
        canvas.perform("header"); try state("chinese-header", ops: [["language": "simplifiedChinese"], ["perform": "header"]])
        canvas.dismissPopover(animated: false)
        L10n.language = .english
        config = HUDRuntimeAppearance.configuration
        let accent = config.accentColor.usingColorSpace(.sRGB)!
        return ["states": states, "accent": [accent.redComponent, accent.greenComponent, accent.blueComponent, accent.alphaComponent].map(Double.init),
                "unsupported": encoder.unsupported]
    }

    // MARK: Gauge
    static func rgba(_ image: CGImage) -> Data {
        let width = image.width, height = image.height
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        let space = CGColorSpace(name: CGColorSpace.sRGB)!
        let context = CGContext(data: &pixels, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4, space: space,
                                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setBlendMode(.copy)
        context.draw(image, in: CGRect(x: 0, y: 0, width: width, height: height))
        return Data(pixels)
    }
    static func providerBytes(_ image: CGImage) -> Data { image.dataProvider!.data! as Data }
    static func child<T>(_ value: Any, _ label: String) -> T { Mirror(reflecting: value).children.first { $0.label == label }!.value as! T }
    static func gaugeRows() throws -> [String: Any] {
        let gauge = HUDAccountGauge()
        let now = Date(timeIntervalSinceReferenceDate: 781_000_000)
        var numbers: [[String: Any]] = []
        let number: CALayer = child(gauge, "number")
        for scale in [1.0, 2.0, 3.0, 1.5] {
            for value in ["42 / 360", "— / —", "0 / 30", "123 / 4567", "35791394 / 35791395", "1 / 1", "999 / 999", "7"] {
                gauge.update(value: value, accessibilityLabel: "Sanity " + value, visible: true, accent: .systemYellow, scale: CGFloat(scale), sanity: nil, at: now)
                let image = number.contents as! CGImage
                let bytes = providerBytes(image)
                var row: [String: Any] = ["value": value, "scale": scale, "width": image.width, "height": image.height,
                                          "sha256": SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(),
                                          "nonzero": bytes.reduce(0) { $0 + ($1 == 0 ? 0 : 1) }]
                if scale == 1 { row["base64"] = bytes.base64EncodedString() }
                numbers.append(row)
            }
        }
        gauge.update(value: "", accessibilityLabel: "", visible: false, accent: .systemYellow, scale: 2, sanity: nil, at: now)
        let hiddenEmpty: [String: Any] = ["hidden": gauge.layer.isHidden, "contents": number.contents == nil ? "nil" : "retained", "canOpen": gauge.canOpen]
        var tooltips: [[String: Any]] = []
        let next: CALayer = child(gauge, "nextLabel"), full: CALayer = child(gauge, "fullLabel")
        let nextValue: CALayer = child(gauge, "nextValue"), fullValue: CALayer = child(gauge, "fullValue")
        for (language, deadlines) in [(AppLanguage.english, (49.0, 38.0 * 3600 + 193)), (.simplifiedChinese, (0.2, 3599.0)), (.english, (-5.0, 359_999_999.0 + 10))] {
            L10n.language = language
            let recovery = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: now,
                nextRecoveryAt: now.addingTimeInterval(deadlines.0), fullRecoveryAt: now.addingTimeInterval(deadlines.1), isRefreshing: false, refreshAvailable: true)
            gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: recovery, at: now)
            if !gauge.isPopoverOpen { gauge.perform("toggle") }
            var row: [String: Any] = ["language": language.rawValue, "next": deadlines.0, "full": deadlines.1,
                                      "nextText": gauge.nextRecoveryText, "fullText": gauge.fullRecoveryText,
                                      "actions": gauge.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": [$0.rect.minX, $0.rect.minY, $0.rect.width, $0.rect.height].map(Double.init), "enabled": $0.enabled] }]
            for (name, layer) in [("nextLabel", next), ("fullLabel", full), ("nextValue", nextValue), ("fullValue", fullValue)] {
                guard let contents = layer.contents else { row[name] = NSNull(); continue }
                let image = contents as! CGImage
                if language == .simplifiedChinese || name.hasSuffix("Value") || language == .english {
                    let bytes = providerBytes(image)
                    row[name] = ["width": image.width, "height": image.height, "sha256": SHA256.hash(data: bytes).map { String(format: "%02x", $0) }.joined(),
                                 "numerals": image.bitsPerPixel == 32 && image.alphaInfo == .premultipliedLast]
                }
            }
            tooltips.append(row)
        }
        let refreshing = HypergryphSanityPresentation(game: .arknights, current: 5, maximum: 135, observedAt: now, nextRecoveryAt: nil, fullRecoveryAt: nil,
                                                       isRefreshing: true, refreshAvailable: true)
        gauge.update(value: "5 / 135", accessibilityLabel: "x", visible: true, accent: .systemYellow, scale: 2, sanity: refreshing, at: now)
        var refreshed = 0; gauge.onRefresh = { refreshed += 1 }
        gauge.perform("refresh")
        let refreshingRow: [String: Any] = ["open": gauge.isPopoverOpen, "nextText": gauge.nextRecoveryText, "fullText": gauge.fullRecoveryText, "refreshed": refreshed,
                                            "actions": gauge.accessibleActions.map { ["id": $0.id, "enabled": $0.enabled] }]
        let available = HypergryphSanityPresentation(game: .arknights, current: 5, maximum: 135, observedAt: now, nextRecoveryAt: nil, fullRecoveryAt: nil,
                                                      isRefreshing: false, refreshAvailable: true)
        gauge.update(value: "5 / 135", accessibilityLabel: "x", visible: true, accent: .systemYellow, scale: 2, sanity: available, at: now)
        gauge.perform("refresh")
        var pointer: [[String: Any]] = []
        gauge.dismiss(animated: false)
        for point in [CGPoint(x: 170, y: 20), CGPoint(x: 90, y: 20), CGPoint(x: -56 + 195 + 5, y: 40 + 19 + 5), CGPoint(x: -40, y: 60),
                      CGPoint(x: -80, y: 60), CGPoint(x: 167, y: 41), CGPoint(x: 0, y: 0), CGPoint(x: 90, y: 20), CGPoint(x: 168, y: 20)] {
            let handled = gauge.mouseDown(at: point)
            pointer.append(["point": [point.x, point.y].map(Double.init), "handled": handled, "open": gauge.isPopoverOpen, "refreshed": refreshed])
        }
        gauge.update(value: "x", accessibilityLabel: "", visible: true, accent: .systemYellow, scale: 2, sanity: nil, at: now)
        let closedWithoutSanity: [String: Any] = ["open": gauge.isPopoverOpen, "canOpen": gauge.canOpen, "handled": gauge.mouseDown(at: CGPoint(x: 90, y: 20))]
        var countdowns: [[String: Any]] = []
        for offset in [nil, -10, 0, 0.0001, 0.5, 1, 59.5, 60, 3599.2, 3600, 359_999_999, 359_999_999.5, 400_000_000, Double.infinity, Double.nan] as [Double?] {
            for hours in [false, true] {
                countdowns.append(["offset": offset.map(d) as Any? ?? NSNull(), "hours": hours,
                    "text": HUDAccountGauge.countdown(until: offset.map { now.addingTimeInterval($0) }, at: now, hours: hours)])
            }
        }
        let sub = gauge.layer.sublayers ?? []
        func layerImage(_ name: String) -> [String: Any] {
            guard let layer = sub.first(where: { $0.name == name }), let contents = layer.contents else { return ["missing": true] }
            let image = contents as! CGImage
            return ["width": image.width, "height": image.height, "frame": [layer.frame.minX, layer.frame.minY, layer.frame.width, layer.frame.height].map(Double.init),
                    "contentsCenter": [layer.contentsCenter.minX, layer.contentsCenter.minY, layer.contentsCenter.width, layer.contentsCenter.height].map(Double.init),
                    "gravity": layer.contentsGravity.rawValue, "rgba": rgba(image).base64EncodedString()]
        }
        // Whole-gauge CALayer.render(in:) (popover closed, highlights idle) for
        // composition checks of the nine-slice wallet bars, icon and numerals.
        var renders: [[String: Any]] = []
        gauge.dismiss(animated: false)
        for scale in [1.0, 2.0] {
            gauge.update(value: "42 / 360", accessibilityLabel: "x", visible: true, accent: .systemYellow, scale: CGFloat(scale), sanity: nil, at: now)
            let width = Int(168 * scale), height = Int(54 * scale)
            var pixels = [UInt8](repeating: 0, count: width * height * 4)
            let context = CGContext(data: &pixels, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                                    space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            context.scaleBy(x: CGFloat(scale), y: CGFloat(scale)); context.translateBy(x: 0, y: 6)
            gauge.layer.render(in: context)
            renders.append(["scale": scale, "width": width, "height": height, "origin": [0.0, -6.0], "rgba": Data(pixels).base64EncodedString()])
        }
        // Structural export of the open recovery popover (background, refresh
        // arrow, highlight) for exact colours and geometry.
        let popoverEncoder = try ModuleReferenceLayerEncoder(output: FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldAccountGaugeLayers-" + UUID().uuidString))
        let enabledRecovery = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: now, nextRecoveryAt: now.addingTimeInterval(49),
                                                           fullRecoveryAt: now.addingTimeInterval(100), isRefreshing: false, refreshAvailable: true)
        gauge.update(value: "42 / 360", accessibilityLabel: "x", visible: true, accent: .systemYellow, scale: 2, sanity: enabledRecovery, at: now)
        if !gauge.isPopoverOpen { gauge.perform("toggle") }
        let popoverLayer: CALayer = child(gauge, "popover")
        let popoverEnabled = try popoverEncoder.encode(popoverLayer, id: "popover")
        var disabledRecovery = enabledRecovery; disabledRecovery = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: now,
            nextRecoveryAt: now.addingTimeInterval(49), fullRecoveryAt: now.addingTimeInterval(100), isRefreshing: true, refreshAvailable: true)
        gauge.update(value: "42 / 360", accessibilityLabel: "x", visible: true, accent: .systemYellow, scale: 2, sanity: disabledRecovery, at: now)
        let popoverDisabled = try popoverEncoder.encode(popoverLayer, id: "popover")
        gauge.dismiss(animated: false)
        let wholeGauge = try popoverEncoder.encode(gauge.layer, id: "gauge")
        L10n.language = .english
        return ["numbers": numbers, "hiddenEmpty": hiddenEmpty, "renders": renders,
                "popoverEnabled": popoverEnabled, "popoverDisabled": popoverDisabled, "layer": wholeGauge, "tooltips": tooltips, "refreshing": refreshingRow, "pointer": pointer,
                "closedWithoutSanity": closedWithoutSanity, "countdowns": countdowns,
                "artwork": ["back": layerImage("hud.account.stamina.back"), "deco": layerImage("hud.account.stamina.deco"), "icon": layerImage("hud.account.stamina.item_ap"),
                            "silhouette": { () -> [String: Any] in
                                guard let highlight = sub.first(where: { $0.name == "hud.control.highlight" && $0.mask != nil }), let mask = highlight.mask,
                                      let contents = mask.contents else { return ["missing": true] }
                                let image = contents as! CGImage
                                return ["width": image.width, "height": image.height, "frame": [highlight.frame.minX, highlight.frame.minY, highlight.frame.width, highlight.frame.height].map(Double.init),
                                        "contentsCenter": [mask.contentsCenter.minX, mask.contentsCenter.minY, mask.contentsCenter.width, mask.contentsCenter.height].map(Double.init),
                                        "gravity": mask.contentsGravity.rawValue, "rgba": rgba(image).base64EncodedString()]
                            }()],
                "constants": ["size": [168.0, 42.0], "popoverRect": [Double(HUDAccountGauge.popoverRect.minX), 40, 224, 65], "refreshRect": [195.0, 19, 23, 25],
                              "headerPosition": [292.0, 0], "font": HUDAccountGauge.sourceNumberFont ?? "", "artworkAvailable": HUDAccountGauge.sourceArtworkAvailable]]
    }
}
