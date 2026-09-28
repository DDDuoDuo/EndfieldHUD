import AppKit

/// UUIDs remain stable when macOS assigns a different transient display number.
struct HUDDisplayDescriptor: Equatable {
    let uuid: String
    let name: String
    let displayID: UInt32
    let frame: CGRect

    var dimensions: String { "\(Int(frame.width)) × \(Int(frame.height))" }
}

enum HUDDisplayPolicy {
    static func connectedDisplays(screens: [NSScreen] = NSScreen.screens) -> [HUDDisplayDescriptor] {
        screens.compactMap { screen in
            guard let id = displayID(for: screen),
                  let uuid = CGDisplayCreateUUIDFromDisplayID(id)?.takeRetainedValue() else { return nil }
            return HUDDisplayDescriptor(uuid: CFUUIDCreateString(nil, uuid) as String,
                name: screen.localizedName, displayID: id, frame: screen.frame)
        }
    }

    static func displayID(for screen: NSScreen) -> UInt32? {
        (screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber)?.uint32Value
    }

    /// A temporarily absent fixed display falls back to the pointer's monitor,
    /// then the primary monitor. Keep the preference so reconnecting restores it.
    static func resolvedIndex(configuration: AppConfiguration,
                              displays: [HUDDisplayDescriptor], pointerLocation: CGPoint,
                              primaryDisplayID: UInt32?) -> Int? {
        guard !displays.isEmpty else { return nil }
        if let uuid = configuration.hudDisplayUUID,
           let index = displays.firstIndex(where: { $0.uuid.caseInsensitiveCompare(uuid) == .orderedSame }) {
            return index
        }
        if configuration.hudDisplayUUID != nil || configuration.openOnActiveDisplay,
           let index = displays.firstIndex(where: { $0.frame.contains(pointerLocation) }) { return index }
        if let primaryDisplayID, let index = displays.firstIndex(where: { $0.displayID == primaryDisplayID }) { return index }
        return displays.startIndex
    }

    static func targetScreen(configuration: AppConfiguration, screens: [NSScreen] = NSScreen.screens,
                             pointerLocation: CGPoint = NSEvent.mouseLocation) -> NSScreen? {
        let displays = connectedDisplays(screens: screens)
        if let index = resolvedIndex(configuration: configuration, displays: displays,
                                     pointerLocation: pointerLocation, primaryDisplayID: CGMainDisplayID()),
           let screen = screens.first(where: { displayID(for: $0) == displays[index].displayID }) { return screen }
        // Some remote/virtual display providers do not vend a UUID. They must
        // still be usable in automatic mode instead of preventing HUD opening.
        if configuration.hudDisplayUUID != nil || configuration.openOnActiveDisplay,
           let screen = screens.first(where: { $0.frame.contains(pointerLocation) }) { return screen }
        return screens.first(where: { displayID(for: $0) == CGMainDisplayID() }) ?? screens.first
    }

    static func selectionTitle(configuration: AppConfiguration, displays: [HUDDisplayDescriptor]) -> String {
        guard let uuid = configuration.hudDisplayUUID else {
            return configuration.openOnActiveDisplay ? L10n.text("Pointer display", "鼠标所在显示器") : L10n.text("Main display", "主显示器")
        }
        if let display = displays.first(where: { $0.uuid.caseInsensitiveCompare(uuid) == .orderedSame }) { return display.name }
        return (configuration.hudDisplayName ?? L10n.text("Saved display", "已选显示器")) + L10n.text(" (disconnected)", "（未连接）")
    }
}
