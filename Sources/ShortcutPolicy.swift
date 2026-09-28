/// State for a single registered physical hotkey. Carbon delivers only this
/// chord; ordinary keyboard input is never inspected by the controller.
struct ShortcutPolicy {
    static let backtickKeyCode: UInt16 = 50
    var shortcut: SummonShortcut = .default
    enum Phase { case down, up }
    enum Decision: Equatable { case passThrough, consume, requestToggle }

    struct Input {
        let keyCode: UInt16
        let phase: Phase
        let modifiers: SummonShortcut.Modifiers
        let isRepeat: Bool
    }

    private(set) var ownsPress = false

    mutating func decision(for input: Input) -> Decision {
        guard input.keyCode == shortcut.keyCode else { return .passThrough }
        if input.phase == .up {
            let owned = ownsPress
            ownsPress = false
            return owned ? .consume : .passThrough
        }
        if ownsPress { return .consume }
        guard input.modifiers == shortcut.modifiers, !input.isRepeat else { return .passThrough }
        // Carbon still owns the whole chord if app state declines the action.
        ownsPress = true
        return .requestToggle
    }

    mutating func reset() { ownsPress = false }
}
