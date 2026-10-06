import AppKit
import ApplicationServices

/// Compatibility fallback restricted to known music apps and their existing
/// player controls. No keystroke interception, filesystem/library inspection,
/// screenshots, app activation, permission prompts or global UI monitoring.
final class NowPlayingAccessibility {
    private struct Controls {
        let app: NowPlayingApplication
        let track: NowPlayingTrack
        let volume: AXUIElement?
        let artwork: URL?
        let heading: AXUIElement
        let position: AXUIElement?
        let duration: AXUIElement?
        let playback: AXUIElement?
    }
    private let invalidationLock = NSLock()
    private var invalidated = Set<Int32>()
    private var controls: [Int32: Controls] = [:] // Accessed on the playback worker.
    private var observers: [Int32: AXObserver] = [:] // Main-thread subscription owner.
    private var registrations: [(AXObserver, AXUIElement, CFString)] = []
    private var coalescer: NowPlayingNotificationCoalescer?
    private var active = false
    private var workspaceTokens: [NSObjectProtocol] = []

    func read(_ app: NowPlayingApplication, cancelled: () -> Bool) throws -> NowPlayingTrack? {
        try check(app, cancelled: cancelled)
        invalidationLock.lock(); let invalid = invalidated.remove(app.pid) != nil; invalidationLock.unlock()
        if invalid { controls.removeValue(forKey: app.pid) }
        let deadline = ProcessInfo.processInfo.systemUptime + 1.1
        if let cached = controls[app.pid], cached.app == app,
           let track = Self.track(heading: Self.label(cached.heading),
                position: cached.position.flatMap { Self.clock(Self.label($0).replacingOccurrences(of: "/", with: "")) },
                duration: cached.duration.flatMap { Self.clock(Self.label($0)) },
                playing: cached.playback.map { ["Pause", "暂停", "暫停"].contains(Self.label($0)) } ?? false) {
            controls[app.pid] = Controls(app: app, track: track, volume: cached.volume, artwork: cached.artwork,
                heading: cached.heading, position: cached.position, duration: cached.duration, playback: cached.playback)
            return track
        }
        let root = AXUIElementCreateApplication(app.pid)
        AXUIElementSetMessagingTimeout(root, 0.15)
        let menus = Self.element(root, kAXMenuBarAttribute).map { Self.walk($0, limit: 180, cancelled: cancelled) } ?? []
        let menuTitles = menus.compactMap { Self.string($0, kAXTitleAttribute) }
        let playing = menuTitles.contains { ["Pause", "暂停", "暫停"].contains($0) }
        let windows = Self.attribute(root, kAXWindowsAttribute) as? [AXUIElement] ?? []
        var heading: String?, position: Double?, duration: Double?, volume: AXUIElement?, art: URL?
        var watch: [AXUIElement] = []
        var headingNode: AXUIElement?, positionNode: AXUIElement?, durationNode: AXUIElement?
        for window in windows.prefix(2) {
            let nodes = Self.walk(window, limit: 700, cancelled: cancelled)
            for node in nodes {
                guard !cancelled(), ProcessInfo.processInfo.systemUptime < deadline else { break }
                let role = Self.string(node, kAXRoleAttribute) ?? ""
                let name = Self.label(node)
                if role == "AXHeading", heading == nil, name.contains(" - ") { heading = name; headingNode = node; watch.append(node) }
                if role == kAXStaticTextRole as String {
                    if name.trimmingCharacters(in: .whitespaces).hasSuffix("/"), position == nil {
                        position = Self.clock(name.replacingOccurrences(of: "/", with: "")); positionNode = node; watch.append(node)
                    } else if duration == nil, let seconds = Self.clock(name) { duration = seconds; durationNode = node; watch.append(node) }
                }
                if role == kAXSliderRole as String, Self.isVolumeLabel(name) { volume = node; watch.append(node) }
                if heading != nil, art == nil, role == kAXImageRole as String,
                   let url = Self.url(node), NowPlayingArtworkLoader.isAllowedRemoteURL(url) { art = url }
                if heading != nil, position != nil, duration != nil, volume != nil { break }
            }
            if heading != nil { break }
        }
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        guard let heading, let headingNode, let track = Self.track(heading: heading, position: position, duration: duration, playing: playing) else { return nil }
        let playback = menus.first { ["Play", "Pause", "播放", "暂停", "暫停"].contains(Self.string($0, kAXTitleAttribute) ?? "") }
        controls[app.pid] = Controls(app: app, track: track, volume: volume, artwork: art,
            heading: headingNode, position: positionNode, duration: durationNode, playback: playback)
        if controls.count > 4 { controls = [app.pid: controls[app.pid]!] }
        let targets = watch + menus.filter { ["Play", "Pause", "播放", "暂停", "暫停"].contains(Self.string($0, kAXTitleAttribute) ?? "") }
        DispatchQueue.main.async { [weak self] in self?.watch(app.pid, targets: targets) }
        return track
    }

    static func track(heading: String, position: Double?, duration: Double?, playing: Bool) -> NowPlayingTrack? {
        guard let split = heading.range(of: " - ", options: .backwards) else { return nil }
        let title = String(heading[..<split.lowerBound]).trimmingCharacters(in: .whitespacesAndNewlines)
        let artist = String(heading[split.upperBound...]).trimmingCharacters(in: .whitespacesAndNewlines)
        guard !title.isEmpty else { return nil }
        return NowPlayingTrack(title: title, artist: artist, album: "", duration: duration,
            position: position, isPlaying: playing, sampledAt: ProcessInfo.processInfo.systemUptime, supportsSeeking: false)
    }

    func artwork(for track: NowPlayingTrack, in app: NowPlayingApplication) -> NowPlayingArtworkPayload? {
        guard let cached = controls[app.pid], cached.app == app, cached.track.hasSameIdentity(as: track), let url = cached.artwork else { return nil }
        return .remote(url)
    }
    func volume(in app: NowPlayingApplication, cancelled: () -> Bool) throws -> Double? {
        try check(app, cancelled: cancelled)
        guard let cached = controls[app.pid], cached.app == app, let slider = cached.volume,
              let raw = Self.number(slider, kAXValueAttribute), let min = Self.number(slider, kAXMinValueAttribute),
              let max = Self.number(slider, kAXMaxValueAttribute), max > min else { return nil }
        return Swift.min(1, Swift.max(0, (raw - min) / (max - min)))
    }
    func setVolume(_ value: Double, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        try check(app, cancelled: cancelled)
        guard value.isFinite, let cached = controls[app.pid], cached.app == app, let slider = cached.volume,
              let min = Self.number(slider, kAXMinValueAttribute), let max = Self.number(slider, kAXMaxValueAttribute), max > min else { throw NowPlayingFailure.unsupported }
        var settable: DarwinBoolean = false
        guard AXUIElementIsAttributeSettable(slider, kAXValueAttribute as CFString, &settable) == .success, settable.boolValue,
              AXUIElementSetAttributeValue(slider, kAXValueAttribute as CFString,
                NSNumber(value: min + Swift.min(1, Swift.max(0, value)) * (max - min))) == .success else { throw NowPlayingFailure.unsupported }
    }
    func perform(_ command: NowPlayingCommand, in app: NowPlayingApplication, cancelled: () -> Bool) throws {
        try check(app, cancelled: cancelled)
        let names: Set<String>
        switch command {
        case .playPause: names = ["Play", "Pause", "播放", "暂停", "暫停"]
        case .previous: names = ["Previous", "上一首", "上一个"]
        case .next: names = ["Next", "下一首", "下一个"]
        case .seek: throw NowPlayingFailure.unsupported
        }
        let root = AXUIElementCreateApplication(app.pid)
        guard let menu = Self.element(root, kAXMenuBarAttribute),
              let target = Self.walk(menu, limit: 180, cancelled: cancelled).first(where: {
                  Self.string($0, kAXRoleAttribute) == kAXMenuItemRole as String && names.contains(Self.string($0, kAXTitleAttribute) ?? "")
              }), !cancelled(), AXUIElementPerformAction(target, kAXPressAction as CFString) == .success else { throw NowPlayingFailure.unavailable }
    }
    private func check(_ app: NowPlayingApplication, cancelled: () -> Bool) throws {
        guard !cancelled() else { throw NowPlayingFailure.cancelled }
        guard [.netease, .qqMusic, .kugou].contains(app.source),
              let running = NSRunningApplication(processIdentifier: app.pid), !running.isTerminated,
              running.bundleIdentifier == app.bundleIdentifier else { throw NowPlayingFailure.unavailable }
        guard AXIsProcessTrusted() else { throw NowPlayingFailure.accessibilityRequired }
    }
    func subscribe(_ action: @escaping () -> Void) -> (() -> Void) {
        active = true; coalescer = NowPlayingNotificationCoalescer(action: action)
        let workspace = NSWorkspace.shared.notificationCenter
        workspaceTokens = [NSWorkspace.didLaunchApplicationNotification, NSWorkspace.didTerminateApplicationNotification].map { name in
            workspace.addObserver(forName: name, object: nil, queue: .main) { [weak self] note in
                guard let app = note.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication,
                      [NowPlayingSource.netease, .qqMusic, .kugou].map(\.bundleIdentifier).contains(app.bundleIdentifier ?? "") else { return }
                self?.coalescer?.signal()
            }
        }
        return { [weak self] in self?.stop() }
    }
    private func watch(_ pid: Int32, targets: [AXUIElement]) {
        guard active else { return }
        // Replace this process's registrations so destroyed track labels cannot
        // accumulate over a long session. The app-level window observer stays.
        if let observer = observers[pid] {
            for entry in registrations where CFEqual(entry.0, observer) { AXObserverRemoveNotification(entry.0, entry.1, entry.2) }
            registrations.removeAll { CFEqual($0.0, observer) }
        }
        let observer: AXObserver
        if let existing = observers[pid] { observer = existing }
        else {
            var created: AXObserver?
            guard AXObserverCreate(pid, { _, element, notification, context in
                guard let context else { return }
                let owner = Unmanaged<NowPlayingAccessibility>.fromOpaque(context).takeUnretainedValue()
                if notification as String == kAXWindowCreatedNotification || notification as String == kAXFocusedWindowChangedNotification {
                    var process: pid_t = 0
                    if AXUIElementGetPid(element, &process) == .success {
                        owner.invalidationLock.lock(); owner.invalidated.insert(process); owner.invalidationLock.unlock()
                    }
                }
                owner.coalescer?.signal()
            }, &created) == .success, let created else { return }
            observers[pid] = created; observer = created
            CFRunLoopAddSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(created), .commonModes)
        }
        let app = AXUIElementCreateApplication(pid)
        let entries: [(AXUIElement, CFString)] = [(app, kAXWindowCreatedNotification as CFString), (app, kAXFocusedWindowChangedNotification as CFString)]
            + targets.prefix(12).flatMap { [($0, kAXValueChangedNotification as CFString), ($0, kAXTitleChangedNotification as CFString)] }
        for (target, name) in entries {
            if AXObserverAddNotification(observer, target, name, Unmanaged.passUnretained(self).toOpaque()) == .success {
                registrations.append((observer, target, name))
            }
        }
    }
    private func stop() {
        active = false; coalescer?.cancel(); coalescer = nil
        workspaceTokens.forEach(NSWorkspace.shared.notificationCenter.removeObserver); workspaceTokens.removeAll()
        for (observer, target, name) in registrations { AXObserverRemoveNotification(observer, target, name) }
        registrations.removeAll()
        for observer in observers.values { CFRunLoopRemoveSource(CFRunLoopGetMain(), AXObserverGetRunLoopSource(observer), .commonModes) }
        observers.removeAll()
    }
    deinit { stop() }

    static func clock(_ value: String) -> Double? {
        let pieces = value.trimmingCharacters(in: .whitespacesAndNewlines).split(separator: ":")
        guard pieces.count == 2 || pieces.count == 3,
              pieces.allSatisfy({ !$0.isEmpty && $0.count <= 3 && $0.allSatisfy(\.isNumber) }) else { return nil }
        let numbers = pieces.compactMap { Double($0) }
        guard numbers.count == pieces.count, numbers.last! < 60, numbers.dropFirst().allSatisfy({ $0 < 60 }) else { return nil }
        return numbers.reduce(0) { $0 * 60 + $1 }
    }
    static func isVolumeLabel(_ value: String) -> Bool {
        let text = value.lowercased(); return text.contains("volume") || text.contains("音量")
    }
    private static func walk(_ root: AXUIElement, limit: Int, cancelled: () -> Bool) -> [AXUIElement] {
        var pending = [(root, 0)], result: [AXUIElement] = []
        let deadline = ProcessInfo.processInfo.systemUptime + 0.7
        while let (node, depth) = pending.popLast(), result.count < limit,
              !cancelled(), ProcessInfo.processInfo.systemUptime < deadline {
            result.append(node)
            guard depth < 12 else { continue }
            let children = attribute(node, kAXChildrenAttribute) as? [AXUIElement] ?? []
            // Bottom player/footer children are visited first, avoiding the
            // large library/sidebar before current playback has been found.
            pending.append(contentsOf: children.suffix(160).map { ($0, depth + 1) })
        }
        return result
    }
    private static func attribute(_ element: AXUIElement, _ key: String) -> CFTypeRef? {
        var value: CFTypeRef?
        return AXUIElementCopyAttributeValue(element, key as CFString, &value) == .success ? value : nil
    }
    private static func string(_ element: AXUIElement, _ key: String) -> String? { attribute(element, key) as? String }
    private static func number(_ element: AXUIElement, _ key: String) -> Double? {
        guard let value = (attribute(element, key) as? NSNumber)?.doubleValue, value.isFinite else { return nil }; return value
    }
    private static func element(_ element: AXUIElement, _ key: String) -> AXUIElement? {
        guard let value = attribute(element, key), CFGetTypeID(value) == AXUIElementGetTypeID() else { return nil }
        return (value as! AXUIElement)
    }
    private static func url(_ element: AXUIElement) -> URL? { attribute(element, kAXURLAttribute) as? URL }
    private static func label(_ element: AXUIElement) -> String {
        for name in [kAXTitleAttribute, kAXDescriptionAttribute, kAXValueAttribute] {
            if let value = string(element, name), !value.isEmpty { return String(value.prefix(1024)) }
        }
        if string(element, kAXRoleAttribute) == "AXHeading" {
            return ((attribute(element, kAXChildrenAttribute) as? [AXUIElement]) ?? []).prefix(8).map { child in
                string(child, kAXValueAttribute) ?? string(child, kAXTitleAttribute) ?? string(child, kAXDescriptionAttribute) ?? ""
            }.joined()
        }
        return ""
    }
}
