import AppKit
import ApplicationServices

/// Public Accessibility controls, inspected on macOS 15.7. Unknown UI layouts
/// fail closed; these identifiers are not a promise of compatibility with every
/// macOS version. No preferences, private Focus APIs or global events are used.
struct ControlCenterFocusChoice: Equatable {
    static let doNotDisturb = "focus-mode-activity-com.apple.donotdisturb.mode.default"
    let identifier: String
    let enabled: Bool
}

enum ControlCenterFocusDecision: Equatable {
    case preserve, unchanged, toggle
    static func evaluate(_ command: WorkModeFocusCommand, choices: [ControlCenterFocusChoice]) throws -> Self {
        guard !choices.isEmpty, Set(choices.map(\.identifier)).count == choices.count,
              choices.contains(where: { $0.identifier == ControlCenterFocusChoice.doNotDisturb }) else {
            throw WorkModeFocusRunError.controlCenterUnavailable
        }
        let active = choices.filter(\.enabled)
        if command == .start { return active.isEmpty ? .toggle : .preserve }
        return active.count == 1 && active[0].identifier == ControlCenterFocusChoice.doNotDisturb ? .toggle : .unchanged
    }
}

protocol ControlCenterFocusAccessing: AnyObject {
    var isTrusted: Bool { get }
    func open() throws
    func read() throws -> [ControlCenterFocusChoice]
    func pressDoNotDisturb(expectedEnabled: Bool) throws
    func waitForReadback(enabled: Bool) throws
    func close()
}

final class ControlCenterWorkModeFocusExecutor: WorkModeFocusExecuting {
    private let access: ControlCenterFocusAccessing
    private let worker = DispatchQueue(label: "EndfieldCharge.workMode.controlCenter", qos: .utility)
    private let lock = NSLock()
    private var presenting = false
    private var presentationGeneration: UInt64 = 0
    private let scheduleNotificationDrain: (@escaping () -> Void) -> Void
    var isAvailable: Bool { access.isTrusted }
    var isPresentingSystemControls: Bool { lock.lock(); defer { lock.unlock() }; return presenting }
    init(access: ControlCenterFocusAccessing = NativeControlCenterFocusAccess(),
         scheduleNotificationDrain: @escaping (@escaping () -> Void) -> Void = { action in
             let timer = Timer(timeInterval: 0.15, repeats: false) { _ in action() }
             RunLoop.main.add(timer, forMode: .common)
             RunLoop.main.add(timer, forMode: RunLoop.Mode("NSModalPanelRunLoopMode"))
         }) {
        self.access = access; self.scheduleNotificationDrain = scheduleNotificationDrain
    }
    /// Tests may inspect the same native controls without invoking a toggle.
    func inspectWithoutChangingFocus(completion: @escaping (Result<[ControlCenterFocusChoice], WorkModeFocusRunError>) -> Void) {
        guard access.isTrusted else { completion(.failure(.accessibilityRequired)); return }
        worker.async {
            let result: Result<[ControlCenterFocusChoice], WorkModeFocusRunError>
            do { try self.access.open(); result = .success(try self.access.read()) }
            catch { result = .failure(error as? WorkModeFocusRunError ?? .controlCenterUnavailable) }
            self.access.close()
            RunLoop.main.perform(inModes: [.common, RunLoop.Mode("NSModalPanelRunLoopMode")]) { completion(result) }
        }
    }
    func run(_ command: WorkModeFocusCommand, completion: @escaping (Result<String, WorkModeFocusRunError>) -> Void) {
        guard access.isTrusted else { completion(.failure(.accessibilityRequired)); return }
        lock.lock()
        presentationGeneration &+= 1
        let generation = presentationGeneration
        presenting = true
        lock.unlock()
        worker.async {
            var mayHaveChangedFocus = false
            let result: Result<String, WorkModeFocusRunError>
            do {
                try self.access.open()
                // Re-read immediately before the action. A user-selected Focus
                // between opening and this check must be preserved.
                let decision = try ControlCenterFocusDecision.evaluate(command, choices: self.access.read())
                switch decision {
                case .preserve: result = .success("preserved")
                case .unchanged: result = .success("unchanged")
                case .toggle:
                    mayHaveChangedFocus = true
                    try self.access.pressDoNotDisturb(expectedEnabled: command == .end)
                    try self.access.waitForReadback(enabled: command == .start)
                    result = .success(command == .start ? "enabled" : "released")
                }
            } catch {
                // A timeout or failed acknowledgement after AXPress may still
                // have changed Focus. Never fall back and toggle a second time.
                result = .failure(mayHaveChangedFocus ? .invalidResponse
                                  : (error as? WorkModeFocusRunError ?? .controlCenterUnavailable))
            }
            self.access.close()
            RunLoop.main.perform(inModes: [.common, RunLoop.Mode("NSModalPanelRunLoopMode")]) {
                // Workspace/key-window notifications may already be queued on
                // main when the AX worker dismisses the menu. Let that finite
                // queue drain before normal focus-loss handling resumes.
                self.scheduleNotificationDrain { [weak self] in
                    guard let self else { return }
                    self.lock.lock(); defer { self.lock.unlock() }
                    guard self.presentationGeneration == generation else { return }
                    self.presenting = false
                }
                completion(result)
            }
        }
    }
}

/// Keeps Start and End on the same backend. Existing user-owned shortcuts remain
/// a fallback when Accessibility is not granted or the UI cannot be recognized.
final class AutomaticWorkModeFocusExecutor: WorkModeFocusExecuting {
    private let direct: WorkModeFocusExecuting
    private let shortcuts: WorkModeFocusExecuting
    private var owner: WorkModeFocusExecuting?
    var isAvailable: Bool {
        // Availability is distinct from permission so an untrusted, supported
        // Mac can explain its Accessibility requirement instead of claiming an
        // OS incompatibility. Checking permission never triggers a prompt.
        if #available(macOS 11, *) { return true }
        return direct.isAvailable || shortcuts.isAvailable
    }
    var isPresentingSystemControls: Bool { direct.isPresentingSystemControls }
    var canRetryAfterAccessibilityGrant: Bool { direct.isAvailable }
    init(direct: WorkModeFocusExecuting = ControlCenterWorkModeFocusExecutor(),
         shortcuts: WorkModeFocusExecuting = ShortcutsWorkModeFocusExecutor()) {
        self.direct = direct; self.shortcuts = shortcuts
    }
    func run(_ command: WorkModeFocusCommand, completion: @escaping (Result<String, WorkModeFocusRunError>) -> Void) {
        if command == .end {
            guard let owner else { completion(.failure(.invalidResponse)); return }
            owner.run(.end) { [weak self] result in
                if case .success(let value) = result, ["released", "unchanged"].contains(value.trimmingCharacters(in: .whitespacesAndNewlines)) {
                    self?.owner = nil
                }
                completion(result)
            }
            return
        }
        let useShortcuts: (WorkModeFocusRunError) -> Void = { [weak self] directError in
            guard let self else { return }
            guard self.shortcuts.isAvailable else { completion(.failure(directError)); return }
            self.shortcuts.run(.start) { result in
                if case .success(let value) = result, value.trimmingCharacters(in: .whitespacesAndNewlines) == "enabled" { self.owner = self.shortcuts }
                if case .failure(.notConfigured) = result { completion(.failure(directError)) }
                else { completion(result) }
            }
        }
        guard direct.isAvailable else { useShortcuts(.accessibilityRequired); return }
        direct.run(.start) { [weak self] result in
            switch result {
            case .success(let value):
                if value.trimmingCharacters(in: .whitespacesAndNewlines) == "enabled" { self?.owner = self?.direct }
                completion(result)
            case .failure(let error) where [.unavailable, .accessibilityRequired, .controlCenterUnavailable].contains(error):
                useShortcuts(error)
            case .failure: completion(result)
            }
        }
    }
}

private final class NativeControlCenterFocusAccess: ControlCenterFocusAccessing {
    var isTrusted: Bool { if #available(macOS 11, *) { return AXIsProcessTrusted() }; return false }
    private var app: AXUIElement?
    private var menu: AXUIElement?
    private var opened = false
    private var deadline: TimeInterval = 0
    private var dnd: AXUIElement?
    private var initialFocusEnabled = false
    private var didAttemptToggle = false
    private var foregroundPID: pid_t?
    private var controlCenterPID: pid_t?
    func open() throws {
        guard isTrusted else { throw WorkModeFocusRunError.accessibilityRequired }
        guard let running = NSRunningApplication.runningApplications(withBundleIdentifier: "com.apple.controlcenter").first else {
            throw WorkModeFocusRunError.controlCenterUnavailable
        }
        deadline = ProcessInfo.processInfo.systemUptime + 3
        didAttemptToggle = false
        foregroundPID = NSWorkspace.shared.frontmostApplication?.processIdentifier
        controlCenterPID = running.processIdentifier
        let app = AXUIElementCreateApplication(running.processIdentifier); self.app = app
        AXUIElementSetMessagingTimeout(app, 0.15)
        // Do not take over or dismiss a Control Center window opened by the user.
        guard (value(app, "AXWindows") as? [AXUIElement])?.isEmpty == true else { throw WorkModeFocusRunError.controlCenterUnavailable }
        let menuItems = try descendants(app)
        let menus = menuItems.filter { string($0, "AXIdentifier") == "com.apple.menuextra.controlcenter" && string($0, "AXRole") == "AXMenuBarItem" }
        guard menus.count == 1 else { throw WorkModeFocusRunError.controlCenterUnavailable }
        menu = menus[0]
        opened = true
        try perform(menus[0], "AXPress")
        let focus = try waitForNode(identifier: "controlcenter-focus-modes")
        guard string(focus, "AXRole") == "AXCheckBox", let focused = binaryValue(focus) else { throw WorkModeFocusRunError.controlCenterUnavailable }
        initialFocusEnabled = focused
        // Use only the separate action actually exposed by the observed UI.
        // Never use the main tile's AXPress, which is itself a Focus toggle.
        let details = actions(focus).filter { $0.components(separatedBy: "\n").first == "Name:show details" }
        guard details.count == 1 else { throw WorkModeFocusRunError.controlCenterUnavailable }
        try perform(focus, details[0])
        _ = try waitForNode(identifier: "focus-modes-header")
    }
    func read() throws -> [ControlCenterFocusChoice] {
        try requireForeground()
        guard let app else { throw WorkModeFocusRunError.controlCenterUnavailable }
        let nodes = try descendants(app)
        guard nodes.contains(where: { string($0, "AXIdentifier") == "focus-modes-header" }) else {
            throw WorkModeFocusRunError.controlCenterUnavailable
        }
        var result: [ControlCenterFocusChoice] = []
        dnd = nil
        for node in nodes {
            if string(node, "AXRole") == "AXCheckBox",
               string(node, "AXIdentifier")?.hasPrefix("focus-mode-activity-") != true {
                throw WorkModeFocusRunError.controlCenterUnavailable
            }
            guard let id = string(node, "AXIdentifier"), id.hasPrefix("focus-mode-activity-") else { continue }
            guard string(node, "AXRole") == "AXCheckBox", let enabled = binaryValue(node) else { throw WorkModeFocusRunError.controlCenterUnavailable }
            result.append(ControlCenterFocusChoice(identifier: id, enabled: enabled))
            if id == ControlCenterFocusChoice.doNotDisturb { dnd = node }
        }
        _ = try ControlCenterFocusDecision.evaluate(.start, choices: result)
        // An active aggregate tile with no recognizable selected mode is not
        // evidence that all modes are off (for example after a macOS UI change).
        guard !initialFocusEnabled || didAttemptToggle || result.contains(where: \.enabled) else { throw WorkModeFocusRunError.controlCenterUnavailable }
        return result
    }
    func pressDoNotDisturb(expectedEnabled: Bool) throws {
        let current = try read()
        guard try ControlCenterFocusDecision.evaluate(expectedEnabled ? .end : .start, choices: current) == .toggle else {
            throw WorkModeFocusRunError.controlCenterUnavailable
        }
        guard let dnd, (value(dnd, "AXEnabled") as? NSNumber)?.boolValue == true else { throw WorkModeFocusRunError.controlCenterUnavailable }
        didAttemptToggle = true
        try perform(dnd, "AXPress")
    }
    func waitForReadback(enabled: Bool) throws {
        repeat {
            let choices = try read()
            if choices.first(where: { $0.identifier == ControlCenterFocusChoice.doNotDisturb })?.enabled == enabled,
               !enabled || !choices.contains(where: { $0.identifier != ControlCenterFocusChoice.doNotDisturb && $0.enabled }) { return }
            Thread.sleep(forTimeInterval: 0.04)
        } while ProcessInfo.processInfo.systemUptime < deadline
        throw WorkModeFocusRunError.invalidResponse
    }
    func close() {
        if opened, let menu { _ = AXUIElementPerformAction(menu, kAXCancelAction as CFString) }
        opened = false; menu = nil; app = nil; dnd = nil; foregroundPID = nil; controlCenterPID = nil; didAttemptToggle = false
    }
    private func waitForNode(identifier: String) throws -> AXUIElement {
        guard let app else { throw WorkModeFocusRunError.controlCenterUnavailable }
        repeat {
            let matches = try descendants(app).filter { string($0, "AXIdentifier") == identifier }
            if matches.count == 1 { return matches[0] }
            if matches.count > 1 { break }
            Thread.sleep(forTimeInterval: 0.04)
        } while ProcessInfo.processInfo.systemUptime < deadline
        throw WorkModeFocusRunError.controlCenterUnavailable
    }
    private func descendants(_ root: AXUIElement) throws -> [AXUIElement] {
        var result: [AXUIElement] = [], pending = [(root, 0)], seen: [AXUIElement] = []
        while let (node, depth) = pending.popLast() {
            guard ProcessInfo.processInfo.systemUptime < deadline, result.count < 160, depth < 10 else { throw WorkModeFocusRunError.controlCenterUnavailable }
            guard !seen.contains(where: { CFEqual($0, node) }) else { continue }
            seen.append(node)
            result.append(node)
            for key in ["AXChildren", "AXMenuBar", "AXExtrasMenuBar", "AXWindows"] {
                guard let child = value(node, key) else { continue }
                if CFGetTypeID(child) == AXUIElementGetTypeID() { pending.append((unsafeBitCast(child, to: AXUIElement.self), depth + 1)) }
                else if let children = child as? [AXUIElement] { pending.append(contentsOf: children.map { ($0, depth + 1) }) }
            }
        }
        return result
    }
    private func value(_ node: AXUIElement, _ key: String) -> CFTypeRef? {
        guard ProcessInfo.processInfo.systemUptime < deadline else { return nil }
        var result: CFTypeRef?
        return AXUIElementCopyAttributeValue(node, key as CFString, &result) == .success ? result : nil
    }
    private func string(_ node: AXUIElement, _ key: String) -> String? { value(node, key) as? String }
    private func binaryValue(_ node: AXUIElement) -> Bool? {
        guard let number = value(node, "AXValue") as? NSNumber, number == 0 || number == 1 else { return nil }
        return number.boolValue
    }
    private func actions(_ node: AXUIElement) -> [String] {
        guard ProcessInfo.processInfo.systemUptime < deadline else { return [] }
        var names: CFArray?
        return AXUIElementCopyActionNames(node, &names) == .success ? names as? [String] ?? [] : []
    }
    private func perform(_ node: AXUIElement, _ action: String) throws {
        try requireForeground()
        guard ProcessInfo.processInfo.systemUptime < deadline, actions(node).contains(action),
              AXUIElementPerformAction(node, action as CFString) == .success else { throw WorkModeFocusRunError.controlCenterUnavailable }
    }
    private func requireForeground() throws {
        let current = NSWorkspace.shared.frontmostApplication?.processIdentifier
        guard current == foregroundPID || current == controlCenterPID else { throw WorkModeFocusRunError.interrupted }
    }
}
