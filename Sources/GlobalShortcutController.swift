import AppKit
import Carbon

/// A standard macOS registered hotkey, with no event tap or Accessibility use.
enum GlobalShortcutStatus: Equatable {
    case stopped
    case ready
    case unavailable
}

final class GlobalShortcutController {
    var onToggle: (() -> Bool)?
    var onStatusChange: ((GlobalShortcutStatus) -> Void)?
    private(set) var status: GlobalShortcutStatus = .stopped
    private(set) var shortcut: SummonShortcut = .default
    private(set) var isCapturing = false

    private var wantsRunning = false
    private var hotKey: EventHotKeyRef?
    private var eventHandler: EventHandlerRef?
    private var policy = ShortcutPolicy()
    private var registrationID: UInt32 = 1
    private let signature: OSType = 0x45464348 // EFCH

    var statusDescription: String {
        let name = shortcut.displayName
        if isCapturing { return L10n.text("Press your new shortcut. Escape cancels.", "请按新的快捷键，Escape 取消。") }
        switch status {
        case .stopped: return L10n.text("\(name) is stopped.", "\(name) 已停用。")
        case .ready: return L10n.text("\(name) is ready, including in text fields.", "\(name) 已就绪，在文本框中也可使用。")
        case .unavailable:
            return L10n.text("\(name) is unavailable or in use. Open from the menu bar.",
                             "\(name) 不可用或已被占用，请从菜单栏打开浮层。")
        }
    }

    func start(shortcut candidate: SummonShortcut = .default) {
        precondition(Thread.isMainThread)
        wantsRunning = true
        if hotKey != nil {
            _ = applyShortcut(candidate)
            return
        }
        shortcut = candidate.validationError == nil ? candidate : .default
        SummonShortcut.active = shortcut
        policy.shortcut = shortcut
        registerIfNeeded()
    }

    /// Register the candidate before releasing the old key. A conflict leaves
    /// the previous reservation and persisted value intact. During capture the
    /// old key is temporarily suspended so it can itself be recorded again.
    @discardableResult
    func applyShortcut(_ candidate: SummonShortcut) -> String? {
        precondition(Thread.isMainThread)
        if let error = candidate.validationError { return error }
        if candidate == shortcut, hotKey != nil { return nil }
        if let error = systemConflict(for: candidate) { return error }
        guard installHandlerIfNeeded() else {
            return L10n.text("Could not register the shortcut. Your previous shortcut is unchanged.",
                             "无法注册快捷键，原快捷键保持不变。")
        }
        let nextID = registrationID &+ 1
        var candidateKey: EventHotKeyRef?
        let result = RegisterEventHotKey(UInt32(candidate.keyCode), candidate.modifiers.carbonValue,
                                        EventHotKeyID(signature: signature, id: nextID),
                                        GetApplicationEventTarget(), OptionBits(kEventHotKeyExclusive), &candidateKey)
        guard result == noErr, let registered = candidateKey else {
            if let unused = candidateKey { UnregisterEventHotKey(unused) }
            return L10n.text("That shortcut is already in use or unavailable. Choose another combination.",
                             "该快捷键已被占用或不可用，请选择其他组合。")
        }
        if let old = hotKey { UnregisterEventHotKey(old) }
        hotKey = registered
        registrationID = nextID
        shortcut = candidate
        SummonShortcut.active = candidate
        policy.shortcut = candidate
        policy.reset()
        // A successful Apply finishes recording; there is no window during
        // which a newly stored hotkey has not actually been registered.
        isCapturing = false
        wantsRunning = true
        setStatus(.ready)
        return nil
    }

    func beginCapture() {
        precondition(Thread.isMainThread)
        guard !isCapturing else { return }
        isCapturing = true
        unregisterKey()
    }

    func endCapture() {
        precondition(Thread.isMainThread)
        guard isCapturing else { return }
        isCapturing = false
        registerIfNeeded()
    }

    func stop() {
        precondition(Thread.isMainThread)
        wantsRunning = false
        isCapturing = false
        unregister()
        setStatus(.stopped)
    }

    func refreshRegistration() {
        precondition(Thread.isMainThread)
        guard wantsRunning else { return }
        registerIfNeeded()
    }

    func retryRegistration() {
        precondition(Thread.isMainThread)
        wantsRunning = true
        unregister()
        registerIfNeeded()
    }

    func resetPressedState() {
        precondition(Thread.isMainThread)
        policy.reset()
    }

    deinit { unregister() }

    private func registerIfNeeded() {
        guard wantsRunning, !isCapturing, hotKey == nil else { return }
        if applyShortcut(shortcut) != nil { setStatus(.unavailable) }
    }

    private func installHandlerIfNeeded() -> Bool {
        if eventHandler != nil { return true }
        var eventTypes = [
            EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyPressed)),
            EventTypeSpec(eventClass: OSType(kEventClassKeyboard), eventKind: UInt32(kEventHotKeyReleased))
        ]
        let result = InstallEventHandler(GetApplicationEventTarget(), { _, event, context in
            guard let event = event, let context = context else { return OSStatus(eventNotHandledErr) }
            return Unmanaged<GlobalShortcutController>.fromOpaque(context).takeUnretainedValue().handle(event)
        }, eventTypes.count, &eventTypes, Unmanaged.passUnretained(self).toOpaque(), &eventHandler)
        return result == noErr && eventHandler != nil
    }

    private func systemConflict(for candidate: SummonShortcut) -> String? {
        var copied: Unmanaged<CFArray>?
        guard CopySymbolicHotKeys(&copied) == noErr, let records = copied?.takeRetainedValue() as? [[String: Any]] else {
            return L10n.text("macOS shortcut conflicts could not be checked. Try again.",
                             "暂时无法检查 macOS 快捷键冲突，请重试。")
        }
        let conflict = records.contains { record in
            guard let enabled = record[kHISymbolicHotKeyEnabled as String] as? NSNumber, enabled.boolValue,
                  let key = record[kHISymbolicHotKeyCode as String] as? NSNumber,
                  let flags = record[kHISymbolicHotKeyModifiers as String] as? NSNumber else { return false }
            let supported = UInt32(controlKey | optionKey | shiftKey | cmdKey)
            return key.uint16Value == candidate.keyCode && flags.uint32Value & supported == candidate.modifiers.carbonValue
        }
        return conflict ? L10n.text("That shortcut is enabled in macOS Keyboard Shortcuts. Choose another combination.",
                                    "该快捷键已在 macOS 键盘快捷键中启用，请选择其他组合。") : nil
    }

    private func unregisterKey() {
        if let hotKey = hotKey { UnregisterEventHotKey(hotKey) }
        hotKey = nil
        policy.reset()
    }

    private func unregister() {
        unregisterKey()
        if let handler = eventHandler { RemoveEventHandler(handler) }
        eventHandler = nil
    }

    private func setStatus(_ value: GlobalShortcutStatus) {
        guard status != value else { return }
        status = value
        onStatusChange?(value)
    }

    private func handle(_ event: EventRef) -> OSStatus {
        var identifier = EventHotKeyID()
        let result = GetEventParameter(event, EventParamName(kEventParamDirectObject),
                                       EventParamType(typeEventHotKeyID), nil,
                                       MemoryLayout<EventHotKeyID>.size, nil, &identifier)
        guard result == noErr, hotKey != nil, !isCapturing,
              identifier.signature == signature, identifier.id == registrationID else {
            return OSStatus(eventNotHandledErr)
        }
        let kind = GetEventKind(event)
        guard kind == UInt32(kEventHotKeyPressed) || kind == UInt32(kEventHotKeyReleased) else {
            return OSStatus(eventNotHandledErr)
        }
        let input = ShortcutPolicy.Input(keyCode: shortcut.keyCode,
                                         phase: kind == UInt32(kEventHotKeyPressed) ? .down : .up,
                                         modifiers: shortcut.modifiers, isRepeat: false)
        if policy.decision(for: input) == .requestToggle { _ = onToggle?() }
        return noErr
    }
}
