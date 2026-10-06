import AppKit
import QuartzCore

enum HUDAccountCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let language = L10n.language; L10n.language = .english
        defer { L10n.language = language }
        let canvas = HUDAccountCanvas(), host = NSView(frame: CGRect(x: 0, y: 0, width: 700, height: 600))
        host.wantsLayer = true
        let input = HUDAccountInteraction(canvas: canvas, host: host)
        let projection = CGAffineTransform(a: 1.1, b: 0.12, c: 0.08, d: 0.95, tx: 24, ty: 20)
        input.project = { $0.applying(projection) }; input.unproject = { $0.applying(projection.inverted()) }
        // Factory geometry is independent of the parent module registration.
        host.layer?.addSublayer(canvas.makeContent(for: .power, style: HUDModuleContentStyle(dark: true, accent: .systemPurple, contentsScale: 2)))
        input.setActive(true)
        defer { input.deactivate() }
        var emitted: [HUDAccountCanvas.Action] = []
        canvas.onAction = { emitted.append($0) }
        check(canvas.layer.bounds == CGRect(x: 0, y: 0, width: 400, height: 334), "Account module uses the established central content geometry")
        check(canvas.accessibleActions.first { $0.id == "connect" }?.enabled == true, "An unlinked account can connect")
        check(canvas.accessibleActions.first { $0.id == "refresh" }?.enabled == false, "Unlinked account cannot start a remote sync")
        canvas.perform("connect"); check(emitted == [.connect(.china)], "Connect routes the chosen server without logging in from the canvas")
        canvas.perform("header")
        check(canvas.isPopoverOpen && input.capturesPointer && !input.isInputLocked, "Header settings capture clicks without freezing tilt")
        check(canvas.accessibleActions.count == 4 && canvas.accessibleActions.allSatisfy { $0.id.hasPrefix("header:") }, "A dropdown exposes only its own accessible controls")
        check(canvas.popoverBounds.map { canvas.layer.bounds.contains($0) } == true, "Header dropdown stays inside the HUD content")
        let bounds = canvas.popoverBounds!
        let layers = canvas.layer.sublayers?.first { $0.name == "account.secondaryMenu" }
        check(layers?.superlayer === canvas.layer && layers?.animation(forKey: "account.menu.open") != nil || HUDRuntimeAppearance.reduceMotion,
              "Secondary menu opens on the same retained plane with a finite transition")
        let button = host.subviews.compactMap { $0 as? NSButton }.first { $0.accessibilityLabel() == "Arknights: Endfield" }
        let rect = canvas.accessibleActions.first { $0.id == "header:endfield" }!.rect
        check(button?.frame == rect.applying(projection), "Dropdown accessibility frames follow the projected HUD geometry")
        canvas.perform("syncProfile"); check(emitted.count == 1, "An open dropdown prevents actions on controls behind it")
        _ = canvas.mouseDown(at: CGPoint(x: bounds.minX - 5, y: bounds.midY))
        check(!canvas.isPopoverOpen && emitted.count == 1, "Outside click dismisses dropdown without clicking a control behind it")
        canvas.perform("header"); canvas.perform("header:endfield")
        check(emitted.last == .selectHeaderMode(.endfield) && !canvas.isPopoverOpen, "Selected game gauge mode routes to persistence through the controller")
        canvas.perform("region"); canvas.perform("region:global")
        check(emitted.last == .selectRegion(.global), "Global server is selectable from the same HUD dropdown")
        var model = HUDAccountPresentation(); model.region = .global; model.status = .connected; model.isLinked = true; model.accountName = "Fixture"
        model.roles = (0..<30).map { HUDAccountPresentation.Role(id: "role\($0)", title: "Role \($0)", subtitle: $0.isMultiple(of: 2) ? "Endfield" : "Arknights") }
        model.selectedRoleID = "role0"; canvas.update(model)
        canvas.perform("connect"); check(emitted.last == .connect(.global), "Reconnect uses the selected region")
        canvas.perform("role"); let roleBounds = canvas.popoverBounds!
        check(canvas.accessibleActions.count < model.roles.count, "Account selector retains a bounded viewport for many roles")
        _ = canvas.scroll(at: CGPoint(x: roleBounds.midX, y: roleBounds.midY), delta: 10_000)
        _ = canvas.scroll(at: CGPoint(x: roleBounds.midX, y: roleBounds.midY), delta: 10_000)
        _ = canvas.scroll(at: CGPoint(x: roleBounds.midX, y: roleBounds.midY), delta: 10_000)
        _ = canvas.scroll(at: CGPoint(x: roleBounds.midX, y: roleBounds.midY), delta: 10_000)
        check(canvas.menuScrollOffset > 0 && canvas.accessibleActions.contains { $0.id == "role:role29" }, "Last game account is reachable with bounded smooth scrolling")
        check(canvas.accessibleActions.allSatisfy { roleBounds.contains($0.rect) }, "Clipped role rows cannot intercept input outside their menu")
        canvas.perform("role:role29"); check(emitted.last == .selectRole("role29"), "Role selection carries an opaque account identifier intact")
        canvas.perform("disconnect"); check(canvas.accessibleActions.map(\.id) == ["menu:cancel", "menu:disconnect"], "Disconnect requires a retained confirmation")
        let before = emitted.count; canvas.perform("menu:cancel")
        check(emitted.count == before && !canvas.isPopoverOpen, "Cancel preserves the linked account")
        canvas.perform("disconnect"); canvas.perform("menu:disconnect")
        check(emitted.last == .disconnect, "Explicit confirmation routes disconnect once")
        canvas.perform("syncProfile"); check(emitted.last == .setSyncProfile(true), "Profile synchronization is independently selectable")
        canvas.perform("syncAvatar"); check(emitted.last == .setSyncAvatar(true), "Avatar synchronization remains an explicit independent opt-in")
        let renders = canvas.renderCount; canvas.update(model)
        check(canvas.renderCount == renders, "Unchanged account snapshots never rebuild the surface")
        model.status = .refreshing; canvas.update(model)
        check(canvas.accessibleActions.filter { ["connect", "refresh", "disconnect", "role", "region"].contains($0.id) }.allSatisfy { !$0.enabled }, "Busy state prevents duplicate requests")
        model.status = .connected; canvas.update(model); canvas.perform("role")
        model.status = .refreshing; canvas.update(model)
        check(!canvas.isPopoverOpen, "Pending choices are invalidated when a new sync starts")
        input.deactivate(); check(host.subviews.compactMap { $0 as? NSButton }.allSatisfy(\.isHidden), "Closed account module hides every native accessibility peer")
        let hiddenRenders = canvas.renderCount; model.status = .connected; canvas.update(model)
        check(canvas.renderCount == hiddenRenders, "Hidden updates defer all account artwork work")
        input.setActive(true); check(canvas.renderCount == hiddenRenders + 1, "Re-entry applies the latest cached snapshot once")
        let gauge = HUDAccountGauge()
        check(HUDAccountGauge.sourceArtworkAvailable, "Gauge loads the exact three source branch textures")
        check(HUDAccountGauge.sourceNumberFont == "HarmonyOS Sans SC Medium", "Gauge numbers use the original MoneyCell font instead of macOS digits")
        check(270 + HUDAccountGauge.headerPosition.x + HUDAccountGauge.size.width == 1_000 - 270,
              "Gauge right edge mirrors the ENDFIELDHUD left edge around the central HUD axis")
        check(gauge.layer.sublayers?.filter { $0.name?.contains("AddBtn") == true }.isEmpty == true, "Gauge has no source plus control")
        let art = gauge.layer.sublayers?.first { $0.name == "hud.account.stamina.item_ap" }?.contents
        check(art.map { CFGetTypeID($0 as CFTypeRef) == CGImage.typeID } == true && gauge.layer.bounds.size == HUDAccountGauge.size, "Stamina artwork is retained as one tiny texture in a fixed-size header gauge")
        gauge.update(value: "30 / 30", accessibilityLabel: "Work mode 30 of 30 minutes", visible: true, accent: .systemPurple, scale: 2)
        check(!gauge.layer.isHidden && gauge.value == "30 / 30", "Unlinked work-mode minutes are visible using the same gauge")
        let number = gauge.layer.sublayers?.first { $0.name == "hud.account.stamina.value" }
        check(number?.contents.map { CFGetTypeID($0 as CFTypeRef) == CGImage.typeID } == true,
              "Original SDF samples produce a retained numeric image without loading a system font")
        let updates = gauge.updateCount
        let numberRenders = gauge.numberRenderCount
        for _ in 0..<60 { gauge.update(value: "30 / 30", accessibilityLabel: "Work mode 30 of 30 minutes", visible: true, accent: .systemPurple, scale: 2) }
        check(gauge.updateCount == updates, "A minute that has not changed causes no per-second layer mutations")
        check(gauge.numberRenderCount == numberRenders, "Unchanged values retain their source glyph raster across clock ticks")
        gauge.update(value: "110 / 120", accessibilityLabel: "Sanity 110 of 120", visible: true, accent: .systemPurple, scale: 2)
        check(gauge.updateCount == updates + 1 && gauge.accessibilityLabel == "Sanity 110 of 120", "Only changed stamina values update the gauge")
        let changedRenders = gauge.numberRenderCount
        gauge.update(value: "110 / 120", accessibilityLabel: "Sanity 110 of 120", visible: true, accent: .systemCyan, scale: 2)
        check(gauge.numberRenderCount == changedRenders, "Theme changes reuse the white source numeric raster")
        for value in ["— / —", "999999999999 / 99999999999"] {
            gauge.update(value: value, accessibilityLabel: value, visible: true, accent: .systemCyan, scale: 2)
            let image = number?.contents.flatMap { CFGetTypeID($0 as CFTypeRef) == CGImage.typeID ? ($0 as! CGImage) : nil }
            let pixels = image?.dataProvider?.data as Data?
            check(pixels?.contains { $0 > 0 } == true && image?.width == 242,
                  "Missing service values and extreme work durations retain visible source glyphs within the gauge")
        }
        let now = Date(timeIntervalSince1970: 1_000)
        let recovery = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360,
            observedAt: now, nextRecoveryAt: now.addingTimeInterval(49),
            fullRecoveryAt: now.addingTimeInterval(38 * 3600 + 3 * 60 + 13), isRefreshing: false, refreshAvailable: true)
        gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: recovery, at: now)
        check(gauge.canOpen && gauge.accessibleActions.map(\.id) == ["toggle"], "A linked game exposes one projected recovery control")
        let closedRasters = gauge.tooltipRenderCount, closedNumberRasters = gauge.numberRenderCount
        for tick in 1...60 { gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: recovery, at: now.addingTimeInterval(Double(tick))) }
        check(gauge.tooltipRenderCount == closedRasters && gauge.numberRenderCount == closedNumberRasters,
              "Closed recovery tooltips never rasterize on HUD clock ticks")
        gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: recovery, at: now)
        check(gauge.mouseDown(at: CGPoint(x: 90, y: 20)) && gauge.isPopoverOpen,
              "Clicking the wallet opens its retained dropdown on the same plane")
        check(gauge.nextRecoveryText == "00:49" && gauge.fullRecoveryText == "38:03:13",
              "Recovery display matches the reference and preserves durations beyond 24 hours")
        check(gauge.accessibleActions.map(\.id) == ["toggle", "refresh"] && gauge.accessibleActions.last?.enabled == true,
              "Manual refresh is available from the recovery dropdown")
        let popover = gauge.layer.sublayers!.first { $0.name == "hud.account.stamina.recovery" }!
        check(popover.frame == HUDAccountGauge.popoverRect && popover.frame.minY >= HUDAccountGauge.size.height - 2,
              "The compact dark menu anchors immediately below the wallet")
        check(popover.frame.maxX == HUDAccountGauge.size.width
              && popover.frame.contains(gauge.accessibleActions.last!.rect),
              "The recovery menu and refresh control stay left of the wallet's right edge, clear of the clock")
        check(popover.animation(forKey: "account.gauge.menu")?.duration == 0.16 || HUDRuntimeAppearance.reduceMotion,
              "Recovery menu has a finite opening animation")
        gauge.hover(at: CGPoint(x: 90, y: 20))
        check(HUDControlHighlightLayer.highlightedCount(in: gauge.layer) == 1, "Wallet hover uses its retained grey feedback")
        var refreshes = 0; gauge.onRefresh = { refreshes += 1 }
        gauge.perform("refresh"); check(refreshes == 1 && gauge.isPopoverOpen, "Manual refresh is explicit and leaves recovery information visible")
        let priorTooltipRasters = gauge.tooltipRasterCount
        let busy = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360,
            observedAt: now, nextRecoveryAt: recovery.nextRecoveryAt, fullRecoveryAt: recovery.fullRecoveryAt,
            isRefreshing: true, refreshAvailable: false)
        gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: busy, at: now.addingTimeInterval(1))
        gauge.perform("refresh")
        check(refreshes == 1 && gauge.accessibleActions.last?.enabled == false && gauge.nextRecoveryText == "00:48",
              "The shared clock advances countdown while an in-flight refresh cannot duplicate requests")
        check(gauge.tooltipRasterCount == priorTooltipRasters + 2, "One-second ticks redraw only changed countdown digits and retain both static label rasters")
        let tooltipRenders = gauge.tooltipRenderCount
        gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: busy, at: now.addingTimeInterval(1))
        check(gauge.tooltipRenderCount == tooltipRenders, "Unchanged open countdown retains all label rasters")
        check(gauge.mouseDown(at: CGPoint(x: -20, y: 60)) && gauge.isPopoverOpen,
              "The leftward part of the right-aligned menu remains inside its input region")
        check(gauge.mouseDown(at: CGPoint(x: HUDAccountGauge.popoverRect.minX - 20, y: 60)) && !gauge.isPopoverOpen,
              "Outside clicks dismiss and are consumed without activating controls behind the tooltip")
        check(HUDAccountGauge.countdown(until: nil, at: now, hours: true) == "—"
              && HUDAccountGauge.countdown(until: now.addingTimeInterval(-1), at: now, hours: false) == "00:00",
              "Unknown or elapsed deadlines stay safe and never become negative")
        for language in [AppLanguage.simplifiedChinese, .traditionalChinese, .japanese, .korean] {
            L10n.language = language
            gauge.perform("toggle")
            check(popover.sublayers?.filter { $0.contents != nil }.count == 4,
                  "Every supported language retains visible recovery labels and original numeric countdowns")
            gauge.dismiss(animated: false)
        }
        L10n.language = .english
        gauge.perform("toggle")
        gauge.update(value: "", accessibilityLabel: "", visible: false, accent: .systemPurple, scale: 2)
        check(gauge.layer.isHidden && !gauge.isPopoverOpen && popover.animationKeys() == nil, "Hidden setting removes the gauge and tears down all finite tooltip animation")
        return count
    }
}
