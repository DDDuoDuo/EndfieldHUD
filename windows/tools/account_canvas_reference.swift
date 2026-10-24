import AppKit
import QuartzCore

// Actual-source oracle of the redesigned Account page (Mac 1.3.0 Sources/HUDAccountCanvas.swift) and of the
// 1.3.0 account presentation it shows (HypergryphAccountController.presentation / gaugeValue / setSyncField and
// the profile-cache.json codec with unsyncedFields + partialSyncProfile). A synthetic Account/profile-cache.json
// (linked Global region, two roles, an Endfield snapshot with stamina) is encoded by the unchanged controller's own
// Codable Cache into a temporary CFFIXED_USER_HOME and read back by an unchanged HypergryphAccountController
// with a fixed clock and the default in-memory vault (no request is ever started: nothing calls connect or refresh).
// The canvas is built by the unchanged Mac code in scripted states (offline per region, linked, menus, field chips,
// reconnect, busy, no snapshot, dark / Light, English / Simplified Chinese) and exported as its model layer tree with
// the design recorder (tools/hud_design_reference_support.swift), its page controls, accessibility items, the
// toast / confirm the scene raised, and the scenario's INPUT (the cache bytes and clock), so the Windows suite
// decodes the same cache with its own controller and rebuilds the same state.
// Isolation: temporary CFFIXED_USER_HOME, TZ=UTC, no window, no input posted, no network (HypergryphAccountAPI is
// constructed by the controller's default argument but never asked), no Keychain (memory vault).

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })
private let wall = Date(timeIntervalSinceReferenceDate: 781_000_000)

private func row(_ canvas: HUDAccountCanvas, _ recorder: Recorder, _ context: HUDDesignContext, state: String, language: AppLanguage,
                 light: Bool, input: [String: Any], extra: [String: Any] = [:]) throws -> [String: Any] {
    var out: [String: Any] = ["state": state, "language": languageName(language), "light": light,
        "layer": try recorder.tree(canvas.root, id: "root"),
        "controls": (canvas.header.controls + canvas.page.controls).map { controlState($0, in: canvas.root) },
        "accessibility": accessibility(canvas.accessibilityItems), "input": input,
        "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0]]
    if canvas.isMenuOpen { out["top"] = try recorder.tree(canvas.top, id: "top") }
    let overlay = context.overlay
    out["toast"] = overlay.toastState.current.map { ["title": $0.title, "caption": $0.caption] } ?? NSNull()
    out["confirm"] = overlay.confirmState.request.map { ["id": $0.id, "title": $0.title, "message": $0.message, "confirm": $0.confirmTitle,
                                                          "danger": $0.danger] } ?? NSNull()
    for (k, v) in extra { out[k] = v }
    return out
}

private func presentationJSON(_ p: HUDAccountPresentation) -> [String: Any] {
    ["region": p.region.rawValue, "regionTitle": p.region.title, "linked": p.isLinked, "needsReconnect": p.needsReconnect,
     "status": "\(p.status)", "statusMessage": p.statusMessage, "accountName": p.accountName, "lastSync": p.lastSync,
     "roles": p.roles.map { ["id": $0.id, "title": $0.title, "subtitle": $0.subtitle] },
     "selectedRoleID": p.selectedRoleID ?? NSNull(), "headerMode": p.headerMode.rawValue, "syncProfile": p.syncProfile,
     "syncAvatar": p.syncAvatar, "syncFields": p.syncFields.map(\.rawValue).sorted(), "snapshot": p.snapshot != nil]
}

@main private enum AccountCanvasReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 2 && ProcessInfo.processInfo.environment["TZ"] == "UTC")
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        let scratch = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        try FileManager.default.createDirectory(at: scratch.appendingPathComponent("Account"), withIntermediateDirectories: true)
        let cacheURL = scratch.appendingPathComponent("Account/profile-cache.json")
        var payload: [String: Any] = ["schema": 1, "wall": wall.timeIntervalSinceReferenceDate]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        func themed(_ light: Bool) { HUDColor.apply(accentHex: "FAD41F", light: light) }

        // MARK: The synthetic cache (encoded by the controller's own Codable Cache)
        let observed = wall.addingTimeInterval(-60)
        let endfield = HypergryphRole(region: .global, game: .endfield, bindingUID: "9", roleID: "4000500060", serverID: "1",
                                      name: "Endmin#1234", serverName: "Asia", isDefault: true)
        let arknights = HypergryphRole(region: .global, game: .arknights, bindingUID: "88001", roleID: "88001", serverID: "1",
                                       name: "Doctor", serverName: "")
        let snapshot = HypergryphProfileSnapshot(role: endfield, observedAt: observed, name: "Endmin#1234", level: 41, worldLevel: 5,
            createdAt: Date(timeIntervalSinceReferenceDate: 700_000_000), operatorCount: 23, weaponCount: 18, documentCount: 7,
            stamina: HypergryphStamina(current: 42, maximum: 360, fullRecoveryAt: observed.addingTimeInterval(318 * 432), serverObservedAt: observed))
        func writeCache(linked: Bool, reconnect: Bool = false, snapshots: Bool = true, header: String = "endfield") throws -> String {
            var cache = HypergryphAccountController.Cache()
            cache.region = .global; cache.header = header
            var record = HypergryphAccountController.RegionRecord()
            record.linked = linked; record.requiresReconnect = reconnect
            if linked {
                record.roles = [endfield, arknights]; record.selectedRoleID = endfield.id
                if snapshots { record.snapshots = [endfield.id: snapshot] }
                record.bindingsAt = observed
            }
            cache.records = ["global": record]
            let data = try JSONEncoder().encode(cache)
            try data.write(to: cacheURL, options: .atomic)
            return String(decoding: data, as: UTF8.self)
        }
        func controller() -> HypergryphAccountController { HypergryphAccountController(fileURL: cacheURL, now: { wall }) }
        let workRows: [WorkModeSnapshot] = [WorkModeSnapshot(kind: .countdown, phase: .running, duration: 1500, elapsed: 60)]

        // MARK: Controller rows (1.3.0 presentation, gauge label, setSyncField + codec)
        var controllerRows: [[String: Any]] = []
        for (name, linked, reconnect, header) in [("linked", true, false, "endfield"), ("linkedArknights", true, false, "arknights"),
                                                  ("reconnect", true, true, "endfield"), ("unlinked", false, false, "endfield")] {
            for language in [AppLanguage.english, .simplifiedChinese] {
                L10n.language = language
                let cacheText = try writeCache(linked: linked, reconnect: reconnect, header: header)
                let c = controller()
                let gauge = c.gaugeValue(work: workRows[0])
                controllerRows.append(["state": name, "language": languageName(language), "cache": cacheText,
                    "presentation": presentationJSON(c.presentation), "gauge": [gauge.0, gauge.1, gauge.2],
                    "chinaTitle": HUDAccountPresentation.Region.china.title, "globalTitle": HUDAccountPresentation.Region.global.title,
                    "fieldTitles": HUDAccountPresentation.SyncField.allCases.map(\.title)])
            }
        }
        L10n.language = .english
        do {
            // Field chips through the controller: unsyncedFields sorted, partial sync written for v1.2.0 readers.
            let cacheText = try writeCache(linked: true)
            let c = controller()
            var steps: [[String: Any]] = []
            func step(_ label: String, _ action: HUDAccountCanvas.Action) {
                c.perform(action, window: nil)
                steps.append(["step": label, "cache": (try? String(contentsOf: cacheURL, encoding: .utf8)) ?? "",
                              "syncFields": c.presentation.syncFields.map(\.rawValue).sorted(), "syncProfile": c.presentation.syncProfile])
            }
            step("levels off", .setSyncField(.levels, false))
            step("uid off", .setSyncField(.uid, false))
            step("sync on", .setSyncProfile(true))
            step("levels on", .setSyncField(.levels, true))
            step("uid on", .setSyncField(.uid, true))
            step("sync off", .setSyncProfile(false))
            payload["syncFields"] = ["cache": cacheText, "steps": steps]
        }

        // MARK: Canvas
        let context = HUDDesignContext(env: env)
        let canvas = HUDAccountCanvas(context: context)
        canvas.now = { wall }
        var actions: [String] = []
        canvas.onAction = { action in
            switch action {
            case .connect(let r): actions.append("connect:" + r.rawValue)
            case .refresh: actions.append("refresh")
            case .disconnect: actions.append("disconnect")
            case .selectRegion(let r): actions.append("selectRegion:" + r.rawValue)
            case .selectRole(let id): actions.append("selectRole:" + id)
            case .selectHeaderMode(let m): actions.append("selectHeaderMode:" + m.rawValue)
            case .setSyncProfile(let v): actions.append("setSyncProfile:" + String(v))
            case .setSyncAvatar(let v): actions.append("setSyncAvatar:" + String(v))
            case .setSyncField(let f, let v): actions.append("setSyncField:" + f.rawValue + ":" + String(v))
            }
        }
        var rows: [[String: Any]] = []
        func show() { _ = canvas.makeContent(for: .account, style: style) }
        func capture(_ state: String, _ input: [String: Any], language: AppLanguage = .english, light: Bool = false, extra: [String: Any] = [:]) throws {
            var more = extra; more["actions"] = actions; actions = []
            rows.append(try row(canvas, recorder, context, state: state, language: language, light: light, input: input, extra: more))
        }
        func control(_ id: String) -> HUDControl {
            guard let found = (canvas.header.controls + canvas.page.controls).first(where: { $0.id == id }) else { fatalError("no control \(id) in \(canvas.page.controls.map(\.id))") }
            return found
        }
        // Every scene's input: the cache the controller read and the overrides applied to its presentation.
        func scene(_ cacheText: String, _ overrides: [String: Any] = [:]) -> [String: Any] { ["cache": cacheText, "overrides": overrides] }

        // Offline, Mainland China (a fresh cache) then Global.
        try? FileManager.default.removeItem(at: cacheURL)
        var c = controller()
        canvas.update(c.presentation)
        show(); canvas.setActive(true)
        try capture("offline", ["cache": NSNull(), "overrides": [:]])
        control("region:1").activate()
        try capture("offlineRegionPress", ["cache": NSNull(), "overrides": [:]])
        var cacheText = try writeCache(linked: false)
        c = controller(); canvas.update(c.presentation)
        try capture("offlineGlobal", scene(cacheText))
        // Linked: the link announcement, then the page.
        context.overlay.reset()
        cacheText = try writeCache(linked: true)
        c = controller(); canvas.update(c.presentation)
        try capture("linked", scene(cacheText))
        context.overlay.reset()
        let firstRow = control("row:role")
        HUDControlHighlightLayer.update(in: canvas.root, point: centre(firstRow, in: canvas.root), pressed: false)
        try capture("hoverRow", scene(cacheText))
        HUDControlHighlightLayer.update(in: canvas.root, point: nil, pressed: false)
        control("role").activate()
        try capture("roleMenu", scene(cacheText))
        canvas.closeMenu()
        control("header").activate()
        try capture("headerMenu", scene(cacheText))
        canvas.closeMenu()
        control("sanity").activate()
        try capture("sanityPress", scene(cacheText))
        context.overlay.reset()
        control("field:levels").activate()
        try capture("chipOff", scene(cacheText))
        c.perform(.setSyncField(.levels, false), window: nil)
        canvas.update(c.presentation)
        try capture("chipsAfterController", scene(String((try? String(contentsOf: cacheURL, encoding: .utf8)) ?? "")))
        control("disconnect").activate()
        try capture("disconnectConfirm", scene(cacheText))
        context.overlay.dismissConfirm()
        // Busy (a running refresh) and its end: the refresh toast.
        cacheText = try writeCache(linked: true)
        c = controller(); canvas.update(c.presentation)
        control("refresh").activate()
        var busy = c.presentation; busy.status = .refreshing
        canvas.update(busy)
        try capture("busy", scene(cacheText, ["status": "refreshing"]))
        canvas.update(c.presentation)
        try capture("refreshed", scene(cacheText))
        context.overlay.reset()
        // A failure message: the Notice toast.
        var failed = c.presentation; failed.status = .failed; failed.statusMessage = "Sync unavailable. Saved data is preserved."
        canvas.update(failed)
        try capture("failed", scene(cacheText, ["status": "failed", "statusMessage": failed.statusMessage]))
        context.overlay.reset()
        // Reconnect required (structure change: the header's Reconnect).
        cacheText = try writeCache(linked: true, reconnect: true)
        c = controller(); canvas.update(c.presentation)
        try capture("reconnect", scene(cacheText))
        context.overlay.reset()
        // Linked without a snapshot: dashes and no sync row sublabel.
        cacheText = try writeCache(linked: true, snapshots: false)
        c = controller(); canvas.update(c.presentation)
        try capture("noSnapshot", scene(cacheText))
        // Chinese and Light.
        cacheText = try writeCache(linked: true)
        c = controller(); canvas.update(c.presentation)
        L10n.language = .simplifiedChinese; canvas.setActive(false); show(); canvas.setActive(true)
        canvas.update(c.presentation)
        try capture("linked", scene(cacheText), language: .simplifiedChinese)
        control("header").activate()
        try capture("headerMenu", scene(cacheText), language: .simplifiedChinese)
        canvas.closeMenu()
        L10n.language = .english; canvas.setActive(false); show(); canvas.setActive(true)
        themed(true); context.refreshPalette(); show()
        try capture("linked", scene(cacheText), light: true)
        themed(false); context.refreshPalette(); show()
        // Unlinking: the page slides back; the toast only when the user asked.
        control("disconnect").activate()
        context.overlay.decide(.confirm)
        RunLoop.main.run(until: Date().addingTimeInterval(0.2))
        cacheText = try writeCache(linked: false)
        c = controller(); canvas.update(c.presentation)
        try capture("unlinked", scene(cacheText))
        canvas.setActive(false)
        payload["canvas"] = rows
        payload["controller"] = controllerRows
        payload["countdowns"] = ([nil, -5, 0, 0.2, 372, 3599.2, 137_376, 360_000_009] as [Double?]).map { (offset: Double?) -> [String: Any] in
            ["offset": offset as Any? ?? NSNull(), "text": HUDAccountGauge.countdown(until: offset.map { wall.addingTimeInterval($0) }, at: wall, hours: true)]
        }
        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("account-canvas-source.json"))
        print("Exported \(rows.count) Account scenes and \(controllerRows.count) controller rows")
    }
}
