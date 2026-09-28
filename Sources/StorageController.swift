import Foundation
import Darwin

struct StorageCapacity: Equatable {
    let volumeName: String
    let totalBytes: Int64
    let availableBytes: Int64
    let updatedAt: Date
    var usedBytes: Int64 { totalBytes - availableBytes }

    init?(volumeName: String, totalBytes: Int64, availableBytes: Int64, updatedAt: Date) {
        guard totalBytes > 0, availableBytes >= 0, availableBytes <= totalBytes else { return nil }
        self.volumeName = volumeName; self.totalBytes = totalBytes
        self.availableBytes = availableBytes; self.updatedAt = updatedAt
    }
}

struct StorageSnapshot: Equatable {
    var capacity: StorageCapacity?
    var isLoading: Bool
    var error: String?
}

struct StorageCategoryUsage: Equatable {
    let id: String
    let title: String
    /// Allocated bytes in this scoped folder. Nil means unavailable, not empty.
    let bytes: Int64?
    let isPartial: Bool
}

struct StorageDetailsSnapshot: Equatable {
    var categories: [StorageCategoryUsage]
    var isLoading: Bool
    var updatedAt: Date?
    var isPartial: Bool
    var error: String?
}

protocol StorageRefreshTimer: AnyObject { func invalidate() }
extension Timer: StorageRefreshTimer {}

/// Shared by one bounded utility-queue walk and the main-thread owner.
final class StorageScanCancellation {
    private let lock = NSLock()
    private var value = false
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return value }
    func cancel() { lock.lock(); value = true; lock.unlock() }
}

/// The app owns this model for its lifetime. Only the lightweight capacity query
/// repeats, once per minute while visible. Folder enumeration is explicit and
/// cached for fifteen minutes; hiding cancels it. There is no whole-disk scan.
final class StorageController {
    typealias ScheduleTimer = (TimeInterval, @escaping () -> Void) -> StorageRefreshTimer
    private(set) var snapshot = StorageSnapshot(capacity: nil, isLoading: false, error: nil)
    private(set) var details = StorageDetailsSnapshot(categories: [], isLoading: false,
                                                     updatedAt: nil, isPartial: false, error: nil)
    private(set) var isActive = false
    private let clock: () -> Date
    private let readCapacity: () -> StorageCapacity?
    private let scanDetails: (StorageScanCancellation) -> StorageDetailsSnapshot
    private let runWorker: (@escaping () -> Void) -> Void
    private let scheduleTimer: ScheduleTimer
    private var timer: StorageRefreshTimer?
    private var capacityRequestedAt: Date?
    private var capacityInFlight = false
    private var detailToken: StorageScanCancellation?
    private var pendingDetails = false
    private var observers: [UUID: () -> Void] = [:]
    static let capacityCacheDuration: TimeInterval = 60
    static let detailsCacheDuration: TimeInterval = 15 * 60

    init(clock: @escaping () -> Date = Date.init,
         readCapacity: (() -> StorageCapacity?)? = nil,
         scanDetails: ((StorageScanCancellation) -> StorageDetailsSnapshot)? = nil,
         runWorker: ((@escaping () -> Void) -> Void)? = nil,
         scheduleTimer: ScheduleTimer? = nil) {
        precondition(Thread.isMainThread)
        self.clock = clock
        self.readCapacity = readCapacity ?? { Self.readStartupCapacity(date: clock()) }
        self.scanDetails = scanDetails ?? { token in
            guard let device = Self.startupDevice() else {
                return StorageDetailsSnapshot(categories: [], isLoading: false, updatedAt: clock(),
                                              isPartial: true, error: "The startup filesystem is unavailable.")
            }
            return StorageFolderScanner.scan(scopes: StorageFolderScanner.standardScopes(), cancellation: token,
                                             date: clock(), expectedDevice: device)
        }
        let queue = DispatchQueue(label: "EndfieldCharge.Storage", qos: .utility)
        self.runWorker = runWorker ?? { action in queue.async(execute: action) }
        self.scheduleTimer = scheduleTimer ?? { interval, tick in
            let timer = Timer(timeInterval: interval, repeats: true) { _ in tick() }
            timer.tolerance = 5
            RunLoop.main.add(timer, forMode: .common)
            return timer
        }
    }

    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let id = UUID(); observers[id] = callback; return id
    }
    func removeObserver(_ id: UUID) { precondition(Thread.isMainThread); observers.removeValue(forKey: id) }

    func activate() {
        precondition(Thread.isMainThread)
        guard !isActive else { return }
        isActive = true
        refreshCapacity()
        guard isActive else { return }
        timer = scheduleTimer(Self.capacityCacheDuration) { [weak self] in
            guard let self, self.isActive else { return }
            self.refreshCapacity()
        }
    }

    func deactivate() {
        precondition(Thread.isMainThread)
        isActive = false; timer?.invalidate(); timer = nil
        pendingDetails = false; detailToken?.cancel()
        if details.isLoading { details.isLoading = false; publish() }
    }

    func requestDetails(refresh: Bool = false) {
        precondition(Thread.isMainThread)
        guard isActive else { return }
        refreshCapacity()
        if let token = detailToken {
            // A reopened panel waits for cancellation to finish before starting
            // a replacement. The serial worker never runs overlapping walks.
            if token.isCancelled { pendingDetails = true }
            return
        }
        if !refresh, let date = details.updatedAt,
           clock().timeIntervalSince(date) >= 0,
           clock().timeIntervalSince(date) < Self.detailsCacheDuration { return }
        let token = StorageScanCancellation()
        detailToken = token; details.isLoading = true; details.error = nil; publish()
        let scan = scanDetails
        runWorker { [weak self] in
            let result = scan(token)
            Self.onMain { [weak self] in
                guard let self, self.detailToken === token else { return }
                self.detailToken = nil
                if !token.isCancelled { self.details = result; self.details.isLoading = false; self.publish() }
                if self.pendingDetails && self.isActive {
                    self.pendingDetails = false; self.requestDetails(refresh: true)
                }
            }
        }
    }

    /// Explicit capacity refresh is inexpensive and never enumerates folders.
    /// Calls arriving during an existing read coalesce into that in-flight read.
    func refresh() {
        precondition(Thread.isMainThread)
        refreshCapacity(force: true)
    }

    private func refreshCapacity(force: Bool = false) {
        guard isActive, !capacityInFlight else { return }
        if !force, let date = capacityRequestedAt, clock().timeIntervalSince(date) >= 0,
           clock().timeIntervalSince(date) < Self.capacityCacheDuration { return }
        capacityRequestedAt = clock(); capacityInFlight = true
        snapshot.isLoading = true; publish()
        let read = readCapacity
        runWorker { [weak self] in
            let result = read()
            Self.onMain { [weak self] in
                guard let self else { return }
                self.capacityInFlight = false; self.snapshot.isLoading = false
                if let result { self.snapshot.capacity = result; self.snapshot.error = nil }
                else { self.snapshot.error = "Storage capacity is unavailable." }
                self.publish()
            }
        }
    }

    private func publish() { precondition(Thread.isMainThread); Array(observers.values).forEach { $0() } }
    private static func onMain(_ action: @escaping () -> Void) {
        if Thread.isMainThread { action() } else { DispatchQueue.main.async(execute: action) }
    }

    /// Query one startup Data filesystem. Summing APFS volumes would count their
    /// shared free space repeatedly. Free means presently unallocated capacity,
    /// not macOS's larger, policy-dependent "available for important usage".
    private static var startupPath: String {
        let dataPath = "/System/Volumes/Data"
        return FileManager.default.fileExists(atPath: dataPath) ? dataPath : "/"
    }

    private static func startupDevice() -> dev_t? {
        var info = stat()
        return stat(startupPath, &info) == 0 ? info.st_dev : nil
    }

    static func readStartupCapacity(date: Date = Date()) -> StorageCapacity? {
        let path = startupPath
        var statistics = statfs()
        guard statfs(path, &statistics) == 0,
              let blocks = Int64(exactly: statistics.f_blocks),
              let free = Int64(exactly: statistics.f_bavail) else { return nil }
        let total = blocks.multipliedReportingOverflow(by: Int64(statistics.f_bsize))
        let available = free.multipliedReportingOverflow(by: Int64(statistics.f_bsize))
        guard !total.overflow, !available.overflow else { return nil }
        let name = (try? URL(fileURLWithPath: path).resourceValues(forKeys: [.volumeNameKey]).volumeName) ?? "Startup disk"
        return StorageCapacity(volumeName: name, totalBytes: total.partialValue,
                               availableBytes: available.partialValue, updatedAt: date)
    }

    static func fixture() -> StorageController {
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let capacity = StorageCapacity(volumeName: "Macintosh HD", totalBytes: 1_000_000_000_000,
                                       availableBytes: 420_000_000_000, updatedAt: date)!
        let detail = StorageDetailsSnapshot(categories: [
            StorageCategoryUsage(id: "applications", title: "Applications", bytes: 58_000_000_000, isPartial: false),
            StorageCategoryUsage(id: "documents", title: "Documents", bytes: 21_000_000_000, isPartial: false),
            StorageCategoryUsage(id: "downloads", title: "Downloads", bytes: 6_000_000_000, isPartial: true)
        ], isLoading: false, updatedAt: date, isPartial: true, error: nil)
        let value = StorageController(clock: { date }, readCapacity: { capacity }, scanDetails: { _ in detail },
                                      runWorker: { $0() }, scheduleTimer: { _, _ in FixtureStorageTimer() })
        value.snapshot = StorageSnapshot(capacity: capacity, isLoading: false, error: nil)
        value.details = detail; value.capacityRequestedAt = date
        return value
    }

    deinit { timer?.invalidate(); detailToken?.cancel() }
}

private final class FixtureStorageTimer: StorageRefreshTimer { func invalidate() {} }

struct StorageFolderScope {
    let id: String
    let title: String
    let url: URL
}

struct StorageScanLimits {
    var maximumEntries = 40_000
    var maximumSeconds: TimeInterval = 10
    var entriesPerFolder = 8_000
    var secondsPerFolder: TimeInterval = 2
    var maximumDepth = 32
}

/// Metadata-only POSIX walk: no file contents, symlinks, mounted subvolumes or
/// dataless iCloud directories are opened. Descriptor-relative O_NOFOLLOW
/// prevents a changed directory entry from turning into a followed symlink.
/// File blocks are an estimate (APFS clone sharing is not publicly attributable),
/// so these scoped categories must never be presented as a total-disk partition.
enum StorageFolderScanner {
    static func standardScopes() -> [StorageFolderScope] {
        let home = FileManager.default.homeDirectoryForCurrentUser
        var result = [StorageFolderScope(id: "applications", title: "Applications", url: URL(fileURLWithPath: "/Applications"))]
        for (id, title) in [("documents", "Documents"), ("downloads", "Downloads"), ("pictures", "Pictures"),
                            ("music", "Music"), ("movies", "Movies")] {
            result.append(StorageFolderScope(id: id, title: title, url: home.appendingPathComponent(title, isDirectory: true)))
        }
        return result
    }

    static func scan(scopes: [StorageFolderScope], cancellation: StorageScanCancellation,
                     date: Date = Date(), limits: StorageScanLimits = StorageScanLimits(), expectedDevice: dev_t? = nil,
                     uptime: () -> TimeInterval = { ProcessInfo.processInfo.systemUptime }) -> StorageDetailsSnapshot {
        let start = uptime()
        var totalEntries = 0
        struct Identity: Hashable { let device: dev_t; let inode: ino_t }
        var seen = Set<Identity>()
        var categories: [StorageCategoryUsage] = []
        for scope in scopes {
            let folderStart = uptime()
            var entries = 0, partial = false
            var bytes: Int64 = 0
            func limitReached() -> Bool {
                cancellation.isCancelled || totalEntries >= max(0, limits.maximumEntries)
                    || entries >= max(0, limits.entriesPerFolder)
                    || uptime() - start >= max(0, limits.maximumSeconds)
                    || uptime() - folderStart >= max(0, limits.secondsPerFolder)
            }
            if limitReached() {
                categories.append(StorageCategoryUsage(id: scope.id, title: scope.title, bytes: nil, isPartial: true))
                continue
            }
            var rootInfo = stat()
            guard lstat(scope.url.path, &rootInfo) == 0,
                  rootInfo.st_mode & S_IFMT == S_IFDIR,
                  expectedDevice == nil || rootInfo.st_dev == expectedDevice,
                  rootInfo.st_flags & UInt32(SF_DATALESS) == 0 else {
                categories.append(StorageCategoryUsage(id: scope.id, title: scope.title, bytes: nil, isPartial: true))
                continue
            }
            let rootFD = open(scope.url.path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
            guard rootFD >= 0 else {
                categories.append(StorageCategoryUsage(id: scope.id, title: scope.title, bytes: nil, isPartial: true))
                continue
            }
            // fstat again after opening closes a root-replacement race.
            guard fstat(rootFD, &rootInfo) == 0,
                  expectedDevice == nil || rootInfo.st_dev == expectedDevice, rootInfo.st_flags & UInt32(SF_DATALESS) == 0 else {
                close(rootFD)
                categories.append(StorageCategoryUsage(id: scope.id, title: scope.title, bytes: nil, isPartial: true))
                continue
            }
            func walk(_ descriptor: Int32, depth: Int) {
                guard let directory = fdopendir(descriptor) else { close(descriptor); partial = true; return }
                defer { closedir(directory) }
                while true {
                    if limitReached() { partial = true; return }
                    errno = 0
                    guard let entry = readdir(directory) else { if errno != 0 { partial = true }; return }
                    let name = withUnsafePointer(to: &entry.pointee.d_name) {
                        $0.withMemoryRebound(to: CChar.self, capacity: Int(MAXNAMLEN) + 1) { String(cString: $0) }
                    }
                    if name == "." || name == ".." { continue }
                    entries += 1; totalEntries += 1
                    var info = stat()
                    guard fstatat(dirfd(directory), name, &info, AT_SYMLINK_NOFOLLOW) == 0 else { partial = true; continue }
                    let type = info.st_mode & S_IFMT
                    // Symlink targets and mount points are outside this folder's scope.
                    if type == S_IFLNK { continue }
                    guard info.st_dev == rootInfo.st_dev else { partial = true; continue }
                    guard info.st_flags & UInt32(SF_DATALESS) == 0 else { partial = true; continue }
                    if type == S_IFDIR {
                        guard depth < max(0, limits.maximumDepth) else { partial = true; continue }
                        let child = openat(dirfd(directory), name, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC)
                        guard child >= 0 else { partial = true; continue }
                        var opened = stat()
                        guard fstat(child, &opened) == 0, opened.st_dev == rootInfo.st_dev,
                              opened.st_flags & UInt32(SF_DATALESS) == 0 else { close(child); partial = true; continue }
                        walk(child, depth: depth + 1)
                    } else if type == S_IFREG {
                        guard seen.insert(Identity(device: info.st_dev, inode: info.st_ino)).inserted else { continue }
                        let allocated = max(0, Int64(info.st_blocks)).multipliedReportingOverflow(by: 512)
                        let sum = bytes.addingReportingOverflow(allocated.partialValue)
                        if allocated.overflow || sum.overflow { partial = true } else { bytes = sum.partialValue }
                    }
                }
            }
            walk(rootFD, depth: 0)
            categories.append(StorageCategoryUsage(id: scope.id, title: scope.title, bytes: bytes, isPartial: partial))
        }
        let unavailable = !categories.isEmpty && categories.allSatisfy { $0.bytes == nil }
        return StorageDetailsSnapshot(categories: categories, isLoading: false, updatedAt: date,
                                      isPartial: categories.contains { $0.isPartial },
                                      error: unavailable ? "Folder sizes are unavailable or the scan was interrupted." : nil)
    }
}
