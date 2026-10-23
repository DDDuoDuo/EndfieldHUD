import AppKit
import Darwin
import DiskArbitration
import IOKit

struct StorageCapacity: Equatable {
    let volumeName: String
    let totalBytes: Int64
    let availableBytes: Int64
    let updatedAt: Date
    var usedBytes: Int64 { totalBytes - availableBytes }
    /// Share in use, 0...1: the USED percentage and the filled capacity cells.
    var usedFraction: Double { Double(usedBytes) / Double(totalBytes) }
    /// Less than a tenth free: the low-space (red) presentation.
    var isLowOnSpace: Bool { Double(availableBytes) < Double(totalBytes) * Self.lowSpaceFraction }
    static let lowSpaceFraction = 0.1

    init?(volumeName: String, totalBytes: Int64, availableBytes: Int64, updatedAt: Date) {
        guard totalBytes > 0, availableBytes >= 0, availableBytes <= totalBytes else { return nil }
        self.volumeName = volumeName; self.totalBytes = totalBytes
        self.availableBytes = availableBytes; self.updatedAt = updatedAt
    }
}

/// One mounted disk the Storage module can show. Volumes that share an APFS
/// container report the same free space, so their numbers are never summed.
struct StorageVolume: Equatable {
    /// Stable across launches and remounts: the volume UUID when the
    /// filesystem has one, else the mount path.
    let id: String
    let url: URL
    let isStartup: Bool
    let isInternal: Bool
    /// The display name is `capacity.volumeName`.
    let capacity: StorageCapacity
    var name: String { capacity.volumeName }
    var totalBytes: Int64 { capacity.totalBytes }
    var usedBytes: Int64 { capacity.usedBytes }
    var availableBytes: Int64 { capacity.availableBytes }
}

/// What the system reports about one mounted filesystem, before the listing
/// rules of `StorageVolumeCatalog.volumes(from:date:startupName:)` apply.
struct StorageVolumeCandidate: Equatable {
    var url: URL
    var uuid: String? = nil
    var name: String? = nil
    var isRootFileSystem = false
    var isLocal = true
    var isBrowsable = true
    var isInternal = false
    var isReadOnly = false
    var isSnapshot = false
    var isDiskImage = false
    var isOpticalMedia = false
    var fileSystemType = ""
    /// APFS volume roles (Preboot, Recovery, VM, Data, Backup…); empty when none.
    var roles: [String] = []
    var totalBytes: Int64? = nil
    /// For the startup disk the capacity available for important usage, which
    /// is the figure Finder shows; for other disks the plain available capacity.
    var availableBytes: Int64? = nil
}

/// The disks a user recognises: the startup disk plus mounted local volumes
/// that Finder shows. Hidden, system-support, snapshot, network, optical and
/// read-only disk-image mounts are left out.
enum StorageVolumeCatalog {
    /// The volume menu lists at most this many disks, the startup disk first.
    static let limit = 7
    /// APFS support roles; their volumes hold no user files.
    static let supportRoles: Set<String> = ["Preboot", "Recovery", "VM", "Update", "xART", "Hardware", "Baseband", "Installer"]
    /// Pseudo, automount and optical-disc filesystems.
    static let ignoredFileSystems: Set<String> = ["autofs", "devfs", "fdesc", "nullfs", "bindfs", "cd9660", "cddafs", "udf"]

    /// Pure listing rules: startup disk first, then internal before external
    /// disks, then by name. One entry per identity; unreadable capacities are
    /// left out rather than shown as empty disks.
    static func volumes(from candidates: [StorageVolumeCandidate], date: Date, startupName: String) -> [StorageVolume] {
        var result: [StorageVolume] = [], seen = Set<String>()
        // The startup disk is considered first, so a duplicate identity (such
        // as its Data volume) always resolves to it.
        for candidate in candidates.filter(\.isRootFileSystem) + candidates.filter({ !$0.isRootFileSystem }) {
            let startup = candidate.isRootFileSystem && !result.contains(where: \.isStartup)
            guard startup || isListable(candidate),
                  let total = candidate.totalBytes, let available = candidate.availableBytes else { continue }
            let id = candidate.uuid.flatMap { $0.isEmpty ? nil : $0 } ?? candidate.url.path
            let name = candidate.name.flatMap { $0.isEmpty ? nil : $0 }
                ?? (startup ? startupName : candidate.url.lastPathComponent)
            guard let capacity = StorageCapacity(volumeName: name, totalBytes: total,
                                                 availableBytes: min(max(0, available), total), updatedAt: date),
                  seen.insert(id).inserted else { continue }
            result.append(StorageVolume(id: id, url: candidate.url, isStartup: startup,
                                        isInternal: candidate.isInternal, capacity: capacity))
        }
        return result.sorted { a, b in
            if a.isStartup != b.isStartup { return a.isStartup }
            if a.isInternal != b.isInternal { return a.isInternal }
            let order = a.name.localizedStandardCompare(b.name)
            return order == .orderedSame ? a.url.path < b.url.path : order == .orderedAscending
        }
    }

    static func isListable(_ candidate: StorageVolumeCandidate) -> Bool {
        let path = candidate.url.path
        return candidate.isLocal && candidate.isBrowsable && !candidate.isSnapshot
            && path != "/" && !path.hasPrefix("/System/Volumes/")
            && !ignoredFileSystems.contains(candidate.fileSystemType.lowercased())
            && supportRoles.isDisjoint(with: candidate.roles)
            && !candidate.isOpticalMedia && !(candidate.isDiskImage && candidate.isReadOnly)
    }

    /// At most `limit` entries; a chosen disk beyond the limit takes the last
    /// place, so the menu can always show the current choice.
    static func limited(_ volumes: [StorageVolume], keeping id: String?) -> [StorageVolume] {
        guard volumes.count > limit else { return volumes }
        var result = Array(volumes.prefix(limit))
        if let id, !result.contains(where: { $0.id == id }), let kept = volumes.first(where: { $0.id == id }) {
            result[result.count - 1] = kept
        }
        return result
    }

    /// Worker thread only: each capacity query can wait on a slow disk.
    /// Network shares are recognised from the kernel's cached mount table and
    /// never queried. The startup disk is always considered.
    static func mountedVolumes(date: Date = Date(), language: AppLanguage = .system) -> [StorageVolume] {
        let mounts = mountTable()
        let session = DASessionCreate(kCFAllocatorDefault)
        let visible = FileManager.default.mountedVolumeURLs(includingResourceValuesForKeys: [],
                                                            options: [.skipHiddenVolumes]) ?? []
        let urls = [URL(fileURLWithPath: "/", isDirectory: true)] + visible.filter { $0.path != "/" }
        let candidates = urls.compactMap { candidate(at: $0, mount: mounts[$0.path], session: session) }
        return volumes(from: candidates, date: date, startupName: L10n.text("Startup disk", "启动磁盘", language: language))
    }

    struct MountEntry: Equatable {
        let flags: UInt32
        let fileSystemType: String
    }

    /// The kernel's cached mount list (MNT_NOWAIT): no filesystem is asked, so
    /// an unresponsive network share cannot block the listing.
    static func mountTable() -> [String: MountEntry] {
        let count = getfsstat(nil, 0, MNT_NOWAIT)
        guard count > 0, count < 4096 else { return [:] }
        var entries = Array(repeating: statfs(), count: Int(count) + 8)
        let filled = entries.withUnsafeMutableBufferPointer { buffer in
            getfsstat(buffer.baseAddress, Int32(buffer.count * MemoryLayout<statfs>.stride), MNT_NOWAIT)
        }
        guard filled > 0 else { return [:] }
        var table: [String: MountEntry] = [:]
        for var entry in entries.prefix(Int(filled)) {
            table[cString(&entry.f_mntonname)] = MountEntry(flags: entry.f_flags, fileSystemType: cString(&entry.f_fstypename))
        }
        return table
    }

    private static func cString<T>(_ tuple: inout T) -> String {
        withUnsafeBytes(of: &tuple) { String(decoding: $0.prefix { $0 != 0 }, as: UTF8.self) }
    }

    /// Reads one mounted volume. Only the startup disk is read without a local
    /// mount-table entry; anything else could be a share whose queries hang.
    static func candidate(at url: URL, mount: MountEntry?, session: DASession?) -> StorageVolumeCandidate? {
        let root = url.path == "/"
        guard root || mount.map({ $0.flags & UInt32(MNT_LOCAL) != 0 }) == true else { return nil }
        var keys: Set<URLResourceKey> = [.volumeNameKey, .volumeLocalizedNameKey, .volumeUUIDStringKey,
                                         .volumeIsBrowsableKey, .volumeIsLocalKey, .volumeIsInternalKey,
                                         .volumeIsReadOnlyKey, .volumeTotalCapacityKey, .volumeAvailableCapacityKey]
        if root { keys.insert(.volumeAvailableCapacityForImportantUsageKey) }
        // A new URL carries no cached resource values from an earlier read.
        guard let values = try? URL(fileURLWithPath: url.path, isDirectory: true).resourceValues(forKeys: keys) else { return nil }
        var result = StorageVolumeCandidate(url: url)
        result.uuid = values.volumeUUIDString
        result.name = values.volumeLocalizedName ?? values.volumeName
        result.isRootFileSystem = root
        result.isLocal = values.volumeIsLocal ?? true
        result.isBrowsable = values.volumeIsBrowsable ?? true
        result.isInternal = values.volumeIsInternal ?? false
        result.isReadOnly = values.volumeIsReadOnly ?? ((mount?.flags ?? 0) & UInt32(MNT_RDONLY) != 0)
        result.isSnapshot = (mount?.flags ?? 0) & UInt32(MNT_SNAPSHOT) != 0
        result.fileSystemType = mount?.fileSystemType ?? ""
        result.totalBytes = values.volumeTotalCapacity.map(Int64.init)
        if root, let important = values.volumeAvailableCapacityForImportantUsage, important > 0 {
            result.availableBytes = important
        } else {
            result.availableBytes = values.volumeAvailableCapacity.map(Int64.init)
        }
        guard let session, let disk = DADiskCreateFromVolumePath(kCFAllocatorDefault, session, url as CFURL) else { return result }
        let description = DADiskCopyDescription(disk) as? [String: Any] ?? [:]
        func text(_ key: CFString) -> String? { description[key as String] as? String }
        result.isDiskImage = text(kDADiskDescriptionDeviceModelKey) == "Disk Image"
            || text(kDADiskDescriptionDeviceProtocolKey) == "Disk Image"
        result.isOpticalMedia = ["IOCDMedia", "IODVDMedia", "IOBDMedia"].contains(text(kDADiskDescriptionMediaKindKey) ?? "")
        if values.volumeIsInternal == nil, let isInternal = description[kDADiskDescriptionDeviceInternalKey as String] as? Bool {
            result.isInternal = isInternal
        }
        let media = DADiskCopyIOMedia(disk)
        if media != IO_OBJECT_NULL {
            result.roles = IORegistryEntryCreateCFProperty(media, "Role" as CFString, kCFAllocatorDefault, 0)?
                .takeRetainedValue() as? [String] ?? []
            IOObjectRelease(media)
        }
        return result
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
///
/// Each capacity read lists every mounted disk with its numbers (on the
/// worker); `snapshot.capacity` is the chosen disk's. The choice is remembered
/// under `volumeSelectionKey`. While that disk is not mounted the startup disk
/// is shown and the choice is kept, so it returns when the disk does.
final class StorageController {
    typealias ScheduleTimer = (TimeInterval, @escaping () -> Void) -> StorageRefreshTimer
    private(set) var snapshot = StorageSnapshot(capacity: nil, isLoading: false, error: nil)
    private(set) var details = StorageDetailsSnapshot(categories: [], isLoading: false,
                                                     updatedAt: nil, isPartial: false, error: nil)
    private(set) var isActive = false
    /// Disks from the latest read, startup first (at most `StorageVolumeCatalog.limit`).
    private(set) var volumes: [StorageVolume] = []
    /// The remembered choice. It can name a disk that is not mounted now.
    private(set) var selectedVolumeIdentifier: String?
    private let clock: () -> Date
    private let readCapacity: (AppLanguage) -> StorageCapacity?
    private let readVolumes: (AppLanguage) -> [StorageVolume]
    private let scanDetails: (StorageScanCancellation) -> StorageDetailsSnapshot
    private let runWorker: (@escaping () -> Void) -> Void
    private let scheduleTimer: ScheduleTimer
    private let defaults: UserDefaults?
    private var timer: StorageRefreshTimer?
    private var capacityRequestedAt: Date?
    private var capacityInFlight = false
    private var pendingVolumeRead = false
    private var detailToken: StorageScanCancellation?
    private var pendingDetails = false
    private var observers: [UUID: () -> Void] = [:]
    private var volumeEventCenter: NotificationCenter?
    private var volumeEventTokens: [NSObjectProtocol] = []
    static let capacityCacheDuration: TimeInterval = 60
    static let detailsCacheDuration: TimeInterval = 15 * 60
    static let volumeSelectionKey = "storageVolumeIdentifier"

    /// The disk `snapshot` describes: the remembered choice while it is
    /// mounted, otherwise the startup disk.
    var currentVolume: StorageVolume? {
        volumes.first { $0.id == selectedVolumeIdentifier } ?? volumes.first(where: \.isStartup)
    }
    /// A volume menu is worth showing only with two or more disks.
    var canChooseVolume: Bool { volumes.count > 1 }

    /// Injected readers keep this Mac's disks, preferences and mount events out
    /// of tests: injecting `readCapacity` alone lists no disks, and nil
    /// `defaults`/`volumeEvents` mean UserDefaults.standard and NSWorkspace
    /// only when neither reader is injected.
    init(clock: @escaping () -> Date = Date.init,
         readCapacity: (() -> StorageCapacity?)? = nil,
         readVolumes: (() -> [StorageVolume])? = nil,
         scanDetails: ((StorageScanCancellation) -> StorageDetailsSnapshot)? = nil,
         runWorker: ((@escaping () -> Void) -> Void)? = nil,
         scheduleTimer: ScheduleTimer? = nil,
         defaults: UserDefaults? = nil,
         volumeEvents: NotificationCenter? = nil) {
        precondition(Thread.isMainThread)
        let production = readCapacity == nil && readVolumes == nil
        self.clock = clock
        if let readCapacity { self.readCapacity = { _ in readCapacity() } }
        else { self.readCapacity = { Self.readStartupCapacity(date: clock(), language: $0) } }
        if let readVolumes { self.readVolumes = { _ in readVolumes() } }
        else if readCapacity != nil { self.readVolumes = { _ in [] } }
        else { self.readVolumes = { StorageVolumeCatalog.mountedVolumes(date: clock(), language: $0) } }
        self.defaults = defaults ?? (production ? .standard : nil)
        selectedVolumeIdentifier = Self.validVolumeIdentifier(self.defaults?.string(forKey: Self.volumeSelectionKey))
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
        if let center = volumeEvents ?? (production ? NSWorkspace.shared.notificationCenter : nil) {
            volumeEventCenter = center
            volumeEventTokens = [NSWorkspace.didMountNotification, NSWorkspace.didUnmountNotification,
                                 NSWorkspace.didRenameVolumeNotification].map { name in
                center.addObserver(forName: name, object: nil, queue: nil) { [weak self] _ in
                    Self.onMain { self?.volumesDidChange() }
                }
            }
        }
    }

    /// Every change (capacity, loading, the disk list, the choice) is reported
    /// to these callbacks on the main thread.
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
    /// It re-lists the mounted disks with their numbers. Calls arriving during
    /// an existing read coalesce into that in-flight read.
    func refresh() {
        precondition(Thread.isMainThread)
        refreshCapacity(force: true)
    }

    /// Remembers one of `volumes` across launches and shows it at once with
    /// its listed numbers, then reads fresh ones. False for an unlisted disk.
    @discardableResult func selectVolume(identifier: String) -> Bool {
        precondition(Thread.isMainThread)
        guard let volume = volumes.first(where: { $0.id == identifier }) else { return false }
        let newChoice = selectedVolumeIdentifier != identifier
        let newDisk = currentVolume?.id != identifier
        if newChoice {
            selectedVolumeIdentifier = identifier
            defaults?.set(identifier, forKey: Self.volumeSelectionKey)
        }
        if newDisk { snapshot.capacity = volume.capacity; snapshot.error = nil }
        if newChoice || newDisk { publish() }
        if newDisk { refreshCapacity(force: true) }
        return true
    }

    /// A disk was mounted, unmounted or renamed. A visible panel re-reads now
    /// (after any read already running); a hidden one on its next activation.
    private func volumesDidChange() {
        precondition(Thread.isMainThread)
        capacityRequestedAt = nil
        guard isActive else { return }
        if capacityInFlight { pendingVolumeRead = true } else { refreshCapacity(force: true) }
    }

    private func refreshCapacity(force: Bool = false) {
        guard isActive, !capacityInFlight else { return }
        if !force, let date = capacityRequestedAt, clock().timeIntervalSince(date) >= 0,
           clock().timeIntervalSince(date) < Self.capacityCacheDuration { return }
        capacityRequestedAt = clock(); capacityInFlight = true; pendingVolumeRead = false
        snapshot.isLoading = true; publish()
        let listVolumes = readVolumes, readStartup = readCapacity, language = L10n.language
        runWorker { [weak self] in
            let listed = listVolumes(language)
            // The startup reader stands in when the list lacks the startup disk
            // (and is the only source when disk listing is not injected).
            let startup = listed.contains(where: \.isStartup) ? nil : readStartup(language)
            Self.onMain { [weak self] in
                guard let self else { return }
                self.capacityInFlight = false; self.snapshot.isLoading = false
                self.volumes = StorageVolumeCatalog.limited(listed, keeping: self.selectedVolumeIdentifier)
                if let result = self.currentVolume?.capacity ?? startup { self.snapshot.capacity = result; self.snapshot.error = nil }
                else { self.snapshot.error = "Storage capacity is unavailable." }
                self.publish()
                if self.pendingVolumeRead && self.isActive { self.refreshCapacity(force: true) }
            }
        }
    }

    private static func validVolumeIdentifier(_ value: String?) -> String? {
        guard let value, !value.isEmpty, value.utf8.count <= 1024 else { return nil }
        return value
    }

    private func publish() { precondition(Thread.isMainThread); Array(observers.values).forEach { $0() } }
    private static func onMain(_ action: @escaping () -> Void) {
        if Thread.isMainThread { action() } else { DispatchQueue.main.async(execute: action) }
    }

    /// The folder walk's expected device: the startup Data filesystem, where
    /// the scanned home and Applications folders live.
    private static var startupPath: String {
        let dataPath = "/System/Volumes/Data"
        return FileManager.default.fileExists(atPath: dataPath) ? dataPath : "/"
    }

    private static func startupDevice() -> dev_t? {
        var info = stat()
        return stat(startupPath, &info) == 0 ? info.st_dev : nil
    }

    /// The startup disk ("/") as Finder reports it: its container's total and
    /// the capacity available for important usage (free plus purgeable
    /// space), falling back to plain free space. Never a sum of APFS volumes,
    /// which would count their shared free space repeatedly. Worker thread.
    static func readStartupCapacity(date: Date = Date(), language: AppLanguage = .system) -> StorageCapacity? {
        guard let candidate = StorageVolumeCatalog.candidate(at: URL(fileURLWithPath: "/", isDirectory: true),
                                                             mount: nil, session: nil) else { return nil }
        return StorageVolumeCatalog.volumes(from: [candidate], date: date,
                                            startupName: L10n.text("Startup disk", "启动磁盘", language: language)).first?.capacity
    }

    static func fixture() -> StorageController {
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let capacity = StorageCapacity(volumeName: "Macintosh HD", totalBytes: 1_000_000_000_000,
                                       availableBytes: 420_000_000_000, updatedAt: date)!
        func volume(_ id: String, _ name: String, total: Int64, available: Int64) -> StorageVolume {
            StorageVolume(id: id, url: URL(fileURLWithPath: "/Volumes/" + name, isDirectory: true), isStartup: false,
                          isInternal: false, capacity: StorageCapacity(volumeName: name, totalBytes: total,
                                                                       availableBytes: available, updatedAt: date)!)
        }
        let volumes = [StorageVolume(id: "fixture-startup", url: URL(fileURLWithPath: "/", isDirectory: true),
                                     isStartup: true, isInternal: true, capacity: capacity),
                       volume("fixture-backup", "Backup SSD", total: 1_864_600_000_000, available: 1_160_000_000_000),
                       volume("fixture-card", "EF-CARD", total: 59_500_000_000, available: 46_700_000_000)]
        let detail = StorageDetailsSnapshot(categories: [
            StorageCategoryUsage(id: "applications", title: "Applications", bytes: 58_000_000_000, isPartial: false),
            StorageCategoryUsage(id: "documents", title: "Documents", bytes: 21_000_000_000, isPartial: false),
            StorageCategoryUsage(id: "downloads", title: "Downloads", bytes: 6_000_000_000, isPartial: true)
        ], isLoading: false, updatedAt: date, isPartial: true, error: nil)
        let value = StorageController(clock: { date }, readCapacity: { capacity }, readVolumes: { volumes },
                                      scanDetails: { _ in detail },
                                      runWorker: { $0() }, scheduleTimer: { _, _ in FixtureStorageTimer() })
        value.snapshot = StorageSnapshot(capacity: capacity, isLoading: false, error: nil)
        value.volumes = volumes
        value.details = detail; value.capacityRequestedAt = date
        return value
    }

    deinit {
        timer?.invalidate(); detailToken?.cancel()
        volumeEventTokens.forEach { volumeEventCenter?.removeObserver($0) }
    }
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
