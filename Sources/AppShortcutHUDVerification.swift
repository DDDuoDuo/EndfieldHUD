import AppKit

/// Run explicitly with --ui-test --app-shortcut-smoke-test. LaunchServices is
/// injected: this harness never launches a target app or changes real shortcuts.
enum AppShortcutHUDVerification {
    static func run(overlay: OverlayController) {
        var assertions = 0
        func check(_ value: Bool, _ message: String) {
            assertions += 1
            if !value {
                fputs("FAIL: App shortcut assertion \(assertions): \(message); phase=\(overlay.systemPhase.rawValue) module=\(overlay.systemSelectedModule?.rawValue ?? "nil") targets=\(overlay.appNavigationTargetsForVerification.map { $0.identifier })\n", stderr)
                fflush(stderr)
                preconditionFailure(message)
            }
        }
        func later(_ delay: TimeInterval, _ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + delay, execute: action)
        }
        func verifyMapButtonCutouts() {
            guard let source = overlay.systemSourceWatchForVerification,
                  let frame = source.currentFrameForVerification,
                  let camera = source.currentCameraForVerification else {
                check(false, "Map cutout routing requires the live source frame"); return
            }
            let original = source.desktopMapOcclusionEnabled
            defer { source.desktopMapOcclusionEnabled = original }
            source.desktopMapOcclusionEnabled = false
            let targets: [HUDNavigationTarget] = [.module(.storage), .module(.activityMonitor)]
            var samples: [(point: CGPoint, target: HUDNavigationTarget)] = []
            for hit in frame.hits {
                let center = hit.rect.origin + hit.rect.size * 0.5
                guard let projected = camera.camera.project(SIMD3(center.x, center.y, 0), world: hit.world, viewport: source.bounds)?.point,
                      let target = source.navigationTarget(at: projected), targets.contains(target) else { continue }
                for y in 1...9 { for x in 1...9 {
                    let p = hit.rect.origin + hit.rect.size * SIMD2(Double(x) / 10, Double(y) / 10)
                    guard let point = camera.camera.project(SIMD3(p.x, p.y, 0), world: hit.world, viewport: source.bounds)?.point,
                          source.navigationTarget(at: point) == target else { continue }
                    samples.append((point, target))
                } }
            }
            source.desktopMapOcclusionEnabled = true
            for target in targets {
                let visible = samples.filter { $0.target == target && source.bottomButtonContains($0.point) }
                let cutouts = samples.filter { $0.target == target && !source.bottomButtonContains($0.point) }
                check(!visible.isEmpty && !cutouts.isEmpty,
                      "Both bottom plates have sampled visible faces and transparent parts inside their original raycast quads")
                check(visible.allSatisfy { source.navigationTarget(at: $0.point) == target },
                      "Map clipping preserves navigation on each visible bottom plate")
                check(cutouts.allSatisfy { source.navigationTarget(at: $0.point) == nil },
                      "Transparent bottom-card cutouts leave native Map clicks and pin actions available")
            }
            source.desktopMapOcclusionEnabled = false
            check(samples.allSatisfy { source.navigationTarget(at: $0.point) == $0.target },
                  "Other modules retain the authored bottom-button hit regions")
        }
        func verifyDesktopPresentations() {
            guard let source = overlay.systemSourceWatchForVerification else {
                check(false, "Shortcut artwork requires the existing live source shell"); return
            }
            let original = source.desktopNavigationForVerification, language = L10n.language
            defer {
                L10n.language = language
                source.setDesktopNavigation(original)
                source.refreshDesktopLanguage()
            }
            let bitmap = CGContext(data: nil, width: 12, height: 8, bitsPerComponent: 8,
                bytesPerRow: 48, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            bitmap.setFillColor(NSColor.systemRed.cgColor); bitmap.fill(CGRect(x: 2, y: 1, width: 8, height: 6))
            let originalIcon = NSImage(cgImage: bitmap.makeImage()!, size: CGSize(width: 12, height: 8))
            let shortcuts = AppShortcutIcon.allCases.enumerated().map { index, preset in
                HUDAppShortcutPresentation(id: UUID(), name: index == 1 ? "微信 WeChat" : "Saved application \(index)",
                    iconPreset: preset, icon: originalIcon)
            }
            source.setDesktopNavigation(HUDDesktopWatchNavigation.entries(shortcuts: shortcuts))
            func reveal(_ target: HUDNavigationTarget) {
                for _ in 0..<100 { if !source.scrollDesktopNavigation(-1) { break } }
                for _ in 0..<100 {
                    if source.desktopPointForVerification(target: target) != nil
                        && source.desktopPresentationForVerification(target: target)?.captionVisible == true { return }
                    if !source.scrollDesktopNavigation(1) { break }
                }
                check(false, "Every recycled shortcut must show its saved name when reached: \(target.identifier)")
            }
            for shortcut in shortcuts {
                let target = HUDNavigationTarget.appShortcut(shortcut.id)
                reveal(target)
                do {
                    check(try source.verifyCurrentAccessibilityGeometryForVerification() > 0,
                          "Queried accessibility geometry matches every recycled shortcut after scrolling")
                } catch { check(false, "Accessibility projection after scrolling: \(error)") }
                guard let shown = source.desktopPresentationForVerification(target: target) else {
                    check(false, "A reached shortcut must have rendered artwork and a caption"); continue
                }
                check(shown.caption == shortcut.name && shown.captionVisible && !shown.wrapped,
                      "Recycled plates keep the complete, single-line saved app name, including previously hidden BackPack text")
                let chosen = AppShortcutArtwork.image(for: shortcut.iconPreset, original: originalIcon,
                    size: 96, color: NSColor(white: 0.12, alpha: 1)).map(HUDSourceDesktopIconLayout.image)
                check((shown.image != nil) == (chosen != nil) && shown.vectorVisible == (chosen == nil),
                      "The displayed app uses its chosen game/original raster or shared vector fallback")
                if let chosen, let image = shown.image {
                    check(image.width == chosen.width && image.height == chosen.height
                        && CFEqual(image.dataProvider!.data!, chosen.dataProvider!.data!),
                          "Displayed shortcut pixels match the selected artwork, including original app colors")
                }
            }
            source.isHidden = true
            check(source.desktopAccessibilityAvailabilityForVerification.allSatisfy { $0.hidden && !$0.enabled },
                  "Hiding the source shell immediately hides and disables all accessibility buttons")
            source.isHidden = false
            do {
                check(try source.verifyCurrentAccessibilityGeometryForVerification() > 0,
                      "Showing the source shell restores current accessibility geometry without another frame")
            } catch { check(false, "Accessibility projection after visibility change: \(error)") }
            let selected = source.selectedDesktopModule
            for language in [AppLanguage.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean] {
                L10n.language = language
                source.refreshDesktopLanguage()
                check(source.desktopPresentationForVerification(target: .module(.system))?.caption == HUDModule.system.title
                    && source.desktopPresentationForVerification(target: .module(.about))?.caption == HUDModule.about.title,
                      "Language changes update visible navigation captions without recreating the HUD")
                check(source.desktopAccessibilityLabelsForVerification.contains(HUDModule.profile.title)
                    && source.desktopAccessibilityLabelsForVerification.contains(L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"))
                    && source.desktopProfileCaptionsForVerification.contains(L10n.text("Authority", "权限等级")),
                      "Profile text and accessibility action labels follow the new language immediately")
                reveal(.module(.fileShelf))
                let shelf = source.desktopPresentationForVerification(target: .module(.fileShelf))
                check(shelf?.wrapped == !L10n.isChinese,
                      "Japanese and Korean shelf titles wrap while Chinese keeps its compact single line")
                if language == .japanese || language == .korean, let shelf {
                    check(shelf.fontSize >= 20 && shelf.captionSize.width >= 124 && shelf.captionSize.height >= 56,
                          "The live source shelf caption keeps a readable font and a two-line area after language changes")
                    if language == .japanese {
                        check(shelf.caption == "一時ファイル\nシェルフ",
                              "Japanese uses two natural caption lines instead of shrinking the complete name")
                    }
                }
                reveal(.appShortcut(shortcuts[1].id))
                check(source.desktopPresentationForVerification(target: .appShortcut(shortcuts[1].id))?.caption == shortcuts[1].name
                    && source.selectedDesktopModule == selected,
                      "Language refresh preserves custom names and the selected module")
            }
        }
        func clickSource(_ target: HUDNavigationTarget) {
            guard let source = overlay.systemSourceWatchForVerification, let window = source.window else {
                check(false, "A source navigation click requires the live scene and its shared window")
                return
            }
            // Exercise the same bounded scroll action exposed to accessibility,
            // then the real projected mouse-down/up path. Never post global input.
            for _ in 0..<100 { if !source.scrollDesktopNavigation(-1) { break } }
            var point = source.desktopPointForVerification(target: target)
            for _ in 0..<100 {
                if point != nil || !source.scrollDesktopNavigation(1) { break }
                point = source.desktopPointForVerification(target: target)
            }
            guard let point else {
                check(false, "The saved desktop action must be reachable through projected source navigation: \(target.identifier)")
                return
            }
            check(source.navigationTarget(at: point) == target,
                  "The displayed source button raycast resolves the requested desktop identity")
            let location = source.convert(point, to: nil)
            for type: NSEvent.EventType in [.leftMouseDown, .leftMouseUp] {
                guard let event = NSEvent.mouseEvent(with: type, location: location, modifierFlags: [],
                    timestamp: ProcessInfo.processInfo.systemUptime, windowNumber: window.windowNumber,
                    context: nil, eventNumber: 0, clickCount: 1, pressure: type == .leftMouseDown ? 1 : 0) else {
                    check(false, "The local source click fixture must create a mouse event")
                    return
                }
                if type == .leftMouseDown { source.mouseDown(with: event) }
                else { source.mouseUp(with: event) }
            }
        }
        let target = URL(fileURLWithPath: "/System/Applications/TextEdit.app")
        let item: AppShortcut
        do { item = try overlay.addAppShortcutForVerification(target) }
        catch { preconditionFailure("App shortcut fixture: \(error)") }
        var launches = 0
        var observedCompletion: ((Result<NSRunningApplication, Error>) -> Void)?
        overlay.launchAppShortcut = { url, completion in
            launches += 1
            check(overlay.systemPhase == .closed && overlay.systemShellIdentity == nil,
                  "Launch waits until the HUD has closed and released its presentation")
            check(overlay.lastClosedAnimationCount == 0, "Close animation is completely retired before launch")
            check(url == target, "The chosen app installation is passed through")
            observedCompletion = completion
        }
        _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
        later(SystemHUDView.entranceDuration + 0.3) {
            verifyMapButtonCutouts()
            verifyDesktopPresentations()
            let shell = overlay.systemShellIdentity, host = overlay.systemCenterHostIdentity
            overlay.selectSystemModule(.addApp)
            later(HUDModuleContent.transitionDuration + 0.3) {
                check(overlay.systemSelectedModule == .addApp && overlay.systemShellIdentity == shell
                      && overlay.systemCenterHostIdentity == host, "Add App retains the shared shell and host")
                check(Array(overlay.appNavigationTargetsForVerification.suffix(2)) == [.appShortcut(item.id), .module(.addApp)],
                      "The saved application is a distinct right tile before the final Add App tile")
                clickSource(.module(.power))
                later(HUDModuleContent.transitionDuration + 0.3) {
                    check(overlay.systemSelectedModule == .power && overlay.systemPhase == .open
                          && !overlay.systemQuitConfirmationVisibleForVerification,
                          "The projected Power module opens the battery page instead of hijacking overview or quit")
                    overlay.selectSystemModule(.notes, animated: false)
                    clickSource(.appShortcut(item.id))
                    overlay.activateAppNavigationForVerification(item.id)
                    check(overlay.systemPhase == .closing && launches == 0, "Click begins closing without activating a target")
                    later(SystemHUDView.exitDuration + 0.3) {
                        check(launches == 1 && overlay.systemPhase == .closed, "Repeated click launches only once")
                        check(!overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults),
                              "A pending OS launch cannot race a reopened HUD")
                        observedCompletion?(.success(NSRunningApplication.current))
                        check(overlay.eventLog.events.contains { $0.kind == .appShortcutOpened && $0.detail == item.name },
                              "Successful launch adds an app action event")
                        _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                        later(SystemHUDView.entranceDuration + 0.3) {
                            check(overlay.systemSelectedModule == .notes, "Launching a navigation shortcut preserves the last content module")
                            overlay.activateAppNavigationForVerification(item.id)
                            overlay.forceCloseSystemOverlay()
                            later(SystemHUDView.exitDuration + 0.3) {
                                check(launches == 1 && overlay.systemPhase == .closed, "Forced close cancels an unlaunched request")
                                _ = overlay.toggleSystemOverlay(snapshot: .unavailable, configuration: .defaults)
                                later(SystemHUDView.entranceDuration + 0.3) {
                                    overlay.activateAppNavigationForVerification(item.id)
                                    later(SystemHUDView.exitDuration + 0.3) {
                                        check(launches == 2, "A later explicit launch is accepted")
                                        observedCompletion?(.failure(NSError(domain: "ShortcutFixture", code: 1,
                                            userInfo: [NSLocalizedDescriptionKey: "Test launch failure"])))
                                        later(SystemHUDView.entranceDuration + 0.3) {
                                            check(overlay.systemPhase == .open && overlay.systemSelectedModule == .addApp
                                                  && overlay.systemAppLaunchErrorForVerification == "Test launch failure",
                                                  "An OS launch failure returns to an editable shortcut with the error")
                                            overlay.activateAppNavigationForVerification(item.id)
                                            later(SystemHUDView.exitDuration + 0.3) {
                                                check(launches == 3, "Retry hands off after another complete close")
                                                overlay.forceCloseSystemOverlay()
                                                observedCompletion?(.failure(NSError(domain: "ShortcutFixture", code: 2)))
                                                check(overlay.systemPhase == .closed, "A stale launch callback cannot reopen after shutdown")
                                                print("PASS: \(assertions) app-shortcut HUD assertions; shared shell, close-first launch, duplicate-click gate, cancellation, errors and retry")
                                                NSApp.terminate(nil)
                                            }
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}
