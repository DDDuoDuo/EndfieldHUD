#if HUD_CONTENT_REFERENCE
extension HUDSourceWatchView {
    // Added only to the pinned build copy. All binding, artwork, font fitting,
    // localization and projected caption sizing run unchanged source methods.
    var contentReferenceSlots: [[String: Any]] {
        desktopButtons.compactMap { button in
            guard let label = button.label, let icon = desktopIconIDs[button.nodeID] else { return nil }
            return ["buttonID": button.nodeID.rawValue, "path": button.path,
                    "captionID": label.nodeID.rawValue, "iconID": icon.rawValue,
                    "group": button.path.contains("/RightBottomNode/") ? "right"
                        : (button.path.hasSuffix("/TechtreeBtn") || button.path.hasSuffix("/ReportBtn")) ? "bottom" : "left"]
        }
    }
    var contentReferenceInitialBindings: [String: String] {
        Dictionary(uniqueKeysWithValues: desktopBindings.map { ($0.key.rawValue, desktopEntries[$0.value].target.identifier) })
    }
    func contentReferenceBind(_ entry: HUDDesktopWatchNavigation.Entry, to id: HUDSourceID,
                              selected: Bool, dark: Bool,
                              frame: HUDSourceWatchFrameBuilder.Frame,
                              camera: HUDSourceWatchCamera.Frame) throws -> (caption: CALayer, icon: CALayer) {
        precondition(CommandLine.arguments.contains("--ui-test") && desktopMode && window == nil)
        guard let button = desktopButtons.first(where: { $0.nodeID == id }), let label = button.label else {
            throw HUDSourceError.invalid("Unknown original desktop content slot")
        }
        desktopEntries = [entry]
        selectedDesktopModule = selected ? (entry.target.module ?? .power)
            : (entry.target.module == .power ? .system : .power)
        desktopDark = dark
        bindDesktopButtons([id: 0])
        updateDesktopLabels(frame: frame, camera: camera)
        guard let text = desktopLabels[label.nodeID]?.text,
              let iconID = desktopIconIDs[id], let icon = desktopIcons[iconID]?.content,
              text.bounds.width > 0, text.bounds.height > 0 else {
            throw HUDSourceError.invalid("Original local caption/icon did not resolve")
        }
        return (text, icon)
    }
}
#endif
