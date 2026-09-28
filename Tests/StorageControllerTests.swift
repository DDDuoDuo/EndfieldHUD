import Foundation
import Darwin

enum StorageControllerTests {
    static func run() -> Int {
        precondition(Thread.isMainThread)
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        let date = Date(timeIntervalSince1970: 1_700_000_000)
        let capacity = StorageCapacity(volumeName: "Test", totalBytes: 1000, availableBytes: 400, updatedAt: date)!
        check(capacity.usedBytes == 600 && capacity.availableBytes + capacity.usedBytes == capacity.totalBytes,
              "Used and free partition one filesystem's capacity without summing APFS volumes")
        for pair in [(Int64(0), Int64(0)), (-1, 0), (100, -1), (100, 101)] {
            check(StorageCapacity(volumeName: "Test", totalBytes: pair.0, availableBytes: pair.1, updatedAt: date) == nil,
                  "Invalid capacity must remain unavailable rather than fabricating zero used space")
        }
        check(StorageCapacity(volumeName: "Full", totalBytes: Int64.max, availableBytes: 0, updatedAt: date)?.usedBytes == Int64.max,
              "A completely full filesystem is represented without arithmetic overflow")
        let fixture = StorageController.fixture()
        check(fixture.snapshot.capacity != nil && !fixture.snapshot.isLoading && !fixture.isActive,
              "The fixture is deterministic and starts without background work")
        check(!fixture.details.categories.isEmpty && fixture.details.isPartial,
              "Fixture folder data preserves the distinction between a known folder and a complete disk breakdown")

        let driver = StorageTestDriver(date: date)
        var reads = 0, scans = 0, observedMain = true, notifications = 0
        var failCapacity = false
        let detail = StorageDetailsSnapshot(categories: [StorageCategoryUsage(id: "documents", title: "Documents", bytes: 100, isPartial: false)],
                                            isLoading: false, updatedAt: date, isPartial: false, error: nil)
        let controller = StorageController(clock: { driver.date }, readCapacity: {
            reads += 1
            return failCapacity ? nil : StorageCapacity(volumeName: "Test", totalBytes: 1000, availableBytes: 400, updatedAt: driver.date)
        }, scanDetails: { token in
            scans += 1
            var value = detail; value.updatedAt = driver.date
            if token.isCancelled { value.isPartial = true }
            return value
        }, runWorker: driver.enqueue, scheduleTimer: driver.schedule)
        let observation = controller.observe { notifications += 1; observedMain = observedMain && Thread.isMainThread }
        controller.requestDetails()
        check(driver.jobs.isEmpty && !controller.details.isLoading, "Hidden panels cannot begin an explicit scan")
        controller.activate()
        check(controller.isActive && controller.snapshot.isLoading && driver.jobs.count == 1 && driver.liveTimers.count == 1,
              "Activation starts one asynchronous metadata query and one minute timer")
        controller.activate()
        check(driver.jobs.count == 1 && driver.liveTimers.count == 1, "Repeated activation never duplicates work or timers")
        check(driver.liveTimers[0].interval == 60, "Storage capacity samples once a minute, not once per display frame")
        driver.finishOne()
        check(controller.snapshot.capacity?.usedBytes == 600 && !controller.snapshot.isLoading && reads == 1,
              "The worker publishes valid capacity on completion")
        controller.deactivate()
        check(!controller.isActive && driver.liveTimers.isEmpty && driver.jobs.isEmpty,
              "Closing the panel removes its capacity timer")
        driver.advance(30); controller.activate()
        check(driver.jobs.isEmpty && reads == 1, "Reopening within a minute uses the app-lifetime capacity cache")
        driver.advance(31); driver.fireTimers()
        check(driver.jobs.count == 1 && controller.snapshot.isLoading, "A visible stale capacity is refreshed after its cache interval")
        driver.fireTimers()
        check(driver.jobs.count == 1, "Capacity reads cannot overlap while a previous worker is pending")
        driver.finishOne()
        check(reads == 2 && controller.snapshot.capacity?.updatedAt == driver.date, "Refresh advances the reported sample time")
        failCapacity = true; driver.advance(61); driver.fireTimers(); driver.finishOne()
        check(controller.snapshot.error != nil && controller.snapshot.capacity?.usedBytes == 600,
              "An unavailable refresh retains the last valid measurement and marks it stale/error")
        driver.fireTimers()
        check(driver.jobs.isEmpty, "Capacity failures share the cache cooldown instead of retrying continuously")

        controller.requestDetails()
        check(controller.details.isLoading && driver.jobs.count == 1 && scans == 0,
              "Details start only after an explicit action and run asynchronously")
        controller.requestDetails(refresh: true)
        check(driver.jobs.count == 1, "Repeated refresh clicks cannot overlap folder scans")
        driver.finishOne()
        check(scans == 1 && controller.details.categories.first?.bytes == 100 && !controller.details.isLoading,
              "A completed scoped scan publishes the detail snapshot")
        controller.requestDetails(); controller.deactivate(); controller.activate(); controller.requestDetails()
        check(driver.jobs.isEmpty && scans == 1, "Details survive overlay recreation and reuse their fifteen-minute cache")
        driver.advance(899); controller.requestDetails()
        check(driver.jobs.count == 1, "At fourteen minutes a details request refreshes only the stale cheap capacity")
        driver.finishOne()
        check(scans == 1, "Fresh details are not rescanned alongside capacity refresh")
        driver.advance(2); controller.requestDetails()
        check(driver.jobs.count == 1 && controller.details.isLoading, "Expired details are rescanned on an explicit request")
        controller.deactivate()
        check(!controller.details.isLoading && driver.liveTimers.isEmpty, "Hiding cancels the active walk and immediately exits loading state")
        controller.activate(); controller.requestDetails()
        check(driver.jobs.count == 1, "A reopen waits for the cancelled worker instead of overlapping it")
        driver.finishOne()
        check(driver.jobs.count == 1 && controller.details.isLoading && scans == 2,
              "Cancelled results are discarded and a requested replacement starts after cancellation finishes")
        driver.finishOne()
        check(scans == 3 && !controller.details.isLoading && controller.details.updatedAt == driver.date,
              "The replacement publishes only its current result")
        controller.requestDetails(refresh: true)
        check(driver.jobs.count == 1, "Explicit refresh can bypass the fifteen-minute folder cache")
        controller.deactivate(); driver.finishOne()
        check(!controller.isActive && driver.jobs.isEmpty && !controller.details.isLoading,
              "Cancelled hidden work never restarts by itself")
        check(observedMain && notifications > 0, "Every snapshot observer callback is delivered on the main thread")
        controller.removeObserver(observation)
        let observedBefore = notifications
        controller.activate(); controller.deactivate()
        check(notifications == observedBefore, "Observer removal prevents later notifications")

        let scansBeforeRefresh = scans
        controller.refresh()
        check(driver.jobs.isEmpty, "Explicit capacity refresh remains paused while hidden")
        controller.activate(); controller.refresh(); controller.refresh()
        check(driver.jobs.count == 1 && scans == scansBeforeRefresh,
              "Explicit capacity refresh bypasses its cache but coalesces requests without scanning folders")
        driver.finishOne(); controller.deactivate()

        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("EndfieldStorageTests-" + UUID().uuidString, isDirectory: true)
        let folderA = root.appendingPathComponent("A", isDirectory: true)
        let folderB = root.appendingPathComponent("B", isDirectory: true)
        let outside = root.appendingPathComponent("Outside", isDirectory: true)
        do {
            try fm.createDirectory(at: folderA, withIntermediateDirectories: true)
            try fm.createDirectory(at: folderB, withIntermediateDirectories: true)
            try fm.createDirectory(at: outside, withIntermediateDirectories: true)
            defer { try? fm.removeItem(at: root) }
            let fileA = folderA.appendingPathComponent("one.dat")
            let fileB = folderB.appendingPathComponent("two.dat")
            try Data(repeating: 0x41, count: 4096).write(to: fileA)
            try Data(repeating: 0x42, count: 8192).write(to: fileB)
            try Data(repeating: 0x43, count: 16_384).write(to: outside.appendingPathComponent("outside.dat"))
            check(link(fileA.path, folderB.appendingPathComponent("shared.dat").path) == 0, "Test fixture creates an actual hard link")
            try fm.createSymbolicLink(at: folderA.appendingPathComponent("escape"), withDestinationURL: outside)
            try fm.createSymbolicLink(at: folderA.appendingPathComponent("loop"), withDestinationURL: folderA)
            func allocated(_ url: URL) -> Int64 { var info = stat(); precondition(lstat(url.path, &info) == 0); return Int64(info.st_blocks) * 512 }
            let scopes = [StorageFolderScope(id: "a", title: "A", url: folderA), StorageFolderScope(id: "b", title: "B", url: folderB)]
            let scanned = StorageFolderScanner.scan(scopes: scopes, cancellation: StorageScanCancellation(), date: date)
            check(scanned.categories[0].bytes == allocated(fileA) && scanned.categories[1].bytes == allocated(fileB),
                  "Allocated sizes ignore symlink targets and deduplicate hard links across folder categories")
            check(!scanned.isPartial && scanned.error == nil && scanned.updatedAt == date,
                  "A complete bounded walk is labeled complete for these folders only")
            var rootInfo = stat(); precondition(lstat(folderA.path, &rootInfo) == 0)
            let otherVolume = StorageFolderScanner.scan(scopes: scopes, cancellation: StorageScanCancellation(),
                                                        expectedDevice: rootInfo.st_dev &+ 1)
            check(otherVolume.categories.allSatisfy { $0.bytes == nil } && otherVolume.isPartial,
                  "Category roots outside the measured startup volume cannot be included in its folder estimates")
            let missing = StorageFolderScanner.scan(scopes: [StorageFolderScope(id: "missing", title: "Missing", url: root.appendingPathComponent("missing"))], cancellation: StorageScanCancellation())
            check(missing.categories[0].bytes == nil && missing.isPartial && missing.error != nil,
                  "Missing or denied folders remain unavailable instead of being shown as zero bytes")
            let symlinkRoot = StorageFolderScanner.scan(scopes: [StorageFolderScope(id: "link", title: "Link", url: folderA.appendingPathComponent("escape"))], cancellation: StorageScanCancellation())
            check(symlinkRoot.categories[0].bytes == nil && symlinkRoot.isPartial, "Even explicit category roots cannot follow symlinks outside their scope")
            let empty = root.appendingPathComponent("Empty", isDirectory: true)
            try fm.createDirectory(at: empty, withIntermediateDirectories: true)
            let emptyResult = StorageFolderScanner.scan(scopes: [StorageFolderScope(id: "empty", title: "Empty", url: empty)], cancellation: StorageScanCancellation())
            check(emptyResult.categories[0].bytes == 0 && !emptyResult.isPartial, "An accessible empty folder is distinguished from an unavailable folder")
            var limits = StorageScanLimits(); limits.maximumEntries = 0
            let capped = StorageFolderScanner.scan(scopes: scopes, cancellation: StorageScanCancellation(), limits: limits)
            check(capped.isPartial && capped.categories.allSatisfy { $0.bytes == nil }, "Global entry caps prevent unbounded work and label all unvisited folders unavailable")
            limits = StorageScanLimits(); limits.entriesPerFolder = 1
            let folderCapped = StorageFolderScanner.scan(scopes: scopes, cancellation: StorageScanCancellation(), limits: limits)
            check(folderCapped.isPartial && folderCapped.categories.allSatisfy { $0.isPartial }, "Per-folder caps prevent one large folder from consuming every category's budget")
            limits = StorageScanLimits(); limits.maximumSeconds = 0.1
            var uptime = 0.0
            let timed = StorageFolderScanner.scan(scopes: scopes, cancellation: StorageScanCancellation(), limits: limits,
                                                  uptime: { uptime += 0.25; return uptime })
            check(timed.isPartial && timed.categories.allSatisfy { $0.bytes == nil }, "The elapsed-time budget bounds work independently of file count")
            let token = StorageScanCancellation(); token.cancel()
            let cancelled = StorageFolderScanner.scan(scopes: scopes, cancellation: token)
            check(cancelled.isPartial && cancelled.categories.allSatisfy { $0.bytes == nil }, "Cancelled scans stop before opening a directory")
            let nested = folderA.appendingPathComponent("Nested", isDirectory: true)
            try fm.createDirectory(at: nested, withIntermediateDirectories: true)
            try Data(repeating: 0x44, count: 4096).write(to: nested.appendingPathComponent("nested.dat"))
            limits = StorageScanLimits(); limits.maximumDepth = 0
            let shallow = StorageFolderScanner.scan(scopes: [scopes[0]], cancellation: StorageScanCancellation(), limits: limits)
            check(shallow.isPartial && shallow.categories[0].bytes == allocated(fileA), "Depth caps close traversal without counting inaccessible descendants")
        } catch { fatalError("Storage test fixture failed: \(error)") }
        let standard = StorageFolderScanner.standardScopes()
        check(standard.count == 6 && !standard.contains { $0.url.path == "/" || $0.url == fm.homeDirectoryForCurrentUser },
              "Production detail roots are known folders, never the entire disk or home directory")
        return count
    }
}

private final class StorageTestTimer: StorageRefreshTimer {
    let interval: TimeInterval
    let action: () -> Void
    var invalidated = false
    init(interval: TimeInterval, action: @escaping () -> Void) { self.interval = interval; self.action = action }
    func invalidate() { invalidated = true }
}

private final class StorageTestDriver {
    var date: Date
    var jobs: [() -> Void] = []
    var timers: [StorageTestTimer] = []
    var liveTimers: [StorageTestTimer] { timers.filter { !$0.invalidated } }
    init(date: Date) { self.date = date }
    func advance(_ seconds: TimeInterval) { date.addTimeInterval(seconds) }
    func enqueue(_ action: @escaping () -> Void) { jobs.append(action) }
    func finishOne() { precondition(!jobs.isEmpty); jobs.removeFirst()() }
    func schedule(_ interval: TimeInterval, _ action: @escaping () -> Void) -> StorageRefreshTimer {
        let timer = StorageTestTimer(interval: interval, action: action); timers.append(timer); return timer
    }
    func fireTimers() { liveTimers.forEach { $0.action() } }
}
