import AppKit
import QuartzCore

enum HUDSettingsCanvasTests {
    private final class NoopTimer: HUDSettingsTimer { func invalidate() {} }
    static func run() -> Int {
        _ = NSApplication.shared
        var assertions = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            assertions += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        let suite = "EndfieldHUD.SettingsCanvasTests.\(UUID().uuidString)"
        let defaults = UserDefaults(suiteName: suite)!
        let language = L10n.language
        defer { defaults.removePersistentDomain(forName: suite); L10n.language = language }
        let store = ConfigurationStore(defaults: defaults)
        let controller = HUDSettingsController(store: store, scheduleTimer: { _, _ in NoopTimer() })
        controller.update { $0.language = .english }
        let style = HUDModuleContentStyle(dark: true, accent: .systemYellow, contentsScale: 2)
        let modules: [HUDModule] = [.system, .display, .hotkeys, .about]
        let internalUUID = "AAAAAAAA-1111-2222-3333-444444444444"
        let externalUUID = "BBBBBBBB-1111-2222-3333-444444444444"
        var connectedDisplays = [
            HUDDisplayDescriptor(uuid: internalUUID, name: "Built-in Display", displayID: 1, frame: CGRect(x: 0, y: 0, width: 1440, height: 900)),
            HUDDisplayDescriptor(uuid: externalUUID, name: "Studio Display", displayID: 9, frame: CGRect(x: -2560, y: 0, width: 2560, height: 1440))
        ]
        let canvases = modules.map { HUDSettingsCanvas(module: $0, controller: controller,
            reduceMotion: { controller.configuration.reduceMotion }, displayProvider: { connectedDisplays }) }
        let layers = zip(canvases, modules).map { $0.makeContent(for: $1, style: style) }
        check(Set(layers.map(ObjectIdentifier.init)).count == 4, "Each settings section retains a separate layer for outgoing/incoming transitions")
        func reach(_ id: String, in canvas: HUDSettingsCanvas) -> Bool {
            _ = canvas.scroll(at: CGPoint(x: 100, y: 100), delta: -10_000)
            while true {
                if canvas.accessibleActions.contains(where: { $0.id == id }) || canvas.accessibleSliders.contains(where: { $0.id == id }) { return true }
                let previous = canvas.scrollOffset
                _ = canvas.scroll(at: CGPoint(x: 100, y: 100), delta: 32)
                if canvas.scrollOffset == previous { return false }
            }
        }
        func strings(_ layer: CALayer) -> [String] {
            let own = (layer as? CATextLayer).flatMap { $0.string as? String }.map { [$0] } ?? []
            return own + (layer.sublayers ?? []).flatMap(strings)
        }
        func checkPageHandoff(_ canvas: HUDSettingsCanvas, direction: CGFloat) {
            guard let pages = canvas.layer.sublayers, pages.count == 2,
                  let outgoingMask = pages[0].mask as? CAShapeLayer,
                  let incomingMask = pages[1].mask as? CAShapeLayer,
                  let outgoing = outgoingMask.animation(forKey: "settings.page.reveal") as? CABasicAnimation,
                  let incoming = incomingMask.animation(forKey: "settings.page.reveal") as? CABasicAnimation,
                  let oldStart = outgoing.fromValue as! CGPath?, let oldEnd = outgoing.toValue as! CGPath?,
                  let newStart = incoming.fromValue as! CGPath?, let newEnd = incoming.toValue as! CGPath? else {
                fatalError("Both retained pages need a coordinated reveal")
            }
            check(incoming.duration == outgoing.duration && incoming.timingFunction == outgoing.timingFunction,
                  "Both page masks share one timing curve so their boundary cannot diverge")
            func interpolate(_ first: CGPath, _ last: CGPath, _ fraction: CGFloat) -> CGRect {
                let a = first.boundingBoxOfPath, b = last.boundingBoxOfPath
                return CGRect(x: a.minX + (b.minX - a.minX) * fraction,
                    y: a.minY + (b.minY - a.minY) * fraction,
                    width: a.width + (b.width - a.width) * fraction,
                    height: a.height + (b.height - a.height) * fraction)
            }
            for progress: CGFloat in [0, 0.1, 0.25, 0.5, 0.75, 0.9, 1] {
                let old = interpolate(oldStart, oldEnd, progress), new = interpolate(newStart, newEnd, progress)
                let oldEdge = direction > 0 ? old.maxX : old.minX
                let newEdge = direction > 0 ? new.minX : new.maxX
                check(abs(oldEdge - newEdge) < 0.001 && abs(old.width + new.width - canvas.layer.bounds.width) < 0.001,
                      "Page handoff has no text overlap or uncovered strip at progress \(progress)")
            }
            for page in pages {
                let movement = page.animation(forKey: "settings.page.depth") as? CABasicAnimation
                check(movement?.keyPath == "sublayerTransform" && page.animation(forKey: "subsection.depth") == nil,
                      "Only the artwork moves beneath stationary masks; no competing shutter or perspective runs")
            }
        }
        let system = canvases[0], display = canvases[1], hotkeys = canvases[2], about = canvases[3]
        func visibleRows(_ canvas: HUDSettingsCanvas) -> [CALayer] {
            func collect(_ layer: CALayer) -> [CALayer] {
                if layer.name?.hasPrefix("settings.row.") == true { return [layer] }
                return (layer.sublayers ?? []).flatMap(collect)
            }
            return collect(canvas.layer)
        }
        let preparedSystemRows = visibleRows(system).map(ObjectIdentifier.init)
        var systemChanges = 0
        system.onChange = { systemChanges += 1 }
        var platformStatusReads = 0
        controller.loginStatusProvider = { platformStatusReads += 1; return "Enabled in macOS" }
        system.activate()
        check(visibleRows(system).map(ObjectIdentifier.init) == preparedSystemRows,
              "Activating prepared settings retains existing text/control layers instead of repainting after the transition")
        check(systemChanges == 1, "Activation announces accessibility once even when its prepared artwork is unchanged")
        check(platformStatusReads == 0, "Selecting settings consumes cached OS status instead of querying login services on the tab transition")
        system.refresh()
        check(systemChanges == 1 && visibleRows(system).map(ObjectIdentifier.init) == preparedSystemRows,
              "Unchanged refreshes neither reconstruct settings rows nor repeat accessibility layout")
        system.deactivate(); _ = system.makeContent(for: .system, style: style); system.activate()
        check(visibleRows(system).map(ObjectIdentifier.init) == preparedSystemRows && systemChanges == 2,
              "Reentering an unchanged left-side tab reuses its exact retained artwork and reactivates accessibility once")
        controller.refreshExternalStatus()
        check(strings(system.layer).contains("Enabled in macOS") && systemChanges == 3,
              "A real external-status change still repaints the visible page immediately")
        controller.refreshExternalStatus()
        check(systemChanges == 3, "Repeated identical platform status does not republish or repaint the settings page")
        system.onChange = nil
        for id in ["language", "login", "focus", "screen", "ambient", "batteryEnabled", "restore"] {
            check(reach(id, in: system), "Every system setting remains reachable by continuous scrolling: \(id)")
        }
        check(reach("language", in: system), "Language chooser is reachable in System settings")
        _ = system.scroll(at: CGPoint(x: 100, y: 100), delta: 12)
        let languageMainScroll = system.scrollOffset
        let retainedSystemLayer = system.layer
        system.perform(actionID: "language")
        check(controller.configuration.language == .english && store.configuration.language == .english,
              "Opening the language chooser does not cycle or persist a different language")
        check(system.isTransitioning && system.layer === retainedSystemLayer,
              "Language choices open through the retained page handoff used by display selection")
        checkPageHandoff(system, direction: 1); system.settleTransition()
        check(system.accessibleActions.map(\.id) == ["back"] + AppLanguage.allCases.map { "language:" + $0.rawValue },
              "All five language choices fit on the selection page in their stable order")
        check(["System", "English", "简体中文", "繁體中文", "日本語"].allSatisfy { strings(system.layer).contains($0) },
              "The chooser identifies each explicit language in its own writing system")
        check(system.accessibleActions.first(where: { $0.id == "language:english" })?.label == "English, Selected"
              && strings(system.layer).filter { $0 == "✓" }.count == 1,
              "The saved language has one visible checkmark and an accessible selection announcement")
        check(!system.accessibleActions.contains(where: { $0.id == "login" || $0.id == "language" }),
              "Language choices do not expose controls from the outgoing System page")
        check(system.escape() && controller.configuration.language == .english,
              "Escape returns from language selection without changing the saved preference")
        checkPageHandoff(system, direction: -1); system.settleTransition()
        check(system.scrollOffset == languageMainScroll, "Returning from language selection restores the main-list scroll position")
        for language in AppLanguage.allCases {
            system.perform(actionID: "language"); system.settleTransition()
            system.perform(actionID: "language:" + language.rawValue)
            check(controller.configuration.language == language && ConfigurationStore(defaults: defaults).configuration.language == language,
                  "Choosing \(language.rawValue) immediately updates the interface and persists across launches")
            checkPageHandoff(system, direction: -1); system.settleTransition()
            check(system.accessibleActions.contains(where: { $0.id == "language" }) && system.scrollOffset == languageMainScroll,
                  "A language choice returns to System with the prior scroll position")
            system.perform(actionID: "language"); system.settleTransition()
            let selected = system.accessibleActions.filter { $0.id.hasPrefix("language:") && $0.label.hasSuffix(L10n.text("Selected", "已选择")) }
            check(selected.map(\.id) == ["language:" + language.rawValue] && strings(system.layer).filter { $0 == "✓" }.count == 1,
                  "Reopening the chooser checks exactly the saved \(language.rawValue) row")
            system.perform(actionID: "back"); system.settleTransition()
        }
        system.perform(actionID: "language"); system.perform(actionID: "language:english"); system.settleTransition()
        check(reach("screen", in: system), "Display chooser is reachable in System settings")
        system.perform(actionID: "screen")
        check(system.isTransitioning && system.layer === retainedSystemLayer, "Display choices open through the same retained mechanical page handoff")
        checkPageHandoff(system, direction: 1); system.settleTransition()
        check(system.accessibleActions.map(\.id) == ["back", "screen:pointer", "screen:main", "screen:" + internalUUID, "screen:" + externalUUID],
              "Chooser contains automatic, primary and explicit connected display choices")
        check(!system.accessibleActions.contains(where: { $0.id == "language" }), "Display choices do not leak taps to controls on the old page")
        system.perform(actionID: "screen:" + externalUUID)
        check(controller.configuration.hudDisplayUUID == externalUUID && store.configuration.hudDisplayName == "Studio Display",
              "Choosing a monitor immediately persists its stable identity and returns to System")
        checkPageHandoff(system, direction: -1); system.settleTransition()
        check(strings(system.layer).contains("Studio Display"), "The main row identifies the selected monitor")
        connectedDisplays.removeLast()
        NotificationCenter.default.post(name: NSApplication.didChangeScreenParametersNotification, object: nil)
        check(strings(system.layer).contains("Studio Display (disconnected)"), "Display disconnection updates the visible preference without discarding it")
        system.perform(actionID: "screen"); system.settleTransition()
        check(system.accessibleActions.contains(where: { $0.id == "screen:disconnected" && !$0.enabled }),
              "A saved disconnected display remains visible but cannot be chosen again while unavailable")
        system.perform(actionID: "screen:main"); system.settleTransition()
        check(controller.configuration.hudDisplayUUID == nil && !controller.configuration.openOnActiveDisplay,
              "Main-display choice clears the fixed UUID and preserves legacy primary semantics")
        system.perform(actionID: "screen"); system.perform(actionID: "screen:pointer"); system.settleTransition()
        check(controller.configuration.hudDisplayUUID == nil && controller.configuration.openOnActiveDisplay,
              "Pointer-display choice clears the fixed target and restores automatic placement")
        check(reach("restore", in: system), "Restore remains reachable after choosing a display")
        check(!strings(system.layer).contains("Device battery panel"), "System settings no longer offers the removed battery hover panel")
        system.perform(actionID: "restore")
        check(system.accessibleActions.map(\.id) == ["restore:cancel", "restore:confirm"], "Restore requires an explicit inline confirmation")
        check(system.escape() && !system.accessibleActions.contains(where: { $0.id == "restore:confirm" }), "Escape dismisses restore without modifying settings")
        check(reach("focus", in: system), "Focus setting is reachable again after cancelling reset")
        system.perform(actionID: "focus")
        check(!controller.configuration.closeOnFocusLost, "Toggle updates shared persistent configuration")
        check(reach("restore", in: system), "Restore can be reopened")
        system.perform(actionID: "restore"); system.perform(actionID: "restore:confirm")
        check(controller.configuration.closeOnFocusLost, "Confirmed restore applies defaults")
        system.deactivate()

        display.activate()
        let scale = display.accessibleSliders.first { $0.id == "uiScale" }!
        check(scale.minimum == 0.2 && scale.maximum == 2, "HUD scale exposes the complete requested range")
        _ = display.mouseDown(at: CGPoint(x: scale.rect.maxX - 6, y: scale.rect.midY))
        check(display.isDragging && controller.scaleConfirmationRemaining == nil && store.configuration.hudScale == 1,
              "Scale drag stages the value without moving its own hit target or persisting an unsafe scale")
        display.mouseUp()
        check(controller.configuration.hudScale == 2 && store.configuration.hudScale == 1 && controller.scaleConfirmationRemaining == 12,
              "Mouse-up requests a timed uncommitted scale preview")
        controller.revertScale()
        _ = display.mouseDown(at: CGPoint(x: scale.rect.minX + 6, y: scale.rect.midY))
        display.deactivate()
        check(controller.scaleConfirmationRemaining == nil && controller.configuration.hudScale == 1,
              "Closing during a scale drag cancels staging instead of resurrecting a preview after rollback")
        display.activate()
        check(!display.setSlider(id: "uiScale", value: .nan), "Nonfinite values cannot enter scale preview")
        check(display.setSlider(id: "uiScale", value: 0.2) && controller.configuration.hudScale == 0.2,
              "Accessibility adjustments use the same safe preview transaction")
        controller.confirmScale()
        check(store.configuration.hudScale == 0.2, "Explicit scale confirmation commits the preview")
        let initialSliderIDs = display.accessibleSliders.map(\.id)
        check(Array(initialSliderIDs.prefix(3)) == ["uiScale", "positionX", "positionY"], "X and Y position controls follow UI scale directly")
        let xPosition = display.accessibleSliders.first { $0.id == "positionX" }!
        _ = display.mouseDown(at: CGPoint(x: xPosition.rect.maxX - 6, y: xPosition.rect.midY))
        check(display.isDragging && controller.configuration.hudOffsetX == 0 && controller.layoutConfirmationRemaining == nil,
              "Position dragging stages the value without moving the HUD away from the pointer")
        check(display.accessibleSliders.first(where: { $0.id == "positionX" })?.valueDescription == "+50%", "Staged position uses signed screen-relative offsets")
        display.mouseUp()
        check(controller.configuration.hudOffsetX == 0.5 && store.configuration.hudOffsetX == 0 && controller.layoutConfirmationRemaining == 12,
              "Position mouse-up opens a recoverable uncommitted preview")
        check(display.setSlider(id: "positionY", value: -0.5) && controller.configuration.hudOffsetY == -0.5 && store.configuration.hudOffsetY == 0,
              "Accessible Y updates preview immediately and preserve the other pending axis")
        controller.confirmLayout()
        check(store.configuration.hudOffsetX == 0.5 && store.configuration.hudOffsetY == -0.5,
              "The shared recovery confirmation commits both position axes atomically")
        _ = display.mouseDown(at: CGPoint(x: xPosition.rect.minX + 6, y: xPosition.rect.midY))
        display.deactivate(); display.activate()
        check(controller.configuration.hudOffsetX == 0.5 && controller.layoutConfirmationRemaining == nil,
              "Closing during a position drag discards staging instead of restarting the safety preview")
        check(!display.setSlider(id: "positionY", value: .infinity), "Nonfinite position values cannot reach preview state")
        for id in ["positionX", "positionY", "parallax", "perspective", "darkness", "blur", "motion", "theme", "clockFormat", "clockStyle", "centerLogo", "accent:FAD41F", "customColor", "battery", "lowPower"] {
            check(reach(id, in: display), "Every display setting is scroll-reachable: \(id)")
        }
        check(reach("clockFormat", in: display), "Time format is available in Display")
        display.perform(actionID: "clockFormat")
        check(controller.configuration.clockFormat == .twelveHour && store.configuration.clockFormat == .twelveHour,
              "The time-format row changes and persists the live format")
        display.perform(actionID: "clockFormat")
        check(controller.configuration.clockFormat == .twentyFourHour, "Time format cycles back to 24-hour time")
        check(reach("clockStyle", in: display), "The five clock presentations remain scroll-reachable beside the existing time format")
        let initialClockStyle = controller.configuration.clockStyle
        var visitedClockStyles = Set<String>()
        for step in 1...5 {
            display.perform(actionID: "clockStyle")
            let expected = initialClockStyle.advanced(step)
            visitedClockStyles.insert(controller.configuration.clockStyle.rawValue)
            check(controller.configuration.clockStyle == expected && store.configuration.clockStyle == expected
                  && ConfigurationStore(defaults: defaults).configuration.clockStyle == expected,
                  "Clock presentation choice persists every step of its five-style cycle")
            check(display.accessibleActions.first(where: { $0.id == "clockStyle" })?.label.hasSuffix("0\(expected.index + 1) / 05") == true
                  && controller.configuration.clockFormat == .twentyFourHour,
                  "Clock style updates its indicator while preserving the independently selected time format")
        }
        check(visitedClockStyles.count == 5 && controller.configuration.clockStyle == initialClockStyle,
              "Five clock-style activations visit every presentation and wrap back to the initial choice")

        check(reach("centerLogo", in: display), "Center-logo presets have a reachable Display settings entry")
        let logoScroll = display.scrollOffset
        let priorLogo = controller.configuration.centerLogo
        let retainedLogoLayer = display.layer
        display.perform(actionID: "centerLogo")
        check(display.layer === retainedLogoLayer && display.isTransitioning && controller.configuration.centerLogo == priorLogo,
              "Opening logo choices retains the settings surface without changing the saved artwork")
        checkPageHandoff(display, direction: 1); display.settleTransition()
        check(display.accessibleActions.map(\.id) == ["back"] + HUDCenterLogo.allCases.map { "logo:" + $0.rawValue }
              && display.accessibleSliders.isEmpty,
              "All preset and custom logo choices fit in stable order without main-page hit targets")
        var logoRequests = 0, committedLogoSelections = 0
        display.onChooseLogo = { logoRequests += 1 }
        display.onLogoSelection = { committedLogoSelections += 1 }
        let beforeLogoChooser = store.configuration
        display.perform(actionID: "logo:custom")
        check(logoRequests == 1 && store.configuration == beforeLogoChooser,
              "Custom-logo selection requests its native chooser without persisting an unfinished selection")
        display.showImportError("Fixture import failed")
        check(display.accessibilityStatus == "Fixture import failed" && store.configuration == beforeLogoChooser,
              "Import errors remain accessible and preserve the saved logo choice")
        let logoRevision = UUID().uuidString
        display.setCustomLogo(revision: logoRevision)
        check(controller.configuration.centerLogo == .custom && store.configuration.centerLogoRevision == logoRevision
              && ConfigurationStore(defaults: defaults).configuration.centerLogoRevision == logoRevision
              && display.customLogoRevision == logoRevision,
              "A completed bounded import selects and persists only its supplied revision")
        for choice in HUDCenterLogo.allCases where choice != .custom {
            let beforeSelection = committedLogoSelections
            display.perform(actionID: "logo:" + choice.rawValue)
            check(committedLogoSelections == beforeSelection + 1,
                  "Every explicit preset selection invalidates a pending custom-image import")
            check(controller.configuration.centerLogo == choice && store.configuration.centerLogo == choice
                  && store.configuration.centerLogoRevision == logoRevision,
                  "Preset selection preserves the user's independently retained custom image revision")
            check(display.accessibleActions.filter { $0.id.hasPrefix("logo:") && $0.label.hasSuffix(L10n.text("Selected", "已选择")) }.map(\.id)
                  == ["logo:" + choice.rawValue], "Only the selected center-logo preset is announced as selected")
        }
        let lastChoice = controller.configuration.centerLogo
        let beforeSameChoice = committedLogoSelections
        display.perform(actionID: "logo:" + lastChoice.rawValue)
        check(committedLogoSelections == beforeSameChoice + 1 && display.selectedLogo == lastChoice,
              "Choosing the already-selected preset also cancels an in-flight import without changing artwork")
        let beforeInvalidLogo = store.configuration
        let beforeInvalidSelection = committedLogoSelections
        display.perform(actionID: "logo:not-a-choice")
        check(store.configuration == beforeInvalidLogo && committedLogoSelections == beforeInvalidSelection,
              "Unknown logo actions cannot change saved preferences or invalidate valid work")
        check(display.escape(), "Escape closes logo choices through the retained back transition")
        checkPageHandoff(display, direction: -1); display.settleTransition()
        check(display.scrollOffset == logoScroll && display.accessibleActions.contains(where: { $0.id == "centerLogo" }),
              "Leaving the logo chooser restores the prior Display-list position")
        check(reach("customColor", in: display), "Custom color stays reachable")
        var requests = 0
        display.onChooseColor = { _ in requests += 1 }
        display.perform(actionID: "customColor")
        check(requests == 1, "Custom color delegates to the native color wheel bridge")
        display.setCustomColor(NSColor(srgbRed: 0.2, green: 0.4, blue: 0.6, alpha: 1))
        check(controller.configuration.accentHex == "336699", "Color-wheel edits are persisted as sRGB colors")
        check(reach("appIcon", in: display), "Icon chooser remains inside Display settings")
        display.perform(actionID: "appIcon"); display.settleTransition()
        check(display.accessibleActions.contains(where: { $0.id == "appIcon:endfield" }), "Icon presets expose accessible names")
        check(!display.accessibleActions.contains(where: { $0.id == "appIcon:originium" || $0.id == "appIcon:orundum" }),
              "The removed hand-drawn currency symbols are absent from the first icon row")
        let previousIcon = controller.configuration.applicationIcon
        display.perform(actionID: "appIcon:originium"); display.perform(actionID: "appIcon:orundum")
        check(controller.configuration.applicationIcon == previousIcon,
              "Retired icon IDs cannot be selected through the picker action handler")
        check(!strings(display.layer).contains(where: { HUDApplicationIcon.pickerCases.map(\.title).contains($0) }),
              "Icon tiles show artwork only; names remain available to accessibility")
        let firstIconRow = display.accessibleActions.filter { $0.id.hasPrefix("appIcon:") && $0.rect.minY == display.accessibleActions.first(where: { $0.id == "appIcon:endfield" })!.rect.minY }
        check(firstIconRow.count == 4 && firstIconRow.allSatisfy { !$0.label.isEmpty },
              "Four compact symbol buttons fit each icon row and retain accessible labels")
        display.perform(actionID: "appIcon:perlica")
        check(controller.configuration.applicationIcon == .perlica && store.configuration.applicationIcon == .perlica,
              "Selecting an icon updates the shared and persistent configuration immediately")
        for preset in HUDApplicationIcon.pickerCases {
            check(reach("appIcon:\(preset.rawValue)", in: display), "Every expanded icon preset remains reachable by continuous scrolling")
        }
        display.perform(actionID: "appIcon:gameStrength")
        check(controller.configuration.applicationIcon == .gameStrength, "The last verified game preset is selectable")
        display.perform(actionID: "back"); display.settleTransition()
        check(reach("battery", in: display), "Battery detail page remains reachable")
        let mainScroll = display.scrollOffset
        check(mainScroll > 0, "Battery settings are reached through the main display list")
        let retainedDisplayLayer = display.layer
        display.perform(actionID: "battery")
        check(display.isTransitioning && display.layer === retainedDisplayLayer && display.layer.sublayers?.count == 2,
              "Battery subsection retains outgoing artwork beneath one incoming page instead of blanking the list")
        checkPageHandoff(display, direction: 1)
        let incomingPage = display.layer.sublayers!.last!
        display.refresh()
        check(display.layer.sublayers?.last === incomingPage && incomingPage.mask?.animation(forKey: "settings.page.reveal") != nil,
              "A status refresh while entering Battery redraws content without restarting its reveal")
        display.settleTransition()
        check(!display.isTransitioning && display.layer.sublayers?.count == 1, "Completed subsection transitions remove the outgoing page")
        check(display.accessibleActions.contains(where: { $0.id == "back" }) && display.accessibleSliders.contains(where: { $0.id == "duration" }),
              "Battery detail controls replace the center content inside the same module layer")
        check(display.setSlider(id: "duration", value: 3), "Battery duration can be adjusted")
        check(controller.configuration.displayDuration == 3, "Battery duration saves the requested three seconds")
        let metricBaseline = controller.configuration
        display.perform(actionID: "metric")
        check(display.isTransitioning && display.layer === retainedDisplayLayer,
              "Battery metric choices reuse the same retained page handoff")
        checkPageHandoff(display, direction: 1); display.settleTransition()
        check(display.accessibleActions.map(\.id) == ["back"] + HUDChargeMetric.allCases.map { "metric:" + $0.rawValue }
              && display.accessibleSliders.isEmpty,
              "All five reading types replace battery controls without leaking duration or display actions")
        check(display.escape(), "Escape from the metric chooser returns one level to Battery settings")
        checkPageHandoff(display, direction: -1); display.settleTransition()
        check(display.accessibleActions.contains(where: { $0.id == "metric" })
              && display.accessibleSliders.contains(where: { $0.id == "duration" })
              && !display.accessibleActions.contains(where: { $0.id == "centerLogo" })
              && controller.configuration.alertMetric == metricBaseline.alertMetric,
              "Cancelling metric selection returns to its Battery parent and preserves the reading")
        for metric in HUDChargeMetric.allCases {
            display.perform(actionID: "metric"); display.settleTransition()
            display.perform(actionID: "metric:" + metric.rawValue)
            checkPageHandoff(display, direction: -1); display.settleTransition()
            check(controller.configuration.alertMetric == metric && store.configuration.alertMetric == metric
                  && ConfigurationStore(defaults: defaults).configuration.alertMetric == metric,
                  "Every battery reading choice persists and returns through its mechanical parent transition")
            check(display.accessibleActions.first(where: { $0.id == "metric" })?.label.hasSuffix(metric.title) == true
                  && controller.configuration.displayMode == metricBaseline.displayMode
                  && controller.configuration.displayDuration == metricBaseline.displayDuration
                  && controller.configuration.scale == metricBaseline.scale,
                  "Changing readings preserves alert timing, display method and size")
            display.perform(actionID: "metric"); display.settleTransition()
            check(display.accessibleActions.filter { $0.id.hasPrefix("metric:") && $0.label.hasSuffix(L10n.text("Selected", "已选择")) }.map(\.id)
                  == ["metric:" + metric.rawValue], "The metric chooser checks only its persisted reading")
            display.perform(actionID: "back"); display.settleTransition()
        }
        check(display.escape(), "Battery details return through a mechanical subsection transition")
        check(display.scrollOffset == mainScroll, "Returning from battery details restores the prior main-list scroll position")
        checkPageHandoff(display, direction: -1)
        display.settleTransition()
        display.perform(actionID: "battery")
        controller.update { $0.reduceMotion = true }
        check(!display.isTransitioning && display.layer.sublayers?.count == 1, "Enabling Reduce Motion settles an in-flight subsection immediately")
        controller.update { $0.reduceMotion = false }
        display.deactivate()

        let directBattery = HUDSettingsCanvas(module: .display, controller: controller,
            reduceMotion: { controller.configuration.reduceMotion })
        let directLayer = directBattery.makeContent(for: .display, style: style)
        directBattery.showBatterySettings(); directBattery.activate()
        check(directBattery.layer === directLayer && !directBattery.isTransitioning
              && directBattery.accessibleActions.contains(where: { $0.id == "metric" })
              && directBattery.accessibleSliders.contains(where: { $0.id == "duration" }),
              "The Power-page shortcut opens Battery settings directly inside the retained Display surface")
        directBattery.perform(actionID: "metric")
        check(directBattery.isTransitioning, "The direct Battery route still supports its nested metric transition")
        directBattery.showBatterySettings()
        check(!directBattery.isTransitioning && directBattery.layer.sublayers?.count == 1
              && directBattery.accessibleActions.contains(where: { $0.id == "metric" }),
              "Repeating the Battery shortcut settles an interrupted nested transition without overlapping pages")
        check(directBattery.escape(), "Back from the Power shortcut reaches the Display parent")
        directBattery.settleTransition()
        check(directBattery.accessibleSliders.contains(where: { $0.id == "uiScale" }) && !directBattery.escape(),
              "Direct Battery navigation returns to main Display settings without an extra phantom parent")
        let hotkeyActions = hotkeys.accessibleActions.map(\.id)
        hotkeys.showBatterySettings()
        check(hotkeys.accessibleActions.map(\.id) == hotkeyActions, "The direct Battery route is ignored by other Settings modules")
        controller.update { $0.reduceMotion = true }
        directBattery.showBatterySettings(); directBattery.perform(actionID: "metric")
        check(!directBattery.isTransitioning && directBattery.accessibleActions.contains(where: { $0.id == "metric:battery" }),
              "Direct and nested Battery navigation settle immediately with Reduce Motion")
        directBattery.deactivate(); controller.update { $0.reduceMotion = false }

        controller.update { $0.language = .english }
        hotkeys.activate()
        check(!strings(hotkeys.layer).contains("Shortcut keys") && !strings(hotkeys.layer).contains("Conflict detection"),
              "Hotkeys omits the removed explanatory rows while retaining actual validation")
        hotkeys.perform(actionID: "capture")
        check(hotkeys.isCapturingShortcut && controller.isCapturingShortcut, "Inline recorder suspends the summon binding through controller capture state")
        func event(_ key: UInt16, _ flags: NSEvent.ModifierFlags) -> NSEvent {
            NSEvent.keyEvent(with: .keyDown, location: .zero, modifierFlags: flags, timestamp: 0, windowNumber: 0,
                context: nil, characters: "", charactersIgnoringModifiers: "", isARepeat: false, keyCode: key)!
        }
        check(hotkeys.capture(event(12, .command)) && hotkeys.isCapturingShortcut,
              "Reserved commands remain rejected without abandoning the recorder")
        check(!hotkeys.accessibilityStatus.isEmpty, "Shortcut conflicts are exposed to accessibility")
        check(hotkeys.capture(event(0, [.control, .option])) && !hotkeys.isCapturingShortcut,
              "A valid three-key combination applies and exits recording")
        check(controller.configuration.summonShortcut == SummonShortcut(keyCode: 0, modifiers: [.control, .option]),
              "Recorder persists the selected physical key and modifiers")
        hotkeys.perform(actionID: "capture"); hotkeys.deactivate()
        check(!controller.isCapturingShortcut, "Leaving Hotkeys cannot leave global summon registration suspended")
        about.activate()
        var updateChecks = 0
        var automaticChoice: Bool?
        controller.onCheckForUpdates = { updateChecks += 1 }
        controller.onAutomaticUpdatesChange = { automaticChoice = $0 }
        check(about.accessibleActions.contains(where: { $0.id == "updates" }), "About offers an accessible update action without another settings window")
        about.perform(actionID: "updates")
        check(updateChecks == 1, "About requests a check through the application-owned updater")
        about.perform(actionID: "automaticUpdates")
        check(automaticChoice == false, "Automatic installation preference is delegated to its single owner")
        var update = HUDUpdateState()
        update.phase = .ready; update.latestVersion = "v0.5.0"; update.detail = "Ready to install"
        update.releaseURL = URL(string: "https://github.com/DDDuoDuo/EndfieldHUD/releases/tag/v0.5.0")
        controller.receiveUpdateState(update)
        check(about.accessibleActions.contains(where: { $0.id == "latestRelease" }) && about.accessibilityStatus == "Ready to install",
              "Release metadata and status refresh the retained About view")
        _ = about.scroll(at: CGPoint(x: 100, y: 100), delta: 220)
        check(strings(about.layer).contains("Credits:"), "About separates attribution with the Credits heading")
        controller.update { $0.language = .simplifiedChinese }
        check(strings(about.layer).contains("Credits:"), "The requested Credits heading remains English in the Chinese interface")
        check(controller.about.credits.first?.role == "非官方同人项目",
              "Cached About metadata resolves its localized credits again when the language changes")
        check(about.layer === about.makeContent(for: .about, style: style), "About styling updates reuse the retained section layer")
        check(!about.accessibleActions.contains(where: { $0.id == "github" }), "Unset repository metadata never creates an invented GitHub destination")
        about.deactivate()
        let accessibilityHost = NSView(frame: CGRect(x: 0, y: 0, width: 400, height: 334))
        let retainedCanvas = HUDSettingsCanvas(module: .display, controller: controller)
        _ = retainedCanvas.makeContent(for: .display, style: style)
        let retainedInteraction = HUDSettingsInteraction(canvas: retainedCanvas, host: accessibilityHost)
        retainedInteraction.setActive(true)
        let retainedControls = accessibilityHost.subviews.map(ObjectIdentifier.init)
        check(!retainedControls.isEmpty && accessibilityHost.subviews.allSatisfy { !$0.isHidden },
              "A reused settings page exposes its native accessibility controls on activation")
        retainedInteraction.setActive(false)
        check(accessibilityHost.subviews.allSatisfy(\.isHidden), "Inactive retained settings controls stay hidden")
        _ = retainedCanvas.makeContent(for: .display, style: style)
        retainedInteraction.setActive(true)
        check(accessibilityHost.subviews.map(ObjectIdentifier.init) == retainedControls
              && accessibilityHost.subviews.allSatisfy { !$0.isHidden },
              "Reentering the unchanged page restores the same accessibility controls without repainting")
        retainedInteraction.setActive(false)
        return assertions
    }
}
