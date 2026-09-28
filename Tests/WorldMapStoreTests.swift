import Foundation

enum WorldMapStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ operation: () throws -> Void) -> Bool {
            do { try operation(); return false } catch { return true }
        }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("WorldMapStoreTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        do {
            let directory = root.appendingPathComponent("persistent", isDirectory: true)
            let file = directory.appendingPathComponent("map.json")
            let store = try WorldMapStore(directory: directory)
            check(store.pins.isEmpty && store.viewport == WorldMapViewport(), "A new map starts at the Shenzhen camera without pins")
            check(abs(store.viewport.centerX * 360 - 180 - 114.0579) < 0.000_001
                  && abs(90 - store.viewport.centerY * 180 - 22.5431) < 0.000_001 && store.viewport.zoom == 3,
                  "The default camera opens over Shenzhen at the requested 3× overview")
            check(!fm.fileExists(atPath: file.path), "Loading an empty map does not create an idle persistence write")
            try store.setViewport(WorldMapViewport())
            check(!fm.fileExists(atPath: file.path), "Setting an unchanged viewport does not write")
            let first = try store.addPin(x: 0.24, y: 0.63)
            check(first.x == 0.24 && first.y == 0.63 && store.pins == [first], "Pin coordinates and identity are retained")
            check(abs(first.createdAt.timeIntervalSinceNow) < 5, "New pins record their creation time")
            let wrapped = try store.addPin(x: -1.25, y: 1.1)
            check(wrapped.x == 0.75 && wrapped.y == 1, "Panning across the seam wraps pin X and bounds Y")
            let seam = try store.addPin(x: 2, y: -0.3)
            check(seam.x == 0 && seam.y == 0, "The far horizontal seam has the same position as the near seam")
            try store.setViewport(WorldMapViewport(centerX: 2.75, centerY: -2, zoom: 800))
            check(store.viewport == WorldMapViewport(centerX: 0.75, centerY: 0, zoom: 128), "Viewport persistence wraps and limits coordinates and zoom")
            let reopened = try WorldMapStore(directory: directory)
            check(reopened.pins == store.pins && reopened.viewport == store.viewport, "Pins, IDs, dates and viewport survive app relaunch")
            check(try fm.contentsOfDirectory(atPath: directory.path) == ["map.json"], "Persistence contains no terrain copies, tiles or image caches")
            check(try Data(contentsOf: file).count < 2_000, "Small map state stays compact")
            let saved = try Data(contentsOf: file)
            let modificationDate = try fm.attributesOfItem(atPath: file.path)[.modificationDate] as? Date
            try store.setViewport(store.viewport)
            try store.removePin(id: UUID())
            check(try Data(contentsOf: file) == saved && fm.attributesOfItem(atPath: file.path)[.modificationDate] as? Date == modificationDate,
                  "No-op viewport and unknown pin removal leave archive bytes and modification time unchanged")
            for invalid in [Double.nan, .infinity, -.infinity] {
                check(rejected { _ = try store.addPin(x: invalid, y: 0.5) }, "Nonfinite pin X is rejected")
                check(rejected { _ = try store.addPin(x: 0.5, y: invalid) }, "Nonfinite pin Y is rejected")
                check(rejected { try store.setViewport(WorldMapViewport(centerX: invalid)) }, "Nonfinite viewport X is rejected")
                check(rejected { try store.setViewport(WorldMapViewport(centerY: invalid)) }, "Nonfinite viewport Y is rejected")
                check(rejected { try store.setViewport(WorldMapViewport(zoom: invalid)) }, "Nonfinite zoom is rejected")
            }
            check(try Data(contentsOf: file) == saved && store.pins == reopened.pins && store.viewport == reopened.viewport,
                  "Invalid coordinates cannot mutate either stored or in-memory map state")
            try store.removePin(id: wrapped.id)
            check(store.pins.map(\.id) == [first.id, seam.id], "Removing a pin preserves other pins and their ordering")
            check(try WorldMapStore(directory: directory).pins == store.pins, "Pin deletion persists")
            try store.setViewport(WorldMapViewport(centerX: -0.125, centerY: 3, zoom: 0))
            check(store.viewport == WorldMapViewport(centerX: 0.875, centerY: 1, zoom: 2.1), "Minimum zoom and southern bound are enforced")
            for finiteX in [-Double.leastNonzeroMagnitude, -1e-20, Double.greatestFiniteMagnitude, -Double.greatestFiniteMagnitude] {
                let normalized = try WorldMapViewport(centerX: finiteX).normalized()
                check(normalized.centerX >= 0 && normalized.centerX < 1, "Extreme finite positions still produce a canonical seam coordinate")
            }

            let limitStore = try WorldMapStore(directory: root.appendingPathComponent("limit"))
            for index in 0..<WorldMapStore.maximumPinCount { try limitStore.addPin(x: Double(index) / 128, y: 0.5) }
            let fullPins = limitStore.pins
            check(rejected { _ = try limitStore.addPin(x: 0.5, y: 0.5) }, "The bounded pin count cannot be exceeded")
            check(limitStore.pins == fullPins, "Reaching the limit never silently evicts existing pins")
            try limitStore.removePin(id: fullPins[12].id)
            let replacement = try limitStore.addPin(x: 0.37, y: 0.71)
            check(limitStore.pins.count == 128 && limitStore.pins.last == replacement,
                  "Removing one pin makes room for another without changing other pin identities")
            check(try WorldMapStore(directory: root.appendingPathComponent("limit")).pins == limitStore.pins,
                  "A full pin collection remains reloadable")

            let corruptDirectory = root.appendingPathComponent("corrupt", isDirectory: true)
            try fm.createDirectory(at: corruptDirectory, withIntermediateDirectories: true)
            let corruptFile = corruptDirectory.appendingPathComponent("map.json")
            let canonical = try JSONSerialization.jsonObject(with: Data(contentsOf: file)) as! [String: Any]
            var duplicate = canonical
            duplicate["pins"] = Array(repeating: (canonical["pins"] as! [[String: Any]])[0], count: 2)
            var outside = canonical
            var outsidePins = canonical["pins"] as! [[String: Any]]
            outsidePins[0]["x"] = 1.5
            outside["pins"] = outsidePins
            var invalidZoom = canonical
            invalidZoom["viewport"] = ["centerX": 0.5, "centerY": 0.5, "zoom": 129]
            var oldZoomInNewArchive = canonical
            oldZoomInNewArchive["viewport"] = ["centerX": 0.5, "centerY": 0.5, "zoom": 1]
            var tooMany = canonical
            tooMany["pins"] = (0...128).map { _ in
                var pin = (canonical["pins"] as! [[String: Any]])[0]
                pin["id"] = UUID().uuidString
                return pin
            }
            let badArchives = [Data("not JSON".utf8), Data("{\"version\":99}".utf8), Data("{\"version\":0}".utf8),
                               Data("{\"version\":1,\"pins\":[]}".utf8),
                               try JSONSerialization.data(withJSONObject: duplicate), try JSONSerialization.data(withJSONObject: outside),
                               try JSONSerialization.data(withJSONObject: invalidZoom), try JSONSerialization.data(withJSONObject: oldZoomInNewArchive),
                               try JSONSerialization.data(withJSONObject: tooMany),
                               Data(repeating: 32, count: WorldMapStore.maximumArchiveBytes + 1)]
            for bad in badArchives {
                try bad.write(to: corruptFile)
                check(rejected { _ = try WorldMapStore(directory: corruptDirectory) }, "Malformed, unsupported and unbounded archives are rejected")
                check(try Data(contentsOf: corruptFile) == bad, "Rejecting a saved archive preserves its original contents")
            }

            let legacyDirectory = root.appendingPathComponent("legacy", isDirectory: true)
            try fm.createDirectory(at: legacyDirectory, withIntermediateDirectories: true)
            let legacyFile = legacyDirectory.appendingPathComponent("map.json")
            var legacy = canonical
            legacy["version"] = 1
            for legacyCamera in [["centerX": 0.5, "centerY": 0.5, "zoom": 1.0],
                                 ["centerX": 0.23, "centerY": 0.72, "zoom": 16.0]] {
                legacy["viewport"] = legacyCamera
                let legacyBytes = try JSONSerialization.data(withJSONObject: legacy)
                try legacyBytes.write(to: legacyFile)
                let migrated = try WorldMapStore(directory: legacyDirectory)
                check(migrated.viewport == WorldMapViewport(), "Both old overview and old panned cameras migrate to Shenzhen exactly once")
                check(migrated.pins == store.pins, "Viewport migration preserves every pin's identity, geographic position and creation time")
                let migratedBytes = try Data(contentsOf: legacyFile)
                let migratedObject = try JSONSerialization.jsonObject(with: migratedBytes) as! [String: Any]
                check(migratedObject["version"] as? Int == 4, "Migration writes schema four atomically")
                let customCamera = WorldMapViewport(centerX: 0.32, centerY: 0.58, zoom: 96)
                try migrated.setViewport(customCamera)
                let userBytes = try Data(contentsOf: legacyFile)
                check(try WorldMapStore(directory: legacyDirectory).viewport == customCamera,
                      "A later launch honors the migrated user's own saved viewport")
                check(try Data(contentsOf: legacyFile) == userBytes, "Opening a schema four map does not repeat migration or rewrite its bytes")
            }
            var previous = canonical
            previous["version"] = 2
            for camera in [WorldMapViewport(zoom: 24), WorldMapViewport(centerX: 0.25, centerY: 0.5, zoom: 24)] {
                previous["viewport"] = ["centerX": camera.centerX, "centerY": camera.centerY, "zoom": camera.zoom]
                try JSONSerialization.data(withJSONObject: previous).write(to: legacyFile)
                let migrated = try WorldMapStore(directory: legacyDirectory)
                check(migrated.viewport == (camera == WorldMapViewport(zoom: 24) ? WorldMapViewport() : camera),
                      "Only the previous default camera changes to 3×; custom version two views survive")
                check(migrated.pins == store.pins, "The new default migration preserves pins")
                try migrated.setViewport(WorldMapViewport(zoom: 24))
                check(try WorldMapStore(directory: legacyDirectory).viewport == WorldMapViewport(zoom: 24),
                      "A user can choose 24× after migration without having it reset on every launch")
            }
            previous["version"] = 3
            for camera in [WorldMapViewport(zoom: 72), WorldMapViewport(centerX: 0.25, centerY: 0.5, zoom: 72),
                           WorldMapViewport(zoom: 24), WorldMapViewport()] {
                previous["viewport"] = ["centerX": camera.centerX, "centerY": camera.centerY, "zoom": camera.zoom]
                try JSONSerialization.data(withJSONObject: previous).write(to: legacyFile)
                let migrated = try WorldMapStore(directory: legacyDirectory)
                check(migrated.viewport == (camera == WorldMapViewport(zoom: 72) ? WorldMapViewport() : camera),
                      "Only the old Shenzhen 72× default migrates to 3×; custom saved locations and zoom survive")
                check(migrated.pins == store.pins, "Migrating the 3× default preserves all pins")
                let migratedBytes = try Data(contentsOf: legacyFile)
                _ = try WorldMapStore(directory: legacyDirectory)
                check(try Data(contentsOf: legacyFile) == migratedBytes, "The 3× default migration runs only once")
                try migrated.setViewport(WorldMapViewport(zoom: 72))
                check(try WorldMapStore(directory: legacyDirectory).viewport == WorldMapViewport(zoom: 72),
                      "Explicitly choosing the previous 72× default after migration survives relaunch")
            }
            legacy["viewport"] = ["centerX": 0.5, "centerY": 0.5, "zoom": 17.0]
            let malformedLegacy = try JSONSerialization.data(withJSONObject: legacy)
            try malformedLegacy.write(to: legacyFile)
            check(rejected { _ = try WorldMapStore(directory: legacyDirectory) }, "Migration does not excuse malformed version one viewports")
            check(try Data(contentsOf: legacyFile) == malformedLegacy, "Failed legacy validation preserves the original archive")
            legacy["viewport"] = ["centerX": 0.5, "centerY": 0.5, "zoom": 1.0]
            let validLegacy = try JSONSerialization.data(withJSONObject: legacy)
            try validLegacy.write(to: legacyFile)
            try fm.setAttributes([.posixPermissions: 0o500], ofItemAtPath: legacyDirectory.path)
            let migrationBlocked = rejected { _ = try WorldMapStore(directory: legacyDirectory) }
            try fm.setAttributes([.posixPermissions: 0o700], ofItemAtPath: legacyDirectory.path)
            check(try migrationBlocked && Data(contentsOf: legacyFile) == validLegacy,
                  "An unsuccessful migration leaves the original pins and old archive recoverable")

            let beforeConflict = store.pins
            let beforeViewport = store.viewport
            let externalBytes = Data("External change".utf8)
            try externalBytes.write(to: file)
            check(rejected { try store.removePin(id: first.id) }, "Stale stores cannot overwrite external changes")
            check(try store.pins == beforeConflict && store.viewport == beforeViewport && Data(contentsOf: file) == externalBytes,
                  "Conflict rejection preserves both memory and external data")

            let blockedDirectory = root.appendingPathComponent("blocked", isDirectory: true)
            let blocked = try WorldMapStore(directory: blockedDirectory)
            let blockedFile = blockedDirectory.appendingPathComponent("map.json")
            let kept = try blocked.addPin(x: 0.2, y: 0.4)
            let blockedBytes = try Data(contentsOf: blockedFile)
            try fm.setAttributes([.posixPermissions: 0o500], ofItemAtPath: blockedDirectory.path)
            let failedWrite = rejected { try blocked.setViewport(WorldMapViewport(zoom: 2)) }
            try fm.setAttributes([.posixPermissions: 0o700], ofItemAtPath: blockedDirectory.path)
            check(failedWrite, "Atomic replacement failures are reported")
            check(try blocked.pins == [kept] && blocked.viewport == WorldMapViewport() && Data(contentsOf: blockedFile) == blockedBytes,
                  "A failed write rolls back memory and preserves the previous valid archive")
        } catch { fatalError("Map store fixture failed: \(error)") }
        return count
    }
}
