import AppKit
import CoreAudio
import QuartzCore

// Actual-source oracle of the redesigned Clipboard, Event Log and Volume canvases (Mac 1.3.0
// Sources/ClipboardCanvas.swift, EventLogCanvas.swift, VolumeCanvas.swift with their real stores:
// ClipboardStore, SystemEventLog, AudioDeviceController(snapshot:) and PerAppAudioController.fixture()).
// Each canvas is built by the unchanged Mac code in scripted states (dark / Light, English / Simplified
// Chinese, recording mode, filters, scrolls, menus, empty states) and exported as its model layer tree with
// the design recorder (tools/hud_design_reference_support.swift), its page controls and accessibility items,
// and the scenario's INPUT (records, events, snapshots), so the Windows suites rebuild the same state.
// Also exported: EventLogText.line for every kind in five languages, EventLogCanvas.time across zones,
// the event store's metadata allowlist, and ClipboardCanvas meta / age / ByteCountFormatter samples.
// Isolation: temporary CFFIXED_USER_HOME; a private NSPasteboard with a unique name (never the general
// pasteboard); synthetic files in the temporary home; no window, no input posted (NSEvents are constructed,
// never sent), no audio device (AudioDeviceController(snapshot:), the fixture route factory), Reduce Motion
// on so every page installs at once and nothing animates.

private var reduce = true
private let env = HUDDesignEnvironment(renderScale: 2, reduceMotion: { reduce })

private func uuid(_ n: Int) -> UUID { UUID(uuidString: String(format: "00000000-0000-0000-0000-%012d", n))! }
private func seconds(_ date: Date) -> Double { date.timeIntervalSince1970 }
private func pageRow(_ canvas: HUDDesignCanvas, _ recorder: Recorder, state: String, language: AppLanguage, light: Bool,
                     input: [String: Any], extra: [String: Any] = [:]) throws -> [String: Any] {
    var row: [String: Any] = ["state": state, "language": languageName(language), "light": light,
        "layer": try recorder.tree(canvas.root, id: "root"),
        "controls": canvas.page.controls.map { controlState($0, in: canvas.root) },
        "accessibility": accessibility(canvas.accessibilityItems), "input": input,
        "header": ["counter": canvas.header.counterTextForTesting, "title": canvas.header.titleTextForTesting.0]]
    if canvas.isMenuOpen { row["top"] = try recorder.tree(canvas.top, id: "top") }
    for (k, v) in extra { row[k] = v }
    return row
}
private func png(width: Int, height: Int) -> Data {
    let rep = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: width, pixelsHigh: height, bitsPerSample: 8, samplesPerPixel: 4,
                               hasAlpha: true, isPlanar: false, colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
    for y in 0..<height { for x in 0..<width { rep.setColor(NSColor(srgbRed: CGFloat(x % 7) / 7, green: 0.6, blue: CGFloat(y % 5) / 5, alpha: 1), atX: x, y: y) } }
    return rep.representation(using: .png, properties: [:])!
}

@main private enum UtilityCanvasesReference {
    static func main() throws {
        let home = ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"]!
        precondition(CommandLine.arguments.count >= 2)
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        NSApplication.shared.setActivationPolicy(.prohibited)
        var configuration = AppConfiguration.defaults
        configuration.language = .english; configuration.theme = .dark; configuration.reduceMotion = true
        configuration.accentHex = "FAD41F"
        HUDRuntimeAppearance.configuration = configuration
        L10n.language = .english
        HUDColor.apply(accentHex: "FAD41F", light: false)
        let recorder = try Recorder(output: output)
        let scratch = URL(fileURLWithPath: home).appendingPathComponent("oracle", isDirectory: true)
        try FileManager.default.createDirectory(at: scratch, withIntermediateDirectories: true)
        var payload: [String: Any] = ["schema": 1]
        let style = HUDModuleContentStyle(dark: true, accent: .yellow, contentsScale: 2)
        func themed(_ light: Bool) { HUDColor.apply(accentHex: "FAD41F", light: light) }

        // MARK: Clipboard
        let board = NSPasteboard(name: NSPasteboard.Name("ehud-clipboard-oracle-\(UUID().uuidString)"))
        defer { board.releaseGlobally() }
        let store = ClipboardStore()
        var source: String? = nil
        store.sourceApplication = { _ in source }
        func capture(_ write: () -> Void, from app: String?) { board.clearContents(); write(); source = app; _ = store.capture(from: board) }
        let small = scratch.appendingPathComponent("Report.pdf"), large = scratch.appendingPathComponent("Archive.zip")
        FileManager.default.createFile(atPath: small.path, contents: Data(count: 1_234))
        FileManager.default.createFile(atPath: large.path, contents: nil)
        let handle = try FileHandle(forWritingTo: large); try handle.truncate(atOffset: 48_200_000); try handle.close()
        let folder = scratch.appendingPathComponent("Screenshots", isDirectory: true)
        try FileManager.default.createDirectory(at: folder, withIntermediateDirectories: true)
        capture({ board.writeObjects([folder as NSURL]) }, from: "Finder")
        capture({ board.writeObjects([large as NSURL]) }, from: "Finder")
        capture({ board.writeObjects([small as NSURL, large as NSURL]) }, from: nil)
        capture({ board.setData(png(width: 64, height: 40), forType: .png) }, from: "Preview")
        capture({ board.setString("https://example.com/docs/endfield?tab=2", forType: .string) }, from: nil)
        capture({ board.setString("mailto:operator@example.com", forType: .string) }, from: "Mail")
        capture({ board.setString("Rhodes Island field report: three bridges secured", forType: .string) }, from: "Notes")
        capture({ board.setString("管理员，今天的巡检记录已经同步。", forType: .string) }, from: "备忘录")
        capture({ board.setString("A short note", forType: .string) }, from: nil)
        _ = store.togglePin(id: store.items.first { $0.preview == "Rhodes Island field report: three bridges secured" }!.id)
        let captured = Date()
        let clipboardNow = captured.addingTimeInterval(3 * 3600 + 5)
        func clipboardInput() -> [String: Any] {
            ["now": seconds(clipboardNow), "capacity": store.capacity,
             "items": store.items.map { item -> [String: Any] in
                var row: [String: Any] = ["id": item.id.uuidString, "pinned": item.isPinned, "kind": item.kind.rawValue,
                    "preview": item.preview, "createdAt": seconds(item.createdAt), "thumbnail": item.thumbnail != nil]
                let d = item.details
                if let v = d.source { row["source"] = v }
                if let v = d.characters { row["characters"] = v }
                if let v = d.format { row["format"] = v }
                if let v = d.pixelWidth { row["pixelWidth"] = v }
                if let v = d.pixelHeight { row["pixelHeight"] = v }
                if let v = d.fileCount { row["fileCount"] = v }
                if let v = d.byteCount { row["byteCount"] = v }
                return row
             }]
        }
        let clipboardContext = HUDDesignContext(env: env)
        let clipboard = ClipboardCanvas(context: clipboardContext, store: store, now: { clipboardNow })
        clipboard.onCopy = { _ in true }
        var clipboardRows: [[String: Any]] = []
        func showClipboard() { _ = clipboard.makeContent(for: .clipboard, style: style) }
        showClipboard(); clipboard.setActive(true)
        clipboardRows.append(try pageRow(clipboard, recorder, state: "all", language: .english, light: false, input: clipboardInput()))
        let firstCard = clipboard.page.controls.first { $0.id == "clip:0" }!
        HUDControlHighlightLayer.update(in: clipboard.root, point: centre(firstCard, in: clipboard.root), pressed: false)
        clipboardRows.append(try pageRow(clipboard, recorder, state: "hoverCard", language: .english, light: false, input: clipboardInput()))
        HUDControlHighlightLayer.update(in: clipboard.root, point: nil, pressed: false)
        _ = clipboard.keyDown(key(125)); _ = clipboard.keyDown(key(125))
        clipboardRows.append(try pageRow(clipboard, recorder, state: "focusSecond", language: .english, light: false, input: clipboardInput(),
                                         extra: ["focused": clipboard.focusedCardIndexForTesting as Any? ?? NSNull()]))
        _ = clipboard.rightPointerDown(at: CGPoint(x: 300, y: 125 + 64 + 20))
        clipboardRows.append(try pageRow(clipboard, recorder, state: "contextMenu", language: .english, light: false, input: clipboardInput()))
        clipboard.closeMenu()
        _ = clipboard.scroll(at: CGPoint(x: 300, y: 300), delta: CGPoint(x: 0, y: 80))
        clipboardRows.append(try pageRow(clipboard, recorder, state: "scrolled", language: .english, light: false, input: clipboardInput()))
        _ = clipboard.scroll(at: CGPoint(x: 300, y: 300), delta: CGPoint(x: 0, y: -1000))
        for (filter, name) in [(ClipboardCanvas.Filter.text, "filterText"), (.link, "filterLink"), (.image, "filterImage"), (.file, "filterFile")] {
            clipboard.selectFilter(filter)
            clipboardRows.append(try pageRow(clipboard, recorder, state: name, language: .english, light: false, input: clipboardInput(),
                                             extra: ["filter": filter.rawValue]))
        }
        clipboard.selectFilter(.all)
        clipboardContext.setRecordingMode(true)
        clipboardRows.append(try pageRow(clipboard, recorder, state: "recording", language: .english, light: false, input: clipboardInput(),
                                         extra: ["recording": true]))
        clipboardContext.setRecordingMode(false)
        L10n.language = .simplifiedChinese; clipboard.setActive(false); showClipboard(); clipboard.setActive(true)
        clipboardRows.append(try pageRow(clipboard, recorder, state: "all", language: .simplifiedChinese, light: false, input: clipboardInput()))
        L10n.language = .english
        themed(true); clipboardContext.refreshPalette(); showClipboard()
        clipboardRows.append(try pageRow(clipboard, recorder, state: "all", language: .english, light: true, input: clipboardInput()))
        themed(false); clipboardContext.refreshPalette(); showClipboard()
        _ = store.clearUnpinned()
        clipboardRows.append(try pageRow(clipboard, recorder, state: "pinnedOnly", language: .english, light: false, input: clipboardInput()))
        clipboard.selectFilter(.image)
        clipboardRows.append(try pageRow(clipboard, recorder, state: "emptyFilter", language: .english, light: false, input: clipboardInput(),
                                         extra: ["filter": ClipboardCanvas.Filter.image.rawValue]))
        clipboard.setActive(false)
        payload["clipboard"] = clipboardRows
        // Meta line pieces the canvas formats with Foundation.
        let byteSamples: [Int64] = [0, 1, 2, 999, 1000, 1001, 1499, 1500, 2500, 12_345, 999_499, 999_500, 1_000_000, 1_049_999, 1_050_000,
                                    48_200_000, 999_949_999, 999_950_000, 1_000_000_000, 1_234_567_890, 12_345_678_901, 1_000_000_000_000, 5_555_555_555_555]
        var fileSizes: [[String: Any]] = []
        for bytes in byteSamples { fileSizes.append(["bytes": bytes, "text": ByteCountFormatter.string(fromByteCount: bytes, countStyle: .file)]) }
        var ages: [[String: Any]] = []
        let ageNow = Date(timeIntervalSinceReferenceDate: 800_000_000)
        for delta in [0, 59, 60, 119, 3599, 3600, 86_399, 86_400, 259_200, -30] {
            let date = ageNow.addingTimeInterval(-Double(delta))
            L10n.language = .english; let english = ClipboardCanvas.age(of: date, now: ageNow)
            L10n.language = .simplifiedChinese; let chinese = ClipboardCanvas.age(of: date, now: ageNow)
            L10n.language = .english
            ages.append(["seconds": delta, "en": english, "zh": chinese])
        }
        payload["clipboardFormat"] = ["fileSizes": fileSizes, "ages": ages]

        // MARK: Event Log
        let eventDirectory = scratch.appendingPathComponent("EventLog", isDirectory: true)
        try FileManager.default.createDirectory(at: eventDirectory, withIntermediateDirectories: true)
        let today = Date().addingTimeInterval(-90)
        let base = Date(timeIntervalSince1970: 1_760_000_000) // 2025-10-09 08:53:20 UTC
        let samples: [(SystemEventKind, [String: String])] = [
            (.workCompleted, ["kind": "countdown", "seconds": "1800"]), (.workStarted, ["kind": "stopwatch"]),
            (.workPaused, ["kind": "countdown", "seconds": "95"]), (.shelfAdded, ["filename": "Project.pdf"]), (.shelfCleared, ["count": "4"]),
            (.powerConnected, ["percentage": "63"]), (.batteryStateChanged, ["state": "charging", "percentage": "64"]),
            (.audioDeviceConnected, ["device": "Studio Headphones"]), (.displayDisconnected, [:]), (.clipboardCopied, ["kind": "url"]),
            (.displaySettingsChanged, ["field": "clockStyle", "value": "rail"]), (.displaySettingsChanged, ["field": "centerLogo", "value": "rhineLab"]),
            (.displaySettingsChanged, ["field": "alertMetric", "value": "gpu"]), (.playbackAction, ["action": "next", "source": "spotify"]),
            (.readerAction, ["action": "progress"]), (.calendarAction, ["action": "created"]), (.minigameAction, ["action": "finished"]),
            (.overlayOpened, [:]), (.moduleOpened, ["module": "notes"])]
        var events: [SystemEvent] = []
        for (n, sample) in samples.enumerated() {
            events.append(SystemEvent(id: uuid(n + 1), kind: sample.0, createdAt: n < 2 ? today.addingTimeInterval(-Double(n)) : base.addingTimeInterval(-Double(n) * 3700),
                                      metadata: sample.1))
        }
        let document = "{\"version\":1,\"events\":" + String(data: try JSONEncoder().encode(events), encoding: .utf8)! + "}"
        try document.write(to: eventDirectory.appendingPathComponent("events.json"), atomically: true, encoding: .utf8)
        let log = SystemEventLog(directory: eventDirectory)
        let eventContext = HUDDesignContext(env: env)
        let eventLog = EventLogCanvas(context: eventContext, store: log)
        func eventInput() -> [String: Any] {
            ["now": seconds(Date()), "events": log.events.map { ["id": $0.id.uuidString, "kind": $0.kind.rawValue, "createdAt": seconds($0.createdAt), "metadata": $0.metadata] }]
        }
        var eventRows: [[String: Any]] = []
        func showEvents() { _ = eventLog.makeContent(for: .eventLog, style: style) }
        showEvents(); eventLog.setActive(true)
        eventRows.append(try pageRow(eventLog, recorder, state: "all", language: .english, light: false, input: eventInput(),
                                     extra: ["rows": eventLog.rowCountForTesting, "offset": eventLog.listOffsetForTesting]))
        let firstRow = eventLog.page.controls.first { $0.id.hasPrefix("eventLog.row:") }!
        HUDControlHighlightLayer.update(in: eventLog.root, point: centre(firstRow, in: eventLog.root), pressed: false)
        eventRows.append(try pageRow(eventLog, recorder, state: "hoverRow", language: .english, light: false, input: eventInput()))
        HUDControlHighlightLayer.update(in: eventLog.root, point: nil, pressed: false)
        eventLog.scrollListForTesting(100)
        eventRows.append(try pageRow(eventLog, recorder, state: "scrolled", language: .english, light: false, input: eventInput(),
                                     extra: ["rows": eventLog.rowCountForTesting, "offset": eventLog.listOffsetForTesting]))
        _ = eventLog.keyDown(key(119))
        eventRows.append(try pageRow(eventLog, recorder, state: "end", language: .english, light: false, input: eventInput(),
                                     extra: ["rows": eventLog.rowCountForTesting, "offset": eventLog.listOffsetForTesting]))
        _ = eventLog.keyDown(key(115))
        for (tab, name) in [(1, "filterTimer"), (3, "filterDevices"), (2, "filterShelf")] {
            (eventLog.page.tabs!.tabs[tab]).activate()
            eventRows.append(try pageRow(eventLog, recorder, state: name, language: .english, light: false, input: eventInput(),
                                         extra: ["filter": tab, "rows": eventLog.rowCountForTesting]))
        }
        eventLog.page.tabs!.tabs[0].activate()
        L10n.language = .simplifiedChinese; eventLog.setActive(false); showEvents(); eventLog.setActive(true)
        eventRows.append(try pageRow(eventLog, recorder, state: "all", language: .simplifiedChinese, light: false, input: eventInput()))
        L10n.language = .english
        themed(true); eventContext.refreshPalette(); showEvents()
        eventRows.append(try pageRow(eventLog, recorder, state: "all", language: .english, light: true, input: eventInput()))
        themed(false); eventContext.refreshPalette(); showEvents()
        log.clear(); eventLog.setActive(false); showEvents(); eventLog.setActive(true)
        eventRows.append(try pageRow(eventLog, recorder, state: "empty", language: .english, light: false, input: eventInput()))
        L10n.language = .simplifiedChinese; eventLog.setActive(false); showEvents(); eventLog.setActive(true)
        eventRows.append(try pageRow(eventLog, recorder, state: "empty", language: .simplifiedChinese, light: false, input: eventInput()))
        L10n.language = .english; eventLog.setActive(false)
        payload["eventLog"] = eventRows
        // EventLogText.line: every kind and metadata branch, five languages.
        var lineSamples: [(SystemEventKind, [String: String])] = [
            (.overlayOpened, [:]), (.moduleOpened, [:]), (.appShortcutOpened, ["app": "Calculator"]), (.appShortcutOpened, [:]),
            (.clipboardCopied, ["kind": "text"]), (.clipboardCopied, ["kind": "url"]), (.clipboardCopied, ["kind": "image"]), (.clipboardCopied, ["kind": "files"]),
            (.clipboardCopied, [:]), (.shelfAdded, ["filename": "Plan.key"]), (.shelfAdded, [:]), (.shelfRemoved, ["filename": "Plan.key"]),
            (.shelfRemoved, [:]), (.shelfCleared, ["count": "3"]), (.shelfCleared, [:]),
            (.workStarted, ["kind": "stopwatch"]), (.workStarted, ["kind": "countdown", "seconds": "1800"]), (.workPaused, ["kind": "stopwatch"]),
            (.workPaused, ["kind": "countdown", "seconds": "95"]), (.workResumed, ["kind": "stopwatch"]), (.workResumed, ["kind": "countdown", "seconds": "3600"]),
            (.workReset, ["kind": "stopwatch"]), (.workReset, ["kind": "countdown"]), (.workCompleted, ["kind": "countdown", "seconds": "86400"]),
            (.workCompleted, ["kind": "stopwatch", "seconds": "60"]), (.powerConnected, ["percentage": "80"]), (.powerConnected, [:]),
            (.powerDisconnected, ["percentage": "79"]), (.batteryStateChanged, ["state": "charging", "percentage": "50"]),
            (.batteryStateChanged, ["state": "full"]), (.batteryStateChanged, ["state": "connected", "percentage": "100"]),
            (.batteryStateChanged, ["state": "battery", "percentage": "99"]), (.batteryStateChanged, ["state": "noBattery"]),
            (.batteryStateChanged, ["state": "unavailable"]), (.batteryStateChanged, ["state": "mystery", "percentage": "1"]),
            (.audioDeviceConnected, ["device": "AirPods"]), (.audioDeviceConnected, [:]), (.displayConnected, ["device": "LG UltraFine"]), (.displayConnected, [:]),
            (.audioDeviceDisconnected, ["device": "AirPods"]), (.audioDeviceDisconnected, [:]), (.displayDisconnected, ["device": "Dell"]), (.displayDisconnected, [:]),
            (.displaySettingsChanged, ["field": "clockStyle", "value": "digital"]), (.displaySettingsChanged, ["field": "clockStyle", "value": "stacked"]),
            (.displaySettingsChanged, ["field": "centerLogo", "value": "customImported"]), (.displaySettingsChanged, ["field": "centerLogo", "value": "endfield"]),
            (.displaySettingsChanged, ["field": "centerLogo", "value": "rhodesIsland"]), (.displaySettingsChanged, ["field": "centerLogo", "value": "babel"]),
            (.displaySettingsChanged, ["field": "centerLogo", "value": "custom"]), (.displaySettingsChanged, ["field": "alertMetric", "value": "battery"]),
            (.displaySettingsChanged, ["field": "alertMetric", "value": "ram"]), (.displaySettingsChanged, ["field": "alertMetric", "value": "cpu"]),
            (.displaySettingsChanged, ["field": "alertMetric", "value": "network"]), (.displaySettingsChanged, ["field": "alertMetric", "value": "disk"]),
            (.displaySettingsChanged, ["field": "alertMetric", "value": "gpu"]), (.displaySettingsChanged, [:]),
            (.profileCropChanged, ["target": "background"]), (.profileCropChanged, ["target": "thumbnail"]), (.profileCropChanged, ["target": "both"]),
            (.profileCropChanged, [:]), (.mapPinStyleChanged, ["style": "green"]), (.mapRecentered, [:])]
        lineSamples += ["createdText", "createdTODO", "createdMedia", "createdDrawing", "editedText", "formattedText", "editedTODO", "drawingEdited", "deletedNote", "mediaPlayback", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.noteAction, ["action": action]) }
        lineSamples += ["playPause", "previous", "next", "seek", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.playbackAction, ["action": action, "source": "music"]) }
        lineSamples += ["music", "spotify", "netease", "qqMusic", "kugou", "system"].map { (action: String) -> (SystemEventKind, [String: String]) in (.playbackAction, ["action": "next", "source": action]) }
        lineSamples.append((.playbackAction, ["action": "seek"]))
        lineSamples += ["drawingEdited", "erased", "brushChanged", "backgroundChanged", "mediaAdded", "mediaRemoved", "cleared", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.projectionAction, ["action": action]) }
        lineSamples += ["created", "edited", "deleted", "mediaAdded", "mediaRemoved", "categoryCreated", "categoryChanged", "categoryDeleted", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.archiveAction, ["action": action]) }
        lineSamples += ["imported", "deleted", "bookmarked", "progress", "settings", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.readerAction, ["action": action]) }
        lineSamples += ["imported", "edited", "exported", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.mediaAssemblyAction, ["action": action]) }
        lineSamples += ["created", "edited", "deleted", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.calendarAction, ["action": action]) }
        lineSamples += ["started", "restarted", "finished", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.minigameAction, ["action": action]) }
        lineSamples += ["linked", "unlinked", "synced", "settings", "other"].map { (action: String) -> (SystemEventKind, [String: String]) in (.accountAction, ["action": action]) }
        let languages: [AppLanguage] = [.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean]
        payload["eventLogLines"] = lineSamples.map { sample -> [String: Any] in
            let event = SystemEvent(id: uuid(9000), kind: sample.0, createdAt: base, metadata: sample.1)
            var row: [String: Any] = ["kind": sample.0.rawValue, "metadata": sample.1, "filter": EventLogFilter.of(sample.0).rawValue]
            for language in languages { L10n.language = language; row[languageName(language)] = EventLogText.line(event) }
            L10n.language = .english
            return row
        }
        // EventLogCanvas.time in zones (the Windows civil conversion), and the store's metadata allowlist.
        var times: [[String: Any]] = []
        // IANA zone -> Windows registry key (the Windows suite resolves it through EnumDynamicTimeZoneInformation).
        let zones: [(String, String)] = [("UTC", "UTC"), ("America/Los_Angeles", "Pacific Standard Time"), ("Europe/Berlin", "W. Europe Standard Time"),
            ("Europe/London", "GMT Standard Time"), ("Asia/Shanghai", "China Standard Time"), ("Asia/Kolkata", "India Standard Time"),
            ("America/St_Johns", "Newfoundland Standard Time"), ("Australia/Lord_Howe", "Lord Howe Standard Time"), ("Pacific/Chatham", "Chatham Islands Standard Time")]
        // Fixed instants (epoch, pre-2001, a leap day, year ends) and the sub-millisecond instants around a minute
        // boundary that pin how the calendar rounds Foundation's seconds.
        let minute = (floor((812_000_000 + 978_307_200) / 60) + 1) * 60 - 978_307_200
        let fixed: [Double] = [0, 812_000_000, 812_000_000.999, -978_307_200, -1.5, 780_000_000, 783_000_000.5, 799_999_999.9995,
            minute - 0.0006, minute - 0.0005, minute - 0.0004, minute, minute - 60.0005,
            Date(timeIntervalSince1970: 1_835_395_199.5).timeIntervalSinceReferenceDate, Date(timeIntervalSince1970: 1_835_481_600).timeIntervalSinceReferenceDate,
            Date(timeIntervalSince1970: 1_798_761_599).timeIntervalSinceReferenceDate, Date(timeIntervalSince1970: 1_798_761_600).timeIntervalSinceReferenceDate]
        let window = (Date(timeIntervalSince1970: 1_735_689_600), Date(timeIntervalSince1970: 1_830_297_600)) // 2025-01-01 ... 2028-01-01
        for (identifier, windowsKey) in zones {
            let zone = TimeZone(identifier: identifier)!
            var calendar = Calendar(identifier: .gregorian); calendar.timeZone = zone
            var instants = fixed
            // Every daylight-saving transition in 2025...2027 with the seconds and half hours around it.
            var cursor = window.0
            while let next = zone.nextDaylightSavingTimeTransition(after: cursor), next < window.1 {
                for delta in [-1800.0, -1, -0.001, 0, 0.5, 1, 1800] { instants.append(next.timeIntervalSinceReferenceDate + delta) }
                cursor = next
            }
            for instant in instants {
                let date = Date(timeIntervalSinceReferenceDate: instant), now = Date(timeIntervalSinceReferenceDate: 812_000_000)
                let value = EventLogCanvas.time(date, now: now, calendar: calendar)
                times.append(["zone": identifier, "windowsZone": windowsKey, "reference": instant, "now": 812_000_000.0,
                              "clock": value.clock, "day": value.day as Any? ?? NSNull()])
            }
        }
        payload["eventLogTimes"] = times
        let privacyStore = SystemEventLog()
        let raws: [(SystemEventKind, [String: String])] = [
            (.clipboardCopied, ["kind": "text", "text": "PRIVATE_BODY", "path": "/private/a"]), (.shelfAdded, ["filename": "C:\\private\\Project.pdf", "url": "PRIVATE_URL"]),
            (.appShortcutOpened, ["app": "  Example\n\tTool  "]), (.audioDeviceConnected, ["device": "扬声器"]), (.appShortcutOpened, ["app": "file:///private/secret"]),
            (.shelfCleared, ["count": "+000004"]), (.workStarted, ["kind": "countdown", "seconds": "00001800"]), (.batteryStateChanged, ["percentage": "101", "state": "charging"]),
            (.displaySettingsChanged, ["field": "centerLogo", "value": "customImported", "path": "/private/art.png"]),
            (.displaySettingsChanged, ["field": "alertMetric", "value": "gpu"]), (.profileCropChanged, ["target": "both", "zoom": "9.5"]),
            (.noteAction, ["action": "editedText", "body": "PRIVATE_BODY"]), (.playbackAction, ["action": "next", "source": "system", "title": "PRIVATE_TITLE"]),
            (.accountAction, ["action": "linked", "token": "PRIVATE_TOKEN", "uid": "PRIVATE_UID"]), (.readerAction, ["action": "progress", "page": "12"])]
        payload["eventLogMetadata"] = raws.map { kind, raw -> [String: Any] in
            privacyStore.record(kind: kind, metadata: raw)
            let event = privacyStore.events[0]
            return ["kind": kind.rawValue, "raw": raw, "metadata": event.metadata, "detail": event.detail]
        }

        // MARK: Volume
        let builtIn = AudioDeviceInfo(id: 41, name: "MacBook Pro Speakers", outputChannels: 2, transportType: kAudioDeviceTransportTypeBuiltIn)
        let headphones = AudioDeviceInfo(id: 42, name: "Studio Headphones", outputChannels: 2, isHeadphones: true, canBeDefaultOutput: false,
                                         transportType: kAudioDeviceTransportTypeUSB)
        let microphone = AudioDeviceInfo(id: 51, name: "MacBook Pro Microphone", inputChannels: 1, transportType: kAudioDeviceTransportTypeBuiltIn)
        var apps: [AudioApplicationInfo] = []
        for n in 1...5 {
            let name: String = n == 5 ? "PID 4105" : "Player \(n)"
            apps.append(AudioApplicationInfo(id: UInt32(900 + n), pid: Int32(4100 + n), name: name, outputDeviceIDs: [41]))
        }
        var snapshot = AudioDeviceSnapshot(outputs: [builtIn, headphones], inputs: [microphone], defaultOutputID: 41, defaultInputID: 51,
            outputVolume: 0.55, canSetOutputVolume: true, outputMuted: false, canSetOutputMute: true, balance: -0.2, canSetBalance: true,
            canSetDefaultOutput: true, canSetDefaultInput: true, activeApplications: apps, availableApplications: apps,
            applicationActivitySupported: true, applicationActivityMessage: nil)
        var controller = AudioDeviceController(snapshot: snapshot)
        let routes = PerAppAudioController.fixture()
        let volumeContext = HUDDesignContext(env: env)
        var volume = VolumeCanvas(context: volumeContext, controller: controller, perAppAudio: routes)
        func volumeInput() -> [String: Any] {
            let s = controller.snapshot
            func device(_ d: AudioDeviceInfo) -> [String: Any] { ["id": String(d.id), "name": d.name, "canBeDefaultOutput": d.canBeDefaultOutput, "canBeDefaultInput": d.canBeDefaultInput] }
            var row: [String: Any] = [:]
            row["outputs"] = s.outputs.map(device); row["inputs"] = s.inputs.map(device)
            row["outputID"] = s.defaultOutputID.map { String($0) } ?? ""; row["inputID"] = s.defaultInputID.map { String($0) } ?? ""
            row["volume"] = s.outputVolume as Any? ?? NSNull(); row["muted"] = s.outputMuted as Any? ?? NSNull(); row["balance"] = s.balance as Any? ?? NSNull()
            row["canSetVolume"] = s.canSetOutputVolume; row["canSetMute"] = s.canSetOutputMute; row["canSetBalance"] = s.canSetBalance
            row["canSetDefaultOutput"] = s.canSetDefaultOutput; row["canSetDefaultInput"] = s.canSetDefaultInput
            row["supported"] = s.applicationActivitySupported; row["applicationMessage"] = s.applicationActivityMessage as Any? ?? NSNull()
            row["perAppStatus"] = routes.statusMessage as Any? ?? NSNull(); row["status"] = controller.statusMessage as Any? ?? NSNull()
            var listed: [[String: Any]] = []
            for app in volume.listedApplications {
                let session = routes.sessions.first { $0.processID == app.id }
                var item: [String: Any] = ["id": String(app.id), "name": app.name, "pid": Int(app.pid), "available": session == nil]
                item["state"] = session?.state.rawValue ?? "direct"; item["gain"] = session?.gain as Any? ?? NSNull()
                listed.append(item)
            }
            row["applications"] = listed
            return row
        }
        var volumeRows: [[String: Any]] = []
        func showVolume() { _ = volume.makeContent(for: HUDModule.volume, style: style) }
        showVolume(); volume.setActive(true)
        volumeRows.append(try pageRow(volume, recorder, state: "devices", language: .english, light: false, input: volumeInput(),
                                      extra: ["news": volume.hasAdjustableApplication]))
        let outputField = volume.page.controls.first { $0.id == "output" }!
        _ = volume.pointerDown(at: centre(outputField, in: volume.root)); volume.pointerUp(at: centre(outputField, in: volume.root))
        volumeRows.append(try pageRow(volume, recorder, state: "outputMenu", language: .english, light: false, input: volumeInput()))
        volume.closeMenu()
        let volumeSlider = volume.page.controls.first { $0.id == "volume" }!
        HUDControlHighlightLayer.update(in: volume.root, point: centre(volumeSlider, in: volume.root), pressed: false)
        volumeRows.append(try pageRow(volume, recorder, state: "hoverSlider", language: .english, light: false, input: volumeInput()))
        HUDControlHighlightLayer.update(in: volume.root, point: nil, pressed: false)
        // Muted and read-only devices: a new controller snapshot (the canvas observes the controller it was made with).
        volume.setActive(false)
        snapshot.outputMuted = true; snapshot.canSetBalance = false; snapshot.balance = nil; snapshot.canSetDefaultOutput = false
        controller = AudioDeviceController(snapshot: snapshot)
        volume = VolumeCanvas(context: volumeContext, controller: controller, perAppAudio: routes)
        showVolume(); volume.setActive(true)
        volumeRows.append(try pageRow(volume, recorder, state: "mutedReadOnly", language: .english, light: false, input: volumeInput()))
        volume.page.tabs!.tabs[1].activate()
        volumeRows.append(try pageRow(volume, recorder, state: "apps", language: .english, light: false, input: volumeInput(),
                                      extra: ["offset": volume.applicationScrollOffset]))
        _ = volume.scroll(at: CGPoint(x: 300, y: 200), delta: CGPoint(x: 0, y: 70))
        volumeRows.append(try pageRow(volume, recorder, state: "appsScrolled", language: .english, light: false, input: volumeInput(),
                                      extra: ["offset": volume.applicationScrollOffset]))
        _ = volume.setGain(0.4, for: apps[0]); _ = volume.setGain(1, for: apps[1])
        volumeRows.append(try pageRow(volume, recorder, state: "appsRouted", language: .english, light: false, input: volumeInput(),
                                      extra: ["offset": volume.applicationScrollOffset, "rows": volume.visibleApplicationRows]))
        L10n.language = .simplifiedChinese; volume.setActive(false); showVolume(); volume.setActive(true)
        volumeRows.append(try pageRow(volume, recorder, state: "appsRouted", language: .simplifiedChinese, light: false, input: volumeInput()))
        L10n.language = .english
        themed(true); volumeContext.refreshPalette(); showVolume()
        volumeRows.append(try pageRow(volume, recorder, state: "appsRouted", language: .english, light: true, input: volumeInput()))
        themed(false); volumeContext.refreshPalette(); showVolume()
        volume.setActive(false)
        snapshot.applicationActivitySupported = false; snapshot.applicationActivityMessage = "Audio app detection is unavailable."
        controller = AudioDeviceController(snapshot: snapshot)
        volume = VolumeCanvas(context: volumeContext, controller: controller, perAppAudio: PerAppAudioController.fixture())
        showVolume(); volume.setActive(true); volume.page.tabs!.tabs[1].activate()
        volumeRows.append(try pageRow(volume, recorder, state: "appsUnsupported", language: .english, light: false, input: volumeInput()))
        volume.setActive(false)
        payload["volume"] = volumeRows

        payload["text"] = ["widths": Array(recorder.widths.values), "metrics": Array(recorder.metrics.values)]
        payload["layerDefaults"] = recorder.layerDefaults
        let data = try JSONSerialization.data(withJSONObject: sanitize(payload), options: [.sortedKeys])
        try data.write(to: output.appendingPathComponent("utility-canvases-source.json"))
        print("Exported \(clipboardRows.count) Clipboard, \(eventRows.count) Event Log and \(volumeRows.count) Volume scenes")
    }
}
