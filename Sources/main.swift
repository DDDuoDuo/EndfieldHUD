import AppKit

#if HUD_CURSOR_DIAGNOSTICS
// Included only by the opt-in native diagnostic build, never release packaging.
if CommandLine.arguments.contains("--ui-test") && CommandLine.arguments.contains("--cursor-diagnostic") {
    HUDCursorDiagnostics.main()
    exit(0)
}
#endif

let application = NSApplication.shared
let delegate = AppDelegate()
application.delegate = delegate
application.run()
