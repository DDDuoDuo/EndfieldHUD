import AppKit
import Carbon

enum SummonShortcutTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: @autoclosure () -> Bool, _ message: String) {
            count += 1
            precondition(value(), message)
        }
        let originalLanguage = L10n.language
        defer { L10n.language = originalLanguage }
        L10n.language = .english
        check(SummonShortcut.default.displayName == "Ctrl + `", "Default is Control with physical backtick")
        check(SummonShortcut.default.validationError == nil, "Default passes policy")
        check(SummonShortcut.default.modifiers.carbonValue == UInt32(controlKey), "Stable mask translates for Carbon")
        check(SummonShortcut.default.modifiers.appKitValue == [.control], "Native menu uses Control without implicit Command")
        check(SummonShortcut.default.menuKeyEquivalent(translating: { _ in "`" }) == "`", "Default menu shortcut shows backtick in its shortcut column")
        let menuLetter = SummonShortcut(keyCode: 40, modifiers: [.control, .option])
        check(menuLetter.modifiers.appKitValue == [.control, .option], "Custom menu chord retains both modifiers")
        check(menuLetter.menuKeyEquivalent(translating: { _ in "K" }) == "k", "Letters use a base character with separate modifier flags")
        check(menuLetter.menuKeyEquivalent(translating: { _ in "é" }) == "é", "Native menu character follows the user's keyboard layout")
        check(menuLetter.menuKeyEquivalent(translating: { _ in nil }) == "k", "Unavailable keyboard layouts fall back to physical key label")
        check(SummonShortcut(keyCode: 27, modifiers: [.control]).menuKeyEquivalent(translating: { _ in nil }) == "-", "Minus fallback is a typed ASCII key, not a mathematical minus")
        check(SummonShortcut(keyCode: 122, modifiers: [.control]).menuKeyEquivalent == String(UnicodeScalar(Int(NSF1FunctionKey))!), "Function keys use AppKit's function-key equivalent")
        check(SummonShortcut(keyCode: 126, modifiers: [.option, .shift]).menuKeyEquivalent == String(UnicodeScalar(Int(NSUpArrowFunctionKey))!), "Arrow keys show native shortcut glyphs")
        check(SummonShortcut.Modifiers.supported.appKitValue == [.control, .option, .shift, .command], "Every supported modifier has a native glyph")
        check(SummonShortcut(keyCode: 65535, modifiers: [.control]).menuKeyEquivalent == "", "Invalid persisted key never creates a different menu shortcut")
        check(SummonShortcut(keyCode: 50, modifiers: []).validationError != nil, "Plain typing cannot become a global key")
        check(SummonShortcut(keyCode: 50, modifiers: [.control, .option, .shift]).validationError != nil, "Four-key chords exceed limit")
        check(SummonShortcut(keyCode: 50, modifiers: [.control, .option]).validationError == nil, "Three-key chord is supported")
        check(SummonShortcut(keyCode: 58, modifiers: [.control]).validationError != nil, "Modifier-only key code is rejected")
        check(SummonShortcut(keyCode: 65535, modifiers: [.control]).validationError != nil, "Unknown key code is rejected")
        check(SummonShortcut(keyCode: 8, modifiers: [.command]).validationError != nil, "Common copy command is reserved")
        check(SummonShortcut(keyCode: 6, modifiers: [.command, .shift]).validationError != nil, "Common redo command is reserved")
        check(SummonShortcut(keyCode: 40, modifiers: [.control, .option]).validationError == nil, "User's custom physical chord is accepted")
        check(SummonShortcut(keyCode: 50, modifiers: .init(rawValue: 32)).validationError != nil, "Unexpected persisted bits are rejected")
        check(SummonShortcut(keyCode: 122, modifiers: [.control]).displayName == "Ctrl + F1", "Function key labels are usable")
        let custom = SummonShortcut(keyCode: 40, modifiers: [.control, .option])
        let encoded = try! JSONEncoder().encode(custom)
        check((try! JSONDecoder().decode(SummonShortcut.self, from: encoded)) == custom, "Custom keys preserve physical code and modifier bits")
        func event(_ key: UInt16 = 50, _ flags: NSEvent.ModifierFlags = [.control], repeated: Bool = false) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags,
                            timestamp: 0, windowNumber: 0, context: nil, characters: "`",
                            charactersIgnoringModifiers: "`", isARepeat: repeated, keyCode: key)!
        }
        check(SummonShortcut(event: event()) == .default, "Event capture produces physical default")
        check(SummonShortcut.default.matches(event: event(50, [.control, .capsLock])), "Caps Lock does not break registration matching")
        check(!SummonShortcut.default.matches(event: event(50, [.shift])), "Old Shift binding is no longer default")
        check(!SummonShortcut.default.matches(event: event(40, [.control])), "Wrong key cannot dismiss an inline editor")
        check(!SummonShortcut.default.matches(event: event(50, [.control, .option])), "Extra modifier cannot accidentally dismiss")
        L10n.language = .simplifiedChinese
        check(SummonShortcut(keyCode: 50, modifiers: []).validationError?.contains("三个键") == true, "Validation message is localized")
        return count
    }
}
