import AppKit

final class AppDelegate: NSObject, NSApplicationDelegate {
    private let store: ConfigurationStore
    private let diagnosticDomain: String?
    private let monitor = BatteryMonitor()
    private let loginManager = LoginItemManager()
    private let overlay = OverlayController()
    private let shortcut = GlobalShortcutController()
    private lazy var hudSettings = HUDSettingsController(store: store)
    private var appUpdater: HUDUpdateController?
    private var updateAudioObserver: UUID?
    private var updateRestartInProgress = false
    private let updateMenuItem = NSMenuItem(title: "Check for Updates…", action: nil, keyEquivalent: "")
    private var loginRegistrationError: String?
    private let audioTopology = AudioTopologyWatcher()
    private var statusItem: NSStatusItem?
    private let batteryMenuItem = NSMenuItem(title: "Reading battery…", action: nil, keyEquivalent: "")
    private let workMenuItem = NSMenuItem(title: "Work Mode", action: nil, keyEquivalent: "")
    private var workObserver: UUID?
    private var workFocus: WorkModeFocusController?
    private var focusObserver: UUID?
    private var focusTerminationPending = false
    private var focusTerminationFinished = false
    private var focusTerminationDeadline: Timer?
    private var snapshot: BatterySnapshot?
    private var presentedSnapshot: BatterySnapshot?
    private var previewSnapshot: BatterySnapshot?
    private var appliedConfiguration = AppConfiguration.defaults
    private var observers: [(NotificationCenter, NSObjectProtocol)] = []
    private enum SuspensionReason: Hashable { case system, display, session }
    private var suspensionReasons = Set<SuspensionReason>()
    private var suspended: Bool { !suspensionReasons.isEmpty }
    private var terminating = false
    private var completedNormalStartup = false

    override init() {
        // Diagnostics use an empty preferences domain and never touch the user's configuration.
        diagnosticDomain = CommandLine.arguments.contains("--smoke-test") || CommandLine.arguments.contains("--ui-test")
            || CommandLine.arguments.contains("--system-smoke-test") || CommandLine.arguments.contains("--navigation-smoke-test")
            || CommandLine.arguments.contains("--render-system-preview")
            ? "EndfieldCharge.Smoke.\(UUID().uuidString)" : nil
        let defaults = diagnosticDomain.flatMap(UserDefaults.init(suiteName:)) ?? .standard
        store = ConfigurationStore(defaults: defaults)
        super.init()
    }

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.accessory)
        let args = CommandLine.arguments
        if args.contains("--smoke-test") { runSmokeTest(); return }
        if args.contains("--system-smoke-test") { runSystemSmokeTest(); return }
        if args.contains("--navigation-smoke-test") { runNavigationSmokeTest(); return }
        if args.contains("--ui-test") && args.contains("--termination-smoke-test") {
            runTerminationSmokeTest(stall: args.contains("--stall-focus-cleanup")); return
        }
        if args.contains("--ui-test") && args.contains("--lifecycle-smoke-test") {
            configureSystemOverlay()
            HUDLifecycleVerification.run(overlay: overlay, configuration: .defaults) { NSApp.terminate(nil) }
            return
        }
        if args.contains("--ui-test") && args.contains("--notes-shelf-smoke-test") {
            NotesShelfHUDVerification.run(overlay: overlay)
            return
        }
        if args.contains("--ui-test") && args.contains("--charge-badge-smoke-test") {
            ChargeBadgeHUDVerification.run(overlay: overlay)
            return
        }
        if args.contains("--ui-test") && args.contains("--work-smoke-test") {
            WorkModeHUDVerification.run(overlay: overlay)
            return
        }
        if args.contains("--ui-test") && args.contains("--event-log-smoke-test") {
            EventLogHUDVerification.run(overlay: overlay)
            return
        }
        if args.contains("--ui-test") && args.contains("--app-shortcut-smoke-test") {
            AppShortcutHUDVerification.run(overlay: overlay)
            return
        }
        if args.contains("--ui-test") && args.contains("--telemetry-smoke-test") {
            TelemetryHUDVerification.run(overlay: overlay)
            return
        }
        if let index = args.firstIndex(of: "--render-system-preview"), args.indices.contains(index + 1) {
            renderSystemPreview(to: args[index + 1]); return
        }
        if let index = args.firstIndex(of: "--render-preview"), args.indices.contains(index + 1) {
            renderPreview(to: args[index + 1]); return
        }
        // A second copy would create duplicate observers and two menu icons.
        if let identifier = Bundle.main.bundleIdentifier,
           let existing = NSRunningApplication.runningApplications(withBundleIdentifier: identifier)
            .first(where: { $0.processIdentifier != ProcessInfo.processInfo.processIdentifier }) {
            existing.activate(options: [.activateIgnoringOtherApps])
            NSApp.terminate(nil)
            return
        }
        completedNormalStartup = true
        if diagnosticDomain == nil && !args.contains("--ui-test") { HUDSourceWatchDocument.prewarmDesktop() }
        overlay.onPositionEditFinished = { [weak self] position in
            guard let self = self else { return }
            if let position = position {
                var configuration = self.store.configuration
                configuration.customPosition = position
                configuration.placement = .custom
                self.store.update(configuration)
            }
            guard !self.suspended, !self.terminating else { return }
            self.presentLatestSnapshot()
            self.openSettingsModule(.display)
        }
        configureSystemOverlay()
        makeMenu()
        if diagnosticDomain == nil { configureUpdater() }
        if diagnosticDomain == nil {
            let focus = WorkModeFocusController()
            workFocus = focus
            focusObserver = focus.observe { [weak self, weak focus] in
                guard let self, let focus else { return }
                switch focus.state {
                case .failed, .unavailable: self.overlay.workFocusStatusMessage = focus.statusMessage
                default: self.overlay.workFocusStatusMessage = nil
                }
                self.appUpdater?.environmentDidChange()
            }
            focus.receive(overlay.workMode.snapshot)
        }
        workObserver = overlay.workMode.observe { [weak self] in
            guard let self else { return }
            self.updateWorkMenu()
            self.workFocus?.receive(self.overlay.workMode.snapshot)
            self.appUpdater?.environmentDidChange()
        }
        appliedConfiguration = store.configuration
        hudSettings.onConfigurationChange = { [weak self] configuration in self?.configurationChanged(configuration) }
        monitor.onChange = { [weak self] snapshot in self?.receive(snapshot) }
        observeWorkspace()
        if diagnosticDomain == nil {
            overlay.eventRecorder.refreshDisplays()
            audioTopology.onSnapshot = { [weak self] in self?.overlay.eventRecorder.receiveAudioDevices($0) }
            audioTopology.start()
        }
        monitor.start()
        overlay.activity.start()
        overlay.clipboard.start()
        if args.contains("--ui-test") && args.contains("--clipboard-fixture") {
            ClipboardDiagnostics.seed(overlay.clipboard)
        }
        if args.contains("--ui-test") && args.contains("--event-log-fixture") {
            EventLogHUDVerification.seed(overlay.eventLog)
        }
        shortcut.start(shortcut: store.configuration.summonShortcut)
        if diagnosticDomain == nil && store.configuration.launchAtLogin && !loginManager.isEnabled {
            do { try loginManager.setEnabled(true) }
            catch { loginRegistrationError = error.localizedDescription }
        }
        hudSettings.refreshExternalStatus()
        let firstRun = !UserDefaults.standard.bool(forKey: "hasLaunched")
        if !args.contains("--login") && !args.contains("--no-onboarding") && (firstRun || args.contains("--settings")) {
            if diagnosticDomain == nil { UserDefaults.standard.set(true, forKey: "hasLaunched") }
            if args.contains("--settings") { showPreferences(nil) }
            else if !args.contains("--power") { _ = toggleSystemOverlay() }
        }
        if args.contains("--preview") { preview(nil) }
        if args.contains("--power") { _ = toggleSystemOverlay() }
        if args.contains("--ui-test") && args.contains("--work-fixture") {
            overlay.workMode.chooseCountdown(seconds: 8)
        }
        if args.contains("--ui-test") && args.contains("--power") {
            let initial: HUDModule? = args.contains("--volume-fixture") ? .volume
                : (args.contains("--work-fixture") ? .workMode : (args.contains("--clipboard-fixture") ? .clipboard
                    : (args.contains("--event-log-fixture") ? .eventLog : nil)))
            DispatchQueue.main.asyncAfter(deadline: .now() + SystemHUDView.entranceDuration + 0.3) { [weak self] in
                if let initial { self?.overlay.selectSystemModule(initial) }
            }
        }
    }

    func applicationShouldHandleReopen(_ sender: NSApplication, hasVisibleWindows flag: Bool) -> Bool {
        // Launching the app from Finder/Dock follows the same initial Map / last
        // section policy as the summon hotkey. Settings has its own menu action.
        guard !terminating else { return true }
        if !overlay.isSystemOverlayActive { _ = toggleSystemOverlay() }
        return true
    }

    func applicationShouldTerminate(_ sender: NSApplication) -> NSApplication.TerminateReply {
        terminating = true
        guard !focusTerminationFinished, let workFocus else { return .terminateNow }
        if focusTerminationPending { return .terminateLater }
        focusTerminationPending = true
        // Let the public automation finish its End action before exiting. A
        // stalled external shortcut cannot hold the application open forever.
        workFocus.shutdown { [weak self] _ in
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { self?.finishFocusTermination() }
        }
        let deadline = Timer(timeInterval: 8, repeats: false) { [weak self] _ in self?.finishFocusTermination() }
        focusTerminationDeadline = deadline
        RunLoop.main.add(deadline, forMode: .common)
        RunLoop.main.add(deadline, forMode: .modalPanel)
        return .terminateLater
    }

    private func finishFocusTermination() {
        guard focusTerminationPending else { return }
        focusTerminationDeadline?.invalidate(); focusTerminationDeadline = nil
        focusTerminationPending = false; focusTerminationFinished = true
        NSApp.reply(toApplicationShouldTerminate: true)
    }

    func applicationWillTerminate(_ notification: Notification) {
        if CommandLine.arguments.contains("--termination-smoke-test") {
            precondition(focusTerminationFinished, "Termination must wait for the Focus completion or bounded deadline")
            print("PASS: normal application termination from a main-queue callback completes Focus cleanup / bounded timeout")
            fflush(stdout)
        }
        terminating = true
        appUpdater?.stop()
        if let updateAudioObserver { overlay.perAppAudio.removeObserver(updateAudioObserver) }
        updateAudioObserver = nil
        shortcut.stop()
        monitor.stop()
        audioTopology.stop()
        overlay.clipboard.stop()
        overlay.audio.stop()
        overlay.perAppAudio.stopAll()
        overlay.workMode.shutdown()
        overlay.activity.shutdown()
        overlay.appActivity.deactivate()
        if let workObserver { overlay.workMode.removeObserver(workObserver) }
        workObserver = nil
        if let focusObserver { workFocus?.removeObserver(focusObserver) }
        focusObserver = nil
        overlay.forceCloseSystemOverlay()
        overlay.cancelPositionEditing()
        overlay.hide(animated: false)
        for (center, observer) in observers { center.removeObserver(observer) }
        observers.removeAll()
        // A rejected second launch loaded an older snapshot but never owned
        // the session. It must not overwrite the running instance's history.
        if completedNormalStartup { _ = overlay.eventLog.flushSynchronously() }
        if let domain = diagnosticDomain { UserDefaults.standard.removePersistentDomain(forName: domain) }
    }

    private func receive(_ next: BatterySnapshot) {
        overlay.eventRecorder.receiveBattery(next)
        snapshot = next
        updateMenu(next)
        guard !suspended else { return }
        presentLatestSnapshot()
    }

    private func presentLatestSnapshot() {
        guard let next = snapshot, !terminating, !suspended, !overlay.isEditingPosition else { return }
        let previous = presentedSnapshot
        presentedSnapshot = next
        if overlay.isSystemOverlayActive {
            previewSnapshot = nil
            overlay.update(snapshot: next, configuration: hudSettings.configuration)
            return
        }
        // A manual preview owns its deadline; capacity-only updates must not dismiss it.
        let powerChanged = previous.map {
            $0.hasBattery != next.hasBattery || $0.isPluggedIn != next.isPluggedIn || $0.isCharging != next.isCharging
        } ?? false
        if overlay.isVisible, previewSnapshot != nil, !powerChanged {
            let value = next.hasBattery ? next : Self.demoSnapshot
            previewSnapshot = value
            overlay.update(snapshot: value, configuration: store.configuration, preview: true)
            return
        }
        previewSnapshot = nil
        overlay.update(snapshot: next, configuration: hudSettings.configuration)
        apply(DisplayPolicy.action(for: next, previous: previous, mode: store.configuration.displayMode))
    }

    private func apply(_ action: DisplayAction) {
        guard !terminating else { return }
        guard store.configuration.batteryAlertsEnabled else { overlay.hide(); return }
        switch action {
        case .hide: overlay.hide()
        case .showPersistent:
            if !overlay.isVisible || !overlay.isPersistent { overlay.show(persistent: true, duration: 0) }
        case .showTransient: overlay.show(persistent: false, duration: store.configuration.displayDuration, replay: true)
        case .keepCurrent: break
        }
    }

    private func configurationChanged(_ configuration: AppConfiguration) {
        let previous = appliedConfiguration
        appliedConfiguration = configuration
        if previous.language != configuration.language || previous.summonShortcut != configuration.summonShortcut {
            makeMenu()
            if let value = snapshot { updateMenu(value) }
        }
        if previous.applicationIcon != configuration.applicationIcon { updateApplicationIcon() }
        if !overlay.isSystemOverlayActive { HUDRuntimeAppearance.configuration = configuration }
        guard !suspended else { return }
        let value = snapshot ?? .unavailable
        if overlay.isSystemOverlayActive {
            overlay.update(snapshot: value, configuration: configuration)
            return
        }
        if overlay.isEditingPosition {
            overlay.update(snapshot: previewValue, configuration: configuration, preview: true)
            return
        }
        if configuration.displayMode != previous.displayMode || configuration.batteryAlertsEnabled != previous.batteryAlertsEnabled {
            previewSnapshot = nil
            presentedSnapshot = value
            overlay.update(snapshot: value, configuration: configuration)
            overlay.hide(animated: false)
            apply(DisplayPolicy.action(for: value, previous: nil, mode: configuration.displayMode))
        } else {
            let preview = overlay.isVisible ? previewSnapshot : nil
            overlay.update(snapshot: preview ?? value, configuration: configuration, preview: preview != nil)
            if overlay.isVisible && configuration.displayDuration != previous.displayDuration {
                overlay.show(persistent: overlay.isPersistent, duration: configuration.displayDuration)
            }
        }
    }

    private func makeMenu() {
        let item = statusItem ?? NSStatusBar.system.statusItem(withLength: NSStatusItem.squareLength)
        statusItem = item
        updateApplicationIcon()
        item.button?.setAccessibilityLabel("EndfieldHUD")
        let menu = NSMenu()
        batteryMenuItem.isEnabled = false
        batteryMenuItem.menu?.removeItem(batteryMenuItem)
        menu.addItem(batteryMenuItem)
        menu.addItem(.separator())
        let summon = store.configuration.summonShortcut
        let power = NSMenuItem(title: L10n.text("Open overlay", "打开浮层"), action: #selector(openSystemOverlay(_:)), keyEquivalent: summon.menuKeyEquivalent)
        power.keyEquivalentModifierMask = summon.modifiers.appKitValue
        power.target = self
        menu.addItem(power)
        workMenuItem.menu?.removeItem(workMenuItem)
        workMenuItem.target = self
        workMenuItem.action = #selector(openWorkMode(_:))
        updateWorkMenu()
        menu.addItem(workMenuItem)
        let settings = NSMenuItem(title: L10n.text("Settings…", "设置…"), action: #selector(showPreferences(_:)), keyEquivalent: ",")
        settings.target = self
        menu.addItem(settings)
        let preview = NSMenuItem(title: L10n.text("Preview charging effect", "预览充电效果"), action: #selector(preview(_:)), keyEquivalent: "")
        preview.target = self
        menu.addItem(preview)
        menu.addItem(.separator())
        let about = NSMenuItem(title: L10n.text("About EndfieldHUD", "关于 EndfieldHUD"), action: #selector(about(_:)), keyEquivalent: "")
        about.target = self
        menu.addItem(about)
        updateMenuItem.menu?.removeItem(updateMenuItem)
        updateMenuItem.target = self
        updateMenuItem.action = #selector(checkForAppUpdates(_:))
        updateMenuItem.title = L10n.text("Check for Updates…", "检查更新…")
        menu.addItem(updateMenuItem)
        let quit = NSMenuItem(title: L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"), action: #selector(quit(_:)), keyEquivalent: "q")
        quit.target = self
        menu.addItem(quit)
        item.menu = menu

        // Standard shortcuts also work while editing in the settings window.
        let mainMenu = NSMenu()
        let appMenuItem = NSMenuItem()
        let appMenu = NSMenu()
        let mainQuit = NSMenuItem(title: L10n.text("Quit EndfieldHUD", "退出 EndfieldHUD"), action: #selector(quit(_:)), keyEquivalent: "q")
        mainQuit.target = self
        let mainSettings = NSMenuItem(title: L10n.text("Settings…", "设置…"), action: #selector(showPreferences(_:)), keyEquivalent: ",")
        mainSettings.target = self
        appMenu.addItem(mainSettings)
        appMenu.addItem(.separator())
        appMenu.addItem(mainQuit)
        appMenuItem.submenu = appMenu
        mainMenu.addItem(appMenuItem)
        let editMenuItem = NSMenuItem(title: L10n.text("Edit", "编辑"), action: nil, keyEquivalent: "")
        let edit = NSMenu(title: L10n.text("Edit", "编辑"))
        for (title, selector, key) in [(L10n.text("Cut", "剪切"), "cut:", "x"), (L10n.text("Copy", "复制"), "copy:", "c"),
                                        (L10n.text("Paste", "粘贴"), "paste:", "v"), (L10n.text("Select All", "全选"), "selectAll:", "a")] {
            edit.addItem(NSMenuItem(title: title, action: Selector(selector), keyEquivalent: key))
        }
        editMenuItem.submenu = edit
        mainMenu.addItem(editMenuItem)
        NSApp.mainMenu = mainMenu
        if let appUpdater { updateUpdateMenu(appUpdater.state) }
    }

    private func configureUpdater() {
        let updater = HUDUpdateController()
        appUpdater = updater
        updater.onChange = { [weak self] state in
            self?.hudSettings.receiveUpdateState(state)
            self?.updateUpdateMenu(state)
        }
        updater.onOpenAbout = { [weak self] in self?.openSettingsModule(.about) }
        updater.isSafeToRestart = { [weak self] in
            guard let self else { return false }
            return !self.terminating && !self.suspended && self.overlay.isIdleForUpdate
                && !self.overlay.workMode.snapshot.isActive && self.overlay.perAppAudio.sessions.isEmpty
                && self.workFocus?.isPending != true && NSApp.modalWindow == nil
                && !NSApp.windows.contains(where: { $0.isSheet })
        }
        updater.prepareForRestart = { [weak self] continuation in
            guard let self else { return }
            // Sparkle has accepted installation at this point. Its continuation
            // still uses NSApp.terminate and our normal Focus/save cleanup.
            self.terminating = true
            self.updateRestartInProgress = true
            self.shortcut.stop()
            self.overlay.closeForApplicationUpdate(completion: continuation)
        }
        updater.onRestartCancelled = { [weak self] in
            guard let self, self.updateRestartInProgress, !self.focusTerminationPending, !self.focusTerminationFinished else { return }
            self.updateRestartInProgress = false; self.terminating = false
            self.overlay.cancelApplicationUpdate()
            self.shortcut.start(shortcut: self.store.configuration.summonShortcut)
        }
        updater.presentUpdateUI = { [weak self] show in
            guard let self, !self.terminating else { return }
            if self.overlay.systemPhase == .closed { show() }
            else {
                self.overlay.afterSystemClose = show
                self.overlay.closeSystemOverlay()
            }
        }
        hudSettings.onCheckForUpdates = { [weak updater] in updater?.checkForUpdates() }
        hudSettings.onAutomaticUpdatesChange = { [weak updater] in updater?.setAutomaticallyInstalls($0) }
        updateAudioObserver = overlay.perAppAudio.observe { [weak updater] in updater?.environmentDidChange() }
        overlay.onSystemActivityChange = { [weak updater] in updater?.environmentDidChange() }
        updater.start()
    }

    private func updateUpdateMenu(_ state: HUDUpdateState) {
        updateMenuItem.title = state.hasUpdate
            ? L10n.text("Update available", "发现新版本") + ((state.updateVersion ?? state.latestVersion).map { " · " + $0 } ?? "") + "…"
            : (state.phase == .checking ? L10n.text("Checking for updates…", "正在检查更新…") : L10n.text("Check for Updates…", "检查更新…"))
        updateMenuItem.isEnabled = !terminating
        updateMenuItem.setAccessibilityHelp(state.detail ?? state.title)
        statusItem?.button?.toolTip = state.hasUpdate ? "EndfieldHUD · " + state.title : "EndfieldHUD"
        if let button = statusItem?.button {
            button.wantsLayer = true
            let key = "EndfieldHUD.updateBadge"
            let existing = button.layer?.sublayers?.first { $0.name == key }
            if state.hasUpdate && existing == nil {
                let dot = CALayer(); dot.name = key
                dot.frame = CGRect(x: button.bounds.maxX - 6, y: button.bounds.maxY - 6, width: 4, height: 4)
                dot.cornerRadius = 2; dot.backgroundColor = NSColor.systemOrange.cgColor
                button.layer?.addSublayer(dot)
            } else if !state.hasUpdate { existing?.removeFromSuperlayer() }
        }
    }

    @objc private func checkForAppUpdates(_ sender: Any?) {
        guard !terminating else { return }
        if appUpdater?.state.installationSupported != true { openSettingsModule(.about) }
        appUpdater?.checkForUpdates()
    }

    private func updateWorkMenu() {
        let state: String
        switch overlay.workMode.snapshot.phase {
        case .idle: state = ""
        case .running: state = L10n.text("Active", "进行中")
        case .paused: state = L10n.text("Paused", "已暂停")
        case .stopped: state = L10n.text("Stopped", "已停止")
        case .completed: state = L10n.text("Countdown complete", "倒计时结束")
        }
        let title = L10n.text("Work Mode", "工作模式") + (state.isEmpty ? "" : " · " + state)
        if workMenuItem.title != title { workMenuItem.title = title }
    }

    @objc private func openWorkMode(_ sender: Any?) {
        guard !suspended, !terminating, !overlay.isEditingPosition else { return }
        if overlay.systemPhase == .open { overlay.selectSystemModule(.workMode); return }
        guard overlay.systemPhase == .closed, toggleSystemOverlay() else { return }
        let presentation = overlay.systemPresentationGeneration
        DispatchQueue.main.asyncAfter(deadline: .now() + SystemHUDView.entranceDuration + 0.3) { [weak self] in
            guard let self, !self.terminating, !self.suspended,
                  self.overlay.systemPhase == .open,
                  self.overlay.systemPresentationGeneration == presentation else { return }
            self.overlay.selectSystemModule(.workMode)
        }
    }

    private func updateMenu(_ value: BatterySnapshot) {
        let percent = value.percentage.map { "\($0)%" } ?? L10n.text("Unknown charge", "电量未知")
        let state = value.isCharging ? L10n.text("Charging", "正在充电") : (value.isPluggedIn ? L10n.text("Power connected", "已连接电源") : L10n.text("On battery", "使用电池"))
        let description = value.hasBattery ? "\(percent) · \(state)" : L10n.text("No internal battery · Preview available", "无内置电池 · 可预览效果")
        batteryMenuItem.title = description
        statusItem?.button?.toolTip = "EndfieldHUD — \(description)"
    }

    @objc private func showPreferences(_ sender: Any?) {
        openSettingsModule(.system)
    }

    private func openSettingsModule(_ module: HUDModule) {
        guard !suspended, !terminating, !overlay.isEditingPosition else { return }
        if overlay.systemPhase == .open { overlay.selectSystemModule(module); return }
        guard overlay.systemPhase == .closed else { return }
        overlay.initialModuleRequest = module
        _ = toggleSystemOverlay()
    }

    private var previewValue: BatterySnapshot {
        snapshot?.hasBattery == true ? snapshot! : Self.demoSnapshot
    }

    private static let demoSnapshot = BatterySnapshot(
        percentage: 75, isPluggedIn: true, isCharging: true, isFullyCharged: false, hasBattery: true,
        capacity: BatteryCapacityReading(current: 3600, maximum: 4800, unit: .milliampHours))

    private func editPosition() {
        guard !terminating, !suspended, !overlay.isSystemOverlayActive else { return }
        overlay.beginPositionEditing(snapshot: previewValue, configuration: store.configuration)
    }

    @objc private func preview(_ sender: Any?) {
        guard !terminating, !suspended, !overlay.isEditingPosition, !overlay.isSystemOverlayActive else { return }
        let value = previewValue
        let persistent = store.configuration.displayMode == .always && snapshot?.hasBattery == true
        previewSnapshot = persistent ? nil : value
        overlay.update(snapshot: value, configuration: store.configuration, preview: !persistent)
        overlay.show(persistent: persistent, duration: store.configuration.displayDuration, replay: true)
    }

    @discardableResult private func toggleSystemOverlay() -> Bool {
        guard !suspended, !terminating, !overlay.isEditingPosition else { return false }
        previewSnapshot = nil
        return overlay.toggleSystemOverlay(snapshot: snapshot ?? .unavailable, configuration: hudSettings.configuration)
    }

    private func configureSystemOverlay() {
        overlay.settingsController = hudSettings
        HUDRuntimeAppearance.configuration = hudSettings.configuration
        SummonShortcut.active = hudSettings.configuration.summonShortcut
        hudSettings.onConfigurationChange = { [weak self] in self?.configurationChanged($0) }
        hudSettings.onShortcutChange = { [weak self] in self?.shortcut.applyShortcut($0) }
        hudSettings.shortcutRegistrationStatusProvider = { [weak self] in
            guard let self, self.shortcut.status == .unavailable else { return nil }
            return self.shortcut.statusDescription
        }
        hudSettings.onShortcutCaptureChange = { [weak self] recording in
            if recording { self?.shortcut.beginCapture() } else { self?.shortcut.endCapture() }
        }
        hudSettings.onLaunchAtLoginChange = { [weak self] enabled in
            guard let self else { return nil }
            guard self.diagnosticDomain == nil else { return nil }
            do { try self.loginManager.setEnabled(enabled); self.loginRegistrationError = nil; return nil }
            catch { self.loginRegistrationError = error.localizedDescription; return error.localizedDescription }
        }
        hudSettings.loginStatusProvider = { [weak self] in
            guard let self else { return "" }
            return self.loginRegistrationError ?? self.loginManager.statusDescription
        }
        hudSettings.onEditBatteryPosition = { [weak self] in
            guard let self else { return }
            self.overlay.afterSystemClose = { [weak self] in self?.editPosition() }
            self.overlay.closeSystemOverlay()
        }
        hudSettings.onOpenLink = { [weak self] url in
            guard let self else { return }
            self.overlay.afterSystemClose = { NSWorkspace.shared.open(url) }
            self.overlay.closeSystemOverlay()
        }
        overlay.onQuitAccepted = { [weak self] in
            self?.terminating = true
            self?.shortcut.stop()
        }
        overlay.onQuitAfterSystemClose = {
            // Avoid re-entering termination if an external quit caused teardown.
            DispatchQueue.main.async { NSApp.terminate(nil) }
        }
        overlay.onSystemClosed = { [weak self] in
            guard let self = self, !self.suspended, !self.terminating else { return }
            self.presentLatestSnapshot()
        }
        shortcut.onToggle = { [weak self] in
            guard let self else { return false }
            return self.toggleSystemOverlay()
        }
        shortcut.onStatusChange = { [weak self] _ in self?.hudSettings.refreshExternalStatus() }
    }

    @objc private func openSystemOverlay(_ sender: Any?) {
        guard !overlay.isSystemOverlayActive else { return }
        _ = toggleSystemOverlay()
    }

    @objc private func about(_ sender: Any?) { openSettingsModule(.about) }

    @objc private func quit(_ sender: Any?) { NSApp.terminate(sender) }

    private func observeWorkspace() {
        let center = NSWorkspace.shared.notificationCenter
        let activation = center.addObserver(forName: NSWorkspace.didActivateApplicationNotification,
                                           object: nil, queue: .main) { [weak self] notification in
            guard let app = notification.userInfo?[NSWorkspace.applicationUserInfoKey] as? NSRunningApplication,
                  app.processIdentifier != ProcessInfo.processInfo.processIdentifier else { return }
            self?.overlay.closeSystemOverlayForFocusLoss()
        }
        observers.append((center, activation))
        let ownActivation = NotificationCenter.default.addObserver(forName: NSApplication.didBecomeActiveNotification,
                                                                   object: nil, queue: .main) { [weak self] _ in
            self?.shortcut.refreshRegistration()
            self?.hudSettings.refreshExternalStatus()
        }
        observers.append((NotificationCenter.default, ownActivation))
        let ownDeactivation = NotificationCenter.default.addObserver(forName: NSApplication.didResignActiveNotification,
                                                                     object: nil, queue: .main) { [weak self] _ in
            self?.overlay.closeSystemOverlayForFocusLoss()
        }
        observers.append((NotificationCenter.default, ownDeactivation))
        let sleepEvents: [(Notification.Name, SuspensionReason)] = [
            (NSWorkspace.willSleepNotification, .system),
            (NSWorkspace.screensDidSleepNotification, .display),
            (NSWorkspace.sessionDidResignActiveNotification, .session)
        ]
        for (name, reason) in sleepEvents {
            let token = center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.suspend(reason)
            }
            observers.append((center, token))
        }
        let wakeEvents: [(Notification.Name, SuspensionReason)] = [
            (NSWorkspace.didWakeNotification, .system),
            (NSWorkspace.screensDidWakeNotification, .display),
            (NSWorkspace.sessionDidBecomeActiveNotification, .session)
        ]
        for (name, reason) in wakeEvents {
            let token = center.addObserver(forName: name, object: nil, queue: .main) { [weak self] _ in
                self?.resume(reason)
            }
            observers.append((center, token))
        }
        let token = NotificationCenter.default.addObserver(forName: NSApplication.didChangeScreenParametersNotification,
                                                           object: nil, queue: .main) { [weak self] _ in
            if self?.diagnosticDomain == nil { self?.overlay.eventRecorder.refreshDisplays() }
            self?.overlay.reposition()
        }
        observers.append((NotificationCenter.default, token))
    }

    private func suspend(_ reason: SuspensionReason) {
        suspensionReasons.insert(reason)
        appUpdater?.environmentDidChange()
        overlay.clipboard.setSuspended(true)
        overlay.workMode.setSuspended(true)
        overlay.perAppAudio.stopAll()
        shortcut.resetPressedState()
        overlay.forceCloseSystemOverlay()
        overlay.activity.shutdown()
        overlay.appActivity.deactivate()
        overlay.cancelPositionEditing()
        overlay.hide(animated: false)
        previewSnapshot = nil
    }

    private func resume(_ reason: SuspensionReason) {
        // Ignore duplicate or unmatched wake notifications; they must not dismiss a manual preview.
        guard suspensionReasons.remove(reason) != nil, !suspended else { return }
        // Read while still suspended, so presentation is applied exactly once against the last
        // awake snapshot, even when the monitor reports a change or deduplicates its callback.
        suspensionReasons.insert(reason)
        monitor.refresh()
        suspensionReasons.remove(reason)
        overlay.activity.start()
        overlay.clipboard.setSuspended(false)
        overlay.workMode.setSuspended(false)
        overlay.reposition()
        presentLatestSnapshot()
        appUpdater?.environmentDidChange()
    }

    private func updateApplicationIcon() {
        let icon = store.configuration.applicationIcon
        NSApp.applicationIconImage = icon.image(size: 512)
        statusItem?.button?.image = icon.menuBarImage()
    }

    // Local verification commands do not change settings or login items.
    private func runTerminationSmokeTest(stall: Bool) {
        let executor = ShortcutsWorkModeFocusExecutor(commandExecutor: { arguments, _ in
            if arguments == ["list"] {
                return [WorkModeFocusCommand.start.shortcutName, WorkModeFocusCommand.end.shortcutName].joined(separator: "\n")
            }
            if arguments.last == WorkModeFocusCommand.end.shortcutName {
                if stall { Thread.sleep(forTimeInterval: 30) } // Worker-only timeout fixture; no system shortcut runs.
                return "released"
            }
            return "enabled"
        }, availability: { true })
        let focus = WorkModeFocusController(executor: executor)
        workFocus = focus
        focusObserver = focus.observe { [weak focus] in
            guard focus?.state == .enabled else { return }
            // Reproduce an animation/updater completion already executing on
            // GCD's main queue, which AppKit cannot re-enter during termination.
            DispatchQueue.main.async { NSApp.terminate(nil) }
        }
        DispatchQueue.global().asyncAfter(deadline: .now() + 12) {
            fputs("FAIL: application termination did not finish within its bounded deadline\n", stderr)
            _exit(3)
        }
        focus.receive(WorkModeSnapshot(kind: .countdown, phase: .running, duration: 300, elapsed: 0))
    }

    private func runSystemSmokeTest() {
        let demo = Self.demoSnapshot
        let screen = NSScreen.main?.frame ?? CGRect(x: 0, y: 0, width: 1280, height: 800)
        var screenPointer = CGPoint(x: screen.midX, y: screen.midY)
        overlay.systemPointerLocationProviderForVerification = { screenPointer }
        func setPointer(_ point: CGPoint) {
            screenPointer = CGPoint(x: screen.midX + point.x * screen.width / 2,
                                    y: screen.midY - point.y * screen.height / 2)
            overlay.setSystemPointerForVerification(point)
        }
        configureSystemOverlay()
        observeWorkspace()
        store.onChange = { [weak self] in self?.configurationChanged($0) }
        func later(_ seconds: Double, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: body)
        }
        var assertions = 0
        func check(_ condition: Bool, _ message: String) {
            assertions += 1
            if !condition {
                fputs("FAIL: System assertion \(assertions): \(message); phase=\(overlay.systemPhase.rawValue) source=\(String(describing: overlay.systemSourceWatchForVerification?.playback.phase)) timer=\(overlay.systemSourceWatchForVerification?.hasDisplayTimerForVerification ?? false) ambient=\(overlay.systemAmbientAnimationCount) pointer=\(overlay.systemParallaxAnimationCount)\n", stderr)
                fflush(stderr)
                preconditionFailure(message)
            }
        }
        func cycle(_ index: Int) {
            check(overlay.systemPhase == .closed, "Each repeated cycle starts closed")
            overlay.initialModuleRequest = .eventLog
            precondition(overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults))
            precondition(overlay.systemPhase == .opening)
            for _ in 0..<20 { _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults) }
            receive(BatterySnapshot(percentage: 19, isPluggedIn: false, isCharging: false,
                                    isFullyCharged: false, hasBattery: true))
            overlay.show(persistent: true, duration: 0)
            overlay.hide()
            precondition(overlay.systemPhase == .opening && !overlay.isVisible)
            guard let source = overlay.systemSourceWatchForVerification else {
                check(false, "The real source shell must be available for every cycle")
                return
            }
            let openingFrames = source.renderedFrameCount
            if index == 0 && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
                later(0.18) { [self] in
                    check(overlay.systemPhase == .opening && source.playback.phase == .opening
                          && source.hasDisplayTimerForVerification && source.renderedFrameCount > openingFrames
                          && source.currentFrameForVerification?.batches.isEmpty == false,
                          "The displayed source scene must advance through its finite deployment")
                }
            }
            later(SystemHUDView.entranceDuration + 0.3) { [self] in
                let reduced = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
                precondition(overlay.systemChargeStageForVerification == (reduced ? .compact : .supercharge),
                             "Interactive shell must preserve the badge's full Supercharge entrance")
            }
            later(HUDChargeBadge.entranceDuration + 0.25) { [self] in
                precondition(overlay.systemChargeStageForVerification == .compact,
                             "Full charge entrance must settle as a compact capsule")
                precondition(overlay.systemPhase == .open, "Deployment must complete")
                precondition(overlay.systemDeploymentAnimationCount == 0, "Deployment must leave no finite tracks")
                let ambientCount = overlay.systemAmbientAnimationCount
                let reduced = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
                check(ambientCount == 0 && overlay.systemSourceWatchForVerification === source
                      && source.playback.phase == .visible && !source.isHiddenOrHasHiddenAncestor
                      && source.hasDisplayTimerForVerification == !reduced,
                      "One persistent source clock owns ambient motion without legacy ambient tracks")
                setPointer(.zero)
                setPointer(CGPoint(x: 1, y: -1))
                precondition(overlay.systemParallaxAnimationCount == (source.pointerIsAnimatingForVerification ? 1 : 0),
                             "Pointer motion uses one shared source camera without duplicate native plane tracks")
                precondition(NSApp.windows.filter { $0 is NSPanel }.count == 1, "Both HUDs must share one panel")
                overlay.reposition()
                receive(demo)
                precondition(overlay.systemAmbientAnimationCount == ambientCount, "Data and layout updates cannot duplicate ambient tracks")
                if index % 2 == 0 { overlay.closeSystemOverlay() }
                else { _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults) }
                precondition(overlay.systemPhase == .closing)
                precondition(overlay.systemAmbientAnimationCount == 0 && overlay.systemParallaxAnimationCount <= 13,
                             "Closing stops ambient motion while retaining bounded pointer tracks")
                if index == 0 && !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
                    let closingFrames = source.renderedFrameCount
                    later(0.16) { [self] in
                        check(overlay.systemPhase == .closing && source.playback.phase == .closing
                              && source.hasDisplayTimerForVerification && source.renderedFrameCount > closingFrames,
                              "Retraction keeps the panel and source display clock alive until the source exit ends")
                        check(overlay.systemChargeFollowsDialRetractionForVerification,
                              "The battery badge folds with the central native depth plane during source retraction")
                    }
                    later(source.document.animation.exit.lastKeyTime + 0.15) { [self] in
                        check(overlay.systemPhase == .closed && !overlay.lastClosedSourceTimerActive
                              && overlay.lastClosedSourcePhase == .concealed,
                              "The source exit endpoint must close the panel and stop its display clock")
                    }
                }
                for _ in 0..<20 { _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults) }
                later(SystemHUDView.exitDuration + 0.3) { [self] in
                    check(overlay.systemPhase == .closed && overlay.systemAnimationCount == 0
                          && overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive
                          && overlay.lastClosedSourcePhase == .concealed,
                          "Every completed cycle releases all native tracks and the source display clock")
                    if index < 7 { cycle(index + 1); return }
                    // Focus loss during opening must finish deployment, then retract.
                    _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults)
                    overlay.closeSystemOverlay()
                    precondition(overlay.systemPhase == .opening)
                    later(SystemHUDView.entranceDuration + SystemHUDView.exitDuration + 0.4) { [self] in
                        precondition(overlay.systemPhase == .closed)
                        _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults)
                        overlay.forceCloseSystemOverlay()
                        _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults)
                        later(SystemHUDView.entranceDuration + 0.3) { [self] in
                            precondition(overlay.systemPhase == .open, "Cancelled completions must not affect a newer deployment")
                            overlay.forceCloseSystemOverlay()
                            overlay.beginPositionEditing(snapshot: demo, configuration: .defaults)
                            precondition(!overlay.toggleSystemOverlay(snapshot: demo, configuration: .defaults))
                            overlay.cancelPositionEditing()
                            receive(demo)
                            var always = AppConfiguration.defaults
                            always.displayMode = .always
                            store.update(always)
                            _ = toggleSystemOverlay()
                            later(SystemHUDView.entranceDuration + 0.3) { [self] in
                                overlay.closeSystemOverlay()
                                later(SystemHUDView.exitDuration + 0.3) { [self] in
                                    precondition(overlay.systemPhase == .closed && overlay.isVisible && overlay.isPersistent,
                                                 "Normal close callback must restore the persistent charging HUD")
                                    overlay.hide(animated: false)
                                    print("PASS: 8 full macOS module cycles; rapid repeats; focus-close queued while opening; cancellation generations; data updates; one shared panel; one persistent source clock; one shared pointer camera; zero hidden animations; position-edit exclusion; persistent charging HUD restored through AppDelegate")
                                    NSApp.terminate(nil)
                                }
                            }
                        }
                    }
                }
            }
        }
        cycle(0)
    }

    /// A local graphical check: only the center is replaced during navigation.
    /// No live monitor, global shortcut or user preferences are changed here.
    private func runNavigationSmokeTest() {
        let demo = Self.demoSnapshot
        // Navigation is driven entirely by this fixture. Real desktop focus
        // and pointer clicks must not interrupt a scripted swap/reopen; the
        // lifecycle and System fixtures separately verify focus-loss behavior.
        var fixtureConfiguration = AppConfiguration.defaults
        fixtureConfiguration.closeOnFocusLost = false
        func blockPhysicalInput() {
            NSApp.windows.filter { $0 is NSPanel }.forEach { $0.ignoresMouseEvents = true }
        }
        let clipboardBoard = overlay.clipboard.pasteboard
        clipboardBoard.clearContents()
        clipboardBoard.setString("Navigation keeps clipboard history", forType: .string)
        _ = overlay.clipboard.store.capture(from: clipboardBoard)
        let clipboardID = overlay.clipboard.store.items.first?.id
        let reduced = NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
        let expectedAmbient = 0 // The persistent source scene owns ambient motion.
        var assertionCount = 0
        func check(_ condition: Bool, _ message: String) {
            assertionCount += 1
            if !condition {
                fputs("FAIL: Navigation assertion \(assertionCount): \(message); module=\(overlay.systemSelectedModule?.rawValue ?? "nil") phase=\(overlay.systemPhase.rawValue) source=\(String(describing: overlay.systemSourceWatchForVerification?.playback.phase)) ambient=\(overlay.systemAmbientAnimationCount) pointer=\(overlay.systemParallaxAnimationCount)\n", stderr)
                fflush(stderr)
                preconditionFailure(message)
            }
        }
        check(clipboardID != nil, "The isolated navigation fixture must populate Clipboard Cache")
        func later(_ seconds: Double, _ body: @escaping () -> Void) {
            DispatchQueue.main.asyncAfter(deadline: .now() + seconds, execute: body)
        }
        func windowIdentities() -> Set<ObjectIdentifier> {
            Set(NSApp.windows.map { ObjectIdentifier($0) })
        }
        // Inject screen coordinates without moving the physical pointer. The
        // synchronous assertions run before AppKit can deliver mouse events.
        let screenFrame = (NSScreen.screens.first { $0.frame.contains(NSEvent.mouseLocation) }
                           ?? NSScreen.main)?.frame ?? CGRect(x: 0, y: 0, width: 1280, height: 800)
        var stationaryPointer = CGPoint(x: screenFrame.minX + screenFrame.width * 0.78,
                                        y: screenFrame.minY + screenFrame.height * 0.72)
        overlay.systemPointerLocationProviderForVerification = { stationaryPointer }
        func checkOpeningPointer(differentFrom previous: CGPoint? = nil) -> CGPoint {
            guard let expected = overlay.systemCurrentPointerTargetForVerification else {
                check(false, "Opening must expose its current stationary pointer target")
                return .zero
            }
            check(abs(expected.x) > 0.1 || abs(expected.y) > 0.1,
                  "The opening fixture must point away from the center")
            check(overlay.systemSpatialPoseMatchesPointerForVerification(expected),
                  "Every opening plane must face the stationary pointer before the first mouse event, or remain flat with Reduce Motion")
            let sourceTracks = overlay.systemSourceWatchForVerification?.pointerIsAnimatingForVerification == true ? 1 : 0
            check(overlay.systemParallaxAnimationCount == sourceTracks,
                  "Opening uses only the source gyro and adds no delayed native pointer animation")
            if let previous {
                check(expected != previous, "Reopening must refresh the pointer rather than reuse the old target")
            }
            return expected
        }
        configureSystemOverlay()
        receive(demo)
        overlay.initialModuleRequest = .eventLog
        check(overlay.toggleSystemOverlay(snapshot: demo, configuration: fixtureConfiguration), "System overlay must open")
        check(overlay.systemSelectedModule == .eventLog, "The macOS navigation fixture starts in Event Log")
        // Visits below are programmatic and parallax uses the injected pointer.
        // Physical mouse movement must not start a fresh hover cue just before
        // a settled-animation assertion. Hover tracks have their own fixtures.
        blockPhysicalInput()
        let firstPointerTarget = checkOpeningPointer()
        later(SystemHUDView.entranceDuration + 0.3) { [self] in
            check(overlay.systemPhase == .open, "Initial deployment must finish")
            check(overlay.systemPointerTargetForVerification == (reduced ? .zero : firstPointerTarget)
                  && overlay.systemSpatialPoseMatchesPointerForVerification(firstPointerTarget),
                  "Finishing the entrance must preserve the stationary pointer pose")
            let windows = windowIdentities()
            let panels = Set(NSApp.windows.filter { $0 is NSPanel }.map { ObjectIdentifier($0) })
            let shell = overlay.systemShellIdentity
            let host = overlay.systemCenterHostIdentity
            guard let source = overlay.systemSourceWatchForVerification else {
                preconditionFailure("Persistent source shell unavailable: \(overlay.systemSourceFailureForVerification ?? "missing")")
            }
            let sourceGeneration = source.playback.generation
            var capturedSourceOverview = false
            var capturedPreviewModules = Set<HUDModule>()
            let previewDirectory: URL? = {
                let args = CommandLine.arguments
                guard let index = args.firstIndex(of: "--preview-directory"), args.indices.contains(index + 1) else { return nil }
                return URL(fileURLWithPath: args[index + 1], isDirectory: true)
            }()
            check(panels.count == 1 && shell != nil && host != nil, "One panel owns a shell and center host")
            check(overlay.systemAmbientStartTime == nil,
                  "The new shell does not also start the hidden legacy ambient clock")

            func checkSharedPresentation(stable: Bool) {
                check(overlay.systemPhase == .open, "Navigation must keep the overlay open")
                check(windowIdentities() == windows, "Navigation must not create or replace any window")
                check(Set(NSApp.windows.filter { $0 is NSPanel }.map { ObjectIdentifier($0) }) == panels,
                      "Every section must use the original panel")
                check(overlay.systemShellIdentity == shell && overlay.systemCenterHostIdentity == host,
                      "Navigation must retain the shell and center host")
                check(overlay.systemSourceWatchForVerification === source
                      && source.playback.phase == .visible && source.playback.generation == sourceGeneration
                      && !source.isHiddenOrHasHiddenAncestor,
                      "Every desktop section retains the same visible source scene without restarting its playback")
                check(source.document.buttons.count == 22 && source.currentFrameForVerification?.hits.isEmpty == false
                      && source.visibleMainButtonForVerification != nil,
                      "Desktop navigation retains the authored button plates and their resolved input geometry")
                check(source.hasDisplayTimerForVerification == !reduced
                      && overlay.systemAmbientAnimationCount == expectedAmbient && overlay.systemAmbientStartTime == nil
                      && overlay.systemParallaxAnimationCount <= 13,
                      "Each section has one source display clock and only bounded native pointer motion")
                if stable && !capturedSourceOverview {
                    do {
                        let image = try source.renderedImageForVerification()
                        check(image.width > 0 && image.height > 0, "The persistent scene produces an actual Metal drawable")
                        capturedSourceOverview = true
                    } catch { preconditionFailure("Source drawable failed: \(error)") }
                }
                check((1...2).contains(overlay.systemCenterContentCount), "A swap may retain at most two center screens")
                if stable {
                    let accessible = (source.accessibilityChildren() ?? []).compactMap { $0 as? NSAccessibilityElement }
                    check(!accessible.isEmpty && accessible.count <= 28,
                          "Desktop accessibility stays bounded by the authored cards and two scroll actions")
                    check(accessible.allSatisfy { !$0.isAccessibilityHidden() || !$0.isAccessibilityEnabled() },
                          "Unassigned or clipped source cards cannot remain enabled for accessibility")
                    if let window = source.window {
                        let screen = window.convertToScreen(source.convert(source.bounds, to: nil)).insetBy(dx: -1, dy: -1)
                        let enabled = accessible.filter { $0.isAccessibilityEnabled() }
                        check(!enabled.isEmpty && enabled.allSatisfy {
                            let rect = $0.accessibilityFrame()
                            return rect.minX.isFinite && rect.minY.isFinite && rect.width.isFinite && rect.height.isFinite
                                && !rect.isEmpty && screen.contains(rect)
                        }, "Enabled accessibility controls expose visible, finite clipped frames in the HUD window")
                    }
                    if let selected = overlay.systemSelectedModule {
                        let visibleSelection = accessible.filter { !$0.isAccessibilityHidden() && $0.accessibilityLabel() == selected.title }
                        check(visibleSelection.allSatisfy { ($0.accessibilityValue() as? String) == L10n.text("Selected", "已选择") },
                              "A visible selected module uses the existing localized accessibility state")
                    }
                    check(overlay.systemCenterContentCount == 1 && !overlay.isSwitchingSystemModule,
                          "A settled section must own exactly one center screen")
                    check(overlay.systemReportGeometryMatchesSelectionForVerification,
                          "A settled section must share the committed host scale and center-input mapping")
                    check(overlay.systemDeploymentAnimationCount == 0,
                          "Settled navigation must remove finite animations (\(overlay.systemSelectedModule?.rawValue ?? "none")): \(overlay.systemFiniteAnimationKeys)")
                    if let directory = previewDirectory, let module = overlay.systemSelectedModule,
                       !capturedPreviewModules.contains(module), let view = source.superview as? SystemHUDView {
                        do {
                            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
                            try view.writePNG(to: directory.appendingPathComponent(module.rawValue + ".png"), scale: 1,
                                presentation: true, background: NSColor(white: 0.04, alpha: 1).cgColor)
                            capturedPreviewModules.insert(module)
                        } catch { check(false, "Isolated module preview failed: \(error)") }
                    }
                }
            }

            func verifyCancellation() {
                overlay.selectSystemModule(.about)
                overlay.selectSystemModule(.workMode) // Queue the alternate host size before cancellation.
                checkSharedPresentation(stable: reduced)
                later(0.10) { [self] in
                    overlay.closeSystemOverlay()
                    let restoredModule = overlay.systemSelectedModule
                    check(!overlay.isSwitchingSystemModule && overlay.systemCenterContentCount <= 1,
                          "Retraction must cancel the active center swap and pending destination")
                    check(overlay.systemReportGeometryMatchesSelectionForVerification,
                          "Cancelling a pending Work Mode swap must restore the committed screen's host size and center-input mapping")
                    check(overlay.systemAmbientAnimationCount == 0 && overlay.systemParallaxAnimationCount <= 13,
                          "Retraction stops ambient motion while retaining bounded pointer tracks")
                    later(SystemHUDView.exitDuration + 0.3) { [self] in
                        check(overlay.systemPhase == .closed && overlay.systemAnimationCount == 0
                              && overlay.lastClosedAnimationCount == 0 && !overlay.lastClosedSourceTimerActive
                              && overlay.lastClosedSourcePhase == .concealed,
                              "A closed overlay must retain no animations or source display timer")
                        check(windowIdentities() == windows, "Closing must preserve the shared panel")
                        stationaryPointer = CGPoint(x: screenFrame.minX + screenFrame.width * 0.22,
                                                    y: screenFrame.minY + screenFrame.height * 0.28)
                        _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: fixtureConfiguration)
                        blockPhysicalInput()
                        let reopenedPointerTarget = checkOpeningPointer(differentFrom: firstPointerTarget)
                        later(SystemHUDView.entranceDuration + 0.3) { [self] in
                            check(overlay.systemPointerTargetForVerification == (reduced ? .zero : reopenedPointerTarget)
                                  && overlay.systemSpatialPoseMatchesPointerForVerification(reopenedPointerTarget),
                                  "Reopening must retain the refreshed stationary pointer after deployment")
                            check(restoredModule != nil && overlay.systemPhase == .open
                                  && overlay.systemSelectedModule == restoredModule,
                                  "Reopening restores the last fully committed section")
                            let beforeForcedSwap = overlay.systemSelectedModule
                            overlay.selectSystemModule(.notes)
                            overlay.selectSystemModule(.storage)
                            let forcedRestoreModule: HUDModule? = reduced ? .storage : beforeForcedSwap
                            overlay.forceCloseSystemOverlay()
                            check(overlay.systemPhase == .closed && overlay.lastClosedAnimationCount == 0,
                                  "Forced close must clear an in-flight navigation swap")
                            stationaryPointer = CGPoint(x: screenFrame.minX + screenFrame.width * 0.74,
                                                        y: screenFrame.minY + screenFrame.height * 0.32)
                            _ = overlay.toggleSystemOverlay(snapshot: demo, configuration: fixtureConfiguration)
                            blockPhysicalInput()
                            let forcedPointerTarget = checkOpeningPointer(differentFrom: reopenedPointerTarget)
                            later(SystemHUDView.entranceDuration + HUDModuleContent.transitionDuration + 0.3) { [self] in
                                check(overlay.systemPointerTargetForVerification == (reduced ? .zero : forcedPointerTarget)
                                      && overlay.systemSpatialPoseMatchesPointerForVerification(forcedPointerTarget),
                                      "Forced reopening must settle toward its newly sampled pointer")
                                check(forcedRestoreModule != nil && overlay.systemPhase == .open
                                      && overlay.systemSelectedModule == forcedRestoreModule
                                      && !overlay.isSwitchingSystemModule && overlay.systemCenterContentCount == 1,
                                      "Reopening restores the committed section; old navigation completions cannot replace it")
                                check(overlay.systemAmbientAnimationCount == expectedAmbient
                                      && overlay.systemDeploymentAnimationCount == 0 && windowIdentities() == windows
                                      && overlay.systemSourceWatchForVerification?.playback.phase == .visible
                                      && overlay.systemSourceWatchForVerification?.hasDisplayTimerForVerification == !reduced,
                                      "Reopening uses the same panel with a visible source scene and no duplicate ambient tracks")
                                check(overlay.clipboard.store.items.map(\.id) == [clipboardID!],
                                      "Closing and recreating center views must preserve session clipboard history")
                                overlay.forceCloseSystemOverlay()
                                overlay.systemPointerLocationProviderForVerification = nil
                                later(HUDModuleContent.transitionDuration + 0.2) { [self] in
                                    check(overlay.systemPhase == .closed && overlay.systemAnimationCount == 0
                                          && overlay.lastClosedAnimationCount == 0,
                                          "Stale callbacks must not revive hidden content or animations")
                                    print("PASS: \(assertionCount) navigation assertions; all \(HUDModule.allCases.count) sections; retained panel, shell, source scene and center host; one source display clock; one settled center and at most two during swaps; latest request wins; stationary-pointer opening and refreshed reopening poses; close-during-swap cleanup; safe immediate reopen; zero hidden animations")
                                    NSApp.terminate(nil)
                                }
                            }
                        }
                    }
                }
            }

            func verifyRapidRequests() {
                for module in [HUDModule.notes, .clipboard, .storage, .about, .volume] {
                    overlay.selectSystemModule(module)
                    checkSharedPresentation(stable: reduced)
                }
                later(HUDModuleContent.transitionDuration * 2 + 0.3) { [self] in
                    checkSharedPresentation(stable: true)
                    check(overlay.systemSelectedModule == .volume, "Only the latest queued destination may settle")
                    verifyCancellation()
                }
            }

            func visit(_ index: Int) {
                guard index < HUDModule.allCases.count else { verifyRapidRequests(); return }
                let module = HUDModule.allCases[index]
                overlay.selectSystemModule(module)
                checkSharedPresentation(stable: reduced)
                later(0.12) { checkSharedPresentation(stable: reduced) }
                later(max(HUDModuleContent.transitionDuration, HUDNavigation.selectionTransitionDuration) + 0.25) { [self] in
                    checkSharedPresentation(stable: true)
                    check(overlay.systemSelectedModule == module, "Each requested section must become the selected screen")
                    visit(index + 1)
                }
            }
            checkSharedPresentation(stable: true)
            visit(0)
        }
    }

    private func renderSystemPreview(to path: String) {
        let view = SystemHUDView(frame: NSRect(x: 0, y: 0, width: 1280, height: 800))
        var configuration = AppConfiguration.defaults
        configuration.theme = CommandLine.arguments.contains("--light") ? .light : .dark
        if CommandLine.arguments.contains("--chinese") { L10n.language = .simplifiedChinese }
        view.set(snapshot: Self.demoSnapshot, configuration: configuration)
        view.showStable()
        do { try view.writePNG(to: URL(fileURLWithPath: path)); print("Rendered Power overlay: \(path)") }
        catch { fputs("\(error)\n", stderr); exit(1) }
        NSApp.terminate(nil)
    }

    private func runSmokeTest() {
        var configuration = AppConfiguration.defaults
        configuration.displayDuration = 1
        store.update(configuration)
        let demo = Self.demoSnapshot
        overlay.update(snapshot: demo, configuration: .defaults, preview: true)
        overlay.show(persistent: false, duration: 1)
        precondition(overlay.isVisible)
        DispatchQueue.main.asyncAfter(deadline: .now() + ChargeIndicatorView.entranceDuration + 1 + ChargeIndicatorView.exitDuration + 0.25) { [self] in
            precondition(!overlay.isVisible, "Transient overlay must dismiss")
            overlay.show(persistent: true, duration: 1)
            overlay.hide()
            overlay.show(persistent: true, duration: 0)
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.4) { [self] in
                precondition(overlay.isVisible && overlay.isPersistent, "Reconnecting must cancel stale dismissals")
                overlay.hide(animated: false)
                precondition(!overlay.isVisible)
                monitor.start()
                monitor.stop()
                monitor.start()
                monitor.stop()
                let unplugged = BatterySnapshot(percentage: 60, isPluggedIn: false, isCharging: false,
                                                isFullyCharged: false, hasBattery: true)
                receive(unplugged)
                suspend(.display)
                suspend(.session)
                receive(demo)
                precondition(presentedSnapshot == unplugged, "Sleep must retain the last presented battery state")
                // One wake reason must not unblock a still-inactive session.
                resume(.display)
                precondition(suspended && !overlay.isVisible)
                // Exercise presentation with a controlled latest snapshot, without rereading real hardware.
                suspensionReasons.remove(.session)
                presentLatestSnapshot()
                precondition(overlay.isVisible && presentedSnapshot == demo,
                             "A connection during sleep must be shown on resume")
                overlay.hide(animated: false)
                var cancelled = false
                overlay.onPositionEditFinished = { position in cancelled = position == nil }
                overlay.beginPositionEditing(snapshot: demo, configuration: .defaults)
                precondition(overlay.isEditingPosition && overlay.isVisible)
                overlay.hide()
                precondition(overlay.isVisible, "Editing must survive ordinary battery policy dismissal")
                overlay.cancelPositionEditing()
                precondition(cancelled && !overlay.isEditingPosition && !overlay.isVisible)
                var committed: OverlayPosition?
                overlay.onPositionEditFinished = { committed = $0 }
                overlay.beginPositionEditing(snapshot: demo, configuration: .defaults)
                overlay.confirmEditedPosition()
                precondition(committed != nil && !overlay.isEditingPosition && !overlay.isVisible)
                overlay.onPositionEditFinished = nil
                receive(unplugged)
                preview(nil)
                receive(BatterySnapshot(percentage: 59, isPluggedIn: false, isCharging: false,
                                        isFullyCharged: false, hasBattery: true))
                precondition(overlay.isVisible && previewSnapshot != nil,
                             "Capacity-only updates must preserve manual previews")
                overlay.hide(animated: false)
                receive(demo)
                overlay.hide(animated: false)
                receive(unplugged)
                precondition(overlay.isVisible, "Stopping charging must display the overlay")
                receive(BatterySnapshot(percentage: 58, isPluggedIn: false, isCharging: false,
                                        isFullyCharged: false, hasBattery: true))
                DispatchQueue.main.asyncAfter(deadline: .now() + ChargeIndicatorView.entranceDuration + 0.5) { [self] in
                    precondition(overlay.isVisible, "Battery updates must not cut a stop-charging popup short")
                    DispatchQueue.main.asyncAfter(deadline: .now() + 0.5 + ChargeIndicatorView.exitDuration + 0.3) { [self] in
                        precondition(!overlay.isVisible, "A stop-charging popup must still expire")
                        print("PASS: charging-stop display and timeout, preview retention, position commit/discard, overlay timeout, reconnection race, persistent display, monitor restart, suspended charging and multi-reason wake")
                        NSApp.terminate(nil)
                    }
                }
            }
        }
    }

    private func renderPreview(to path: String) {
        let view = ChargeIndicatorView(frame: NSRect(origin: .zero, size: ChargeIndicatorView.canvasSize))
        view.set(snapshot: Self.demoSnapshot, configuration: .defaults)
        view.renderStage(.compact)
        do {
            try view.writePNG(to: URL(fileURLWithPath: path))
            print("Rendered overlay: \(path)")
        } catch { fputs("\(error)\n", stderr); exit(1) }
        NSApp.terminate(nil)
    }
}
