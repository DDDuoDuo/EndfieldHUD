import AppKit
import AVFoundation

/// Session-only edits: one retained controller per HUD, no persistent media copy,
/// no watcher, no idle timer, and one latest pending preview behind one worker.
final class MediaAssemblyController {
    private(set) var document: MediaAssemblyDocument?
    private(set) var adjustments = MediaAssemblyAdjustments()
    private(set) var preview: CGImage?
    private(set) var player: AVPlayer?
    private(set) var isPlaying = false
    private(set) var isCropPreview = false
    private(set) var currentTime = 0.0
    private(set) var isBusy = false
    private(set) var isExporting = false
    private(set) var progress = 0.0
    private(set) var error: Error?
    private(set) var lastExport: URL?
    private(set) var completedPreviewCount = 0
    var onChange: (() -> Void)?
    var onEvent: ((String) -> Void)?
    private let engine = MediaAssemblyEngine()
    private let queue = DispatchQueue(label: "EndfieldHUD.MediaAssembly", qos: .utility)
    private var active = false
    private var importGeneration = 0, previewGeneration = 0, playerGeneration = 0
    private var working = false, preparingPlayer = false, wantsPlayback = false
    private struct PreviewRequest { let document: MediaAssemblyDocument; let adjustments: MediaAssemblyAdjustments; let time: Double; let generation: Int }
    private var pending: PreviewRequest?
    private var playerAccess: NotesMediaAccess?
    private var timeObserver: Any?, endObserver: NSObjectProtocol?
    private var exportTicket: MediaAssemblyExportTicket?
    private var exportProgressTimer: Timer?
    private var editedEventWork: DispatchWorkItem?
    var previewPixelCount: Int { (preview?.width ?? 0) * (preview?.height ?? 0) }
    var pendingPreviewCount: Int { pending == nil ? 0 : 1 }
    var hasActivePlaybackObserver: Bool { timeObserver != nil || endObserver != nil }
    deinit { editedEventWork?.cancel(); exportProgressTimer?.invalidate(); exportTicket?.cancel(); releasePlayer() }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value { requestPreview() }
        else {
            flushEditedEvent()
            wantsPlayback = false; releasePlayer(); pending = nil; previewGeneration += 1; preview = nil
            engine.clearCaches()
        }
        onChange?()
    }
    /// Discard this editor session only. In-flight decoding may finish on its
    /// serial worker, but generation checks prevent it from restoring the source.
    /// An explicitly requested export must finish/cancel before the source closes.
    @discardableResult func closeDocument() -> Bool {
        guard !isExporting else { return false }
        flushEditedEvent(); importGeneration += 1; previewGeneration += 1
        wantsPlayback = false; releasePlayer(); pending = nil
        document = nil; adjustments = MediaAssemblyAdjustments(); preview = nil
        currentTime = 0; isBusy = false; isCropPreview = false
        error = nil; lastExport = nil; progress = 0
        engine.clearCaches()
        // A queued old decode can start after the immediate purge. Clearing
        // again behind it releases that last bounded source without a poller.
        let engine = self.engine; queue.async { engine.clearCaches() }
        onChange?(); return true
    }
    func importURL(_ url: URL, release: (() -> Void)? = nil, recordEvent: Bool = true) {
        guard !isExporting else { release?(); return }
        flushEditedEvent()
        importGeneration += 1; let token = importGeneration
        wantsPlayback = false; releasePlayer(); isBusy = true; error = nil; onChange?()
        let scoped = url.startAccessingSecurityScopedResource(), engine = self.engine
        queue.async { [weak self] in
            let result: Result<MediaAssemblyDocument, Error>
            do { result = .success(try engine.open(url)) } catch { result = .failure(error) }
            if scoped { url.stopAccessingSecurityScopedResource() }; release?()
            DispatchQueue.main.async { [weak self] in
                guard let self, self.importGeneration == token else { return }; self.isBusy = false
                switch result {
                case .success(let document):
                    self.document = document; self.adjustments = MediaAssemblyAdjustments(); self.currentTime = 0
                    self.preview = nil; if recordEvent { self.lastExport = nil; self.onEvent?("imported") }; self.requestPreview()
                case .failure(let failure): self.error = failure
                }
                self.onChange?()
            }
        }
    }
    func updateAdjustments(_ value: MediaAssemblyAdjustments) { applyAdjustments(value, scrubTo: nil) }
    /// Editing crop only changes retained handles until leaving the crop tool.
    /// This display override never enters saved adjustments or export parameters.
    func setCropPreview(_ value: Bool) {
        guard value != isCropPreview else { return }; isCropPreview = value
        if value { wantsPlayback = false; releasePlayer() }
        requestPreview(); onChange?()
    }
    func updateTrim(start: Double, end: Double, scrubTo time: Double) {
        guard time.isFinite else { return }
        var value = adjustments; value.trimStart = start; value.trimEnd = end
        wantsPlayback = false
        if isPlaying { releasePlayer() }
        applyAdjustments(value, scrubTo: time)
    }
    private func applyAdjustments(_ value: MediaAssemblyAdjustments, scrubTo time: Double?) {
        guard !isExporting, let document else { return }
        guard value.isValid, !document.isVideo || ((try? value.timeRange(duration: document.duration)) != nil && value.stickers.isEmpty) else {
            error = MediaAssemblyError.invalidAdjustment; onChange?(); return
        }
        guard value != adjustments else { if let time { seek(to: time) }; return }
        var oldBase = adjustments, newBase = value
        oldBase.stickers = []; newBase.stickers = []
        if isCropPreview { oldBase.crop = MediaAssemblyCrop(); newBase.crop = MediaAssemblyCrop() }
        let needsRender = oldBase != newBase
        adjustments = value; error = nil
        if document.isVideo { currentTime = min(max(time ?? currentTime, value.trimStart), max(value.trimStart, (value.trimEnd ?? document.duration) - 0.001)); releasePlayer() }
        editedEventWork?.cancel()
        let record = DispatchWorkItem { [weak self] in self?.flushEditedEvent() }
        editedEventWork = record; DispatchQueue.main.asyncAfter(deadline: .now() + 0.4, execute: record)
        if needsRender || time != nil { requestPreview() }; onChange?()
    }
    func updateAdjustments(_ mutation: (inout MediaAssemblyAdjustments) -> Void) { var value = adjustments; mutation(&value); updateAdjustments(value) }
    func reset() { updateAdjustments(MediaAssemblyAdjustments()) }
    func seek(to time: Double) {
        guard let document, document.isVideo, time.isFinite else { return }
        currentTime = min(max(adjustments.trimStart, time), max(adjustments.trimStart, (adjustments.trimEnd ?? document.duration) - 0.001))
        if let player {
            player.seek(to: CMTime(seconds: currentTime, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero)
        }
        if !isPlaying { requestPreview() }; onChange?()
    }
    func togglePlayback() {
        guard active, document?.isVideo == true, !isExporting else { return }
        wantsPlayback.toggle()
        if wantsPlayback { if let player { beginPlayback(player) } else { preparePlayer() } }
        else { player?.pause(); isPlaying = false; removeTimeObserver(); requestPreview(); onChange?() }
    }
    private func requestPreview() {
        guard active, let document else { return }
        previewGeneration += 1
        var display = adjustments; if isCropPreview { display.crop = MediaAssemblyCrop() }
        pending = PreviewRequest(document: document, adjustments: display, time: currentTime, generation: previewGeneration)
        pumpPreview()
    }
    private func pumpPreview() {
        guard !working, let request = pending else { return }
        pending = nil; working = true
        let engine = self.engine
        queue.async { [weak self] in
            let result: Result<CGImage, Error>
            do { result = .success(try engine.preview(request.document, adjustments: request.adjustments, time: request.time, includeStickers: false)) }
            catch { result = .failure(error) }
            DispatchQueue.main.async { [weak self] in
                guard let self else { return }; self.working = false; self.completedPreviewCount += 1
                if self.active, self.document?.id == request.document.id, self.previewGeneration == request.generation {
                    switch result { case .success(let image): self.preview = image; case .failure(let failure): self.error = failure }
                    self.onChange?()
                    if self.wantsPlayback && self.player == nil { self.preparePlayer() }
                }
                self.pumpPreview()
            }
        }
    }
    private func preparePlayer() {
        guard active, wantsPlayback, !preparingPlayer, let document, document.isVideo else { return }
        preparingPlayer = true; playerGeneration += 1; let token = playerGeneration
        let value = adjustments, engine = self.engine
        queue.async { [weak self] in
            let result: Result<(AVPlayerItem, NotesMediaAccess), Error>
            do { result = .success(try engine.makePlaybackItem(document, adjustments: value)) } catch { result = .failure(error) }
            DispatchQueue.main.async { [weak self] in
                guard let self else { if case .success(let pair) = result { pair.1.close() }; return }
                self.preparingPlayer = false
                guard self.active, self.wantsPlayback, self.playerGeneration == token, self.document?.id == document.id, self.adjustments == value else {
                    if case .success(let pair) = result { pair.1.close() }
                    if self.active && self.wantsPlayback { self.preparePlayer() }; return
                }
                switch result {
                case .success(let pair):
                    self.playerAccess = pair.1
                    let player = AVPlayer(playerItem: pair.0); self.player = player
                    player.actionAtItemEnd = .pause
                    self.endObserver = NotificationCenter.default.addObserver(forName: .AVPlayerItemDidPlayToEndTime, object: pair.0, queue: .main) { [weak self] _ in
                        guard let self else { return }; self.wantsPlayback = false; self.isPlaying = false; self.removeTimeObserver(); self.requestPreview(); self.onChange?()
                    }
                    self.beginPlayback(player)
                case .failure(let failure): self.wantsPlayback = false; self.error = failure; self.onChange?()
                }
            }
        }
    }
    private func beginPlayback(_ player: AVPlayer) {
        let end = adjustments.trimEnd ?? document?.duration ?? 0
        if currentTime >= end - 0.05 { currentTime = adjustments.trimStart }
        let token = playerGeneration
        player.seek(to: CMTime(seconds: currentTime, preferredTimescale: 600), toleranceBefore: .zero, toleranceAfter: .zero) { [weak self, weak player] _ in
            DispatchQueue.main.async {
                guard let self, let player, self.active, self.wantsPlayback, self.playerGeneration == token, self.player === player else { return }
                player.play(); self.isPlaying = true; self.addTimeObserver(player); self.onChange?()
            }
        }
    }
    private func addTimeObserver(_ player: AVPlayer) {
        removeTimeObserver()
        timeObserver = player.addPeriodicTimeObserver(forInterval: CMTime(seconds: 0.25, preferredTimescale: 600), queue: .main) { [weak self] time in
            guard let self, self.active, self.isPlaying else { return }; let seconds = CMTimeGetSeconds(time)
            if seconds.isFinite { self.currentTime = seconds; self.onChange?() }
        }
    }
    private func removeTimeObserver() { if let timeObserver { player?.removeTimeObserver(timeObserver) }; timeObserver = nil }
    private func releasePlayer() {
        playerGeneration += 1; player?.pause(); removeTimeObserver()
        if let endObserver { NotificationCenter.default.removeObserver(endObserver) }; endObserver = nil
        player?.replaceCurrentItem(with: nil); player = nil; isPlaying = false
        playerAccess?.close(); playerAccess = nil
    }
    func exportExtensions() -> [String] { document.map(MediaAssemblyEngine.exportExtensions) ?? [] }
    func export(to url: URL, overwrite: Bool, expectedDocumentID: UUID? = nil, completion: ((Result<URL, Error>) -> Void)? = nil) {
        // A save panel can outlive its source document. Never export an old
        // document while its replacement imports, or a replacement into the
        // destination chosen for a previous document.
        guard !isExporting, !isBusy, let document,
              expectedDocumentID == nil || expectedDocumentID == document.id else {
            completion?(.failure(MediaAssemblyError.unavailable)); return
        }
        flushEditedEvent()
        wantsPlayback = false; releasePlayer()
        let ticket = MediaAssemblyExportTicket(); exportTicket = ticket; isExporting = true; progress = 0; error = nil
        // This timer exists only for an explicitly requested finite export;
        // paused editing and closed HUDs otherwise have no Media Assembly poller.
        let progressTimer = Timer(timeInterval: 0.25, repeats: true) { [weak self, weak ticket] _ in
            guard let self, let ticket, self.exportTicket === ticket else { return }
            let progress = ticket.progress
            if abs(progress - self.progress) >= 0.005 { self.progress = progress; if self.active { self.onChange?() } }
        }
        exportProgressTimer = progressTimer; RunLoop.main.add(progressTimer, forMode: .common)
        let value = adjustments, engine = self.engine; onChange?()
        queue.async { [weak self] in
            engine.export(document, adjustments: value, to: url, overwrite: overwrite, ticket: ticket) { result in
                DispatchQueue.main.async { [weak self] in
                    guard let self, self.exportTicket === ticket else { completion?(result); return }
                    self.exportProgressTimer?.invalidate(); self.exportProgressTimer = nil
                    self.exportTicket = nil; self.isExporting = false
                    switch result {
                    case .success(let resultURL):
                        self.lastExport = resultURL; self.progress = 1; self.onEvent?("exported")
                        // Atomic overwrite creates a new inode. Reload the new
                        // source instead of ever applying the edits twice.
                        if resultURL.standardizedFileURL.resolvingSymlinksInPath() == document.sourceURL.standardizedFileURL.resolvingSymlinksInPath() { self.importURL(resultURL, recordEvent: false) }
                    case .failure(let failure): self.error = failure; self.progress = 0
                    }
                    self.onChange?(); completion?(result)
                }
            }
        }
    }
    func report(_ failure: Error) { error = failure; onChange?() }
    func cancelExport() { exportTicket?.cancel() }
    private func flushEditedEvent() {
        guard let work = editedEventWork else { return }; work.cancel(); editedEventWork = nil; onEvent?("edited")
    }
}
