import AppKit

enum ReaderEvent { case imported, deleted, bookmarkChanged, preferencesChanged, progressChanged }

/// Main-thread presentation, one decoder and one reference-only library writer.
/// Page scrolling coalesces pending progress; neither JSON nor disk I/O runs on
/// the input thread. The production library is loaded only when Reader opens.
final class ReaderController {
    private let library: ReaderLibraryWorker
    private var snapshot = ReaderLibrarySnapshot()
    private var libraryLoaded = false, libraryLoading = false
    private var preferencePreview: ReaderPreferences?
    var books: [ReaderBook] { snapshot.books.map { item in
        guard let value = latestProgress[item.id] else { return item }
        var item = item; item.location = value.location; item.progress = value.fraction; return item
    } }
    var onChange: (() -> Void)?
    var onEvent: ((ReaderEvent) -> Void)?
    private(set) var active = false
    private(set) var loading = false
    private(set) var error: String?
    private(set) var pages: [ReaderPage] = []
    private(set) var current: ReaderPage?
    private(set) var imageDetail: (view: ReaderImageView, page: ReaderPage)?
    private var detailRequest: ReaderImageView?, detailRendering = false, detailRevision = 0
    var book: ReaderBook? { books.first { $0.id == snapshot.selected } }
    var preferences: ReaderPreferences { preferencePreview ?? snapshot.preferences }
    private var size = CGSize(width: 376, height: 334), dark = true
    private let queue = DispatchQueue(label: "EndfieldHUD.Reader", qos: .userInitiated)
    private let writer = DispatchQueue(label: "EndfieldHUD.Reader.library", qos: .utility)
    private let writes = DispatchGroup()
    private let worker = ReaderWorker()
    private var generation = 0
    private let gate = ReaderWorkGate()
    private var rendering = false
    private var pending: Request?
    private var history: [ReaderLocation] = []
    private var pendingProgress: [UUID: Progress] = [:], failedProgress: [UUID: Progress] = [:]
    private var latestProgress: [UUID: Progress] = [:]
    private var writingProgress = false
    private struct Progress { let location: ReaderLocation; let fraction: Double; let book: UUID }
    private struct Request { let location: ReaderLocation?; let progress: Double? }
    init(store: ReaderStore) {
        library = ReaderLibraryWorker(load: { store }); snapshot = ReaderLibrarySnapshot(store); libraryLoaded = true
    }
    init(store: Result<ReaderStore, Error>) {
        library = ReaderLibraryWorker(load: { try store.get() })
        if case .success(let value) = store { snapshot = ReaderLibrarySnapshot(value); libraryLoaded = true }
        if case .failure(let failure) = store { error = failure.localizedDescription }
    }
    init(loadStore: @escaping () throws -> ReaderStore) { library = ReaderLibraryWorker(load: loadStore) }
    deinit { let worker = worker; queue.async { worker.release() } }
    func setActive(_ value: Bool) {
        guard active != value else { return }; active = value
        if value {
            if !libraryLoaded { loadLibrary() }
            else if let book { open(book: book) } else { onChange?() }
        } else {
            generation += 1; gate.set(generation); resetImageDetail(); pending = nil; rendering = false; loading = false
            pages = []; current = nil; history = []
            let worker = worker; queue.async { worker.release() }; onChange?()
        }
    }
    private func loadLibrary() {
        guard !libraryLoading else { return }; libraryLoading = true; loading = true; onChange?()
        mutate({ _ in }) { [weak self] success in
            guard let self else { return }; self.libraryLoading = false; self.libraryLoaded = success
            if success, self.active, let book = self.book { self.open(book: book) }
            else { self.loading = false; self.onChange?() }
        }
    }
    func setLayout(size: CGSize, dark: Bool) {
        guard self.size != size || self.dark != dark else { return }
        self.size = size; self.dark = dark
        if active, current != nil { request(location: current?.location) }
    }
    func open(url: URL, retainedAccess: ShelfFileAccess? = nil) {
        guard active else { retainedAccess?.close(); return }
        generation += 1; gate.set(generation); let token = generation, worker = worker, gate = gate
        resetImageDetail(); pending = nil; rendering = false; loading = true; error = nil; pages = []; current = nil; history = []; onChange?()
        queue.async { [weak self, retainedAccess] in
            let result = Result { () throws -> String in
                guard gate.allows(token) else { throw CocoaError(.userCancelled) }
                worker.release(); worker.document = try ReaderDocument(access: ReaderFileAccess(url: url))
                return worker.document!.title
            }
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { [weak self] in
                guard let self, self.active, self.generation == token else { retainedAccess?.close(); return }
                switch result {
                case .success(let title):
                    self.mutate({ store in _ = try store.add(url: url, title: title) }) { [weak self, retainedAccess] success in
                        retainedAccess?.close()
                        guard let self else { return }
                        if !success { self.loading = false }
                        guard self.active, self.generation == token, success else { return }
                        self.libraryLoaded = true; self.onEvent?(.imported); self.request(location: self.book?.location)
                    }
                case .failure(let error): retainedAccess?.close(); self.fail(error)
                }
            }
        }
    }
    func select(_ id: UUID) {
        mutate({ try $0.select(id) }) { [weak self] success in
            guard let self, success, self.active, let book = self.book else { return }; self.open(book: book)
        }
    }
    private func open(book: ReaderBook) {
        generation += 1; gate.set(generation); let token = generation, worker = worker, gate = gate
        resetImageDetail(); pending = nil; rendering = false; loading = true; error = nil; pages = []; current = nil; history = []; onChange?()
        queue.async { [weak self] in
            let result = Result {
                guard gate.allows(token) else { throw CocoaError(.userCancelled) }
                worker.release(); worker.document = try ReaderDocument(access: ReaderFileAccess(book: book))
            }
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { [weak self] in
                guard let self, self.active, self.generation == token else { return }
                switch result { case .success: self.request(location: book.location); case .failure(let error): self.fail(error) }
            }
        }
    }
    func next() { if let next = current?.next { if let current { history.append(current.location); if history.count > 256 { history.removeFirst() } }; request(location: next) } }
    func previous() { if let previous = history.popLast() ?? current?.previous { request(location: previous) } }
    func jump(to location: ReaderLocation) { history = []; request(location: location) }
    func jump(progress: Double) { guard progress.isFinite else { return }; history = []; request(location: nil, progress: min(1, max(0, progress))) }
    func toggleBookmark() {
        guard let book, let current else { return }
        mutate({ store in
            try store.saveProgress(current.location, progress: current.progress, id: book.id)
            try store.toggleBookmark(id: book.id)
        }) { [weak self] success in if success { self?.onEvent?(.bookmarkChanged) } }
    }
    func removeBook(_ id: UUID) {
        let selected = book?.id == id
        mutate({ try $0.remove(id) }) { [weak self] success in
            guard let self, success else { return }
            self.pendingProgress[id] = nil; self.failedProgress[id] = nil; self.latestProgress[id] = nil
            self.onEvent?(.deleted)
            if selected, self.active { self.setActive(false); self.setActive(true) }
        }
    }
    func setPreferences(_ value: ReaderPreferences) {
        guard value.isValid, value != preferences else { return }
        preferencePreview = value; history = []
        if current != nil { request(location: current?.location) } else { onChange?() }
        mutate({ try $0.setPreferences(value) }) { [weak self] success in
            guard let self else { return }
            if self.preferencePreview == value { self.preferencePreview = nil }
            if success { self.onEvent?(.preferencesChanged) }
            else if self.current != nil { self.request(location: self.current?.location) }
        }
    }
    func requestImageDetail(_ view: ReaderImageView) {
        guard active, current?.isIllustration == true, view.isValid else { return }
        let value = view.clamped(to: size)
        guard value.zoom > 1 else { cancelImageDetail(); return }
        if imageDetail?.view == value && imageDetail?.page.location == current?.location { return }
        detailRevision += 1; detailRequest = value
        if !detailRendering { startImageDetail() }
    }
    func cancelImageDetail() { detailRevision += 1; detailRequest = nil; imageDetail = nil }
    private func resetImageDetail() { cancelImageDetail(); detailRendering = false }
    private func startImageDetail() {
        guard active, let view = detailRequest, let location = current?.location else { return }
        detailRequest = nil; detailRendering = true
        let token = generation, revision = detailRevision, worker = worker, gate = gate
        let preferences = preferences, size = size, dark = dark
        queue.async { [weak self] in
            let result = Result { () throws -> ReaderPage in
                guard gate.allows(token), let document = worker.document else { throw CocoaError(.userCancelled) }
                return try document.render(at: location, preferences: preferences, size: size, dark: dark, imageView: view)
            }
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { [weak self] in
                guard let self, self.active, self.generation == token else { return }
                self.detailRendering = false
                if self.detailRevision == revision, self.current?.location == location, case .success(let page) = result {
                    self.imageDetail = (view, page); self.onChange?()
                }
                if self.detailRequest != nil { self.startImageDetail() }
            }
        }
    }
    private func request(location: ReaderLocation?, progress: Double? = nil) {
        guard active else { return }
        cancelImageDetail()
        if let location, location != current?.location, let cached = pages.first(where: { $0.location == location }) {
            current = cached; saveProgress(cached); onChange?()
        }
        pending = Request(location: location, progress: progress)
        if !rendering { startRender() }
    }
    private func startRender() {
        guard active, let request = pending else { return }
        pending = nil; rendering = true; loading = current == nil; error = nil; onChange?()
        let token = generation, worker = worker, preferences = preferences, size = size, dark = dark, gate = gate
        queue.async { [weak self] in
            let result = Result { try worker.render(location: request.location, progress: request.progress, preferences: preferences, size: size, dark: dark, cancelled: { !gate.allows(token) }) }
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { [weak self] in
                guard let self, self.active, self.generation == token else { return }
                self.rendering = false
                if self.pending != nil { self.startRender(); return }
                self.loading = false
                switch result {
                case .success(let pages):
                    self.pages = pages; self.current = pages.first
                    if let page = pages.first { self.saveProgress(page) }; self.onChange?()
                case .failure(let error): self.fail(error)
                }
            }
        }
    }
    private func saveProgress(_ page: ReaderPage) {
        guard let id = snapshot.selected else { return }
        let value = Progress(location: page.location, fraction: page.progress, book: id)
        pendingProgress[id] = value; latestProgress[id] = value
        if !writingProgress { writeProgress() }
    }
    private func writeProgress() {
        guard let value = pendingProgress.values.first else { return }
        pendingProgress[value.book] = nil; writingProgress = true
        let previous = snapshot.books.first { $0.id == value.book }?.location
        mutate({ try $0.saveProgress(value.location, progress: value.fraction, id: value.book) }) { [weak self] success in
            guard let self else { return }; self.writingProgress = false
            if success {
                self.failedProgress[value.book] = nil
                if self.latestProgress[value.book]?.location == value.location, self.latestProgress[value.book]?.fraction == value.fraction { self.latestProgress[value.book] = nil }
                if previous != value.location { self.onEvent?(.progressChanged) }
            }
            else { self.failedProgress[value.book] = value }
            if !self.pendingProgress.isEmpty { self.writeProgress() }
        }
    }
    private func mutate(_ operation: @escaping (ReaderStore) throws -> Void, completion: ((Bool) -> Void)? = nil) {
        let library = library, writes = writes; writes.enter()
        writer.async { [weak self] in
            let result = Result { () throws -> ReaderLibrarySnapshot in
                let store = try library.get(); try operation(store); return ReaderLibrarySnapshot(store)
            }
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { [weak self] in
                defer { writes.leave() }
                guard let self else { return }
                switch result {
                case .success(let value): self.snapshot = value; completion?(true)
                case .failure(let failure): self.error = failure.localizedDescription; completion?(false)
                }
                self.onChange?()
            }
        }
    }
    private func fail(_ error: Error) { loading = false; rendering = false; self.error = error.localizedDescription; onChange?() }
    func report(_ error: Error) { fail(error) }
    func drainPendingWrites(timeout: TimeInterval = 3, completion: @escaping (Bool) -> Void) {
        // A failure on one book must survive successful writes to another.
        // Each dictionary is bounded by the library's maximum 50 references.
        for (id, value) in failedProgress where pendingProgress[id] == nil { pendingProgress[id] = latestProgress[id] ?? value }
        if !writingProgress { writeProgress() }
        var finished = false
        var deadline: Timer?
        let finish: (Bool) -> Void = { success in
            guard !finished else { return }; finished = true; deadline?.invalidate(); deadline = nil; completion(success)
        }
        writes.notify(queue: writer) { [weak self] in
            RunLoop.main.perform(inModes: [.common, .modalPanel]) { finish(self?.failedProgress.isEmpty == true) }
        }
        let timer = Timer(timeInterval: max(0.01, timeout), repeats: false) { _ in finish(false) }
        deadline = timer; RunLoop.main.add(timer, forMode: .common); RunLoop.main.add(timer, forMode: .modalPanel)
    }
}

private struct ReaderLibrarySnapshot {
    var books: [ReaderBook] = [], selected: UUID?, preferences = ReaderPreferences()
    init() {}
    init(_ store: ReaderStore) { books = store.books; selected = store.selected; preferences = store.preferences }
}
private final class ReaderLibraryWorker {
    private let load: () throws -> ReaderStore
    private var result: Result<ReaderStore, Error>?
    init(load: @escaping () throws -> ReaderStore) { self.load = load }
    func get() throws -> ReaderStore {
        if let result { return try result.get() }; let value = Result { try load() }; result = value; return try value.get()
    }
}

private final class ReaderWorker {
    var document: ReaderDocument?
    private var cache: [ReaderLocation: ReaderPage] = [:]
    private var preferences: ReaderPreferences?, size: CGSize?, dark: Bool?
    func release() { document = nil; cache = [:]; preferences = nil; size = nil; dark = nil }
    func render(location: ReaderLocation?, progress: Double?, preferences: ReaderPreferences, size: CGSize, dark: Bool, cancelled: () -> Bool) throws -> [ReaderPage] {
        guard !cancelled() else { throw CocoaError(.userCancelled) }
        guard let document else { throw ReaderError.unavailable }
        if self.preferences != preferences || self.size != size || self.dark != dark { cache = [:] }
        self.preferences = preferences; self.size = size; self.dark = dark
        let anchor = try progress.map { try document.location(at: $0) } ?? document.normalized(location ?? ReaderLocation())
        func page(_ at: ReaderLocation) throws -> ReaderPage {
            guard !cancelled() else { throw CocoaError(.userCancelled) }
            if let found = cache[at] { return found }
            return try document.render(at: at, preferences: preferences, size: size, dark: dark)
        }
        let current = try page(anchor); var result = [current]
        if let next = current.next, let neighbor = try? page(next) { result.append(neighbor) }
        if let previous = current.previous, let neighbor = try? page(previous) { result.append(neighbor) }
        cache = Dictionary(result.map { ($0.location, $0) }, uniquingKeysWith: { first, _ in first })
        return result
    }
}

private final class ReaderWorkGate {
    private let lock = NSLock(); private var value = 0
    func set(_ value: Int) { lock.lock(); self.value = value; lock.unlock() }
    func allows(_ value: Int) -> Bool { lock.lock(); defer { lock.unlock() }; return self.value == value }
}
