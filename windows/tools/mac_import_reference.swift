import AppKit

// Oracle for the offline Mac -> Windows import. Compiled together with the
// unchanged Sources/*.swift (main.swift excluded). It only touches a private
// CFFIXED_USER_HOME, throwaway preference suites and its own temporary files.
@main private enum MacImportReference {
    static func hex(_ value: Double) -> String {
        let bits = String(value.bitPattern, radix: 16)
        return String(repeating: "0", count: 16 - bits.count) + bits
    }
    static func double(_ text: String) -> Double { Double(bitPattern: UInt64(text, radix: 16)!) }

    /// Exact typed form shared with windows/core/migration/plist.hpp.
    static func typed(_ value: Any) throws -> Any {
        switch value {
        case let text as String:
            // Apple keeps unpaired UTF-16 surrogates; they have no UTF-8 form.
            if (text as NSString).cString(using: String.Encoding.utf8.rawValue) == nil { return ["unrepresentableString": (text as NSString).length] }
            return ["string": text]
        case let bytes as Data: return ["data": bytes.base64EncodedString()]
        case let date as Date: return ["date": hex(date.timeIntervalSinceReferenceDate)]
        case let array as [Any]: return ["array": try array.map(typed)]
        case let dictionary as [String: Any]: return ["dict": try dictionary.mapValues(typed)]
        case let number as NSNumber:
            if CFGetTypeID(number) == CFBooleanGetTypeID() { return ["bool": number.boolValue] }
            if CFNumberIsFloatType(number) { return ["real": hex(number.doubleValue)] }
            let text = number.stringValue
            guard UInt64(text) != nil || Int64(text) != nil else { throw NSError(domain: "oracle", code: 1) }
            return ["int": text]
        default: throw NSError(domain: "oracle", code: 2, userInfo: [NSLocalizedDescriptionKey: "Unsupported \(type(of: value))"])
        }
    }
    static func value(_ typed: [String: Any]) -> Any {
        precondition(typed.count == 1)
        let (kind, payload) = typed.first!
        switch kind {
        case "bool": return (payload as! NSNumber).boolValue
        case "int":
            let text = payload as! String
            if let signed = Int64(text) { return NSNumber(value: signed) }
            return NSNumber(value: UInt64(text)!)
        case "real": return NSNumber(value: double(payload as! String))
        case "real32": return NSNumber(value: Float(double(payload as! String)))
        case "string": return payload as! String
        case "data": return Data(base64Encoded: payload as! String)!
        case "date": return Date(timeIntervalSinceReferenceDate: double(payload as! String))
        case "array": return (payload as! [Any]).map { value($0 as! [String: Any]) }
        case "dict": return (payload as! [String: Any]).mapValues { value($0 as! [String: Any]) }
        default: preconditionFailure("Unknown typed kind \(kind)")
        }
    }
    static func decode(_ bytes: Data) -> [String: Any] {
        do {
            var format = PropertyListSerialization.PropertyListFormat.binary
            let decoded = try PropertyListSerialization.propertyList(from: bytes, options: [], format: &format)
            return ["accepted": true, "format": format == .binary ? "binary" : format == .xml ? "xml" : "other", "value": try typed(decoded)]
        } catch { return ["accepted": false] }
    }

    static func plistCases(_ input: [String: Any]) throws -> [String: Any] {
        var encoded: [[String: Any]] = []
        for row in input["values"] as! [[String: Any]] {
            let original = value(row["value"] as! [String: Any])
            var result: [String: Any] = ["name": row["name"] as! String]
            for (name, format) in [("binary", PropertyListSerialization.PropertyListFormat.binary), ("xml", .xml)] {
                if let bytes = try? PropertyListSerialization.data(fromPropertyList: original, format: format, options: 0) {
                    result[name] = bytes.base64EncodedString(); result[name + "Decoded"] = decode(bytes)
                } else { result[name] = NSNull() }
            }
            encoded.append(result)
        }
        var raw: [[String: Any]] = []
        for row in input["raw"] as! [[String: Any]] {
            let bytes = Data(base64Encoded: row["bytes"] as! String)!
            var result = row; result["apple"] = decode(bytes); raw.append(result)
        }
        return ["encoded": encoded, "raw": raw]
    }

    static func configuration(_ c: AppConfiguration) -> [String: Any] {
        [
            "displayMode": c.displayMode.rawValue, "displayDuration": hex(c.displayDuration), "accentHex": c.accentHex,
            "theme": c.theme.rawValue, "scale": hex(c.scale), "placement": c.placement.rawValue,
            "customScreenID": c.customPosition.screenID.map { NSNumber(value: $0) } ?? NSNull(),
            "customPositionX": hex(c.customPosition.x), "customPositionY": hex(c.customPosition.y),
            "language": c.language.rawValue, "hudScale": hex(c.hudScale), "hudOffsetX": hex(c.hudOffsetX), "hudOffsetY": hex(c.hudOffsetY),
            "parallaxIntensity": hex(c.parallaxIntensity), "perspectiveIntensity": hex(c.perspectiveIntensity),
            "backgroundDarkness": hex(c.backgroundDarkness), "blurAmount": hex(c.blurAmount),
            "reduceMotion": c.reduceMotion, "ambientAnimation": c.ambientAnimation, "closeOnFocusLost": c.closeOnFocusLost,
            "openOnActiveDisplay": c.openOnActiveDisplay, "hudDisplayUUID": c.hudDisplayUUID ?? NSNull(),
            "hudDisplayName": c.hudDisplayName ?? NSNull(), "launchAtLogin": c.launchAtLogin,
            "batteryAlertsEnabled": c.batteryAlertsEnabled, "devicePopupEnabled": c.devicePopupEnabled,
            "lowPowerVisualMode": c.lowPowerVisualMode, "applicationIcon": c.applicationIcon.rawValue,
            "clockFormat": c.clockFormat.rawValue, "clockStyle": c.clockStyle.rawValue, "centerLogo": c.centerLogo.rawValue,
            "centerLogoRevision": c.centerLogoRevision ?? NSNull(), "alertMetric": c.alertMetric.rawValue,
            "summonShortcut": ["keyCode": Int(c.summonShortcut.keyCode), "modifiers": Int(c.summonShortcut.modifiers.rawValue)]
        ]
    }

    /// Runs the unchanged ConfigurationStore/OrbiPomSession over a fresh suite
    /// holding exactly `domain`, as decoded back from an exported plist.
    static func evaluate(_ domain: [String: Any]) -> [String: Any] {
        let suite = "EndfieldMacImportReference." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        defer { defaults.removePersistentDomain(forName: suite) }
        for (key, value) in domain { defaults.set(value, forKey: key) }
        var probes: [String: Any] = [:]
        for key in domain.keys {
            probes[key] = ["string": defaults.string(forKey: key) ?? NSNull(), "double": hex(defaults.double(forKey: key)),
                           "bool": defaults.bool(forKey: key), "integer": String(defaults.integer(forKey: key))]
        }
        let markerAbsent = defaults.object(forKey: "hudSettingsSchemaVersion") == nil
        let store = ConfigurationStore(defaults: defaults)
        let best = OrbiPomSession(defaults: defaults).bestScore
        return ["markerAbsent": markerAbsent, "configuration": configuration(store.configuration), "bestScore": best,
                "hasLaunched": defaults.bool(forKey: "hasLaunched"), "probes": probes]
    }

    static func settingsCases(_ input: [[String: Any]]) throws -> [[String: Any]] {
        var output: [[String: Any]] = []
        for row in input {
            let suite = "EndfieldMacImportReference." + UUID().uuidString
            let defaults = UserDefaults(suiteName: suite)!
            defaults.removePersistentDomain(forName: suite)
            defer { defaults.removePersistentDomain(forName: suite) }
            let values = row["values"] as! [String: Any]
            for (key, typedValue) in values { defaults.set(value(typedValue as! [String: Any]), forKey: key) }
            let domain = defaults.persistentDomain(forName: suite) ?? [:]
            precondition(Set(domain.keys) == Set(values.keys), "Suite domain must contain only synthetic keys")
            var result: [String: Any] = ["name": row["name"] as! String]
            for (name, format) in [("binary", PropertyListSerialization.PropertyListFormat.binary), ("xml", .xml)] {
                let bytes = try PropertyListSerialization.data(fromPropertyList: domain, format: format, options: 0)
                var decodedFormat = format
                let decoded = try PropertyListSerialization.propertyList(from: bytes, options: [], format: &decodedFormat) as! [String: Any]
                result[name] = bytes.base64EncodedString()
                result[name + "Result"] = evaluate(decoded)
            }
            output.append(result)
        }
        return output
    }


    // ---- golden export: the unchanged Mac stores write synthetic data ----
    static func png(width: Int, height: Int) throws -> Data {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: width * 4,
                                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(CGColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)); context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        context.setFillColor(CGColor(srgbRed: 0.1, green: 0.1, blue: 0.1, alpha: 1)); context.fill(CGRect(x: 4, y: 4, width: width / 2, height: height / 2))
        return try encode(context.makeImage()!, type: "public.png")
    }
    static func encode(_ image: CGImage, type: String) throws -> Data {
        let data = NSMutableData()
        let destination = CGImageDestinationCreateWithData(data, type as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw NSError(domain: "oracle", code: 3) }
        return data as Data
    }
    /// Replace machine-specific bookmark bytes and file identities with
    /// synthetic values; the owning store must re-encode the result itself.
    static func sanitize(_ url: URL, bookmarks: inout Int) throws {
        var json = try JSONSerialization.jsonObject(with: Data(contentsOf: url)) as! [String: Any]
        var items = json["items"] as? [[String: Any]] ?? json["books"] as! [[String: Any]]
        for index in items.indices {
            bookmarks += 1
            items[index]["bookmark"] = Data("synthetic-bookmark-\(bookmarks)".utf8).base64EncodedString()
            for key in ["lastKnownPath", "path"] where (items[index][key] as? String)?.hasPrefix("/System/") == false {
                items[index][key] = "/Users/doctor/Documents/" + ((items[index][key] as! NSString).lastPathComponent)
            }
            if items[index]["identity"] != nil { items[index]["identity"] = ["inode": 4242 + index, "device": 16777220, "volumeUUID": "00000000-0000-4000-8000-0000000000AA"] }
        }
        json[json["items"] != nil ? "items" : "books"] = items
        try JSONSerialization.data(withJSONObject: json).write(to: url)
    }
    static func golden(_ work: URL) throws {
        let fm = FileManager.default
        let root = work.appendingPathComponent("EndfieldCharge", isDirectory: true)
        let assets = work.appendingPathComponent("assets", isDirectory: true)
        try fm.createDirectory(at: assets, withIntermediateDirectories: true)
        let picture = assets.appendingPathComponent("picture.png"); try png(width: 64, height: 48).write(to: picture)
        let avatarSource = CGImageSourceCreateWithData(try png(width: 40, height: 40) as CFData, nil)!
        let avatar = assets.appendingPathComponent("avatar.jpg"); try encode(CGImageSourceCreateImageAtIndex(avatarSource, 0, nil)!, type: "public.jpeg").write(to: avatar)
        let bookFile = assets.appendingPathComponent("endfield.txt"); try Data("第一章\nTalos-II\n".utf8).write(to: bookFile)
        let report = assets.appendingPathComponent("report.pdf"); try Data("%PDF-1.4 synthetic".utf8).write(to: report)
        let other = assets.appendingPathComponent("other.txt"); try Data("x".utf8).write(to: other)
        let synthetic = NotesMediaReference(kind: .image, bookmark: Data("synthetic-media-bookmark".utf8), isSecurityScoped: true,
            lastKnownPath: "/Users/doctor/Pictures/rhodes.png", displayName: "rhodes.png", pixelWidth: 64, pixelHeight: 48, duration: nil, frameCount: 1)
        let base = Date(timeIntervalSinceReferenceDate: 700_000_000.123_456)
        var expected: [String: Any] = [:]

        // Notes: text with UTF-16 rich runs, checklist, managed image, external media, drawing.
        let notesDirectory = root.appendingPathComponent("Notes", isDirectory: true)
        let notes = try NotesStore(directory: notesDirectory)
        let text = "Doctor 博士 🚀 e\u{301}"
        let rich = NotesRichText(version: 1, runs: [NotesTextRun(location: 7, length: 2, style: NotesTextStyle(fontName: "Georgia", fontSize: 18, color: NotesRGBA(red: 1, green: 0.5, blue: 0), bold: true)),
                                                   NotesTextRun(location: 10, length: 2, style: NotesTextStyle(italic: true))])
        precondition(rich.isValid(for: text))
        try notes.upsert(CanvasNote(kind: .text, text: text, createdAt: base, richText: rich))
        try notes.upsert(CanvasNote(kind: .todo, items: [NoteChecklistItem(text: "Sanity", isChecked: true), NoteChecklistItem(text: "Originium")], width: 180, zIndex: 1, createdAt: base.addingTimeInterval(1)))
        let managed = UUID().uuidString + ".png"
        try fm.createDirectory(at: notesDirectory.appendingPathComponent("Images"), withIntermediateDirectories: true)
        try png(width: 32, height: 24).write(to: notesDirectory.appendingPathComponent("Images").appendingPathComponent(managed))
        try notes.upsert(CanvasNote(kind: .image, imageName: managed, zIndex: 2, createdAt: base.addingTimeInterval(2)))
        try notes.upsert(CanvasNote(kind: .image, zIndex: 3, createdAt: base.addingTimeInterval(3), media: synthetic))
        var drawing = NotesDrawing()
        precondition(drawing.append(NotesDrawingStroke(points: [NotesDrawingPoint(x: 0.1, y: 0.2), NotesDrawingPoint(x: 0.9, y: 0.8)], width: 4, color: NotesRGBA(red: 0.2, green: 0.3, blue: 0.4))))
        try notes.upsert(CanvasNote(kind: .drawing, zIndex: 4, createdAt: base.addingTimeInterval(4), drawing: drawing))

        // Archive: one journal entry with an external attachment.
        let archive = ArchiveStore(directory: root.appendingPathComponent("Archive", isDirectory: true))
        try archive.save(ArchiveEntry(template: .journal, title: "Rhodes Island 档案", date: base, body: "Body 🚀", media: [synthetic], modified: base.addingTimeInterval(60)))

        // Profile with an original-bytes avatar and a PNG background.
        let profile = try UserProfileStore(directory: root.appendingPathComponent("Profile", isDirectory: true), now: base)
        try profile.update { $0.name = "博士Doctor🚀"; $0.tag = "1234"; $0.introduction = "Talos-II e\u{301}" }
        try profile.importImage(from: avatar, kind: .avatar)
        try profile.importImage(from: picture, kind: .background)
        try profile.setWorkSeconds(3600.5)

        var bookmarks = 0
        // File shelf: Mac bookmarks + inode identity (sanitized, then re-encoded by the store).
        let shelfDirectory = root.appendingPathComponent("FileShelf", isDirectory: true)
        do { let shelf = try FileShelfStore(directory: shelfDirectory); try shelf.add(urls: [report]) }
        try sanitize(shelfDirectory.appendingPathComponent("shelf.json"), bookmarks: &bookmarks)
        do {
            let shelf = try FileShelfStore(directory: shelfDirectory)
            try shelf.add(urls: [other]); try shelf.remove(id: shelf.items.last!.id)
            precondition(shelf.items.count == 1)
        }
        // Reader: book with progress and a bookmark (sanitized, then re-encoded by ReaderStore).
        let readerDirectory = root.appendingPathComponent("Reader", isDirectory: true)
        let bookID: UUID
        do { let reader = try ReaderStore(directory: readerDirectory); bookID = try reader.add(url: bookFile, title: "终末地").id }
        try sanitize(readerDirectory.appendingPathComponent("library.json"), bookmarks: &bookmarks)
        do {
            let reader = try ReaderStore(directory: readerDirectory)
            try reader.saveProgress(ReaderLocation(section: 1, block: 2, character: 3), progress: 0.4, id: bookID)
            try reader.toggleBookmark(id: bookID)
        }
        // Calendar in UTC.
        let calendar = try HUDCalendarStore(directory: root.appendingPathComponent("Calendar", isDirectory: true))
        try calendar.save(HUDCalendarEvent(title: "Contingency", details: "Bring sanity", day: HUDCalendarDay(year: 2030, month: 11, day: 3),
                                           created: base, modified: base), now: base, zone: TimeZone(identifier: "UTC")!)
        // World map (current v4).
        let map = try WorldMapStore(directory: root.appendingPathComponent("WorldMap", isDirectory: true))
        _ = try map.addPin(x: 0.25, y: 0.75)
        // App shortcuts: two system apps, one removed after sanitizing so the store re-encodes.
        let shortcutDirectory = root.appendingPathComponent("AppShortcuts", isDirectory: true)
        do {
            let shortcuts = try AppShortcutStore(directory: shortcutDirectory)
            try shortcuts.save(candidate: try shortcuts.inspect(url: URL(fileURLWithPath: "/System/Applications/Calculator.app")), name: "Calculator", iconPreset: .original)
            try shortcuts.save(candidate: try shortcuts.inspect(url: URL(fileURLWithPath: "/System/Applications/Chess.app")), name: "Chess", iconPreset: .game)
        }
        try sanitize(shortcutDirectory.appendingPathComponent("shortcuts.json"), bookmarks: &bookmarks)
        do { let shortcuts = try AppShortcutStore(directory: shortcutDirectory); try shortcuts.remove(id: shortcuts.items.last!.id) }
        // Event log.
        let log = SystemEventLog(directory: root.appendingPathComponent("EventLog", isDirectory: true))
        log.record(kind: .overlayOpened); log.record(kind: .noteAction, metadata: ["action": "created"])
        precondition(log.flushSynchronously())
        // Account cache: synthetic roles only; credentials never exist in this file.
        let role = HypergryphRole(region: .mainland, game: .endfield, bindingUID: "9007199254740993", roleID: "18446744073709551619", serverID: "1", name: "Doctor#1234", isDefault: true)
        var record = HypergryphAccountController.RegionRecord()
        record.linked = true; record.roles = [role]; record.selectedRoleID = role.id; record.bindingsAt = base
        record.snapshots[role.id] = HypergryphProfileSnapshot(role: role, observedAt: base, name: "Doctor#1234", level: 60,
            stamina: HypergryphStamina(current: 120, maximum: 240, fullRecoveryAt: base.addingTimeInterval(50_000), serverObservedAt: base))
        var cache = HypergryphAccountController.Cache(); cache.syncProfile = true; cache.records = ["mainland": record]
        let cacheURL = root.appendingPathComponent("Account/profile-cache.json")
        try fm.createDirectory(at: cacheURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        try JSONEncoder().encode(cache).write(to: cacheURL)
        expected["profileSyncLocked"] = HypergryphAccountController(fileURL: cacheURL, vault: HypergryphMemoryCredentialVault()).gameSyncActive
        // Center logo, then settings that select it.
        let revision = try HUDCenterLogoStore(directory: root.appendingPathComponent("CenterLogo", isDirectory: true)).importImage(from: picture)
        let suite = "EndfieldMacImportGolden." + UUID().uuidString
        let defaults = UserDefaults(suiteName: suite)!
        defaults.removePersistentDomain(forName: suite)
        defer { defaults.removePersistentDomain(forName: suite) }
        let configuration = ConfigurationStore(defaults: defaults)
        var next = configuration.configuration
        next.language = .japanese; next.centerLogo = .custom; next.centerLogoRevision = revision; next.hudScale = 1.25
        next.summonShortcut = SummonShortcut(keyCode: 4, modifiers: [.control, .option]); next.launchAtLogin = false
        configuration.update(next)
        defaults.set(321, forKey: OrbiPomSession.bestScoreKey); defaults.set(true, forKey: "hasLaunched")
        let domain = defaults.persistentDomain(forName: suite) ?? [:]
        try PropertyListSerialization.data(fromPropertyList: domain, format: .binary, options: 0).write(to: work.appendingPathComponent("domain.plist"))
        expected["foundationDate"] = base.timeIntervalSinceReferenceDate
        expected["settings"] = ["language": "japanese", "centerLogo": "custom", "centerLogoRevision": revision, "hudScale": 1.25,
                                "orbipom.bestScore.v1": 321, "launchAtLogin": false, "hasLaunched": true,
                                "summonShortcut": ["keyCode": 4, "modifiers": 3]]
        let data = try JSONSerialization.data(withJSONObject: expected, options: [.sortedKeys])
        try data.write(to: work.appendingPathComponent("expected.json"))
    }

    static func main() throws {
        precondition(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil, "Run only inside a private CFFIXED_USER_HOME")
        if CommandLine.arguments.count == 3 && CommandLine.arguments[1] == "--golden" {
            try golden(URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)); return
        }
        precondition(CommandLine.arguments.count == 3)
        let input = try JSONSerialization.jsonObject(with: Data(contentsOf: URL(fileURLWithPath: CommandLine.arguments[1]))) as! [String: Any]
        let output: [String: Any] = [
            "plist": try plistCases(input["plist"] as! [String: Any]),
            "settings": try settingsCases(input["settings"] as! [[String: Any]])
        ]
        let data = try JSONSerialization.data(withJSONObject: output, options: [.sortedKeys, .prettyPrinted])
        try data.write(to: URL(fileURLWithPath: CommandLine.arguments[2]))
    }
}
