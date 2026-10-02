import AppKit
import QuartzCore

enum PersonalProfileCanvasTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: @autoclosure () -> Bool, _ message: String) { count += 1; precondition(condition(), message) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("PersonalProfileCanvas-\(UUID().uuidString)")
        let oldLanguage = L10n.language; L10n.language = .english
        defer { try? FileManager.default.removeItem(at: root); L10n.language = oldLanguage }
        do {
            func descendants(_ layer: CALayer) -> [CALayer] {
                [layer] + (layer.sublayers ?? []).flatMap(descendants)
            }
            let deferredStore = try UserProfileStore(directory: root.appendingPathComponent("deferred"))
            let deferred = PersonalProfileCanvas(store: deferredStore, reduceMotion: { true })
            let fields = deferred.layer.sublayers!.first { $0.name == "profile.fields" }!
            let backdrop = deferred.backgroundLayer.sublayers!.first!
            check((fields.sublayers ?? []).isEmpty && backdrop.contents == nil && backdrop.mask == nil,
                  "An unopened profile creates no portrait, decoded backdrop or painted fields")
            deferred.updateRenderScale(3); deferred.deactivate()
            try deferredStore.update { $0.name = "Deferred profile" }
            check(deferred.profileValue.name == "Deferred profile" && (fields.sublayers ?? []).isEmpty && backdrop.mask == nil,
                  "Hidden profile state stays current without painting during scale, store or teardown updates")
            _ = deferred.makeContent(for: .profile, style: HUDModuleContentStyle(dark: true, accent: .cyan, contentsScale: 3))
            let nameLayer = descendants(deferred.layer).compactMap { $0 as? CATextLayer }
                .first { ($0.string as? String) == "Deferred profile#0000" }
            check(nameLayer != nil && nameLayer!.contentsScale == HUDRenderScale.contentScale(for: nameLayer!, baseScale: 3)
                  && descendants(deferred.layer).contains { $0.name == "profile.portrait" },
                  "First profile presentation prepares the current identity at its requested backing scale")
            deferred.deactivate(); deferred.updateRenderScale(2)
            try deferredStore.update { $0.name = "Reopened profile" }
            check(nameLayer?.superlayer === fields && (nameLayer?.string as? String) == "Deferred profile#0000",
                  "A previously shown hidden profile preserves its artwork without rebuilding it")
            deferred.activate()
            check(descendants(deferred.layer).compactMap { ($0 as? CATextLayer)?.string as? String }.contains("Reopened profile#0000"),
                  "Activation paints state changes received while the profile was hidden")
            deferred.deactivate()
            let direct = PersonalProfileCanvas(store: deferredStore, reduceMotion: { true })
            direct.perform(actionID: "profile:menu")
            check(direct.isPopoverOpen && descendants(direct.layer).compactMap { ($0 as? CATextLayer)?.string as? String }.contains("Edit name"),
                  "A direct profile action still prepares its visible controls without requiring activation")
            direct.deactivate()
            let store = try UserProfileStore(directory: root)
            var seconds: TimeInterval = 7200
            let canvas = PersonalProfileCanvas(store: store, workSeconds: { seconds }, reduceMotion: { true })
            let retained = canvas.makeContent(for: .profile, style: HUDModuleContentStyle(dark: true, accent: .cyan, contentsScale: 2))
            canvas.activate()
            let portrait = descendants(retained).first { $0.name == "profile.portrait" }!
            let photoLayer = portrait.sublayers!.first { $0.name == "portrait.image" }!
            let portraitFrame = portrait.sublayers!.first { $0.name == "portrait.frame" }!
            let frameBitmap = portraitFrame.contents as! CGImage
            check(photoLayer.bounds.width == photoLayer.bounds.height
                && abs(portraitFrame.bounds.width / photoLayer.bounds.width - 195.0 / 136) < 1e-10,
                  "Profile editing uses the source card's square portrait and exact avatar-frame scale")
            check(frameBitmap.width == 254 && frameBitmap.height == 254
                && portraitFrame.sublayers == nil && !descendants(portrait).contains { $0.name == "portrait.vignette" },
                  "The actual untrimmed source avatar frame replaces the old drawn rails and photo vignette")
            let outerFrame = portraitFrame.frame.offsetBy(dx: portrait.frame.minX, dy: portrait.frame.minY)
            check(outerFrame.minY > 25 && outerFrame.maxY < PersonalProfileCanvas.menuRect.minY
                && outerFrame.maxX < 97 && outerFrame.minX >= 0,
                  "The source frame fits between the heading, name and edit button")
            var requestedFields: [PersonalProfileField] = []; var imageRequests: [UserProfileImageKind] = []
            canvas.onEditField = { field, _, _ in requestedFields.append(field) }; canvas.onChooseImage = { imageRequests.append($0) }
            func labels(_ node: CALayer) -> [String] {
                let own = (node as? CATextLayer)?.string as? String
                return (own.map { [$0] } ?? []) + (node.sublayers ?? []).flatMap(labels)
            }
            let initial = labels(retained)
            check(initial.contains("Endministrator#0000") && initial.contains("24") && initial.contains("54") && initial.contains("325"), "Profile presents requested initial identity and counters")
            check(initial.contains("2.00") && initial.contains("Work Mode"), "Region construction reads cumulative Work Mode hours")
            check(!initial.contains(where: { $0.contains("Progress") || $0.contains("Chapter") }), "Reference chapter/progress panel remains omitted")
            check(canvas.accessibleActions.count == 11 && canvas.accessibleActions.allSatisfy { retained.bounds.contains($0.rect) }, "Profile controls including intro and theme toolbar fit the retained surface")
            let intro = canvas.accessibleActions.first { $0.id == "profile:introduction" }!
            check(intro.rect.contains(CGPoint(x: 382, y: 164)), "The visible pencil is inside the introduction hit region")
            let originalID = store.profile.uid; let originalDate = store.profile.awakeningDate
            canvas.perform(actionID: "profile:menu")
            check(canvas.isPopoverOpen && canvas.accessibleActions.contains { $0.id == "profile:restoreAvatar" }, "Portrait menu is retained HUD content with a restore action")
            check(labels(retained).contains("Edit name") && labels(retained).contains("Restore default picture"), "Custom menu draws its own labels instead of opening NSMenu")
            check(canvas.accessibleActions.contains { $0.id == "profile:portraitMenu" }, "Three-dot menu offers portrait adjustment alongside choosing and restoring the picture")
            canvas.perform(actionID: "profile:portraitMenu")
            check(canvas.isPopoverOpen && canvas.accessibleSliders.map(\.field) == PersonalProfileField.portraitFields
                  && canvas.accessibleActions.allSatisfy { retained.bounds.contains($0.rect) }
                  && canvas.accessibleSliders.allSatisfy { canvas.popoverBounds!.contains($0.rect) }
                  && !canvas.accessibleActions.contains { action in action.id.contains("adjust:") || PersonalProfileField.portraitFields.contains { action.id == "profile:" + $0.rawValue } },
                  "Portrait submenu exposes three sliders without duplicate numeric or stepper buttons")
            check(!canvas.popoverBounds!.intersects(outerFrame), "Portrait and the complete source frame remain visible while crop controls are open")
            check(canvas.setSlider(field: .avatarZoom, value: 21) && store.profile.avatarZoom == 20
                  && canvas.setSlider(field: .avatarZoom, value: 0) && store.profile.avatarZoom == 1, "Zoom slider clamps to1...20")
            check(canvas.setSlider(field: .avatarOffsetX, value: 125) && store.profile.avatarOffsetX == 1
                  && canvas.setSlider(field: .avatarOffsetY, value: -125) && store.profile.avatarOffsetY == -1, "Portrait position sliders clamp at the image edges")
            check(canvas.setSlider(field: .avatarZoom, value: 2.25) && canvas.setSlider(field: .avatarOffsetX, value: 35)
                  && canvas.setSlider(field: .avatarOffsetY, value: -40) && store.profile.avatarZoom == 2.25
                  && store.profile.avatarOffsetX == 0.35 && store.profile.avatarOffsetY == -0.4, "Slider updates retain fractional zoom and normalized offsets")
            let finiteProfile = store.profile
            check(!canvas.setSlider(field: .avatarZoom, value: .nan) && !canvas.setSlider(field: .avatarOffsetX, value: .infinity)
                  && !canvas.setSlider(field: .name, value: 50) && store.profile == finiteProfile, "Nonfinite and nongeometry slider updates preserve the profile")
            var previews: [UserProfile] = []; canvas.onGeometryPreview = { previews.append($0) }
            let zoomTrack = canvas.accessibleSliders.first { $0.field == .avatarZoom }!.rect
            check(canvas.mouseDown(at: CGPoint(x: zoomTrack.midX, y: zoomTrack.midY)) && canvas.isDragging,
                  "Pointer press captures a portrait slider drag")
            canvas.mouseDragged(to: CGPoint(x: zoomTrack.maxX + 1000, y: zoomTrack.maxY + 1000))
            check(canvas.profileValue.avatarZoom == 20 && store.profile.avatarZoom == 2.25
                  && previews.last?.avatarZoom == 20 && canvas.isDragging, "Dragging beyond the track keeps selection and previews a clamped crop without persisting")
            canvas.mouseUp()
            check(!canvas.isDragging && store.profile.avatarZoom == 20 && canvas.isPopoverOpen, "Releasing commits the preview and retains the portrait menu")
            check(canvas.nudgeSlider(-1) && store.profile.avatarZoom < 20 && store.profile.avatarZoom >= 1,
                  "Keyboard nudging keeps the selected portrait slider")
            check(store.profile.backgroundWidth == 600 && store.profile.backgroundOffsetX == 0 && store.profile.thumbnailOffsetX == 0,
                  "Portrait controls leave full-background and thumbnail geometry independent")
            canvas.perform(actionID: "profile:menu")
            check(canvas.accessibleActions.contains { $0.id == "profile:restoreAvatar" }, "Portrait submenu returns to the existing picture actions")
            canvas.perform(actionID: "profile:restoreAvatar")
            check(store.profile.avatarZoom == 1 && store.profile.avatarOffsetX == 0 && store.profile.avatarOffsetY == 0,
                  "Restoring the default portrait also restores its crop")
            canvas.perform(actionID: "profile:avatar"); canvas.perform(actionID: "profile:background")
            check(!canvas.isPopoverOpen && imageRequests == [.avatar, .background], "Image selection closes the identity popover and delegates native file selection")
            for field in PersonalProfileField.allCases { canvas.perform(actionID: "profile:" + field.rawValue) }
            check(requestedFields == PersonalProfileField.allCases.filter { !$0.isGeometry }, "Identity fields retain inline editors while image geometry uses sliders")
            check(canvas.commit(field: .name, text: String(repeating: "名", count: 25)) && store.profile.name.count == 20, "Long Unicode name silently clips at twenty characters")
            check(canvas.commit(field: .tag, text: "#00081234567890") && store.profile.tag == "0008123456", "Tag clips at ten characters without losing leading zeroes")
            check(canvas.commit(field: .introduction, text: String(repeating: "文", count: 170)) && store.profile.introduction.count == 150, "Introduction silently clips at150 Unicode characters")
            check(!canvas.commit(field: .name, text: " ") && !canvas.commit(field: .tag, text: "bad tag"), "Empty identity and invalid tag do not overwrite saved values")
            for (field, maximum) in [(PersonalProfileField.permissionLevel, 60), (.explorationLevel, 7)] {
                check(canvas.commit(field: field, text: "-3") && canvas.commit(field: field, text: String(maximum + 5)), "Level edits silently clamp without error panels")
                check(canvas.commit(field: field, text: "invalid"), "Malformed levels restore their requested default")
                check(field == .permissionLevel ? store.profile.permissionLevel == 60 : store.profile.explorationLevel == 7, "Malformed level fallback uses60 or7")
            }
            for field in PersonalProfileField.counters {
                check(canvas.commit(field: field, text: "987654") && canvas.commit(field: field, text: "-1"), "Counter edits remain nonnegative")
                check(canvas.commit(field: field, text: "99999999999999999999999999999999999999"), "Overflowing counter preserves its last valid value without crash")
            }
            check(store.profile.operatorsCount == 0 && store.profile.weaponsCount == 0 && store.profile.archivesCount == 0, "Negative counters clamp tozero")
            check(store.profile.uid == originalID && store.profile.awakeningDate == originalDate, "Unrelated editable data does not alter UID or awakening day")
            check(!canvas.accessibleActions.contains { $0.id == "profile:resetBackground" }, "Default toolbar has no separate restore button")
            check(canvas.commit(field: .awakeningDate, text: "2024/02/29") && labels(retained).contains("2024/02/29"), "Adjacent awakening date accepts a valid leap-day edit")
            let editedDate = store.profile.awakeningDate
            check(!canvas.commit(field: .awakeningDate, text: "2025/02/29") && store.profile.awakeningDate == editedDate, "An impossible calendar date preserves the previous awakening day")
            canvas.perform(actionID: "profile:toggleDateLabel")
            check(store.profile.showsBirthday && labels(retained).contains("Birthday") && canvas.accessibleActions.contains { $0.id == "profile:birthday" }, "Clicking the label switches its display and adjacent edit action to birthday")
            check(canvas.commit(field: .birthday, text: "02/29") && store.profile.birthdayMonth == 2 && store.profile.birthdayDay == 29,
                  "Birthday is an annual month/day value and allows February29")
            check(labels(retained).contains("02/29") && !canvas.accessibleActions.contains { $0.id == "profile:awakeningDate" }, "Birthday has no visible year or competing awakening edit target")
            check(!canvas.commit(field: .birthday, text: "04/31") && !canvas.commit(field: .birthday, text: "2024/02/29") && store.profile.birthdayMonth == 2,
                  "Malformed or year-bearing birthday input preserves the saved annual date")
            canvas.perform(actionID: "profile:toggleDateLabel")
            check(!store.profile.showsBirthday && labels(retained).contains("2024/02/29") && store.profile.uid == originalID, "Switching back restores the independently edited awakening date and preservesUID")
            check(canvas.backgroundLayer.superlayer == nil && canvas.backgroundLayer.bounds == retained.bounds && !canvas.backgroundLayer.masksToBounds,
                  "Background has an independent unclipped400x334 root for outer HUD integration")
            canvas.perform(actionID: "profile:backgroundMenu")
            check(canvas.accessibleActions.allSatisfy { retained.bounds.contains($0.rect) }
                  && canvas.accessibleSliders.map(\.field) == PersonalProfileField.geometryFields
                  && canvas.accessibleSliders.allSatisfy { canvas.popoverBounds!.contains($0.rect) }
                  && !canvas.accessibleActions.contains { action in action.id.contains("adjust:") || PersonalProfileField.geometryFields.contains { action.id == "profile:" + $0.rawValue } }
                  && canvas.accessibleActions.contains { $0.id == "profile:resetBackground" }, "Theme submenu retains restore and independent thumbnail controls")
            check(canvas.popoverBounds!.maxY == PersonalProfileCanvas.backgroundRect.minY - 6,
                  "Background adjustment is anchored immediately above its toolbar button")
            let globalAccent = HUDRuntimeAppearance.accent
            canvas.perform(actionID: "profile:themeMenu")
            check(canvas.accessibleSliders.isEmpty, "Color submenu hides geometry slider accessibility")
            check(canvas.accessibleActions.filter { $0.id.hasPrefix("profile:theme:") }.count == 5
                  && canvas.accessibleActions.contains { $0.id == "profile:themeCustom" }
                  && canvas.accessibleActions.allSatisfy { canvas.popoverBounds!.contains($0.rect) },
                  "Card colors provide five presets and custom picker inside a reachable anchored submenu")
            canvas.perform(actionID: "profile:theme:A8E58B")
            check(store.profile.themeColorHex == "A8E58B" && canvas.resolvedAccent.isEqual(store.profile.resolvedAccent(fallback: .cyan))
                  && HUDRuntimeAppearance.accent.isEqual(globalAccent), "Card preset changes only the personal-card accent")
            let highlights = retained.sublayers!.flatMap { $0.sublayers ?? [] }.compactMap { $0 as? HUDControlHighlightLayer }
            check(!highlights.isEmpty && highlights.allSatisfy { $0.accentOverride?.isEqual(canvas.resolvedAccent) == true },
                  "Profile interaction highlights use the local card theme")
            canvas.setCustomColor(NSColor(srgbRed: 0.2, green: 0.4, blue: 0.6, alpha: 1))
            check(store.profile.themeColorHex == "336699", "The native color picker stores a local sRGB color")
            var colorRequests = 0
            canvas.onChooseColor = { _ in colorRequests += 1 }
            canvas.perform(actionID: "profile:themeCustom")
            check(colorRequests == 1 && canvas.isPopoverOpen, "Custom color action delegates to the native picker without replacing the HUD submenu")
            canvas.perform(actionID: "profile:themeDefault")
            check(store.profile.themeColorHex == nil && canvas.resolvedAccent.isEqual(NSColor.cyan), "Follow HUD theme restores inherited card colors")
            canvas.perform(actionID: "profile:backgroundMenu")
            check(canvas.setSlider(field: .backgroundWidth, value: 1200) && store.profile.backgroundWidth == 900
                  && canvas.setSlider(field: .backgroundWidth, value: 100) && store.profile.backgroundWidth == 400,
                  "Background width slider retains the400...900 range")
            check(canvas.setSlider(field: .backgroundOffsetX, value: 900) && store.profile.backgroundOffsetX == 400
                  && canvas.setSlider(field: .backgroundOffsetY, value: -900) && store.profile.backgroundOffsetY == -250
                  && canvas.setSlider(field: .thumbnailOffsetY, value: 200) && store.profile.thumbnailOffsetY == 1,
                  "Background and thumbnail sliders preserve their independent bounds")
            _ = canvas.setSlider(field: .backgroundWidth, value: 700); _ = canvas.setSlider(field: .backgroundOffsetX, value: 80)
            _ = canvas.setSlider(field: .backgroundOffsetY, value: 40); _ = canvas.setSlider(field: .thumbnailOffsetX, value: 50)
            let imageLayer = canvas.backgroundLayer.sublayers!.first!
            check(imageLayer.frame == CGRect(x: -70, y: 40, width: 700, height: 334), "Image extent and position overflow the canvas independently")
            check(imageLayer.mask is CAGradientLayer && imageLayer.mask?.mask is CAGradientLayer, "Background fades both horizontal and vertical edges")
            check(store.profile.thumbnailOffsetX == 0.5 && store.profile.backgroundOffsetX == 80, "Thumbnail offset uses normalized independent coordinates")
            let widthTrack = canvas.accessibleSliders.first { $0.field == .backgroundWidth }!.rect
            _ = canvas.mouseDown(at: CGPoint(x: widthTrack.midX, y: widthTrack.midY))
            canvas.mouseDragged(to: CGPoint(x: widthTrack.minX - 1000, y: widthTrack.midY))
            check(store.profile.backgroundWidth == 700 && canvas.backgroundLayer.sublayers!.first!.frame.width == 400,
                  "Background width previews its layout without changing the saved width during drag")
            canvas.dismissPopover()
            check(store.profile.backgroundWidth == 400 && !canvas.isDragging && canvas.accessibleSliders.isEmpty,
                  "Dismissing a dragged background menu commits once and removes its sliders")
            canvas.perform(actionID: "profile:backgroundMenu")
            canvas.perform(actionID: "profile:resetBackground")
            check(store.profile.backgroundWidth == 600 && store.profile.backgroundOffsetX == 0 && store.profile.thumbnailOffsetX == 0, "Restore background resets its geometry and thumbnail crop")
            canvas.dismissPopover(); canvas.perform(actionID: "profile:visibility")
            check(canvas.isTextHidden && canvas.accessibleActions.count == 2 && canvas.accessibleActions.contains { $0.id == "profile:visibility" }, "Hide text leaves the theme submenu and visibility control accessible")
            canvas.perform(actionID: "profile:visibility")
            check(!canvas.isTextHidden && canvas.accessibleActions.count == 11, "Show text restores profile actions")
            check(!canvas.mouseDown(at: CGPoint(x: -5, y: 20)), "Outside clicks remain with shell")
            seconds = 9000; canvas.refreshWorkDuration()
            check(labels(canvas.layer).contains("2.50"), "Work hours read live elapsed seconds")
            L10n.language = .simplifiedChinese
            check(canvas.makeContent(for: .profile, style: HUDModuleContentStyle(dark: false, accent: .magenta, contentsScale: 3)) === retained, "Language and theme retain the same profile surface")
            check(labels(retained).contains("个人名片") && labels(retained).contains("苏醒日"), "Chinese awakening label remains present beside vector triangles")
            check(canvas.activeAnimationCount == 0, "Reduced Motion adds no animations")
            canvas.deactivate()

            let _ = NSApplication.shared
            let host = NSView(frame: CGRect(x: 0, y: 0, width: 420, height: 360)); host.wantsLayer = true
            let window = NSWindow(contentRect: CGRect(x: -10000, y: -10000, width: 420, height: 360), styleMask: .borderless, backing: .buffered, defer: false)
            window.isReleasedWhenClosed = false; window.contentView = host; defer { window.close() }
            let input = HUDPersonalProfileInteraction(canvas: canvas, host: host)
            input.project = { $0.offsetBy(dx: 10, dy: 12) }; input.setActive(true)
            canvas.perform(actionID: "profile:permissionLevel")
            let editor = host.subviews.compactMap { $0 as? NSTextField }.first!
            check(editor.frame == canvas.fieldRect(.permissionLevel).offsetBy(dx: 10, dy: 12) && input.isInputLocked && input.isPresentingPanel, "Projected inline editor protects focus")
            editor.stringValue = "61"
            check(input.finishEditing() && editor.superview == nil && store.profile.permissionLevel == 60, "Out-of-range inline edit silently clamps and closes")
            canvas.perform(actionID: "profile:name")
            let nameEditor = host.subviews.compactMap { $0 as? NSTextField }.first!
            nameEditor.stringValue = String(repeating: "界", count: 30)
            input.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: nameEditor))
            check(nameEditor.stringValue.count == 20, "Live name field clips committed input at20characters")
            check(input.finishEditing(), "Clipped live name saves")
            canvas.perform(actionID: "profile:introduction")
            let introEditor = host.subviews.compactMap { $0 as? NSTextField }.first!
            introEditor.stringValue = String(repeating: "A", count: 180)
            input.controlTextDidChange(Notification(name: NSControl.textDidChangeNotification, object: introEditor))
            check(introEditor.stringValue.count == 150 && introEditor.maximumNumberOfLines == 0 && introEditor.cell?.wraps == true,
                  "Introduction wraps and silentlyclips live input at150characters")
            _ = input.finishEditing(commit: false)
            canvas.perform(actionID: "profile:tag")
            let tagEditor = host.subviews.compactMap { $0 as? NSTextField }.first!
            tagEditor.stringValue = "0099"; let oldTag = store.profile.tag
            check(input.finishEditing(commit: false) && store.profile.tag == oldTag, "Escape preserves saved tag")
            canvas.perform(actionID: "profile:toggleDateLabel")
            canvas.perform(actionID: "profile:birthday")
            let birthdayEditor = host.subviews.compactMap { $0 as? NSTextField }.first!
            check(birthdayEditor.stringValue == "02/29" && birthdayEditor.frame == PersonalProfileCanvas.dateValueRect.offsetBy(dx: 10, dy: 12), "Birthday uses the existing projected inline field without a year")
            birthdayEditor.stringValue = "11/20"
            check(input.finishEditing() && store.profile.birthdayMonth == 11 && store.profile.birthdayDay == 20, "Inline birthday commits its month and day")
            canvas.perform(actionID: "profile:toggleDateLabel")
            canvas.perform(actionID: "profile:backgroundMenu")
            let backgroundSliders = host.subviews.compactMap { $0 as? NSSlider }.filter { !$0.isHidden }
            check(backgroundSliders.count == 5 && host.subviews.compactMap { $0 as? NSTextField }.isEmpty
                  && !input.isInputLocked && input.capturesPointer,
                  "Background menu exposes native slider accessibility without opening an editor or freezing tilt")
            for control in canvas.accessibleSliders {
                let slider = backgroundSliders.first { $0.accessibilityLabel() == control.label }!
                check(slider.frame == control.rect.offsetBy(dx: 10, dy: 12) && slider.minValue == control.minimum && slider.maxValue == control.maximum,
                      "Accessible image sliders follow the HUD projection and their finite bounds")
            }
            let widthAX = backgroundSliders.first { $0.accessibilityLabel() == PersonalProfileField.backgroundWidth.title }!
            widthAX.setAccessibilityValue(NSNumber(value: 760))
            check(store.profile.backgroundWidth == 760, "Setting an accessible background slider persists its value")
            widthAX.setAccessibilityValue(NSNumber(value: Double.infinity))
            check(store.profile.backgroundWidth == 760, "Accessibility rejects a nonfinite geometry value")
            check(widthAX.accessibilityPerformIncrement() && store.profile.backgroundWidth > 760,
                  "Accessible increment changes geometry through the same slider action")
            canvas.dismissPopover()
            canvas.perform(actionID: "profile:portraitMenu")
            check(!input.isInputLocked && input.capturesPointer, "Portrait adjustment menu leaves parallax live while capturing pointer input")
            let portraitSliders = host.subviews.compactMap { $0 as? NSSlider }.filter { !$0.isHidden }
            check(portraitSliders.count == 3 && portraitSliders.allSatisfy { $0.accessibilityLabel() != PersonalProfileField.backgroundWidth.title },
                  "Switching to portrait controls replaces all background slider accessibility")
            let zoomAX = portraitSliders.first { $0.accessibilityLabel() == PersonalProfileField.avatarZoom.title }!
            zoomAX.setAccessibilityValue(NSNumber(value: 25))
            check(store.profile.avatarZoom == 20 && canvas.isPopoverOpen, "Accessible portrait zoom clamps to20 without closing its menu")
            canvas.dismissPopover()
            canvas.perform(actionID: "profile:menu")
            check(!input.isInputLocked && input.capturesPointer && !input.isPresentingPanel, "Custom HUD popover captures pointer input while leaving parallax live")
            check(canvas.containsPopoverPoint(CGPoint(x: 50, y: 150)) && !canvas.containsPopoverPoint(CGPoint(x: 350, y: 10)), "Popover exposes its actual input region without claiming unrelated profile fields")
            let click = NSEvent.mouseEvent(with: .leftMouseDown, location: .zero, modifierFlags: [], timestamp: 0,
                                           windowNumber: window.windowNumber, context: nil, eventNumber: 1, clickCount: 1, pressure: 1)!
            check(input.mouseDown(at: CGPoint(x: -20, y: 10), event: click) && !canvas.isPopoverOpen && !input.capturesPointer,
                  "Clicking outside an open profile menu dismisses and consumes the click")
            canvas.perform(actionID: "profile:portraitMenu")
            let draggedPosition = canvas.accessibleSliders.first { $0.field == .avatarOffsetX }!.rect
            check(input.mouseDown(at: CGPoint(x: draggedPosition.midX, y: draggedPosition.midY), event: click),
                  "Interaction routes portrait slider presses through the retained canvas")
            input.mouseDragged(to: CGPoint(x: draggedPosition.maxX + 20, y: draggedPosition.midY))
            check(input.isInputLocked && canvas.profileValue.avatarOffsetX == 1 && store.profile.avatarOffsetX == 0,
                  "Only an active slider drag locks tilt and preserves the saved crop until completion")
            input.deactivate()
            check(!input.isInputLocked && !input.isPresentingPanel && !canvas.isPopoverOpen && host.subviews.allSatisfy(\.isHidden)
                  && store.profile.avatarOffsetX == 1, "Deactivation commits a pending drag and releases all profile inputs")

            let animated = PersonalProfileCanvas(store: store, reduceMotion: { false })
            host.layer?.addSublayer(animated.layer); animated.activate()
            check(animated.commit(field: .name, text: "Animated edit") && animated.activeAnimationCount > 0, "Successful edit keeps finite update animation")
            animated.perform(actionID: "profile:menu"); animated.dismissPopover()
            check(animated.activeAnimationCount > 0, "Custom menu has finite opening and closing movement")
            animated.deactivate(); check(animated.activeAnimationCount == 0, "Deactivation removes every profile animation")
        } catch { fatalError("Personal profile canvas fixture failed: \(error)") }
        return count
    }
}
