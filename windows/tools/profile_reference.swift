import AppKit
import QuartzCore

/// Compiled with the unchanged Mac Sources. Drives the actual PersonalProfileCanvas
/// and UserProfileStore against temporary profile directories only. No window,
/// account, real profile, clipboard, timer run loop or live provider is used.
@main enum ProfileReferenceExporter {
    struct ProfileArchive: Codable { let version: Int; let profile: UserProfile }
    final class Live { var seconds: TimeInterval = 0 }
    static func require(_ condition: @autoclosure () -> Bool, _ message: String) throws {
        if !condition() { throw HUDSourceError.invalid(message) }
    }
    static func rect(_ r: CGRect) -> [Double] { [Double(r.minX), Double(r.minY), Double(r.width), Double(r.height)] }
    /// Reflection, not a JSON round trip: JSONSerialization.jsonObject drops a
    /// leading U+FEFF inside string values, which the in-memory profile keeps.
    static func encoded(_ profile: UserProfile) throws -> Any {
        var result: [String: Any] = [:]
        let children = Mirror(reflecting: profile).children
        try require(children.count == 30, "Every UserProfile stored property")
        for child in children {
            guard let label = child.label else { continue }
            switch child.value {
            case let value as String: result[label] = value
            case let value as String?: if let value { result[label] = value }
            case let value as Bool: result[label] = value
            case let value as Int: result[label] = value
            case let value as Double: result[label] = value
            case let value as Date: result[label] = value.timeIntervalSinceReferenceDate
            default: throw HUDSourceError.invalid("Unexpected profile field type " + label)
            }
        }
        return result
    }
    static let languages: [(String, AppLanguage)] = [("english", .english), ("simplifiedChinese", .simplifiedChinese),
        ("traditionalChinese", .traditionalChinese), ("japanese", .japanese), ("korean", .korean)]
    static var root: URL!

    final class Harness {
        let directory: URL
        let store: UserProfileStore
        let canvas: PersonalProfileCanvas
        let live = Live()
        var requests: [[String: Any]] = []
        init(_ baseline: UserProfile, live seconds: TimeInterval = 3600, dark: Bool = true) throws {
            directory = ProfileReferenceExporter.root.appendingPathComponent("Profile-" + UUID().uuidString, isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try JSONEncoder().encode(ProfileArchive(version: 1, profile: baseline)).write(to: directory.appendingPathComponent("profile.json"))
            store = try UserProfileStore(directory: directory, now: baseline.awakeningDate)
            live.seconds = seconds
            let box = live
            canvas = PersonalProfileCanvas(store: store, workSeconds: { box.seconds }, reduceMotion: { true })
            canvas.onEditField = { [unowned self] field, rect, value in
                self.requests.append(["kind": "edit", "field": field.rawValue, "rect": ProfileReferenceExporter.rect(rect), "text": value])
            }
            canvas.onChooseImage = { [unowned self] kind in self.requests.append(["kind": kind == .avatar ? "chooseAvatar" : "chooseBackground"]) }
            canvas.onChooseColor = { [unowned self] color in
                let rgb = color.usingColorSpace(.sRGB)!
                self.requests.append(["kind": "chooseColor", "rgb": [Double(rgb.redComponent), Double(rgb.greenComponent), Double(rgb.blueComponent)]])
            }
            _ = canvas.makeContent(for: .profile, style: HUDModuleContentStyle(dark: dark,
                accent: NSColor(srgbRed: 250 / 255, green: 212 / 255, blue: 31 / 255, alpha: 1), contentsScale: 2))
        }
        deinit { canvas.deactivate(); try? FileManager.default.removeItem(at: directory) }
        func workText() -> String {
            func find(_ layer: CALayer) -> String? {
                if let text = layer as? CATextLayer, text.frame == CGRect(x: 105, y: 292, width: 70, height: 28) { return text.string as? String }
                for child in layer.sublayers ?? [] { if let found = find(child) { return found } }
                return nil
            }
            return find(canvas.layer) ?? ""
        }
        func snapshot() throws -> [String: Any] {
            let actions = canvas.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": ProfileReferenceExporter.rect($0.rect)] as [String: Any] }
            let sliders = canvas.accessibleSliders.map { ["field": $0.field.rawValue, "label": $0.label, "rect": ProfileReferenceExporter.rect($0.rect),
                "value": $0.value, "minimum": $0.minimum, "maximum": $0.maximum, "step": $0.step, "valueDescription": $0.valueDescription] as [String: Any] }
            let result: [String: Any] = ["actions": actions, "sliders": sliders,
                "popover": canvas.popoverBounds.map(ProfileReferenceExporter.rect) as Any? ?? NSNull(),
                "open": canvas.isPopoverOpen, "hidden": canvas.isTextHidden, "dragging": canvas.isDragging,
                "status": canvas.accessibilityStatus, "profile": try ProfileReferenceExporter.encoded(store.profile),
                "preview": try ProfileReferenceExporter.encoded(canvas.profileValue), "hours": workText(), "requests": requests,
                "canEdit": PersonalProfileField.allCases.filter { canvas.canEdit($0) }.map(\.rawValue)]
            requests.removeAll()
            return result
        }
        func run(_ step: [String: Any]) throws -> Any {
            let op = step["op"] as! String
            func number(_ key: String) -> Double {
                if let text = step[key] as? String { return text == "nan" ? .nan : text == "inf" ? .infinity : text == "-inf" ? -.infinity : Double(text)! }
                return (step[key] as! NSNumber).doubleValue
            }
            func point() -> CGPoint { CGPoint(x: number("x"), y: number("y")) }
            switch op {
            case "sample": return NSNull()
            case "perform": canvas.perform(actionID: step["id"] as! String); return NSNull()
            case "mouseDown": return canvas.mouseDown(at: point())
            case "mouseDragged": canvas.mouseDragged(to: point()); return NSNull()
            case "mouseUp": canvas.mouseUp(); return NSNull()
            case "setSlider": return canvas.setSlider(field: PersonalProfileField(rawValue: step["field"] as! String)!, value: number("value"))
            case "nudge": return canvas.nudgeSlider(number("direction"))
            case "dismiss": canvas.dismissPopover(); return NSNull()
            case "commit": return canvas.commit(field: PersonalProfileField(rawValue: step["field"] as! String)!, text: step["text"] as! String)
            case "lock": canvas.gameSyncActive = step["value"] as! Bool; return NSNull()
            case "live": live.seconds = number("value"); canvas.refreshWorkDuration(); return NSNull()
            case "setWork":
                do { try store.setWorkSeconds(number("value")); canvas.refreshWorkDuration(); return true } catch { return false }
            case "customColor":
                canvas.setCustomColor(NSColor(srgbRed: CGFloat(number("r")), green: CGFloat(number("g")), blue: CGFloat(number("b")), alpha: 1)); return NSNull()
            case "external":
                // An account sync or another owner commits through the shared store.
                let fields = step["fields"] as! [String: Any]
                try store.update { profile in
                    if let value = fields["name"] as? String { profile.name = value }
                    if let value = fields["tag"] as? String { profile.tag = value }
                    if let value = fields["gamePlayerID"] as? String { profile.gamePlayerID = value; profile.playerIDOverride = nil }
                    if let value = fields["backgroundWidth"] as? Double { profile.backgroundWidth = value }
                    if let value = fields["permissionLevel"] as? Int { profile.permissionLevel = value }
                }
                return NSNull()
            default: throw HUDSourceError.invalid("Unknown profile reference step " + op)
            }
        }
    }

    static func baseline() -> UserProfile {
        var profile = UserProfile(awakeningDate: Date(timeIntervalSince1970: 1_700_000_000), uid: "1000000000")
        profile.name = "Endministrator"; profile.tag = "0000"; profile.introduction = "Neutral reference profile"
        profile.birthdayMonth = 1; profile.birthdayDay = 1; profile.accumulatedWorkSeconds = 3600
        return profile
    }

    static func scenario() -> [[String: Any]] {
        var s: [[String: Any]] = []
        func add(_ op: String, _ values: [String: Any] = [:]) { var row = values; row["op"] = op; s.append(row) }
        func commit(_ field: String, _ text: String) { add("commit", ["field": field, "text": text]) }
        add("sample")
        add("perform", ["id": "profile:menu"])
        add("perform", ["id": "profile:name"])
        commit("name", "  Ångström 管理员 😀👩‍👩‍👧‍👦 🇯🇵 한국어 e\u{301}\u{302} extra long text ")
        commit("name", "  Ångström 管理员 😀🇯🇵👍🏽 한국어 e\u{301}\u{302}❤️ extra long ")
        commit("name", "가각간갇갈갉갊감갑값갓갔강갖갗같갚갛개객갠")
        commit("name", "\u{1100}\u{1161}\u{11A8}\u{1100}\u{1161}x")
        commit("tag", "  #😀12345678901  ")
        commit("tag", "##12")
        commit("tag", "#ABCDEFGHIJKL")
        commit("tag", "has space")
        commit("tag", "a#b")
        commit("tag", "#\u{301}12")
        commit("name", "")
        commit("name", "Line\u{0007}bell")
        commit("name", "Zero\u{200B}width")
        commit("introduction", String(repeating: "介绍🙂\n", count: 40) + " tail ")
        commit("introduction", "")
        commit("introduction", "  \u{200B}keeps\u{FEFF} ")
        commit("tag", "\u{00A0}12")
        commit("tag", "1\u{200B}2")
        commit("tag", "1\u{FEFF}2")
        commit("tag", "1\u{180E}2")
        commit("tag", "ß\u{FB00}")
        commit("playerID", "  12345\n678\u{2028}9  ")
        commit("playerID", String(repeating: "9", count: 70))
        commit("playerID", "\u{3000}\n")
        commit("playerID", "\u{200B}12\u{200B}")
        commit("playerID", "\u{FEFF}34\u{180E}")
        commit("playerID", "\u{1680}\u{2007}56\u{00A0}\u{202F}\u{205F}")
        commit("playerID", "\u{0085}7\u{2029}8\u{000B}9\u{000C}")
        commit("playerID", "\t\r\n0 1\t")
        commit("playerID", "\u{0009}\u{001F}x")
        add("perform", ["id": "profile:playerID"])
        add("perform", ["id": "profile:toggleDateLabel"])
        add("perform", ["id": "profile:birthday"])
        for value in ["2/29", "02-30", "13/01", "1/1/1", "+3/+4", " 3/4 ", "0/5", "4/31", "12/31", "1/", "/1", "a/b", "\u{FF11}/1", "3 /4"] { commit("birthday", value) }
        add("perform", ["id": "profile:toggleDateLabel"])
        add("perform", ["id": "profile:awakeningDate"])
        for value in ["2024-02-29", "2023/02/29", "0/1/1", "9999/12/31", "10000/1/1", "1582/10/10", "1582/10/15", "1/1/1",
                      "2024/2/3/", "2024//3", "+2024/+2/+3", "2024/2/-3", "2024/13/1", "2024/12/32", "2011-12-30", "2020/1/1"] { commit("awakeningDate", value) }
        // Each parse case starts from a distinct sentinel so an accepted value,
        // a fallback and a clamp cannot be confused.
        func cases(_ field: String, _ sentinel: String, _ values: [String]) { for value in values { commit(field, sentinel); commit(field, value) } }
        cases("permissionLevel", "23", ["0", "61", "abc", "+5", "-3", "1e2", "  7 ", "+-5", "--5", "07", "99999999999999999999", "", "+", "-", "5 0", "٣"])
        cases("explorationLevel", "4", ["8", "0", "x", "3", "+2"])
        cases("operatorsCount", "123", ["-5", "abc", "9223372036854775807", "9223372036854775808", "-9223372036854775808", "+12", "1_000", "0x10", "5.0", "0012"])
        commit("weaponsCount", "77"); commit("archivesCount", "-1"); commit("archivesCount", "400")
        cases("backgroundOffsetX", "123", ["abc", "1e400", "0x10", "-0", "nan", "inf", "+5", "5.", ".5", "1e2", "0x1p3", "45.4", "-450.5",
            "-infinity", "+.5e1", "1,5", "0X1.8P1", "1e-400", "0x", "1e", "e1", ".", "+-1", "INF", "Infinity", "NaN", "0x1.fffffffffffffp1023", "1_0", "١٢", "0b11", "00012.500"])
        cases("backgroundWidth", "555", ["450.4", "450.5", "1e3", "-1", "899.999"])
        cases("backgroundZoom", "2", ["0.5", "20.004", "3.14159", "nan", "1.005"])
        cases("thumbnailOffsetX", "50", ["7", "-100.5", "33.333", "0x1p6", "-0.0", "57"])
        cases("avatarOffsetY", "50", ["14", "29", "57", "-7.000000000000001"])
        add("perform", ["id": "profile:backgroundMenu"])
        // Background width row: drag from the track start to an interior point and past the end.
        add("mouseDown", ["x": 142, "y": 92])
        add("mouseDragged", ["x": 260, "y": 400])
        add("mouseDragged", ["x": 900, "y": 92])
        add("mouseDragged", ["x": 200.25, "y": 92])
        add("mouseUp")
        add("setSlider", ["field": "backgroundZoom", "value": 3.14159])
        add("nudge", ["direction": 1]); add("nudge", ["direction": -1]); add("nudge", ["direction": 1000])
        add("setSlider", ["field": "thumbnailOffsetX", "value": 7])
        add("nudge", ["direction": 1])
        add("setSlider", ["field": "backgroundOffsetY", "value": 1000])
        add("setSlider", ["field": "backgroundOffsetY", "value": "nan"])
        add("setSlider", ["field": "avatarZoom", "value": 2])
        // Thumbnail X: values whose percent/100*100 round trip is inexact.
        add("perform", ["id": "profile:backgroundMenu"])
        add("mouseDown", ["x": 266.12, "y": 242])
        add("mouseUp")
        add("mouseDown", ["x": 150.2, "y": 272])
        add("mouseDragged", ["x": 153.7, "y": 272])
        add("mouseDragged", ["x": 381.9, "y": 10])
        add("external", ["fields": ["name": "Synced Name", "backgroundWidth": 812.0]])
        add("mouseUp")
        add("mouseDown", ["x": 250, "y": 150])
        add("perform", ["id": "profile:themeMenu"])
        add("perform", ["id": "profile:theme:6EDFE8"])
        add("perform", ["id": "profile:themeCustom"])
        add("customColor", ["r": 0.5, "g": 0.25, "b": 0.125])
        add("customColor", ["r": 1, "g": 0.999, "b": 0.0019])
        add("customColor", ["r": 1.2, "g": 0.5, "b": 0])
        add("customColor", ["r": 0.501960784, "g": 0.5, "b": 0.498])
        add("perform", ["id": "profile:theme:fad41f"])
        add("perform", ["id": "profile:theme:\u{FB00}1234"])
        add("perform", ["id": "profile:theme:XYZ"])
        add("perform", ["id": "profile:themeDefault"])
        add("perform", ["id": "profile:backgroundMenu"])
        add("perform", ["id": "profile:background"])
        add("perform", ["id": "profile:resetBackground"])
        add("mouseDown", ["x": 372, "y": 20])
        add("perform", ["id": "profile:menu"])
        add("perform", ["id": "profile:portraitMenu"])
        add("mouseDown", ["x": 200, "y": 170])
        add("mouseDragged", ["x": 374, "y": 170])
        add("mouseUp")
        add("setSlider", ["field": "avatarOffsetX", "value": -7])
        add("setSlider", ["field": "avatarOffsetY", "value": 55.5])
        add("nudge", ["direction": -1])
        add("setSlider", ["field": "backgroundZoom", "value": 2])
        add("mouseDown", ["x": 130, "y": 128])
        add("perform", ["id": "profile:restoreAvatar"])
        add("perform", ["id": "profile:menu"])
        add("perform", ["id": "profile:avatar"])
        add("perform", ["id": "profile:menu"])
        add("mouseDown", ["x": 500, "y": 10])
        add("mouseDown", ["x": 500, "y": 10])
        add("mouseDown", ["x": 10, "y": 200])
        add("mouseDown", ["x": 400, "y": 334])
        add("mouseDown", ["x": 399.5, "y": 333.5])
        add("perform", ["id": "profile:visibility"])
        add("mouseDown", ["x": 260, "y": 200])
        add("perform", ["id": "profile:visibility"])
        commit("tag", "bad tag")
        add("perform", ["id": "profile:theme:A8E58B"])
        add("perform", ["id": "profile:toggleDateLabel"])
        commit("introduction", "Clears the error")
        add("lock", ["value": true])
        add("perform", ["id": "profile:menu"])
        add("perform", ["id": "profile:name"])
        add("perform", ["id": "profile:popoverClose"])
        commit("name", "Manual")
        commit("introduction", "Editable while synced")
        commit("birthday", "7/8")
        commit("permissionLevel", "1")
        add("perform", ["id": "profile:playerID"])
        add("perform", ["id": "profile:toggleDateLabel"])
        add("perform", ["id": "profile:birthday"])
        add("perform", ["id": "profile:toggleDateLabel"])
        add("external", ["fields": ["gamePlayerID": "1234567890123456789", "name": "Official", "tag": "5678", "permissionLevel": 42]])
        add("perform", ["id": "profile:backgroundMenu"])
        add("setSlider", ["field": "backgroundWidth", "value": 640])
        add("lock", ["value": false])
        add("perform", ["id": "profile:menu"])
        add("live", ["value": 7200])
        add("setWork", ["value": 9000])
        add("setWork", ["value": 100])
        add("live", ["value": 20000])
        add("setWork", ["value": "nan"])
        add("live", ["value": "inf"])
        add("live", ["value": "nan"])
        add("setWork", ["value": 9000])
        add("live", ["value": 0])
        add("perform", ["id": "profile:bogus"])
        add("perform", ["id": "profile:backgroundWidth"])
        add("perform", ["id": "not-profile"])
        add("dismiss")
        add("dismiss")
        return s
    }

    static func labels() throws -> [[String: Any]] {
        var result: [[String: Any]] = []
        for (name, language) in languages {
            L10n.language = language
            let harness = try Harness(baseline())
            var states: [String: Any] = ["default": try harness.snapshot()]
            for (state, steps) in [("identity", ["profile:menu"]), ("portrait", ["profile:menu", "profile:portraitMenu"]),
                                   ("background", ["profile:backgroundMenu"]), ("theme", ["profile:backgroundMenu", "profile:themeMenu"]),
                                   ("hidden", ["profile:visibility"]), ("birthday", ["profile:visibility", "profile:toggleDateLabel"])] {
                for id in steps { harness.canvas.perform(actionID: id) }
                states[state] = try harness.snapshot()
                harness.canvas.dismissPopover()
            }
            harness.canvas.gameSyncActive = true; harness.canvas.perform(actionID: "profile:menu")
            states["lockedIdentity"] = try harness.snapshot()
            _ = harness.canvas.commit(field: .tag, text: "a b"); states["tagError"] = try harness.snapshot()
            _ = harness.canvas.commit(field: .birthday, text: "x"); states["birthdayError"] = try harness.snapshot()
            harness.canvas.gameSyncActive = false
            _ = harness.canvas.commit(field: .awakeningDate, text: "x"); states["awakeningError"] = try harness.snapshot()
            _ = harness.canvas.commit(field: .name, text: ""); states["nameError"] = try harness.snapshot()
            _ = harness.canvas.commit(field: .permissionLevel, text: "4"); states["cleared"] = try harness.snapshot()
            result.append(["language": name, "titles": Dictionary(uniqueKeysWithValues: PersonalProfileField.allCases.map { ($0.rawValue, $0.title) }),
                           "states": states])
        }
        L10n.language = .english
        return result
    }

    static func decodeCases() throws -> [[String: Any]] {
        let base = try encoded(baseline()) as! [String: Any]
        let longName = String(repeating: "名", count: 25)
        let variants: [(String, [String: Any?])] = [
            ("normalized", ["name": "  " + longName + "\n", "tag": " 12345678901234 ", "permissionLevel": 99, "explorationLevel": 0,
                "birthdayMonth": 2, "birthdayDay": 31, "themeColorHex": " #fad41f\n", "avatarZoom": 50, "avatarOffsetX": -3,
                "backgroundWidth": 100, "backgroundOffsetX": 1000, "backgroundOffsetY": -999, "thumbnailZoom": 0.2, "thumbnailOffsetY": 2,
                "playerIDOverride": " 12\n34 ", "introduction": String(repeating: "x", count: 200)]),
            ("blankOverride", ["playerIDOverride": " \u{3000}\n ", "themeColorHex": "xyz123", "birthdayMonth": 0, "birthdayDay": 0]),
            ("missingOptional", ["introduction": nil, "birthdayMonth": nil, "birthdayDay": nil, "avatarZoom": nil, "backgroundWidth": nil, "showsBirthday": nil, "hasManualAwakeningDate": nil]),
            ("negativeCount", ["operatorsCount": -1]), ("negativeWork", ["accumulatedWorkSeconds": -1]), ("shortUID", ["uid": "123"]),
            ("badImage", ["avatarFilename": "x.png"]), ("hashTag", ["tag": "#12"]), ("blankName", ["name": " \n"]),
            ("controlName", ["name": "a\u{0001}b"]), ("goodImage", ["avatarFilename": "00000000-0000-4000-8000-000000000001.image",
                "backgroundFilename": "00000000-0000-4000-8000-000000000002.png"]),
        ]
        var result: [[String: Any]] = []
        for (name, changes) in variants {
            var profile = base
            for (key, value) in changes { if let value { profile[key] = value } else { profile.removeValue(forKey: key) } }
            let directory = root.appendingPathComponent("Decode-" + UUID().uuidString, isDirectory: true)
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            defer { try? FileManager.default.removeItem(at: directory) }
            try JSONSerialization.data(withJSONObject: ["version": 1, "profile": profile], options: [.sortedKeys])
                .write(to: directory.appendingPathComponent("profile.json"))
            var row: [String: Any] = ["name": name, "input": profile]
            do { row["output"] = try encoded(try UserProfileStore(directory: directory).profile) }
            catch { row["error"] = String(describing: error) }
            result.append(row)
        }
        return result
    }

    static func dateCases() throws -> [[String: Any]] {
        var result: [[String: Any]] = []
        let inputs = ["2024/02/29", "2023/02/29", "2011/12/30", "2011/12/31", "1582/10/10", "1582/10/04", "1582/10/15", "1/1/1", "9999/12/31",
                      "1900/01/01", "1970/1/1", "2038/01/19", "2016/12/31", "1916/04/30", "1945/09/02"]
        for zone in ["Asia/Shanghai", "America/New_York", "Pacific/Apia", "Europe/London", "Australia/Lord_Howe", "UTC"] {
            NSTimeZone.default = TimeZone(identifier: zone)!
            let harness = try Harness(baseline())
            var rows: [[String: Any]] = []
            for input in inputs {
                let ok = harness.canvas.commit(field: .awakeningDate, text: input)
                var row: [String: Any] = ["input": input, "ok": ok]
                if ok {
                    row["seconds"] = harness.store.profile.awakeningDate.timeIntervalSinceReferenceDate
                    harness.canvas.perform(actionID: "profile:awakeningDate")
                    row["formatted"] = (harness.requests.last?["text"] as? String) ?? ""
                    harness.requests.removeAll()
                }
                rows.append(row)
            }
            for seconds in [-63113904000.0, -13197600000.0, 0.0, 1e9, 2.5e11, -1e11] as [Double] {
                try harness.store.update { $0.awakeningDate = Date(timeIntervalSinceReferenceDate: seconds) }
                harness.canvas.perform(actionID: "profile:awakeningDate")
                rows.append(["seconds": seconds, "formattedOnly": (harness.requests.last?["text"] as? String) ?? ""])
                harness.requests.removeAll()
            }
            result.append(["zone": zone, "rows": rows])
        }
        NSTimeZone.default = TimeZone(identifier: "Asia/Shanghai")!
        return result
    }

    static func hoursCases() throws -> [[String: Any]] {
        var result: [[String: Any]] = []
        for (stored, live) in [(0.0, 0.0), (3599.0, 0.0), (0.0, 17.99), (0.0, 18.0), (35.99, 0), (18000, 17999), (1e12, 0),
                               (0, Double.infinity), (100, Double.nan), (0, -50), (123456.789, 123456.79), (3600 * 99.995, 0), (0, 3600 * 0.005)] {
            var profile = baseline(); profile.accumulatedWorkSeconds = stored
            let harness = try Harness(profile, live: live)
            let liveValue: Any; if live.isNaN { liveValue = "nan" } else if live.isInfinite { liveValue = "inf" } else { liveValue = live }
            result.append(["stored": stored, "live": liveValue, "text": harness.workText()])
        }
        return result
    }

    /// SystemEventRecorder.receiveProfileCrop over committed (background, thumbnail) zooms.
    static func cropEvents() -> [[String: Any]] {
        let log = SystemEventLog(directory: nil), recorder = SystemEventRecorder(log: log)
        let inputs: [(Double, Double)] = [(1, 1), (1, 1), (2, 1), (2, 3), (4, 5), (4, 5), (30, 5), (20, 5), (20, -1), (.nan, 1), (1, .infinity), (1, 1)]
        var rows: [[String: Any]] = []
        for (background, thumbnail) in inputs {
            let before = log.events.count
            recorder.receiveProfileCrop(backgroundZoom: background, thumbnailZoom: thumbnail)
            func encode(_ v: Double) -> Any { if v.isNaN { return "nan" }; if v.isInfinite { return v > 0 ? "inf" : "-inf" }; return v }
            let recorded = log.events.count > before
            let event: Any = recorded ? (log.events[0].metadata["target"] ?? "") : NSNull()
            let kind: Any = recorded ? log.events[0].kind.rawValue : NSNull()
            rows.append(["background": encode(background), "thumbnail": encode(thumbnail), "event": event, "kind": kind])
        }
        return rows
    }

    /// Synthetic sRGB gradient photo written to the temporary fixture tree.
    static func syntheticImage(_ name: String, width: Int, height: Int, seed: UInt8) throws -> URL {
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        for y in 0..<height { for x in 0..<width {
            let i = (y * width + x) * 4
            pixels[i] = UInt8((x * 255) / max(1, width - 1)); pixels[i + 1] = UInt8((y * 255) / max(1, height - 1))
            pixels[i + 2] = UInt8((Int(seed) + x * 3 + y * 5) % 256); pixels[i + 3] = 255
        } }
        let provider = CGDataProvider(data: Data(pixels) as CFData)!
        let image = CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
            provider: provider, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        let url = root.appendingPathComponent(name)
        try NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:])!.write(to: url)
        return url
    }

    /// Model-layer exports of the actual canvas for the presentation port.
    static func presentation(_ output: URL) throws -> [[String: Any]] {
        let encoder = try ModuleReferenceLayerEncoder(output: output)
        var states: [[String: Any]] = []
        func emit(_ name: String, _ h: Harness, language: String = "english", dark: Bool = true) throws {
            states.append(["name": name, "language": language, "dark": dark, "profile": try encoded(h.store.profile),
                "hidden": h.canvas.isTextHidden, "popover": h.canvas.popoverBounds.map(rect) as Any? ?? NSNull(), "locked": h.canvas.gameSyncActive,
                "status": h.canvas.accessibilityStatus, "hours": h.workText(),
                "layer": try encoder.encode(h.canvas.layer, id: "profile"), "background": try encoder.encode(h.canvas.backgroundLayer, id: "profile.background")])
        }
        func make(_ dark: Bool = true, _ change: ((inout UserProfile) -> Void)? = nil) throws -> Harness {
            var profile = baseline(); change?(&profile); return try Harness(profile, dark: dark)
        }
        try emit("default", try make())
        try emit("light", try make(false), dark: false)
        do { let h = try make(); h.canvas.perform(actionID: "profile:menu"); try emit("identity", h) }
        do { let h = try make(); h.canvas.gameSyncActive = true; h.canvas.perform(actionID: "profile:menu"); try emit("lockedIdentity", h) }
        do { let h = try make(); h.canvas.gameSyncActive = true; try emit("locked", h) }
        do { let h = try make(); h.canvas.perform(actionID: "profile:menu"); h.canvas.perform(actionID: "profile:portraitMenu"); try emit("portrait", h) }
        do { let h = try make(); h.canvas.perform(actionID: "profile:backgroundMenu"); try emit("background", h) }
        do { let h = try make { $0.themeColorHex = "6EDFE8" }; h.canvas.perform(actionID: "profile:backgroundMenu"); h.canvas.perform(actionID: "profile:themeMenu"); try emit("theme", h) }
        do { let h = try make(); h.canvas.perform(actionID: "profile:visibility"); try emit("hidden", h) }
        do { let h = try make { $0.showsBirthday = true; $0.birthdayMonth = 2; $0.birthdayDay = 29 }; try emit("birthday", h) }
        do { let h = try make(); _ = h.canvas.commit(field: .tag, text: "a b"); try emit("error", h) }
        do { let h = try make { $0.themeColorHex = "C9A2FF"; $0.permissionLevel = 1; $0.explorationLevel = 3; $0.operatorsCount = 1234567; $0.introduction = String(repeating: "长介绍 wraps across lines. ", count: 6) }; try emit("themed", h) }
        do { let h = try make { $0.name = "Ångström 管理员 한국어 extra"; $0.tag = "ABCDEFGHIJ"; $0.introduction = "" }; try emit("long", h) }
        do {
            let h = try make()
            let avatar = try syntheticImage("avatar.png", width: 300, height: 200, seed: 7), background = try syntheticImage("background.png", width: 640, height: 360, seed: 99)
            try require(h.canvas.importImage(from: avatar, kind: .avatar) && h.canvas.importImage(from: background, kind: .background), "Synthetic image import")
            try emit("images", h)
            h.canvas.perform(actionID: "profile:backgroundMenu"); _ = h.canvas.setSlider(field: .backgroundWidth, value: 800); _ = h.canvas.setSlider(field: .backgroundOffsetX, value: -120); _ = h.canvas.setSlider(field: .backgroundZoom, value: 2.5)
            try emit("imagesBackground", h)
            h.canvas.dismissPopover(); h.canvas.perform(actionID: "profile:visibility"); try emit("imagesHidden", h)
            let light = try make(false)
            try require(light.canvas.importImage(from: background, kind: .background), "Synthetic background import")
            try emit("lightImages", light, dark: false)
        }
        L10n.language = .simplifiedChinese
        do { let h = try make(); try emit("chinese", h, language: "simplifiedChinese") }
        do { let h = try make(); h.canvas.perform(actionID: "profile:backgroundMenu"); try emit("chineseBackground", h, language: "simplifiedChinese") }
        L10n.language = .english
        return states
    }

    // MARK: ID card and portrait artwork (HUDSourceProfileArtwork, HUDPortraitArtwork)
    static func rawRGBA(_ image: CGImage) throws -> [String: Any] {
        // Exact stored bytes when possible: straight or premultiplied RGBA8.
        guard image.bitsPerPixel == 32, image.bitsPerComponent == 8, let data = image.dataProvider?.data as Data? else { throw HUDSourceError.invalid("Unsupported artwork pixels") }
        var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
        data.withUnsafeBytes { raw in
            let input = raw.bindMemory(to: UInt8.self)
            for y in 0..<image.height { for x in 0..<(image.width * 4) { bytes[y * image.width * 4 + x] = input[y * image.bytesPerRow + x] } }
        }
        let alpha = image.alphaInfo
        let order = image.bitmapInfo.intersection(.byteOrderMask)
        return ["width": image.width, "height": image.height, "alphaInfo": alpha.rawValue, "byteOrder": order.rawValue, "rgba": Data(bytes).base64EncodedString()]
    }
    static func premultiplied(_ image: CGImage) throws -> [String: Any] {
        guard let context = CGContext(data: nil, width: image.width, height: image.height, bitsPerComponent: 8, bytesPerRow: image.width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { throw HUDSourceError.invalid("Artwork context") }
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        let data = Data(bytes: context.data!, count: image.width * image.height * 4)
        return ["width": image.width, "height": image.height, "rgba": data.base64EncodedString()]
    }
    static func syntheticCG(width: Int, height: Int, seed: Int, alpha: Bool) -> CGImage {
        var pixels = [UInt8](repeating: 0, count: width * height * 4)
        for y in 0..<height { for x in 0..<width {
            let i = (y * width + x) * 4
            pixels[i] = UInt8((x * 251 / max(1, width - 1) + seed) % 256); pixels[i + 1] = UInt8((y * 241 / max(1, height - 1) + seed * 3) % 256)
            pixels[i + 2] = UInt8((x * y + seed * 7) % 256); pixels[i + 3] = alpha ? UInt8((x * 37 + y * 11 + seed) % 256) : 255
        } }
        return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: (alpha ? CGImageAlphaInfo.last : CGImageAlphaInfo.noneSkipLast).rawValue),
            provider: CGDataProvider(data: Data(pixels) as CFData)!, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
    }
    static func artwork() throws -> [String: Any] {
        // A synthetic bottom-origin BGRA mip with yellow card edges, neutral
        // panels, translucent pixels and fully transparent margins.
        let textureWidth = 64, textureHeight = 30
        var bgra = [UInt8](repeating: 0, count: textureWidth * textureHeight * 4)
        for y in 0..<textureHeight { for x in 0..<textureWidth {
            let i = (y * textureWidth + x) * 4
            let edge = x < 6 || y < 4, inner = x > 10 && x < 50 && y > 6 && y < 24
            let a = (x + y) % 9 == 0 ? 0 : edge ? 255 : inner ? 200 : 140
            let (r, g, b): (Int, Int, Int) = edge ? (250, 212, 31 + x) : inner ? (60 + x, 64 + y, 70) : (200 + y, 190 + y, 20 + x)
            bgra[i] = UInt8(b * a / 255); bgra[i + 1] = UInt8(g * a / 255); bgra[i + 2] = UInt8(r * a / 255); bgra[i + 3] = UInt8(a)
            // Source mips hold premultiplied-looking values; keep straight copies too.
            bgra[i] = UInt8(min(255, b)); bgra[i + 1] = UInt8(min(255, g)); bgra[i + 2] = UInt8(min(255, r))
        } }
        let sprite = CGRect(x: 3, y: 2, width: 53, height: 21)
        let source = try HUDSourceProfileArtwork.backgroundArtwork(bgra: Data(bgra), textureWidth: textureWidth, textureHeight: textureHeight, spriteRect: sprite)
        var result: [String: Any] = ["mip": ["width": textureWidth, "height": textureHeight, "bgra": Data(bgra).base64EncodedString()],
            "sprite": rect(sprite), "source": try rawRGBA(source)]
        var accents: [[String: Any]] = []
        for accent in [NSColor(srgbRed: 250 / 255, green: 212 / 255, blue: 31 / 255, alpha: 1), NSColor(srgbRed: 110 / 255, green: 223 / 255, blue: 232 / 255, alpha: 1),
                       NSColor(srgbRed: 0.2, green: 0.6, blue: 0.9, alpha: 1), NSColor(srgbRed: 1, green: 0, blue: 0.5, alpha: 1)] {
            let rgb = accent.usingColorSpace(.sRGB)!
            let themed = try HUDSourceProfileArtwork.themedBackgroundArtwork(source, accent: accent)
            let hover = try HUDSourceProfileArtwork.hoverArtwork(source, accent: accent)
            let photo = syntheticCG(width: 53, height: 21, seed: 5, alpha: false)
            let composited = try HUDSourceProfileArtwork.compositedBackground(photo, artwork: themed)
            let texture = try HUDSourceProfileArtwork.texturePixels(composited)
            accents.append(["accent": [Double(rgb.redComponent), Double(rgb.greenComponent), Double(rgb.blueComponent)],
                "themed": try rawRGBA(themed), "hover": try rawRGBA(hover), "photo": try rawRGBA(photo), "composited": try rawRGBA(composited),
                "texture": ["width": texture.width, "height": texture.height, "rgba": texture.rgba.base64EncodedString()],
                "themedTexture": (try HUDSourceProfileArtwork.texturePixels(themed)).rgba.base64EncodedString(),
                "hoverTexture": (try HUDSourceProfileArtwork.texturePixels(hover)).rgba.base64EncodedString()])
        }
        result["accents"] = accents
        // HUDPortraitArtwork.renderedImage: oriented crop + Core Image Lanczos.
        var portraits: [[String: Any]] = []
        // A smooth photograph-like field with one hard-edged block: geometry
        // and filtering are visible without modular wrap discontinuities.
        let photo: CGImage = {
            let width = 60, height = 40
            var pixels = [UInt8](repeating: 255, count: width * height * 4)
            for y in 0..<height { for x in 0..<width {
                let i = (y * width + x) * 4, dx = Double(x) - 21, dy = Double(y) - 13
                let block = x >= 38 && x < 47 && y >= 22 && y < 31
                pixels[i] = block ? 250 : UInt8(30 + x * 3); pixels[i + 1] = block ? 245 : UInt8(20 + y * 5)
                pixels[i + 2] = block ? 240 : UInt8(max(0, min(255, 220 - Int((dx * dx + dy * dy).squareRoot() * 5))))
            } }
            return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.noneSkipLast.rawValue),
                provider: CGDataProvider(data: Data(pixels) as CFData)!, decode: nil, shouldInterpolate: true, intent: .defaultIntent)!
        }()
        for (orientation, zoom, offset, target, scale) in [(Int32(1), 1.0, CGPoint.zero, CGSize(width: 20, height: 20), CGFloat(1)),
                                                           (Int32(6), 1.7, CGPoint(x: 0.3, y: -0.5), CGSize(width: 20, height: 15), CGFloat(1)),
                                                           (Int32(3), 2.5, CGPoint(x: -1, y: 1), CGSize(width: 12, height: 18), CGFloat(2)),
                                                           (Int32(8), 1.0, CGPoint(x: 0.5, y: 0.5), CGSize(width: 30, height: 30), CGFloat(1)),
                                                           (Int32(2), 1.0, CGPoint.zero, CGSize(width: 16, height: 16), CGFloat(1)),
                                                           (Int32(4), 1.0, CGPoint.zero, CGSize(width: 16, height: 16), CGFloat(1)),
                                                           (Int32(5), 1.0, CGPoint.zero, CGSize(width: 16, height: 16), CGFloat(1)),
                                                           (Int32(7), 1.0, CGPoint.zero, CGSize(width: 16, height: 16), CGFloat(1)),
                                                           (Int32(1), 1.0, CGPoint.zero, CGSize(width: 80, height: 60), CGFloat(1))] {
            let output = HUDPortraitArtwork.renderedImage(photo, targetSize: target, zoom: zoom, offset: offset, contentsScale: scale, orientation: orientation)!
            portraits.append(["orientation": orientation, "zoom": zoom, "offset": [Double(offset.x), Double(offset.y)], "target": [Double(target.width), Double(target.height)],
                              "scale": Double(scale), "output": try premultiplied(output)])
        }
        result["portraitSource"] = try rawRGBA(photo); result["portraits"] = portraits
        // setDesktopProfile default avatar: the photo layer rendered at 272x272.
        let portrait = HUDPortraitArtwork.makeLayer(image: nil, profile: nil, size: CGSize(width: 136, height: 136), ink: .white, accent: .white, contentsScale: 2, includeFrame: false)
        let raster = CGContext(data: nil, width: 272, height: 272, bitsPerComponent: 8, bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        raster.translateBy(x: 0, y: 272); raster.scaleBy(x: 2, y: -2)
        portrait.sublayers?.first?.render(in: raster)
        result["defaultAvatar"] = try premultiplied(raster.makeImage()!)
        return result
    }

    static func run() throws {
        try require(Thread.isMainThread, "Main thread required")
        let args = Array(CommandLine.arguments.dropFirst())
        try require(args.count == 1 || args.count == 2, "Usage: profile-reference OUTPUT.json [PRESENTATION_DIR]")
        guard ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else { throw HUDSourceError.invalid("Use isolated profile_reference.sh wrapper") }
        root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldProfileReference-" + UUID().uuidString, isDirectory: true)
        try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
        defer { try? FileManager.default.removeItem(at: root) }
        NSApplication.shared.setActivationPolicy(.prohibited); NSApp.appearance = NSAppearance(named: .darkAqua)
        NSTimeZone.default = TimeZone(identifier: "Asia/Shanghai")!
        var config = AppConfiguration.defaults
        config.language = .english; config.theme = .dark; config.ambientAnimation = false; config.reduceMotion = true
        HUDRuntimeAppearance.configuration = config; L10n.language = .english

        let steps = scenario()
        let harness = try Harness(baseline())
        var rows: [[String: Any]] = []
        for step in steps {
            var row = step
            row["result"] = try harness.run(step)
            row["state"] = try harness.snapshot()
            rows.append(row)
        }
        try require(harness.requests.isEmpty, "Requests drained")
        let fields = PersonalProfileField.allCases.map { field -> [String: Any] in
            ["id": field.rawValue, "rect": rect(PersonalProfileCanvas(store: nil).fieldRect(field)), "textLimit": field.textLimit as Any? ?? NSNull(),
             "numeric": field.isNumeric, "date": field.isDate, "geometry": field.isGeometry, "zoom": field.isZoom]
        }
        let output: [String: Any] = [
            "schemaVersion": 1, "timeZone": "Asia/Shanghai", "baseline": try encoded(baseline()),
            "presets": HUDSettingsController.presetAccentHexes, "fields": fields, "scenario": rows,
            "labels": try labels(), "decode": try decodeCases(), "cropEvents": cropEvents(), "dates": try dateCases(), "hours": try hoursCases(),
            "staticRects": ["menu": rect(PersonalProfileCanvas.menuRect), "background": rect(PersonalProfileCanvas.backgroundRect),
                "visibility": rect(PersonalProfileCanvas.visibilityRect), "dateLabel": rect(PersonalProfileCanvas.dateLabelRect),
                "dateValue": rect(PersonalProfileCanvas.dateValueRect), "introduction": rect(PersonalProfileCanvas.introductionRect),
                "introductionAction": rect(PersonalProfileCanvas.introductionActionRect), "portrait": rect(PersonalProfileCanvas.portraitRect)],
            "isolation": ["windowCreated": !NSApp.windows.isEmpty, "temporaryStoresOnly": true, "realProfileRead": false],
        ]
        try require(NSApp.windows.isEmpty, "Fixture unexpectedly created a window")
        try JSONSerialization.data(withJSONObject: output, options: [.sortedKeys, .withoutEscapingSlashes])
            .write(to: URL(fileURLWithPath: args[0]), options: .atomic)
        if args.count == 2 {
            let directory = URL(fileURLWithPath: args[1], isDirectory: true)
            let states = try presentation(directory)
            try require(NSApp.windows.isEmpty, "Presentation export unexpectedly created a window")
            try JSONSerialization.data(withJSONObject: ["states": states], options: [.sortedKeys, .withoutEscapingSlashes])
                .write(to: directory.appendingPathComponent("presentation.json"), options: .atomic)
            try JSONSerialization.data(withJSONObject: try artwork(), options: [.sortedKeys, .withoutEscapingSlashes])
                .write(to: directory.appendingPathComponent("artwork.json"), options: .atomic)
        }
        print("Profile reference: \(rows.count) canvas steps, \(fields.count) fields")
    }
    static func main() { do { try run() } catch { fputs("Profile reference failed: \(error)\n", stderr); exit(1) } }
}
