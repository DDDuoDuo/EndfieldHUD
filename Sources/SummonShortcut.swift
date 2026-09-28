import AppKit
import Carbon

/// One physical key plus one or two modifiers. The stored bit mask is our own
/// format, rather than AppKit/Carbon raw values, so it stays stable on disk.
struct SummonShortcut: Codable, Equatable {
    struct Modifiers: OptionSet, Codable, Equatable {
        let rawValue: UInt
        init(rawValue: UInt) { self.rawValue = rawValue }
        static let control = Self(rawValue: 1 << 0)
        static let option = Self(rawValue: 1 << 1)
        static let shift = Self(rawValue: 1 << 2)
        static let command = Self(rawValue: 1 << 3)
        static let supported: Self = [.control, .option, .shift, .command]

        init(flags: NSEvent.ModifierFlags) {
            var result: Self = []
            if flags.contains(.control) { result.insert(.control) }
            if flags.contains(.option) { result.insert(.option) }
            if flags.contains(.shift) { result.insert(.shift) }
            if flags.contains(.command) { result.insert(.command) }
            self = result
        }

        var carbonValue: UInt32 {
            var result: UInt32 = 0
            if contains(.control) { result |= UInt32(controlKey) }
            if contains(.option) { result |= UInt32(optionKey) }
            if contains(.shift) { result |= UInt32(shiftKey) }
            if contains(.command) { result |= UInt32(cmdKey) }
            return result
        }

        var appKitValue: NSEvent.ModifierFlags {
            var result: NSEvent.ModifierFlags = []
            if contains(.control) { result.insert(.control) }
            if contains(.option) { result.insert(.option) }
            if contains(.shift) { result.insert(.shift) }
            if contains(.command) { result.insert(.command) }
            return result
        }
    }

    let keyCode: UInt16
    let modifiers: Modifiers
    static let `default` = SummonShortcut(keyCode: 50, modifiers: [.control])
    /// Used only by the HUD's local text editors; no keyboard observation.
    static var active = SummonShortcut.default

    init(keyCode: UInt16, modifiers: Modifiers) {
        self.keyCode = keyCode; self.modifiers = modifiers
    }

    init(event: NSEvent) {
        self.init(keyCode: event.keyCode, modifiers: Modifiers(flags: event.modifierFlags))
    }

    func matches(event: NSEvent) -> Bool {
        event.keyCode == keyCode && Modifiers(flags: event.modifierFlags) == modifiers
    }

    var displayName: String {
        var parts: [String] = []
        if modifiers.contains(.control) { parts.append("Ctrl") }
        if modifiers.contains(.option) { parts.append("Option") }
        if modifiers.contains(.shift) { parts.append("Shift") }
        if modifiers.contains(.command) { parts.append("⌘") }
        parts.append(Self.keyNames[keyCode] ?? "Key \(keyCode)")
        return parts.joined(separator: " + ")
    }

    /// AppKit renders these in the native, trailing shortcut column. Translate
    /// printable physical keys without modifiers: AppKit draws the modifier
    /// glyphs separately, while Carbon remains the global shortcut owner.
    var menuKeyEquivalent: String {
        menuKeyEquivalent(translating: Self.keyboardLayoutCharacter)
    }

    func menuKeyEquivalent(translating translate: (UInt16) -> String?) -> String {
        if let special = Self.menuSpecialKeys[keyCode] { return special }
        guard let name = Self.keyNames[keyCode] else { return "" }
        if let character = translate(keyCode), character.count == 1 {
            return character.lowercased()
        }
        let fallback = name.replacingOccurrences(of: "Keypad ", with: "")
            .replacingOccurrences(of: "−", with: "-").lowercased()
        return fallback.count == 1 ? fallback : ""
    }

    private static func keyboardLayoutCharacter(_ keyCode: UInt16) -> String? {
        guard let source = TISCopyCurrentASCIICapableKeyboardLayoutInputSource()?.takeRetainedValue(),
              let pointer = TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData) else { return nil }
        let data = Unmanaged<CFData>.fromOpaque(pointer).takeUnretainedValue()
        guard let bytes = CFDataGetBytePtr(data) else { return nil }
        let layout = UnsafeRawPointer(bytes).assumingMemoryBound(to: UCKeyboardLayout.self)
        var deadKeyState: UInt32 = 0
        var length = 0
        var characters = [UniChar](repeating: 0, count: 8)
        let result = UCKeyTranslate(layout, keyCode, UInt16(kUCKeyActionDisplay), 0,
                                    UInt32(LMGetKbdType()), OptionBits(kUCKeyTranslateNoDeadKeysMask),
                                    &deadKeyState, characters.count, &length, &characters)
        guard result == noErr, length > 0 else { return nil }
        return String(utf16CodeUnits: characters, count: length)
    }

    private static let menuSpecialKeys: [UInt16: String] = {
        func key(_ code: Int) -> String { String(UnicodeScalar(code)!) }
        var keys: [UInt16: String] = [
            36: "\r", 48: "\t", 49: " ", 51: key(Int(NSDeleteCharacter)),
            53: "\u{1b}", 71: key(Int(NSClearLineFunctionKey)),
            76: key(Int(NSEnterCharacter)), 114: key(Int(NSHelpFunctionKey)),
            115: key(Int(NSHomeFunctionKey)), 116: key(Int(NSPageUpFunctionKey)),
            117: key(Int(NSDeleteFunctionKey)), 119: key(Int(NSEndFunctionKey)),
            121: key(Int(NSPageDownFunctionKey)), 123: key(Int(NSLeftArrowFunctionKey)),
            124: key(Int(NSRightArrowFunctionKey)), 125: key(Int(NSDownArrowFunctionKey)),
            126: key(Int(NSUpArrowFunctionKey))
        ]
        let functionKeyCodes: [UInt16] = [122, 120, 99, 118, 96, 97, 98, 100, 101, 109,
                                          103, 111, 105, 107, 113, 106, 64, 79, 80, 90]
        for (index, code) in functionKeyCodes.enumerated() {
            keys[code] = key(Int(NSF1FunctionKey) + index)
        }
        return keys
    }()

    var validationError: String? {
        guard modifiers.rawValue & ~Modifiers.supported.rawValue == 0 else {
            return L10n.text("That modifier is not supported.", "不支持该修饰键。")
        }
        guard (1...2).contains(modifiers.rawValue.nonzeroBitCount) else {
            return L10n.text("Use one key with one or two modifiers (up to three keys).",
                             "请使用一个普通键加一至两个修饰键（最多三个键）。")
        }
        guard Self.keyNames[keyCode] != nil else {
            return L10n.text("Choose a letter, number, punctuation, arrow, or function key.",
                             "请选择字母、数字、标点、方向键或功能键。")
        }
        // Protect common editor/application commands, even if their frontmost
        // app has not registered a global hotkey. OS-customized bindings are
        // checked separately through CopySymbolicHotKeys when applying.
        let standardCommandKeys: Set<UInt16> = [0, 1, 3, 4, 5, 6, 7, 8, 9, 12, 13, 17, 31, 35, 45, 46, 48, 49, 50]
        let shiftedCommandKeys: Set<UInt16> = [1, 5, 6, 13, 17, 35, 45, 48, 50]
        if (modifiers == [.command] && standardCommandKeys.contains(keyCode)) ||
            (modifiers == [.command, .shift] && shiftedCommandKeys.contains(keyCode)) {
            return L10n.text("That combination is reserved for common macOS or editing commands.",
                             "该组合用于常见的 macOS 或编辑命令，请选择其他快捷键。")
        }
        return nil
    }

    /// Named physical keys keep the summon binding stable across input methods.
    private static let keyNames: [UInt16: String] = [
        0:"A", 1:"S", 2:"D", 3:"F", 4:"H", 5:"G", 6:"Z", 7:"X", 8:"C", 9:"V",
        11:"B", 12:"Q", 13:"W", 14:"E", 15:"R", 16:"Y", 17:"T", 18:"1", 19:"2",
        20:"3", 21:"4", 22:"6", 23:"5", 24:"=", 25:"9", 26:"7", 27:"−", 28:"8",
        29:"0", 30:"]", 31:"O", 32:"U", 33:"[", 34:"I", 35:"P", 36:"Return",
        37:"L", 38:"J", 39:"'", 40:"K", 41:";", 42:"\\", 43:",", 44:"/", 45:"N",
        46:"M", 47:".", 48:"Tab", 49:"Space", 50:"`", 51:"Delete", 53:"Escape",
        64:"F17", 65:"Keypad .", 67:"Keypad *", 69:"Keypad +", 71:"Clear",
        75:"Keypad /", 76:"Enter", 78:"Keypad −", 79:"F18", 80:"F19", 81:"Keypad =",
        82:"Keypad 0", 83:"Keypad 1", 84:"Keypad 2", 85:"Keypad 3", 86:"Keypad 4",
        87:"Keypad 5", 88:"Keypad 6", 89:"Keypad 7", 90:"F20", 91:"Keypad 8", 92:"Keypad 9",
        96:"F5", 97:"F6", 98:"F7", 99:"F3", 100:"F8", 101:"F9", 103:"F11",
        105:"F13", 106:"F16", 107:"F14", 109:"F10", 111:"F12", 113:"F15",
        114:"Help", 115:"Home", 116:"Page Up", 117:"Forward Delete", 118:"F4",
        119:"End", 120:"F2", 121:"Page Down", 122:"F1", 123:"←", 124:"→", 125:"↓", 126:"↑"
    ]
}
