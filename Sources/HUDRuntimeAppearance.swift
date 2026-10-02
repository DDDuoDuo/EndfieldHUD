import AppKit

/// Shared by retained module canvases as well as the main shell. This changes
/// only this application's presentation, never macOS accessibility preferences.
enum HUDRuntimeAppearance {
    static var configuration = AppConfiguration.defaults
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
