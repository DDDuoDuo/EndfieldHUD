import AppKit

/// Shared by retained module canvases as well as the main shell. This changes
/// only this application's presentation, never macOS accessibility preferences.
enum HUDRuntimeAppearance {
    /// Set by the host on every configuration change; it also applies Theme
    /// and the theme colour to the redesign's colours (`applyPalette`).
    static var configuration = AppConfiguration.defaults {
        didSet { applyPalette() }
    }
    /// Theme resolved: Light, or System while macOS is in light appearance.
    static var prefersLight: Bool {
        switch configuration.theme {
        case .dark: return false
        case .light: return true
        case .system: return NSApp?.effectiveAppearance.bestMatch(from: [.darkAqua, .aqua]) == .aqua
        }
    }
    /// Hands Theme and the theme colour to `HUDColor`; a change bumps
    /// `HUDColor.paletteGeneration` (the host then calls
    /// `HUDDesignContext.refreshPalette`). Also called when macOS changes
    /// appearance, for System.
    @discardableResult
    static func applyPalette() -> Bool { HUDColor.apply(accentHex: configuration.accentHex, light: prefersLight) }
    private static let fixtureReduceMotion = CommandLine.arguments.contains("--ui-test")
        && CommandLine.arguments.contains("--reduce-motion-smoke-test")
    static var accent: NSColor { configuration.accentColor }
    static var reduceMotion: Bool {
        #if HUD_WATCH_MOTION_PREVIEW
        // Only the separately compiled fixture renderer ignores its CI host's
        // preference. The shipped app continues to respect macOS accessibility.
        return configuration.reduceMotion
        #else
        return fixtureReduceMotion || configuration.reduceMotion || NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        #endif
    }
    static var ambientEnabled: Bool {
        configuration.ambientAnimation && !configuration.lowPowerVisualMode
    }
}
