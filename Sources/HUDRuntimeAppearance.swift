import AppKit

/// Shared by retained module canvases as well as the main shell. This changes
/// only this application's presentation, never macOS accessibility preferences.
enum HUDRuntimeAppearance {
    static var configuration = AppConfiguration.defaults
    static var accent: NSColor { configuration.accentColor }
    static var reduceMotion: Bool {
        configuration.reduceMotion || NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
    }
    static var ambientEnabled: Bool {
        configuration.ambientAnimation && !configuration.lowPowerVisualMode
    }
}
