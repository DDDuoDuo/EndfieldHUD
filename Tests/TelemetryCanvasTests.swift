import AppKit
import QuartzCore

enum TelemetryCanvasTests {
    static func run() -> Int {
        precondition(Thread.isMainThread)
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func wait(_ predicate: () -> Bool) -> Bool {
            let limit = Date().addingTimeInterval(3)
            while !predicate() && Date() < limit { RunLoop.current.run(until: Date().addingTimeInterval(0.002)) }
            return predicate()
        }
        func allLayers(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(allLayers) + (root.mask.map(allLayers) ?? [])
        }
        func named(_ name: String, _ root: CALayer) -> CALayer { allLayers(root).first { $0.name == name }! }
        func text(_ name: String, _ root: CALayer) -> String { (named(name, root) as! CATextLayer).string as? String ?? "" }
        func ids(_ root: CALayer) -> [ObjectIdentifier] { allLayers(root).map(ObjectIdentifier.init) }
        func elements(_ path: CGPath?) -> Int { var result = 0; path?.applyWithBlock { _ in result += 1 }; return result }
        let originalLanguage = L10n.language
        defer { L10n.language = originalLanguage }
        L10n.language = .english
        let dark = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        let light = HUDModuleContentStyle(dark: false, accent: .cyan, contentsScale: 3)

        let previewMonitor = SystemActivityMonitor.fixture()
        var preview: ActivityMonitorCanvas? = ActivityMonitorCanvas(controller: previewMonitor, reduceMotion: { false })
        let activityRoot = preview!.makeContent(for: .activityMonitor, style: dark)
        let activityIDs = ids(activityRoot)
        check(activityRoot.bounds.size == CGSize(width: 400, height: 334), "Activity fits the established central content frame")
        check(text("activity.value.0", activityRoot).contains("%") && text("activity.value.1", activityRoot) == "10.2 GB"
              && text("activity.secondary.1", activityRoot) == "/ 17.2 GB",
              "Fixture CPU and memory produce compact real-unit readouts")
        check(text("activity.value.2", activityRoot).hasPrefix("↑ ") && text("activity.secondary.2", activityRoot).hasPrefix("↓ "),
              "Upload and download retain their direction beside the paired graph")
        check(text("activity.value.3", activityRoot).hasPrefix("R ") && text("activity.secondary.3", activityRoot).hasPrefix("W "),
              "Disk read and write remain separately identified")
        let fixtureLine = named("activity.graph.0.line.0", activityRoot) as! CAShapeLayer
        check(elements(fixtureLine.path) == 60, "A trace contains precisely sixty fixed-topology history samples")
        check(elements((fixtureLine.mask as? CAShapeLayer)?.path) > 0, "A valid history has a visible sample-coverage mask")
        check(preview!.animationCount == 0 && !previewMonitor.isActive, "Constructing a preview adds no animation or sampling work")
        preview!.activate(); preview!.activate()
        check(previewMonitor.isActive && preview!.animationCount == 0, "Fixture activation stays deterministic with no repeated reveal animation")
        check(preview!.accessibleActions.map(\.id) == ["activity:overview", "activity:apps"] && !preview!.cancelDetail(),
              "Activity exposes overview and Apps tabs")
        check(preview!.mouseDown(at: CGPoint(x: 30, y: 100)) && !preview!.mouseDown(at: CGPoint(x: -1, y: 100)),
              "Activity consumes only clicks inside its content")
        check(!preview!.mouseDown(at: CGPoint(x: CGFloat.nan, y: 1)), "Nonfinite input coordinates are rejected")
        check(preview!.accessibilityStatus.contains("Upload") && preview!.accessibilityStatus.contains("Disk write"),
              "Accessibility describes every reported metric without exposing layer implementation details")
        L10n.language = .simplifiedChinese
        check(preview!.makeContent(for: .activityMonitor, style: light) === activityRoot && ids(activityRoot) == activityIDs,
              "Theme and language changes retain the complete Activity layer tree")
        check(text("activity.title.1", activityRoot) == "RAM" && preview!.accessibilityStatus.contains("RAM ")
              && preview!.accessibilityStatus.contains("磁盘读取"),
              "Activity keeps RAM consistent while its other labels and accessible readouts localize together")
        check((named("activity.value.0", activityRoot) as! CATextLayer).contentsScale >= 3,
              "Text adopts the supplied render scale without rasterizing its parent")
        let savedAppearance = HUDRuntimeAppearance.configuration
        let savedHistory = previewMonitor.history
        HUDRuntimeAppearance.configuration.accentHex = "6E8CFA"
        _ = preview!.makeContent(for: .activityMonitor, style: light)
        check(fixtureLine.strokeColor == ActivityGraphPalette.yellow.cgColor
              && named("activity.graph.0.latest.0", activityRoot).backgroundColor == ActivityGraphPalette.yellow.cgColor
              && (named("activity.graph.0.area.0", activityRoot) as! CAShapeLayer).fillColor == ActivityGraphPalette.yellow.withAlphaComponent(0.38).cgColor,
              "Activity lines, fills and markers retain their fixed yellow series color when the theme changes")
        check((named("activity.graph.1.line.0", activityRoot) as! CAShapeLayer).strokeColor == ActivityGraphPalette.blue.cgColor
              && (named("activity.graph.2.area.1", activityRoot) as! CAShapeLayer).fillColor == ActivityGraphPalette.blue.withAlphaComponent(0.38).cgColor
              && (named("activity.secondary.2", activityRoot) as! CATextLayer).foregroundColor == ActivityGraphPalette.blue.cgColor,
              "Memory and secondary data series retain the same fixed blue as their legends")
        check(named("activity.control.0", activityRoot).backgroundColor == HUDRuntimeAppearance.accent.cgColor,
              "Activity controls continue to use the chosen theme rather than the fixed data palette")
        check(previewMonitor.history == savedHistory && ids(activityRoot) == activityIDs,
              "Accent changes keep sampled history and retained graph layers intact")
        check(allLayers(activityRoot).filter { $0 is HUDControlHighlightLayer }.count == 7,
              "Both tabs and all five table headings provide animated hover and press feedback")
        HUDRuntimeAppearance.configuration = savedAppearance
        preview = nil
        check(!previewMonitor.isActive, "Destroying an active Activity canvas removes its sampling ownership")

        L10n.language = .english
        let samples = TelemetrySampleSequence()
        var tick: (() -> Void)?, timerCancellations = 0
        let liveMonitor = SystemActivityMonitor(sampler: samples.next, schedule: { action in
            tick = action; return { timerCancellations += 1; tick = nil }
        })
        var reduced = false
        let live = ActivityMonitorCanvas(controller: liveMonitor, reduceMotion: { reduced })
        _ = live.makeContent(for: .activityMonitor, style: dark)
        _ = NSApplication.shared
        let liveWindow = NSWindow(contentRect: live.layer.bounds, styleMask: .borderless, backing: .buffered, defer: false)
        liveWindow.isReleasedWhenClosed = false
        let liveHost = NSView(frame: live.layer.bounds); liveHost.wantsLayer = true
        liveWindow.contentView = liveHost
        liveHost.layer?.addSublayer(live.layer)
        defer { liveWindow.close() }
        let liveIDs = ids(live.layer)
        var notifications = 0, observedAnimations: [(String, CAAnimation)] = []
        // Detached test layers may be flushed by the run loop before the test
        // regains control. Inspect the submitted tracks in the snapshot callback.
        live.onChange = {
            notifications += 1
            observedAnimations = allLayers(live.layer).flatMap { item in
                (item.animationKeys() ?? []).compactMap { key in item.animation(forKey: key).map { (key, $0) } }
            }
        }
        live.activate()
        check(wait { liveMonitor.history.count == 1 }, "The canvas receives the worker's first completed sample")
        check(text("activity.value.0", live.layer) == "—" && text("activity.value.2", live.layer) == "↑ —",
              "Baseline-only counters display unavailable instead of fabricated zero rates")
        check((named("activity.graph.0.line.0", live.layer) as! CAShapeLayer).mask.map { elements(($0 as? CAShapeLayer)?.path) } == 0,
              "Unavailable history has no visible line segments")
        L10n.language = .simplifiedChinese
        _ = live.makeContent(for: .activityMonitor, style: dark)
        check(text("activity.footer", live.layer).isEmpty && text("activity.caption", live.layer).isEmpty && !live.accessibilityStatus.contains("Collecting"),
              "Sampling and baseline descriptions are removed from the visible report")
        L10n.language = .english
        tick?(); check(wait { liveMonitor.history.count == 2 }, "A visible one-second event publishes one additional sample")
        check(text("activity.value.0", live.layer) == "30.0%", "CPU uses the backend's actual interval result")
        check(!observedAnimations.isEmpty && observedAnimations.count <= 12, "A new snapshot starts only the bounded area and line path interpolations")
        for (key, animation) in observedAnimations {
            check(key.hasPrefix("telemetry."), "Telemetry animation keys stay separate from module and shell motion")
            check(animation.duration <= 0.34 && animation.repeatCount == 0, "Each graph animation finishes well before the next sample")
        }
        tick?(); check(wait { liveMonitor.history.count == 3 }, "Subsequent samples extend the same retained history")
        let line = named("activity.graph.0.line.0", live.layer) as! CAShapeLayer
        check(elements((line.mask as? CAShapeLayer)?.path) > 0, "Two adjacent valid readings reveal their connecting line")
        check(ids(live.layer) == liveIDs, "Sampling updates paths and values without rebuilding layers")
        tick?(); check(wait { liveMonitor.history.count == 4 }, "A failed synthetic reading is published as a real gap")
        check(text("activity.value.0", live.layer) == "—" && text("activity.value.1", live.layer) == "—",
              "Missing CPU and memory remain unavailable while old history is retained")
        check(named("activity.graph.0.latest.0", live.layer).isHidden, "A missing latest sample hides its graph marker")
        let coverage = (line.mask as! CAShapeLayer).path!
        check(!coverage.contains(CGPoint(x: 164, y: 18)), "The unavailable right edge is masked rather than joined to zero")
        reduced = true; live.updateRenderScale(2)
        check(live.animationCount == 0, "Turning on reduced motion immediately clears an in-flight path animation")
        tick?(); check(wait { liveMonitor.history.count == 5 }, "Reduced motion still updates current telemetry")
        check(live.animationCount == 0 && observedAnimations.isEmpty, "Reduced motion never adds new graph animations")
        reduced = false
        tick?(); check(wait { liveMonitor.history.count == 6 }, "Recovered counters establish valid rates again")
        check(text("activity.value.0", live.layer) != "—", "Unavailable intervals do not permanently suppress recovered measurements")
        live.deactivate()
        let hiddenNotifications = notifications, hiddenText = text("activity.value.0", live.layer)
        check(!liveMonitor.isActive && timerCancellations == 1 && live.animationCount == 0,
              "Hiding Activity removes its observer, timer ownership and every finite chart animation")
        let retained = liveMonitor.history
        check(liveMonitor.isRunning && liveMonitor.samplingInterval == 5, "Hidden charts leave a slow background sampler running")
        tick?()
        check(wait { liveMonitor.history.count == retained.count + 1 }, "Background sampling extends retained history")
        check(notifications == hiddenNotifications && text("activity.value.0", live.layer) == hiddenText,
              "A hidden canvas receives no redraw callbacks or text updates")
        liveMonitor.shutdown()

        let appModel = AppActivityMonitor.fixture()
        let appCanvas = ActivityMonitorCanvas(controller: .fixture(), apps: appModel, reduceMotion: { true })
        _ = appCanvas.makeContent(for: .activityMonitor, style: dark)
        appCanvas.activate(); appCanvas.perform(actionID: "activity:apps")
        check(appCanvas.isShowingApps && appModel.isActive, "The Apps tab activates the detailed reader")
        let columnIDs = ["name", "cpu", "memory", "network", "disk"].map { "activity:sort:" + $0 }
        let columnActions = Array(appCanvas.accessibleActions.dropFirst(2))
        check(columnActions.map(\.id) == columnIDs, "The five table column headings expose the five sorting actions")
        check(allLayers(appCanvas.layer).filter { ($0.name ?? "").hasPrefix("activity.control.")
            && !($0.name ?? "").hasPrefix("activity.control.label.") }.count == 2,
              "Only Overview and Apps retain button plates; sorting has no separate toolbar")
        for (index, action) in columnActions.enumerated() {
            let header = named("activity.apps.header.\(index)", appCanvas.layer)
            check(action.rect.contains(header.frame) && action.rect.maxY < ActivityMonitorCanvas.appsViewport.minY,
                  "Each header's visible text and hit area stay in the fixed table heading row")
        }
        check(text("activity.apps.header.1", appCanvas.layer) == "CPU ↓"
              && appModel.sortKey == .cpu && appModel.sortDescending,
              "Apps opens with a descending CPU indicator in the table heading")
        check(appCanvas.accessibleActions.first { $0.id == "activity:sort:cpu" }?.label == "Sort by CPU, descending",
              "Accessibility announces the selected column's current direction")
        check(!appCanvas.accessibilityStatus.isEmpty, "App metrics are available through accessibility")
        let appIDs = ids(appCanvas.layer)
        appCanvas.mouseDown(at: CGPoint(x: columnActions[1].rect.midX, y: columnActions[1].rect.midY))
        check(appModel.sortKey == .cpu && !appModel.sortDescending && text("activity.apps.header.1", appCanvas.layer) == "CPU ↑",
              "Clicking the active table heading reverses CPU sorting")
        appCanvas.mouseDown(at: CGPoint(x: columnActions[2].rect.midX, y: columnActions[2].rect.midY))
        check(appModel.sortKey == .memory && appModel.sortDescending && text("activity.apps.header.2", appCanvas.layer) == "RAM ↓"
              && text("activity.apps.header.1", appCanvas.layer) == "CPU", "A different metric starts descending and moves the one sort indicator")
        appCanvas.perform(actionID: "activity:sort:network")
        check(appModel.sortKey == .network && appModel.sortDescending && text("activity.apps.header.3", appCanvas.layer) == "Network ↓",
              "Network heading sorts total traffic while the rows retain separate upload and download readings")
        appCanvas.perform(actionID: "activity:sort:disk")
        check(appModel.sortKey == .disk && appModel.sortDescending && text("activity.apps.header.4", appCanvas.layer) == "Disk ↓",
              "Disk heading sorts total read and write activity")
        appCanvas.perform(actionID: "activity:sort:name")
        check(appModel.sortKey == .name && !appModel.sortDescending && text("activity.apps.header.0", appCanvas.layer) == "App ↑",
              "App name heading starts in ascending alphabetical order")
        L10n.language = .simplifiedChinese
        _ = appCanvas.makeContent(for: .activityMonitor, style: light)
        appCanvas.perform(actionID: "activity:sort:memory")
        check(text("activity.apps.header.2", appCanvas.layer) == "RAM ↓"
              && appCanvas.accessibleActions.first { $0.id == "activity:sort:memory" }?.label == "排序：RAM，降序",
              "RAM column titles stay consistent while selected arrows and accessible sorting directions localize")
        for language in [AppLanguage.english, .simplifiedChinese, .traditionalChinese, .japanese] {
            L10n.language = language
            _ = appCanvas.makeContent(for: .activityMonitor, style: light)
            check(text("activity.apps.header.2", appCanvas.layer) == "RAM ↓" && appCanvas.accessibilityStatus.contains("RAM "),
                  "Every supported language uses RAM in app headings and accessible metric descriptions")
        }
        L10n.language = .english
        _ = appCanvas.makeContent(for: .activityMonitor, style: dark)
        check(ids(appCanvas.layer) == appIDs, "Sorting reuses retained app row layers")
        check(appCanvas.scroll(at: CGPoint(x: 100, y: 150), delta: 20), "App rows accept continuous scrolling")
        check(appCanvas.cancelDetail() && !appModel.isActive, "Back to overview stops detailed process sampling")
        appCanvas.perform(actionID: "activity:apps"); appCanvas.deactivate()
        check(!appModel.isActive, "Closing the HUD releases detailed app sampling")

        var reduceSortMotion = false
        let sortedModel = AppActivityMonitor.fixture()
        let sortingCanvas = ActivityMonitorCanvas(controller: .fixture(), apps: sortedModel, reduceMotion: { reduceSortMotion })
        _ = sortingCanvas.makeContent(for: .activityMonitor, style: dark)
        sortingCanvas.activate(); sortingCanvas.perform(actionID: "activity:apps")
        sortingCanvas.perform(actionID: "activity:sort:memory")
        let sortedRows = named("activity.apps.rows", sortingCanvas.layer)
        let sortMove = sortedRows.animation(forKey: HUDSubsectionTransition.movementKey) as? CABasicAnimation
        check(sortedModel.sortKey == .memory && sortingCanvas.animationCount == 2 && sortMove != nil,
              "Clicking a metric heading reveals the newly sorted rows with exactly two finite tracks")
        check(((sortMove?.fromValue as? NSValue)?.caTransform3DValue.m41 ?? 0) > 0
              && allLayers(sortingCanvas.layer).filter { ($0.name ?? "").hasPrefix("activity.apps.header.") }.allSatisfy { ($0.animationKeys() ?? []).isEmpty },
              "Sort feedback approaches directionally while the table headings remain stationary")
        sortingCanvas.perform(actionID: "activity:sort:memory")
        let reverseSort = sortedRows.animation(forKey: HUDSubsectionTransition.movementKey) as! CABasicAnimation
        check(!sortedModel.sortDescending && (reverseSort.fromValue as! NSValue).caTransform3DValue.m41 < 0,
              "Reversing the same heading reverses the mechanical sort engagement")
        reduceSortMotion = true; sortingCanvas.updateRenderScale(2)
        check(sortingCanvas.animationCount == 0, "Reduce Motion stops in-flight sort feedback")
        sortingCanvas.perform(actionID: "activity:sort:cpu")
        check(sortedModel.sortKey == .cpu && sortingCanvas.animationCount == 0, "Reduced Motion preserves sorting without animated tracks")
        reduceSortMotion = false; sortingCanvas.perform(actionID: "activity:sort:disk"); sortingCanvas.deactivate()
        check(sortingCanvas.animationCount == 0 && !sortedModel.isActive, "Closing Activity removes sort tracks and stops detailed sampling")

        let storageModel = StorageController.fixture()
        var storage: StorageCanvas? = StorageCanvas(controller: storageModel, reduceMotion: { false })
        let storageRoot = storage!.makeContent(for: .storage, style: dark), storageIDs = ids(storage!.layer)
        _ = NSApplication.shared
        let storageWindow = NSWindow(contentRect: storageRoot.bounds, styleMask: .borderless, backing: .buffered, defer: false)
        storageWindow.isReleasedWhenClosed = false
        let storageHost = NSView(frame: storageRoot.bounds); storageHost.wantsLayer = true
        storageWindow.contentView = storageHost; storageHost.layer?.addSublayer(storageRoot)
        defer { storageWindow.close() }
        func finishStorageTurn(_ root: CALayer) {
            let arrow = named("storage.refresh.arrow", root)
            if let animation = arrow.animation(forKey: "telemetry.storage.refresh") {
                animation.delegate?.animationDidStop?(animation, finished: true)
            }
        }
        var openedSettings = 0
        storage!.onOpenSystemStorage = { openedSettings += 1 }
        check(storageRoot.bounds.size == CGSize(width: 400, height: 334), "Storage preserves the existing module frame")
        check(text("storage.metricValue.0", storageRoot) == "1.0 TB" && text("storage.metricValue.1", storageRoot) == "580 GB"
              && text("storage.metricValue.2", storageRoot) == "420 GB", "Storage shows one volume's total, used and available bytes")
        check(text("storage.meterCaption", storageRoot) == "58.0% used", "The compact meter reports the used fraction")
        check(storage!.accessibilityStatus.contains("Macintosh HD") && storage!.accessibilityStatus.contains("Available"),
              "Storage accessibility includes volume identity and the three capacity values")
        check(!storage!.accessibilityStatus.contains("Updated") && !allLayers(storageRoot).contains { $0.name == "storage.footer" },
              "Routine timestamps and footer descriptions are absent from artwork and accessibility")
        check(!allLayers(storageRoot).contains { ($0.name ?? "").contains("category") || $0.name == "storage.scope" },
              "The removed folder panel leaves no hidden category or scope layers")
        storage!.perform(actionID: "storage:settings")
        check(openedSettings == 0 && !storageModel.isActive, "A hidden Storage canvas cannot launch system settings")
        storage!.activate()
        check(storage!.accessibleActions.map(\.id) == ["storage:settings", "storage:refresh"],
              "Overview exposes Storage Settings and one accessible refresh icon")
        storage!.perform(actionID: "storage:details"); storage!.perform(actionID: "storage:back")
        check(openedSettings == 0 && !storage!.cancelDetail() && !storage!.scroll(at: CGPoint(x: 50, y: 150), delta: 30),
              "Obsolete detail actions, Escape and scrolling cannot reopen the removed panel")
        check(storage!.mouseDown(at: CGPoint(x: 35, y: 264)) && openedSettings == 1,
              "The former details position hands off once to macOS Storage Settings")
        check(storage!.animationCount == 0 && !storageModel.details.isLoading,
              "Opening settings never starts a local folder scan or reveal animation")
        check(!allLayers(storageRoot).compactMap { ($0 as? CATextLayer)?.string as? String }.contains { $0.contains("Refresh") },
              "The refresh control uses only an arrow while retaining a spoken action label")
        L10n.language = .simplifiedChinese
        _ = storage!.makeContent(for: .storage, style: light)
        check(text("storage.heading", storageRoot) == "存储" && text("storage.settings.title", storageRoot) == "存储设置"
              && storage!.accessibleActions.last?.label == "刷新存储", "Storage artwork and icon accessibility localize together")
        check(ids(storageRoot) == storageIDs, "Theme and language changes preserve the simplified retained layer tree")
        let fixtureArrow = named("storage.refresh.arrow", storageRoot) as! CAShapeLayer
        check(fixtureArrow.position == CGPoint(x: 18, y: 14.5) && fixtureArrow.anchorPoint == CGPoint(x: 0.5, y: 0.5),
              "The refresh glyph rotates around the fixed center of its button")
        check(fixtureArrow.path!.boundingBoxOfPath.insetBy(dx: -fixtureArrow.lineWidth, dy: -fixtureArrow.lineWidth)
                .intersection(fixtureArrow.bounds) == fixtureArrow.path!.boundingBoxOfPath.insetBy(dx: -fixtureArrow.lineWidth, dy: -fixtureArrow.lineWidth),
              "The circular stroke and tangential arrowhead stay inside their rotation layer")
        let arrowInk = fixtureArrow.path!.boundingBoxOfPath
        check(abs(arrowInk.midX - fixtureArrow.bounds.midX) < 0.01
              && abs(arrowInk.midY - fixtureArrow.bounds.midY) < 0.01
              && fixtureArrow.strokeColor == nil && fixtureArrow.fillColor != nil,
              "The filled clockwise glyph has balanced ink around its rotation center")
        let arrowRadius = hypot(arrowInk.width / 2, arrowInk.height / 2)
        check(arrowRadius < fixtureArrow.bounds.width / 2,
              "Every orientation of the refresh arrow stays inside its fixed square bounds")
        storage!.perform(actionID: "storage:refresh")
        let fastTurn = fixtureArrow.animation(forKey: "telemetry.storage.refresh") as! CABasicAnimation
        check(!storageModel.snapshot.isLoading && storage!.animationCount == 1 && fastTurn.repeatCount == 0
              && fastTurn.duration == 0.72, "Even an immediate successful query gets one finite visible turn")
        check((fastTurn.fromValue as? NSNumber)?.doubleValue == 0
              && abs(((fastTurn.toValue as? NSNumber)?.doubleValue ?? 0) - Double.pi * 2) < 0.000001,
              "One clockwise turn ends at the model's identical resting orientation")
        storage!.perform(actionID: "storage:refresh"); storage!.perform(actionID: "storage:refresh")
        check(storage!.animationCount == 1, "Rapid completed refreshes never stack rotation tracks")
        finishStorageTurn(storageRoot)
        check(storage!.animationCount == 1, "Rapid clicks coalesce into one additional full turn")
        fastTurn.delegate?.animationDidStop?(fastTurn, finished: true)
        check(storage!.animationCount == 1, "A duplicated old completion cannot stop a newer turn")
        finishStorageTurn(storageRoot)
        check(storage!.animationCount == 0 && CATransform3DIsIdentity(fixtureArrow.transform),
              "Completed queries settle at the full-turn boundary with no queued animation backlog")
        storage!.perform(actionID: "storage:refresh")
        check(wait { storage!.animationCount == 0 }, "Core Animation's real finite completion settles a fast refresh without a timer")
        storage = nil
        check(!storageModel.isActive && allLayers(storageRoot).allSatisfy { ($0.animationKeys() ?? []).isEmpty },
              "Storage destruction cancels sampling and all refresh animation")

        L10n.language = .english
        let driver = TelemetryStorageDriver()
        var capacityReads = 0, scans = 0, failCapacity = true, reducedStorageMotion = false
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let model = StorageController(clock: { date }, readCapacity: {
            capacityReads += 1
            return failCapacity ? nil : StorageCapacity(volumeName: "Test disk", totalBytes: 1000, availableBytes: 400, updatedAt: date)
        }, scanDetails: { _ in
            scans += 1; return StorageDetailsSnapshot(categories: [], isLoading: false, updatedAt: date, isPartial: false, error: nil)
        }, runWorker: { driver.jobs.append($0) }, scheduleTimer: { _, _ in driver.timer })
        let panel = StorageCanvas(controller: model, reduceMotion: { reducedStorageMotion })
        _ = panel.makeContent(for: .storage, style: dark)
        storageHost.layer?.addSublayer(panel.layer)
        var storageNotifications = 0; panel.onChange = { storageNotifications += 1 }
        panel.onOpenSystemStorage = { openedSettings += 1 }
        check(panel.animationCount == 0, "The refresh arrow remains still before activation")
        panel.activate()
        check(model.snapshot.isLoading && panel.accessibleActions.map(\.id) == ["storage:settings"],
              "An in-flight query keeps the settings action available and prevents duplicate refreshes")
        check(panel.animationCount == 1 && named("storage.refresh.arrow", panel.layer).animation(forKey: "telemetry.storage.refresh") != nil,
              "Only the arrow rotates while an actual query is pending")
        check(panel.accessibilityStatus == "Reading storage…", "Loading is announced rather than presenting zero capacity")
        panel.perform(actionID: "storage:refresh")
        check(driver.jobs.count == 1, "Clicking a busy refresh control cannot enqueue another query")
        reducedStorageMotion = true; panel.updateRenderScale(2)
        check(panel.animationCount == 0 && model.snapshot.isLoading, "Reduced motion stops rotation without cancelling the requested data")
        reducedStorageMotion = false; panel.updateRenderScale(2)
        check(panel.animationCount == 1, "Restoring motion resumes only the still-pending query's indicator")
        driver.finish()
        check(capacityReads == 1 && text("storage.metricValue.0", panel.layer) == "—"
              && named("storage.capacity.used", panel.layer).isHidden, "A failed capacity read leaves unavailable values and no used segment")
        check(panel.animationCount == 1 && panel.accessibilityStatus == "Storage capacity is unavailable.",
              "A failed query reports its error while its current turn finishes smoothly")
        finishStorageTurn(panel.layer)
        check(panel.animationCount == 0, "A failed query cannot start another turn after its current boundary")
        check(text("storage.caption", panel.layer) == "Storage capacity is unavailable.", "Failure feedback stays in the existing compact caption")
        failCapacity = false
        check(panel.mouseDown(at: CGPoint(x: 370, y: 266)) && model.snapshot.isLoading && driver.jobs.count == 1,
              "One click on the arrow submits one forced capacity refresh")
        let pendingTurn = named("storage.refresh.arrow", panel.layer).animation(forKey: "telemetry.storage.refresh")!
        let stableFace = named("storage.refresh.button", panel.layer).backgroundColor
        finishStorageTurn(panel.layer)
        check(panel.animationCount == 1 && model.snapshot.isLoading, "A slow query continues with one next finite turn")
        driver.finish()
        check(named("storage.refresh.button", panel.layer).opacity == 1
              && named("storage.refresh.button", panel.layer).backgroundColor == stableFace,
              "Query completion never flashes or dims the button face")
        check(panel.animationCount == 1, "Finishing a slow read leaves its last turn intact")
        finishStorageTurn(panel.layer)
        check(capacityReads == 2 && scans == 0 && panel.animationCount == 0
              && text("storage.caption", panel.layer) == "Test disk", "Successful refresh restores the volume label and never walks folders")
        panel.perform(actionID: "storage:settings"); panel.perform(actionID: "storage:details")
        check(openedSettings == 2 && scans == 0 && !model.details.isLoading,
              "The system settings callback replaces all former local folder-scanning behavior")
        reducedStorageMotion = true; panel.perform(actionID: "storage:refresh")
        check(model.snapshot.isLoading && panel.animationCount == 0, "A new query also stays still under reduced motion")
        driver.finish()
        reducedStorageMotion = false; panel.perform(actionID: "storage:refresh")
        check(panel.animationCount == 1, "A later refresh can animate normally after earlier queries complete")
        panel.deactivate()
        pendingTurn.delegate?.animationDidStop?(pendingTurn, finished: true)
        let savedNotifications = storageNotifications
        check(!model.isActive && driver.timer.invalidated && panel.animationCount == 0, "Hiding Storage immediately stops both sampling and rotation")
        driver.finish()
        check(storageNotifications == savedNotifications && panel.animationCount == 0,
              "Late query completion never redraws or restarts a hidden Storage canvas")
        panel.activate()
        check(panel.animationCount == 0, "Reentry after a completed hidden read does not revive an old turn")
        panel.perform(actionID: "storage:refresh")
        check(panel.animationCount == 1, "A new visible refresh starts a fresh animation generation")
        pendingTurn.delegate?.animationDidStop?(pendingTurn, finished: true)
        check(panel.animationCount == 1, "A stale completion from before hide cannot cancel or restart the new generation")
        panel.deactivate(); driver.finish()
        check(!panel.mouseDown(at: CGPoint(x: 370, y: 266)), "Hidden Storage input is rejected")
        panel.perform(actionID: "storage:settings")
        check(openedSettings == 2 && scans == 0, "Hidden or obsolete actions never open settings or enumerate folders")
        return count
    }
}

private final class TelemetrySampleSequence {
    private let lock = NSLock()
    private var index: UInt64 = 0
    func next() -> SystemActivityRawSample {
        lock.lock(); index += 1; let i = index; lock.unlock()
        let available = i != 4
        return SystemActivityRawSample(timestamp: Date(timeIntervalSince1970: Double(i)), uptime: Double(i),
            cpu: available ? SystemActivityCPUTicks(user: i * i * 10, system: 0, idle: i * 70, nice: 0) : nil,
            memory: available ? SystemActivityMemoryCounters(physicalBytes: 16_000_000_000, pageSize: 4096,
                internalPages: 1_000_000 + i * 100, purgeablePages: 20, wiredPages: 10_000, compressedPages: 2000) : nil,
            network: available ? ["en0": SystemActivityByteCounters(received: i * i * 2000, sent: i * i * 200)] : nil,
            disk: available ? ["disk0": SystemActivityByteCounters(received: i * i * 4000, sent: i * i * 800)] : nil)
    }
}
private final class TelemetryStorageTimer: StorageRefreshTimer {
    var invalidated = false
    func invalidate() { invalidated = true }
}
private final class TelemetryStorageDriver {
    var jobs: [() -> Void] = []
    let timer = TelemetryStorageTimer()
    func finish() { if !jobs.isEmpty { jobs.removeFirst()() } }
}
