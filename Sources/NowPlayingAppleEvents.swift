import AppKit
import Carbon

/// Uses the public scriptable-player contract, addressed to an existing PID.
/// No shell/AppleScript compilation, implicit launch, private framework or
/// network request. Only this service's serial worker calls these methods.
final class NowPlayingAppleEvents: NowPlayingBackend {
    private var dictionaries: [URL: NowPlayingScriptingDictionary] = [:]
    // A granted preflight can become stale before dispatch. NeverInteract
    // concerns the target application's UI; this separate public send bit also
    // prevents an Automation consent prompt during ordinary reads/commands.
    static let nonPromptingSendOptions: NSAppleEventDescriptor.SendOptions = [
        .waitForReply, .neverInteract, .dontRecord,
        NSAppleEventDescriptor.SendOptions(rawValue: UInt(kAEDoNotPromptForUserConsent))
    ]

    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
        try check(app, cancelled: cancelled)
        try permission(app, ask: false)
        let dictionary = try scriptingDictionary(app)
        let deadline = ProcessInfo.processInfo.systemUptime + 1.6
        func get(_ name: String, track: Bool = false) throws -> NSAppleEventDescriptor {
            guard let code = dictionary.properties[name] else { throw NowPlayingFailure.unsupported }
            let container = track ? Self.property(dictionary.properties["current track"]!) : nil
            return try send(app, eventClass: Self.code("core"), eventID: Self.code("getd"),
                            object: Self.property(code, container: container), deadline: deadline, cancelled: cancelled)
        }
        let state = try get("player state").enumCodeValue
        let playing = dictionary.enumerators["playing"] == state
        if dictionary.enumerators["stopped"] == state { return nil }
        let title: String
        do { title = try get("name", track: true).stringValue ?? "" }
        catch NowPlayingFailure.unavailable { return nil }
        guard !title.isEmpty else { return nil }
        let artist = (try? get("artist", track: true).stringValue) ?? ""
        let album = (try? get("album", track: true).stringValue) ?? ""
        let identityName = app.source == .music ? "persistent ID" : "id"
        let identifier = dictionary.properties[identityName] == nil ? nil : (try? get(identityName, track: true).stringValue)
        let durationDescriptor = try? get("duration", track: true)
        let positionDescriptor = try? get("player position")
        let duration = durationDescriptor.flatMap(Self.number).map { app.source == .spotify ? $0 / 1_000 : $0 }
        return NowPlayingTrack(title: title, artist: artist, album: album, duration: duration,
                               position: positionDescriptor.flatMap(Self.number), isPlaying: playing,
                               sampledAt: ProcessInfo.processInfo.systemUptime, identifier: identifier)
    }

    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingArtworkPayload? {
        try check(app, cancelled: cancelled); try permission(app, ask: false)
        let dictionary = try scriptingDictionary(app)
        guard let currentTrack = dictionary.properties["current track"] else { return nil }
        let container = Self.property(currentTrack), deadline = ProcessInfo.processInfo.systemUptime + 1.6
        func get(_ code: UInt32, in owner: NSAppleEventDescriptor) throws -> NSAppleEventDescriptor {
            try send(app, eventClass: Self.code("core"), eventID: Self.code("getd"),
                     object: Self.property(code, container: owner), deadline: deadline, cancelled: cancelled)
        }
        // Reject a player change between metadata and artwork requests. Prefer
        // its stable public ID; old dictionaries fall back to bounded metadata.
        let identityName = app.source == .music ? "persistent ID" : "id"
        func matchesTrack() throws -> Bool {
            if let identity = track.identifier, let code = dictionary.properties[identityName] {
                return try get(code, in: container).stringValue == identity
            }
            for (name, expected) in [("name", track.title), ("artist", track.artist), ("album", track.album)] {
                guard let code = dictionary.properties[name],
                      String((try get(code, in: container).stringValue ?? "").prefix(512)) == expected else { return false }
            }
            return true
        }
        guard try matchesTrack() else { return nil }
        if app.source == .music {
            guard let artworkClass = dictionary.classes["artwork"], let raw = dictionary.properties["raw data"] else { return nil }
            let artwork = Self.firstElement(artworkClass, container: container)
            let descriptor = try get(raw, in: artwork)
            let size = AEGetDescDataSize(descriptor.aeDesc)
            guard size > 0, size <= NowPlayingArtworkLoader.maximumBytes, !cancelled() else { return nil }
            guard try matchesTrack(), !cancelled() else { return nil }
            return .embedded(descriptor.data)
        }
        guard let property = dictionary.properties["artwork url"],
              let value = try get(property, in: container).stringValue,
              value.utf8.count <= 2_048, let url = URL(string: value),
              NowPlayingArtworkLoader.isAllowedRemoteURL(url), !cancelled() else { return nil }
        guard try matchesTrack() else { return nil }
        return .remote(url)
    }

    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        try check(app, cancelled: cancelled); try permission(app, ask: false)
        let dictionary = try scriptingDictionary(app)
        let deadline = ProcessInfo.processInfo.systemUptime + 1.6
        if case .seek(let seconds) = command {
            guard seconds.isFinite, seconds >= 0, let position = dictionary.properties["player position"] else {
                throw NowPlayingFailure.unsupported
            }
            _ = try send(app, eventClass: Self.code("core"), eventID: Self.code("setd"),
                         object: Self.property(position), data: NSAppleEventDescriptor(double: seconds),
                         deadline: deadline, cancelled: cancelled)
        } else {
            let name: String
            switch command { case .playPause: name = "playpause"; case .previous: name = "previous track"; default: name = "next track" }
            guard let code = dictionary.commands[name] else { throw NowPlayingFailure.unsupported }
            _ = try send(app, eventClass: code.0, eventID: code.1, deadline: deadline, cancelled: cancelled)
        }
    }

    func requestPermission(for app: NowPlayingApplication, cancelled: () -> Bool) throws {
        try check(app, cancelled: cancelled)
        _ = try scriptingDictionary(app)
        // This is the only call permitted to prompt, reached by Connect only.
        try permission(app, ask: true)
    }

    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? {
        try check(app, cancelled: cancelled); try permission(app, ask: false)
        guard let code = try scriptingDictionary(app).properties["sound volume"] else { return nil }
        let reply = try send(app, eventClass: Self.code("core"), eventID: Self.code("getd"),
            object: Self.property(code), deadline: ProcessInfo.processInfo.systemUptime + 0.6, cancelled: cancelled)
        guard let value = Self.number(reply), value.isFinite else { return nil }
        return min(1, max(0, value / 100))
    }

    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        try check(app, cancelled: cancelled); try permission(app, ask: false)
        guard value.isFinite, let code = try scriptingDictionary(app).properties["sound volume"] else { throw NowPlayingFailure.unsupported }
        _ = try send(app, eventClass: Self.code("core"), eventID: Self.code("setd"), object: Self.property(code),
            data: NSAppleEventDescriptor(int32: Int32((min(1, max(0, value)) * 100).rounded())),
            deadline: ProcessInfo.processInfo.systemUptime + 0.6, cancelled: cancelled)
    }

    private func permission(_ app: NowPlayingApplication, ask: Bool) throws {
        let target = NSAppleEventDescriptor(processIdentifier: app.pid)
        let status = AEDeterminePermissionToAutomateTarget(target.aeDesc, typeWildCard, typeWildCard, ask)
        if status == noErr { return }
        if status == -1744 { throw NowPlayingFailure.permissionRequired }
        if status == -1743 { throw NowPlayingFailure.permissionDenied }
        throw NowPlayingFailure.unavailable
    }

    private func check(_ app: NowPlayingApplication, cancelled: () -> Bool) throws {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        guard app.pid > 0, let running = NSRunningApplication(processIdentifier: app.pid),
              !running.isTerminated, running.bundleIdentifier == app.bundleIdentifier,
              running.bundleURL?.standardizedFileURL == app.bundleURL.standardizedFileURL else {
            throw NowPlayingFailure.unavailable
        }
    }

    private func send(_ app: NowPlayingApplication, eventClass: UInt32, eventID: UInt32,
                      object: NSAppleEventDescriptor? = nil, data: NSAppleEventDescriptor? = nil,
                      deadline: TimeInterval, cancelled: () -> Bool) throws -> NSAppleEventDescriptor {
        try check(app, cancelled: cancelled)
        let remaining = deadline - ProcessInfo.processInfo.systemUptime
        guard remaining > 0 else { throw NowPlayingFailure.timedOut }
        let event = NSAppleEventDescriptor(eventClass: eventClass, eventID: eventID,
            targetDescriptor: NSAppleEventDescriptor(processIdentifier: app.pid), returnID: AEReturnID(kAutoGenerateReturnID),
            transactionID: AETransactionID(kAnyTransactionID))
        if let object { event.setParam(object, forKeyword: keyDirectObject) }
        if let data { event.setParam(data, forKeyword: keyAEData) }
        do {
            let reply = try event.sendEvent(options: Self.nonPromptingSendOptions, timeout: min(0.6, remaining))
            let error = reply.paramDescriptor(forKeyword: keyErrorNumber)?.int32Value ?? 0
            if error != 0 { throw Self.failure(Int(error)) }
            return reply.paramDescriptor(forKeyword: keyDirectObject) ?? NSAppleEventDescriptor.null()
        } catch let failure as NowPlayingFailure { throw failure }
        catch { throw Self.failure((error as NSError).code) }
    }

    private func scriptingDictionary(_ app: NowPlayingApplication) throws -> NowPlayingScriptingDictionary {
        if let cached = dictionaries[app.bundleURL] { return cached }
        guard let bundle = Bundle(url: app.bundleURL),
              let name = bundle.object(forInfoDictionaryKey: "OSAScriptingDefinition") as? String,
              !name.contains("/"), let directory = bundle.resourceURL else { throw NowPlayingFailure.unsupported }
        let url = directory.appendingPathComponent(name)
        guard let size = try? url.resourceValues(forKeys: [.fileSizeKey]).fileSize, size <= 2_097_152,
              let data = try? Data(contentsOf: url), let dictionary = NowPlayingScriptingDictionary(data: data) else {
            throw NowPlayingFailure.unsupported
        }
        if dictionaries.count >= 2 { dictionaries.removeAll() }
        dictionaries[app.bundleURL] = dictionary
        return dictionary
    }

    static func code(_ value: String) -> UInt32 {
        value.utf8.reduce(0) { ($0 << 8) | UInt32($1) }
    }
    static func property(_ code: UInt32, container: NSAppleEventDescriptor? = nil) -> NSAppleEventDescriptor {
        let record = NSAppleEventDescriptor.record()
        record.setDescriptor(NSAppleEventDescriptor(typeCode: typeProperty), forKeyword: AEKeyword(keyAEDesiredClass))
        record.setDescriptor(container ?? NSAppleEventDescriptor.null(), forKeyword: AEKeyword(keyAEContainer))
        record.setDescriptor(NSAppleEventDescriptor(enumCode: OSType(formPropertyID)), forKeyword: AEKeyword(keyAEKeyForm))
        record.setDescriptor(NSAppleEventDescriptor(typeCode: code), forKeyword: AEKeyword(keyAEKeyData))
        return record.coerce(toDescriptorType: typeObjectSpecifier)!
    }
    static func firstElement(_ elementClass: UInt32, container: NSAppleEventDescriptor) -> NSAppleEventDescriptor {
        let record = NSAppleEventDescriptor.record()
        record.setDescriptor(NSAppleEventDescriptor(typeCode: elementClass), forKeyword: AEKeyword(keyAEDesiredClass))
        record.setDescriptor(container, forKeyword: AEKeyword(keyAEContainer))
        record.setDescriptor(NSAppleEventDescriptor(enumCode: OSType(formAbsolutePosition)), forKeyword: AEKeyword(keyAEKeyForm))
        record.setDescriptor(NSAppleEventDescriptor(int32: 1), forKeyword: AEKeyword(keyAEKeyData))
        return record.coerce(toDescriptorType: typeObjectSpecifier)!
    }
    static func number(_ value: NSAppleEventDescriptor) -> Double? {
        guard let number = value.coerce(toDescriptorType: typeIEEE64BitFloatingPoint)?.doubleValue,
              number.isFinite else { return nil }
        return number
    }
    private static func failure(_ code: Int) -> NowPlayingFailure {
        switch code { case -1743: return .permissionDenied; case -1744: return .permissionRequired
        case -1712: return .timedOut; default: return .unavailable }
    }
}

/// Read the supported application's own public dictionary instead of guessing
/// Spotify's command codes or carrying an unverified third-party dictionary.
final class NowPlayingScriptingDictionary: NSObject, XMLParserDelegate {
    private(set) var properties: [String: UInt32] = [:]
    private(set) var commands: [String: (UInt32, UInt32)] = [:]
    private(set) var enumerators: [String: UInt32] = [:]
    private(set) var classes: [String: UInt32] = [:]
    private static let optionalProperties: Set<String> = ["persistent ID", "id", "raw data", "artwork url"]
    private static let propertyNames: Set<String> = ["player state", "player position", "current track", "name", "artist", "album", "duration"]
    private static let commandNames: Set<String> = ["playpause", "previous track", "next track"]

    init?(data: Data) {
        guard data.count <= 2_097_152 else { return nil }
        super.init()
        let parser = XMLParser(data: data); parser.shouldResolveExternalEntities = false; parser.delegate = self
        guard parser.parse(), Self.propertyNames.isSubset(of: Set(properties.keys)),
              Self.commandNames.isSubset(of: Set(commands.keys)), enumerators["playing"] != nil else { return nil }
    }
    func parser(_ parser: XMLParser, didStartElement elementName: String, namespaceURI: String?,
                qualifiedName qName: String?, attributes attributeDict: [String: String]) {
        guard let name = attributeDict["name"], let code = attributeDict["code"], code.utf8.count == code.count else { return }
        if elementName == "property", (Self.propertyNames.contains(name) || Self.optionalProperties.contains(name)), code.utf8.count == 4 {
            properties[name] = NowPlayingAppleEvents.code(code)
        } else if elementName == "class", name == "artwork", code.utf8.count == 4 {
            classes[name] = NowPlayingAppleEvents.code(code)
        } else if elementName == "command", Self.commandNames.contains(name), code.utf8.count == 8 {
            commands[name] = (NowPlayingAppleEvents.code(String(code.prefix(4))), NowPlayingAppleEvents.code(String(code.suffix(4))))
        } else if elementName == "enumerator", ["playing", "paused", "stopped"].contains(name), code.utf8.count == 4 {
            enumerators[name] = NowPlayingAppleEvents.code(code)
        }
    }
}
