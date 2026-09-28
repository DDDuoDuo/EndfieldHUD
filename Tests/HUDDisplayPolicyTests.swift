import AppKit

enum HUDDisplayPolicyTests {
    static func run() -> Int {
        var assertions = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) {
            assertions += 1; precondition(condition(), message)
        }
        let internalID = "AAAAAAAA-1111-2222-3333-444444444444"
        let externalID = "BBBBBBBB-1111-2222-3333-444444444444"
        let builtIn = HUDDisplayDescriptor(uuid: internalID, name: "Built-in Display", displayID: 1, frame: CGRect(x: 0, y: 0, width: 1440, height: 900))
        let external = HUDDisplayDescriptor(uuid: externalID, name: "Studio Display", displayID: 9, frame: CGRect(x: -2560, y: 100, width: 2560, height: 1440))
        let screens = [builtIn, external]
        var config = AppConfiguration.defaults
        func resolve(_ displays: [HUDDisplayDescriptor] = screens, pointer: CGPoint = CGPoint(x: -100, y: 500), primary: UInt32? = 1) -> Int? {
            HUDDisplayPolicy.resolvedIndex(configuration: config, displays: displays, pointerLocation: pointer, primaryDisplayID: primary)
        }
        check(resolve() == 1, "Automatic mode opens on the display containing the pointer")
        check(resolve(pointer: CGPoint(x: 4000, y: -100)) == 0, "An offscreen pointer safely falls back to the primary display")
        config.openOnActiveDisplay = false
        check(resolve() == 0, "A migrated disabled active-display preference still targets the primary display")
        check(resolve([external, builtIn]) == 1, "Primary-display selection does not depend on list ordering")
        config.hudDisplayUUID = externalID; config.hudDisplayName = external.name
        check(resolve(pointer: CGPoint(x: 100, y: 100)) == 1, "An explicit display overrides pointer position")
        let reassigned = HUDDisplayDescriptor(uuid: externalID, name: external.name, displayID: 44, frame: external.frame)
        check(resolve([builtIn, reassigned], pointer: CGPoint(x: 100, y: 100)) == 1, "A display UUID survives a changed macOS display number")
        check(resolve([builtIn]) == 0, "A disconnected saved display falls back to the available monitor")
        check(config.hudDisplayUUID == externalID, "Fallback does not erase the saved choice")
        check(resolve(pointer: CGPoint(x: 100, y: 100)) == 1, "Reconnecting automatically restores the selected monitor")
        config.hudDisplayUUID = "CCCCCCCC-1111-2222-3333-444444444444"
        check(resolve() == 1, "Unavailable fixed displays use the pointer monitor before primary")
        check(resolve(pointer: CGPoint(x: 9000, y: 9000)) == 0, "Unavailable target and pointer fall back to primary")
        check(resolve([], primary: nil) == nil, "No available displays cannot produce an invalid index")
        check(resolve([external], pointer: CGPoint(x: 9000, y: 9000), primary: nil) == 0, "Missing primary still allows an available display")
        let language = L10n.language
        L10n.language = .english
        defer { L10n.language = language }
        check(HUDDisplayPolicy.selectionTitle(configuration: config, displays: screens) == "Studio Display (disconnected)", "Missing saved displays are clearly identified in settings")
        config.hudDisplayUUID = externalID
        check(HUDDisplayPolicy.selectionTitle(configuration: config, displays: screens) == external.name, "A connected explicit target shows its actual display name")
        config.hudDisplayUUID = nil
        check(HUDDisplayPolicy.selectionTitle(configuration: config, displays: screens) == "Main display", "Legacy primary preference has a clear name")
        config.openOnActiveDisplay = true
        check(HUDDisplayPolicy.selectionTitle(configuration: config, displays: screens) == "Pointer display", "Automatic selection explicitly names the pointer instead of ambiguous active display")
        return assertions
    }
}
