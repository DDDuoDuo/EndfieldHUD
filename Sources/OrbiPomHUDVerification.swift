import AppKit

/// Explicit diagnostic entry; the app's --ui-test stores contain no real data.
enum OrbiPomHUDVerification {
    private static var session: Session?
    static func run(overlay: OverlayController) {
        precondition(CommandLine.arguments.contains("--ui-test"))
        session = Session(overlay); session?.start()
    }
    private final class Session {
        let overlay: OverlayController
        var view: SystemHUDView!
        var game: OrbiPomCanvas { view.minigameCanvasForVerification }
        var hud: HUDCenterOverlay { view.designContext.overlay }
        var ids: [String] { view.designControlIDsForVerification(.minigame) }
        var configuration = AppConfiguration.defaults
        var count = 0, done = false, monitor: Any?
        init(_ overlay: OverlayController) { self.overlay = overlay }
        func start() {
            monitor = NSEvent.addLocalMonitorForEvents(matching:[.mouseMoved,.leftMouseDown,.leftMouseUp,.leftMouseDragged,.scrollWheel,.keyDown,.keyUp]) { _ in nil }
            configuration.language = .english; configuration.closeOnFocusLost = false
            overlay.settingsController!.update { $0 = configuration }; overlay.initialModuleRequest = .minigame
            check(overlay.toggleSystemOverlay(snapshot:.unavailable,configuration:configuration),"Native HUD opens minigame")
            wait("open",until:{ self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                wait("active clock",until:{ self.game.isActive && self.game.hasFrameTimer }) { [self] in
                    let s = game.session.snapshot
                    check(s.isPlaying && !game.session.manuallyPaused && s.bodies.isEmpty,"Showing the game starts the original session on its clock")
                    check(overlay.eventLog.events.contains { $0.kind == .minigameAction && $0.metadata == ["action":"started"] },"Game action logs no private data")
                    check(Set(["rules","pause","restart"] + OrbiPomSkill.allCases.map(\.rawValue)).isSubset(of:ids) && !ids.contains("resume"),
                          "Rules, all four skills, Pause and Restart are live HUD controls")
                    let skills = game.accessibilityItems.filter { OrbiPomSkill(rawValue:$0.id) != nil }
                    check(skills.count == 4 && skills.allSatisfy { $0.enabled == s.canUse(OrbiPomSkill(rawValue:$0.id)!) },"Skill keys follow the original SP rules")
                    let p = CGPoint(x:OrbiPomCanvas.board.minX+60,y:OrbiPomCanvas.board.minY+100)
                    press(p)
                    wait("falling piece",until:{ !self.game.session.snapshot.bodies.isEmpty }) { [self] in
                        let bodies = game.session.snapshot.bodies
                        check(abs(bodies[0].x - Double(OrbiPomCanvas.world(p).x)) < 10,"A projected board press drops the piece where it was aimed")
                        check(game.bodyLayerCount == bodies.count,"Original bodies use retained artwork")
                        snapshot(); checkControls()
                    }
                }
            }
        }
        func checkControls() {
            let runtime = game.session.runtime!
            view.performDesignActionForVerification(.minigame,"rules")
            check(game.rulesPresented && game.isPopoverOpen && !game.hasFrameTimer,
                  "The help button opens the instructions and holds play")
            later {
                self.snapshot("minigame-rules")
                self.view.performDesignActionForVerification(.minigame,"rules")
                self.check(!self.game.rulesPresented && !self.game.isPopoverOpen && self.game.hasFrameTimer,
                           "The help button closes the instructions and play resumes")
                self.view.performDesignActionForVerification(.minigame,"pause")
                self.check(self.game.session.manuallyPaused && runtime.snapshot.paused && !self.game.hasFrameTimer,"Manual pause records session intent")
                self.check(self.ids.contains("resume") && !self.ids.contains("pause"),"Pause swaps to the Resume control")
                self.later {
                    self.snapshot("minigame-paused")
                    #if !HUD_RELEASE
                    runtime.evaluateForTesting("var diagnosticOverflow=__game.phys.spawn(5,115,0);Matter.Body.setStatic(diagnosticOverflow.body,true);")
                    self.view.performDesignActionForVerification(.minigame,"resume")
                    self.wait("danger countdown",until:{ runtime.snapshot.dangerSeconds != nil }) {
                        self.snapshot("minigame-danger")
                        self.check(runtime.snapshot.isPlaying,"Top overflow warns before ending the game")
                        self.wait("danger loss",until:{ runtime.snapshot.state == "over" }) {
                            self.later {
                                self.snapshot("minigame-over")
                                self.check(!self.game.hasFrameTimer && self.hud.toastState.current?.title == "Game over",
                                           "Game over stops simulation scheduling and shows its toast")
                                self.check(self.overlay.eventLog.events.contains { $0.kind == .minigameAction && $0.metadata == ["action":"finished"] },
                                           "A finished game logs only its action")
                                self.view.performDesignActionForVerification(.minigame,"restart")
                                self.check(runtime.snapshot.isPlaying && self.game.hasFrameTimer && self.hud.confirmState.request == nil
                                           && self.hud.toastState.current?.title == "New game","Restart after game over starts at once without asking")
                                self.press(CGPoint(x:OrbiPomCanvas.board.midX,y:OrbiPomCanvas.board.minY+100))
                                self.wait("new piece",until:{ !runtime.snapshot.bodies.isEmpty }) { self.checkSwitch() }
                            }
                        }
                    }
                    #else
                    self.view.performDesignActionForVerification(.minigame,"resume"); self.checkSwitch()
                    #endif
                }
            }
        }
        func checkSwitch() {
            let runtime = game.session.runtime!
            overlay.selectSystemModule(.archive)
            check(!game.hasFrameTimer && runtime.snapshot.paused,"Switch immediately stops game work during outgoing animation")
            let step = runtime.advanceCount
            wait("archive",until:{ self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in
                check(runtime.advanceCount == step,"No simulation runs behind another module")
                overlay.selectSystemModule(.minigame)
                wait("game return",until:{ self.game.isActive && !self.overlay.isSwitchingSystemModule }) { [self] in
                    check(game.session.runtime === runtime,"Switching modules preserves the game session")
                    check(game.hasFrameTimer,"Return resumes the visible game clock")
                    view.performDesignActionForVerification(.minigame,"restart")
                    check(game.restartConfirmation && !game.hasFrameTimer && hud.capturesInput && hud.confirmState.request?.id == "minigameRestart"
                          && hud.confirmState.request?.title == "Restart the game?","Restart asks on the shared confirm bar and pauses play")
                    hud.decide(.cancel)
                    check(!game.restartConfirmation && game.hasFrameTimer && !runtime.snapshot.bodies.isEmpty,"Cancel resumes play and keeps the stack")
                    view.performDesignActionForVerification(.minigame,"restart"); hud.decide(.confirm)
                    wait("confirmed restart",until:{ !self.game.restartConfirmation }) { [self] in
                        check(game.session.runtime === runtime && runtime.snapshot.isPlaying && runtime.snapshot.bodies.isEmpty && game.hasFrameTimer,
                              "Confirmed restart reuses the original VM for a new game")
                        check(overlay.eventLog.events.contains { $0.kind == .minigameAction && $0.metadata == ["action":"restarted"] },"Restart logs only its action")
                        view.performDesignActionForVerification(.minigame,"pause")
                        overlay.selectSystemModule(.archive)
                        wait("paused archive",until:{ self.overlay.systemSelectedModule == .archive && !self.overlay.isSwitchingSystemModule }) { [self] in
                            overlay.selectSystemModule(.minigame)
                            wait("paused return",until:{ self.game.isActive && !self.overlay.isSwitchingSystemModule }) { [self] in
                                check(game.session.manuallyPaused && runtime.snapshot.paused && !game.hasFrameTimer && ids.contains("resume"),
                                      "Manual pause survives leaving and returning to the game tab")
                                view.cancelAnimations()
                                check(!game.hasFrameTimer && !game.isActive,"Cancel animations cannot reactivate a hidden game")
                                overlay.forceCloseSystemOverlay()
                                check(!game.hasFrameTimer,"Forced close stops game clock")
                                reopen(runtime)
                            }
                        }
                    }
                }
            }
        }
        func reopen(_ runtime: OrbiPomRuntime) {
            overlay.initialModuleRequest = .minigame
            check(overlay.toggleSystemOverlay(snapshot:.unavailable,configuration:configuration),"HUD reopens")
            wait("reopen",until:{ self.overlay.systemPhase == .open }) { [self] in
                view = NSApp.windows.compactMap { $0.contentView as? SystemHUDView }.first!
                check(game.session.runtime === runtime,"HUD close/reopen retains original simulation")
                wait("reopened game",until:{ self.game.isActive && self.ids.contains("resume") }) { [self] in
                    check(game.session.manuallyPaused && runtime.snapshot.paused && !game.hasFrameTimer,
                          "Manual pause survives a recreated native HUD canvas")
                    view.performDesignActionForVerification(.minigame,"resume")
                    check(game.hasFrameTimer && !runtime.snapshot.paused,"Only the Resume control restarts a manually paused session")
                    overlay.closeSystemOverlay()
                    check(!game.hasFrameTimer,"Closing animation starts with game paused")
                    wait("closed",until:{ self.overlay.systemPhase == .closed }) { [self] in
                        check(overlay.lastClosedAnimationCount == 0,"Closed HUD leaves no animation tracks")
                        check(overlay.notesForVerification.isEmpty,"Game never writes Notes")
                        done = true; if let monitor { NSEvent.removeMonitor(monitor) }
                        print("PASS: \(count) OrbiPom native assertions; projected board input, real controls, instructions pop-over, confirm-bar restart, timer gating, transition cancellation, session retention and isolation")
                        OrbiPomHUDVerification.session = nil; NSApp.terminate(nil)
                    }
                }
            }
        }
        /// A press and release at a content-box point through the host's real pointer path.
        func press(_ point: CGPoint) {
            let location = view.convert(view.designBoxViewPointForVerification(point), to:nil)
            for type in [NSEvent.EventType.leftMouseDown, .leftMouseUp] {
                let event = NSEvent.mouseEvent(with:type,location:location,modifierFlags:[],timestamp:0,windowNumber:view.window!.windowNumber,
                                               context:nil,eventNumber:1,clickCount:1,pressure:1)!
                if type == .leftMouseDown { view.mouseDown(with:event) } else { view.mouseUp(with:event) }
            }
        }
        func later(_ action: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline:.now()+0.22) { [self] in guard !done else { return }; action() }
        }
        func snapshot(_ name: String = "minigame") {
            guard let directory = ProcessInfo.processInfo.environment["HUD_ORBIPOM_PREVIEW_DIR"] else { return }
            do {
                let url = URL(fileURLWithPath:directory); try FileManager.default.createDirectory(at:url,withIntermediateDirectories:true)
                try view.writePNG(to:url.appendingPathComponent(name+".png"),scale:1,background:NSColor(white:0.08,alpha:1).cgColor)
            } catch { check(false,"Snapshot failed: \(error)") }
        }
        func wait(_ label: String,until condition: @escaping () -> Bool,then action: @escaping () -> Void) {
            let deadline = Date().addingTimeInterval(20)
            func poll() {
                guard !done else { return }
                NSApp.windows.compactMap { $0 as? NSPanel }.forEach { $0.ignoresMouseEvents = true }
                if condition() { action(); return }; check(Date() < deadline,"Timed out: " + label,counting:false)
                DispatchQueue.main.asyncAfter(deadline:.now()+0.05,execute:poll)
            }
            DispatchQueue.main.asyncAfter(deadline:.now()+0.05,execute:poll)
        }
        func check(_ value: Bool,_ message: String,counting: Bool = true) {
            if counting { count += 1 }
            guard !value else { return }
            fputs("FAIL: OrbiPom \(count): \(message)\n", stderr); done = true
            overlay.forceCloseSystemOverlay(); preconditionFailure(message)
        }
    }
}
