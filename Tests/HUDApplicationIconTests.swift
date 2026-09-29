import AppKit

enum HUDApplicationIconTests {
    static func run() -> Int {
        var checks = 0
        func check(_ condition: Bool, _ message: String) { checks += 1; precondition(condition, message) }
        _ = NSApplication.shared
        let suite = "EndfieldHUD.IconTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        let language = L10n.language
        defer { defaults.removePersistentDomain(forName: suite); L10n.language = language }
        let store = ConfigurationStore(defaults: defaults)
        check(store.configuration.applicationIcon == .endfield, "A new installation uses the supplied Endfield icon")
        check(HUDApplicationIcon.pickerCases.count == 64, "The picker includes 34 original presets and 30 verified Endfield icons")
        check(HUDApplicationIcon.pickerCases == HUDApplicationIcon.allCases.filter { $0 != .originium && $0 != .orundum },
              "Only the two retired hand-drawn currency icons are omitted, preserving the remaining picker order")
        check(HUDApplicationIcon.pickerCases.contains(.gameOrigeometry) && HUDApplicationIcon.pickerCases.contains(.gameOroberyl),
              "The actual Endfield currency artwork remains selectable")
        check(HUDApplicationIcon.allCases.contains(.originium) && HUDApplicationIcon.allCases.contains(.orundum)
              && HUDApplicationIcon.allCases.contains(.contingencyContract) && HUDApplicationIcon.allCases.contains(.ambienceSynesthesia),
              "Saved currency and faction choices retain their raw values and artwork")
        var artwork = Set<Data>()
        for preset in HUDApplicationIcon.allCases {
            var configuration = store.configuration; configuration.applicationIcon = preset; store.update(configuration)
            check(ConfigurationStore(defaults: defaults).configuration.applicationIcon == preset, "Every icon persists across launches")
            let large = preset.image(size: 512), menu = preset.menuBarImage()
            check(large.size == NSSize(width: 512, height: 512), "Application icons have native square dimensions")
            check(menu.size == NSSize(width: 20, height: 20), "Larger menu icons stay within the existing square menu item")
            check(menu.isTemplate == (preset != .perlica), "Emblems adapt to the menu bar while the portrait retains its color")
            check(preset.menuBarImage() === menu, "Repeated status updates reuse cached icon pixels")
            if let pixels = preset.image(size: 64).tiffRepresentation {
                check(artwork.insert(pixels).inserted, "Every preset uses distinct artwork rather than silently falling back to the battery icon")
            } else { check(false, "Every preset can supply its picker artwork") }
            if let cg = menu.cgImage(forProposedRect: nil, context: nil, hints: nil) {
                let rep = NSBitmapImageRep(cgImage: cg)
                let nonempty = (0..<rep.pixelsHigh).contains { y in
                    (0..<rep.pixelsWide).contains { x in (rep.colorAt(x: x, y: y)?.alphaComponent ?? 0) > 0.5 }
                }
                check(nonempty, "Every preset has visible menu-bar artwork")
                if preset != .perlica {
                    check((rep.colorAt(x: 0, y: 0)?.alphaComponent ?? 1) < 0.05, "Template icons remove sheet background corners")
                }
            } else { check(false, "Menu icon pixels are available") }
        }
        for preset in HUDApplicationIcon.pickerCases where preset.gameIcon != nil {
            check(preset.gameIcon?.sourceImage() != nil, "Every offered game preset has bundled source artwork")
        }
        check(HUDApplicationIcon.gameBaker.gameIcon == .baker && HUDApplicationIcon.gameExclamationMark.gameIcon == .exclamationMark
              && HUDApplicationIcon.gameStrength.gameIcon == .strength, "New presets retain their exact wiki source identities")
        check(AppShortcutArtwork.gameIcon(for: .textBubble) == .baker && AppShortcutArtwork.gameIcon(for: .globe) == .store,
              "Text bubble and globe presets use the specifically requested Baker and Store artwork")
        let depot = EndfieldGameIcon.depot
        let tinted = depot.cgImage(size: 36, tint: .systemRed)!
        check(tinted.width == 36 && tinted.height == 36, "Game icons respect the requested raster size")
        check(depot.image(size: 36, tint: .systemRed) === depot.image(size: 36, tint: .systemRed),
              "Repeated control redraws reuse tinted game icon pixels")
        let parent = CALayer()
        check(depot.add(to: parent, rect: CGRect(x: 4, y: 5, width: 18, height: 18), tint: .white, contentsScale: 2),
              "A verified game asset can be installed as a control glyph")
        check(parent.sublayers?.first?.frame == CGRect(x: 4, y: 5, width: 18, height: 18)
              && parent.sublayers?.first?.contentsGravity == .resizeAspect,
              "Control artwork preserves the supplied geometry and aspect ratio")
        defaults.set("unknown-icon", forKey: "applicationIcon")
        check(ConfigurationStore(defaults: defaults).configuration.applicationIcon == .endfield, "Unknown saved presets fall back safely")
        return checks
    }
}
