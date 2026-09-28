import AppKit
import ImageIO

enum UserProfileStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func rejected(_ action: () throws -> Void) -> Bool {
            do { try action(); return false } catch { return true }
        }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-ProfileTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            let date = Date(timeIntervalSince1970: 1_800_000_000)
            let directory = root.appendingPathComponent("Profile")
            let store = try UserProfileStore(directory: directory, now: date)
            let original = store.profile
            check(original.name == "Endministrator" && original.tag == "0000"
                  && original.permissionLevel == 60 && original.explorationLevel == 7
                  && original.operatorsCount == 24 && original.weaponsCount == 54 && original.archivesCount == 325,
                  "A new personal card has the requested identity, levels, and counters")
            check(original.awakeningDate == date && original.uid.count == 10
                  && original.uid.allSatisfy({ $0.isNumber }) && original.avatarFilename == nil
                  && original.backgroundFilename == nil && original.accumulatedWorkSeconds == 0,
                  "First use establishes a stable numeric UID and awakening date, preserving the default avatar")
            check(original.introduction.isEmpty && original.backgroundWidth == 600
                  && original.backgroundOffsetX == 0 && original.backgroundOffsetY == 0
                  && original.thumbnailOffsetX == 0 && original.thumbnailOffsetY == 0,
                  "New appearance fields begin with an empty introduction and centered background crops")
            check(original.avatarZoom == 1 && original.avatarOffsetX == 0 && original.avatarOffsetY == 0,
                  "New portraits start at their centered aspect-fill presentation without extra zoom")
            let initialBirthday = Calendar(identifier: .gregorian).dateComponents([.month, .day], from: date)
            check(!original.showsBirthday && original.birthdayMonth == initialBirthday.month
                  && original.birthdayDay == initialBirthday.day,
                  "New cards initially show awakening day and seed an editable month/day without a birth year")
            let untouchedReload = try UserProfileStore(directory: directory, now: date.addingTimeInterval(86_400))
            check(untouchedReload.profile == original, "Identity is persisted immediately without opening or editing the card")
            var changes = 0
            let token = store.observe { changes += 1 }
            try store.update {
                $0.name = "  测试管理员  "; $0.tag = "0101"; $0.permissionLevel = 1; $0.explorationLevel = 1
                $0.operatorsCount = 0; $0.weaponsCount = 400; $0.archivesCount = Int.max
            }
            check(store.profile.name == "测试管理员" && store.profile.uid == original.uid
                  && store.profile.awakeningDate == original.awakeningDate && changes == 1,
                  "A valid edit trims the name and preserves identity while notifying once")
            let edited = store.profile
            let mutations: [(inout UserProfile) -> Void] = [
                { $0.name = " " }, { $0.name = "a\nb" },
                { $0.tag = "#001" }, { $0.tag = "has space" }, { $0.tag = "" },
                { $0.operatorsCount = -1 }, { $0.weaponsCount = -1 }, { $0.archivesCount = -1 },
                { $0.accumulatedWorkSeconds = -.infinity }, { $0.accumulatedWorkSeconds = .nan },
                { $0.avatarFilename = "../../other.png" }
            ]
            for mutation in mutations {
                check(rejected { try store.update(mutation) } && store.profile == edited && changes == 1,
                      "Invalid edits preserve the prior profile and do not publish a change")
            }
            try store.setWorkSeconds(3723.25)
            try store.setWorkSeconds(3723.25)
            check(store.profile.accumulatedWorkSeconds == 3723.25 && changes == 2,
                  "Repeated absolute Work Mode checkpoints are idempotent and publish only the first change")
            check(rejected { try store.setWorkSeconds(1) } && rejected { try store.setWorkSeconds(.infinity) },
                  "Cumulative Work Mode time cannot decrease or become nonfinite")
            let saved = try UserProfileStore(directory: directory)
            check(saved.profile == store.profile, "All edited numbers, identity, and cumulative time survive reopening")
            check(original.themeColorHex == nil && original.resolvedAccent(fallback: .cyan).isEqual(NSColor.cyan)
                  && original.resolvedAccent(fallback: .magenta).isEqual(NSColor.magenta), "A new card follows the current HUD color rather than copying its initial value")
            try store.update { $0.themeColorHex = "#a8e58b"; $0.avatarZoom = 20 }
            check(store.profile.themeColorHex == "A8E58B" && store.profile.avatarZoom == 20,
                  "Card colors normalize and the requested20x portrait limit is retained")
            let themeReload = try UserProfileStore(directory: directory)
            let local = themeReload.profile.resolvedAccent(fallback: .magenta).usingColorSpace(.sRGB)!
            check(themeReload.profile.themeColorHex == "A8E58B" && abs(local.redComponent - 168.0/255) < 0.001
                  && abs(local.greenComponent - 229.0/255) < 0.001 && abs(local.blueComponent - 139.0/255) < 0.001,
                  "A personal-card color survives relaunch and remains independent of global theme changes")
            try store.update { $0.themeColorHex = "invalid" }
            check(store.profile.themeColorHex == nil && store.profile.resolvedAccent(fallback: .cyan).isEqual(NSColor.cyan),
                  "Malformed colors safely return to following the HUD without changing identity")
            let longName = String(repeating: "管理员👍🏽", count: 10)
            let longTag = "000012345678900"
            let longIntroduction = String(repeating: "欢迎👩‍💻", count: 100)
            try store.update {
                $0.name = longName; $0.tag = longTag; $0.introduction = longIntroduction
                $0.permissionLevel = 99; $0.explorationLevel = -100
                $0.avatarZoom = 40; $0.avatarOffsetX = -2; $0.avatarOffsetY = 2
                $0.backgroundWidth = 1200; $0.backgroundOffsetX = -999; $0.backgroundOffsetY = 999
                $0.thumbnailOffsetX = -2; $0.thumbnailOffsetY = 2
            }
            check(store.profile.name == String(longName.prefix(20)) && store.profile.tag == String(longTag.prefix(10))
                  && store.profile.introduction == String(longIntroduction.prefix(150))
                  && store.profile.name.count == 20 && store.profile.introduction.count == 150,
                  "Long editable text truncates silently at grapheme boundaries while preserving leading tag zeros")
            check(store.profile.permissionLevel == 60 && store.profile.explorationLevel == 1
                  && store.profile.backgroundWidth == 900 && store.profile.backgroundOffsetX == -400
                  && store.profile.backgroundOffsetY == 250 && store.profile.thumbnailOffsetX == -1
                  && store.profile.thumbnailOffsetY == 1,
                  "Levels and both independent background geometries clamp to their supported ranges")
            check(store.profile.avatarZoom == 20 && store.profile.avatarOffsetX == -1 && store.profile.avatarOffsetY == 1,
                  "Portrait zoom and crop offsets clamp before rendering or persistence")
            let croppedReload = try UserProfileStore(directory: directory)
            check(croppedReload.profile == store.profile && croppedReload.profile.uid == original.uid
                  && croppedReload.profile.awakeningDate == original.awakeningDate,
                  "Introduction and appearance edits persist without changing UID or awakening date")
            try store.update {
                $0.permissionLevel = Int.min; $0.explorationLevel = Int.max
                $0.avatarZoom = -1; $0.avatarOffsetX = 0.25; $0.avatarOffsetY = -0.75
                $0.backgroundWidth = 1; $0.backgroundOffsetX = 999; $0.backgroundOffsetY = -999
                $0.thumbnailOffsetX = 0.75; $0.thumbnailOffsetY = -0.5
            }
            check(store.profile.permissionLevel == 1 && store.profile.explorationLevel == 7
                  && store.profile.backgroundWidth == 400 && store.profile.backgroundOffsetX == 400
                  && store.profile.backgroundOffsetY == -250 && store.profile.thumbnailOffsetX == 0.75
                  && store.profile.thumbnailOffsetY == -0.5,
                  "Opposite range boundaries clamp while valid fractional thumbnail crops remain unchanged")
            check(store.profile.avatarZoom == 1 && store.profile.avatarOffsetX == 0.25 && store.profile.avatarOffsetY == -0.75,
                  "Portrait zoom cannot expose outside the image and valid fractional crop offsets remain unchanged")
            try store.update {
                $0.avatarZoom = .nan; $0.avatarOffsetX = .infinity; $0.avatarOffsetY = -.infinity
                $0.backgroundWidth = .nan; $0.backgroundOffsetX = .infinity; $0.backgroundOffsetY = -.infinity
                $0.thumbnailOffsetX = .nan; $0.thumbnailOffsetY = .infinity
            }
            check(store.profile.backgroundWidth == 600 && store.profile.backgroundOffsetX == 0
                  && store.profile.backgroundOffsetY == 0 && store.profile.thumbnailOffsetX == 0
                  && store.profile.thumbnailOffsetY == 0,
                  "Nonfinite appearance inputs restore centered defaults before rendering or serialization")
            check(store.profile.avatarZoom == 1 && store.profile.avatarOffsetX == 0 && store.profile.avatarOffsetY == 0,
                  "Nonfinite portrait geometry restores a finite centered default")

            let oldDirectory = root.appendingPathComponent("OldProfile")
            try FileManager.default.createDirectory(at: oldDirectory, withIntermediateDirectories: true)
            let oldFile = oldDirectory.appendingPathComponent("profile.json")
            var oldDocument = try JSONSerialization.jsonObject(with: Data(contentsOf: directory.appendingPathComponent("profile.json"))) as! [String: Any]
            var oldProfile = oldDocument["profile"] as! [String: Any]
            for key in ["introduction", "backgroundWidth", "backgroundOffsetX", "backgroundOffsetY", "thumbnailOffsetX", "thumbnailOffsetY",
                        "showsBirthday", "birthdayMonth", "birthdayDay", "avatarZoom", "avatarOffsetX", "avatarOffsetY", "themeColorHex"] {
                oldProfile.removeValue(forKey: key)
            }
            oldProfile["name"] = longName; oldProfile["tag"] = longTag
            oldProfile["permissionLevel"] = 100; oldProfile["explorationLevel"] = 0
            oldDocument["profile"] = oldProfile
            let oldData = try JSONSerialization.data(withJSONObject: oldDocument)
            try oldData.write(to: oldFile)
            let oldReload = try UserProfileStore(directory: oldDirectory)
            check(oldReload.profile.uid == original.uid && oldReload.profile.awakeningDate == original.awakeningDate
                  && oldReload.profile.name == String(longName.prefix(20)) && oldReload.profile.tag == String(longTag.prefix(10))
                  && oldReload.profile.permissionLevel == 60 && oldReload.profile.explorationLevel == 1,
                  "Older persisted names and levels migrate to the current limits without replacing identity")
            check(oldReload.profile.introduction.isEmpty && oldReload.profile.backgroundWidth == 600
                  && oldReload.profile.backgroundOffsetX == 0 && oldReload.profile.backgroundOffsetY == 0
                  && oldReload.profile.thumbnailOffsetX == 0 && oldReload.profile.thumbnailOffsetY == 0,
                  "Version-one profiles lacking new optional fields decode with compatible appearance defaults")
            check(oldReload.profile.themeColorHex == nil, "Legacy cards default to following the current HUD accent")
            check(oldReload.profile.avatarZoom == 1 && oldReload.profile.avatarOffsetX == 0 && oldReload.profile.avatarOffsetY == 0,
                  "Legacy profiles missing portrait geometry retain the original centered presentation")
            check(!oldReload.profile.showsBirthday && oldReload.profile.birthdayMonth == original.birthdayMonth
                  && oldReload.profile.birthdayDay == original.birthdayDay,
                  "Legacy cards retain awakening mode and derive stable birthday defaults from their saved date")
            check(try Data(contentsOf: oldFile) == oldData, "Reading a compatible old profile does not rewrite its file as a side effect")
            try oldReload.update { $0.introduction = "Migration saved" }
            let savedMigration = try UserProfileStore(directory: oldDirectory)
            check(savedMigration.profile.introduction == "Migration saved" && savedMigration.profile.uid == original.uid,
                  "The next explicit edit writes the extended profile without losing migrated identity")

            let cropDirectory = root.appendingPathComponent("CropProfile")
            try FileManager.default.createDirectory(at: cropDirectory, withIntermediateDirectories: true)
            var cropDocument = oldDocument
            var cropProfile = oldProfile
            cropProfile["avatarZoom"] = 200; cropProfile["avatarOffsetX"] = -9; cropProfile["avatarOffsetY"] = 9
            cropDocument["profile"] = cropProfile
            try JSONSerialization.data(withJSONObject: cropDocument).write(to: cropDirectory.appendingPathComponent("profile.json"))
            let cropReload = try UserProfileStore(directory: cropDirectory)
            check(cropReload.profile.avatarZoom == 20 && cropReload.profile.avatarOffsetX == -1
                  && cropReload.profile.avatarOffsetY == 1 && cropReload.profile.uid == original.uid,
                  "Persisted out-of-range portrait geometry normalizes without replacing the saved identity")

            let dateDirectory = root.appendingPathComponent("DateProfile")
            let dates = try UserProfileStore(directory: dateDirectory, now: date)
            let editedDate = date.addingTimeInterval(-180 * 86_400)
            let stableUID = dates.profile.uid
            try dates.update {
                $0.awakeningDate = editedDate; $0.showsBirthday = true
                $0.birthdayMonth = 2; $0.birthdayDay = 29; $0.backgroundWidth = 400
            }
            let dateReload = try UserProfileStore(directory: dateDirectory, now: date.addingTimeInterval(366 * 86_400))
            check(dateReload.profile.awakeningDate == editedDate && dateReload.profile.uid == stableUID
                  && dateReload.profile.showsBirthday && dateReload.profile.birthdayMonth == 2
                  && dateReload.profile.birthdayDay == 29,
                  "Both editable dates and the selected display mode survive a later launch without changing the UID")
            check(dateReload.profile.backgroundWidth == 400,
                  "An explicitly saved 400-point background remains 400 despite the new 600-point default")
            try dates.update { $0.showsBirthday = false }
            check(dates.profile.awakeningDate == editedDate && dates.profile.birthdayMonth == 2
                  && dates.profile.birthdayDay == 29,
                  "Switching back to awakening day retains both independent date values")
            try dates.update { $0.birthdayMonth = 4; $0.birthdayDay = 31 }
            check(dates.profile.birthdayMonth == 4 && dates.profile.birthdayDay == 30,
                  "Birthday day clamps to the selected month's actual maximum")
            try dates.update { $0.birthdayMonth = 2; $0.birthdayDay = 99 }
            check(dates.profile.birthdayDay == 29, "Annual birthdays allow February 29 without inventing a year")
            try dates.update { $0.birthdayMonth = Int.max; $0.birthdayDay = Int.max }
            check(dates.profile.birthdayMonth == 12 && dates.profile.birthdayDay == 31,
                  "High birthday values safely clamp without integer overflow")
            try dates.update { $0.birthdayMonth = Int.min; $0.birthdayDay = Int.min }
            check(dates.profile.birthdayMonth == 1 && dates.profile.birthdayDay == 1,
                  "Low birthday values clamp to the first valid month and day")
            let beforeInvalidDate = dates.profile
            check(rejected { try dates.update { $0.awakeningDate = Date(timeIntervalSinceReferenceDate: .nan) } }
                  && dates.profile == beforeInvalidDate,
                  "Nonfinite awakening dates cannot replace the saved date or identity")
            let dateJSON = try JSONSerialization.jsonObject(with: Data(contentsOf: dateDirectory.appendingPathComponent("profile.json"))) as! [String: Any]
            let storedDates = dateJSON["profile"] as! [String: Any]
            check(storedDates["birthdayYear"] == nil && storedDates["birthdayMonth"] as? Int == 1
                  && storedDates["birthdayDay"] as? Int == 1,
                  "Birthday persistence stores only a month and day, with no birth year")

            let sourceURL = root.appendingPathComponent("source.png")
            let sourceData = try imageData(width: 2400, height: 1200)
            try sourceData.write(to: sourceURL)
            try store.update { $0.avatarZoom = 3; $0.avatarOffsetX = 0.8; $0.avatarOffsetY = -0.4 }
            try store.importImage(from: sourceURL, kind: .avatar)
            guard let avatar = store.profile.avatarFilename else { fatalError("Avatar import must retain its managed filename") }
            let avatarURL = directory.appendingPathComponent("Images").appendingPathComponent(avatar)
            let avatarDimensions = dimensions(at: avatarURL)
            check(avatarDimensions?.0 == 2400 && avatarDimensions?.1 == 1200,
                  "Avatar imports retain all source pixels for high zoom instead of irreversibly capping them at 512")
            check(try avatar.hasSuffix(".image") && Data(contentsOf: avatarURL) == sourceData,
                  "Managed portraits preserve the original encoded image byte-for-byte")
            check(store.profile.avatarZoom == 1 && store.profile.avatarOffsetX == 0 && store.profile.avatarOffsetY == 0,
                  "Choosing a new portrait resets zoom and position in the same committed update")
            let cachedAvatar = store.image(for: .avatar)
            check(cachedAvatar != nil && cachedAvatar === store.image(for: .avatar),
                  "Repeated avatar rendering reuses the lazy native-resolution source")
            let cachedPixels = cachedAvatar?.cgImage(forProposedRect: nil, context: nil, hints: nil)
            check(cachedPixels?.width == 2400 && cachedPixels?.height == 1200
                  && store.imageOrientation(for: .avatar) == 1,
                  "The display source keeps native resolution rather than applying another thumbnail cap")
            try store.update { $0.avatarZoom = 2.5; $0.avatarOffsetX = -0.6; $0.avatarOffsetY = 0.3 }
            try store.importImage(from: sourceURL, kind: .background)
            guard let background = store.profile.backgroundFilename else { fatalError("Background import must retain its managed filename") }
            let backgroundURL = directory.appendingPathComponent("Images").appendingPathComponent(background)
            let backgroundDimensions = dimensions(at: backgroundURL)
            check(backgroundDimensions?.0 == 2048 && backgroundDimensions?.1 == 1024,
                  "Background imports preserve a bounded 2048-pixel rendered asset instead of the original image")
            check(store.profile.avatarZoom == 2.5 && store.profile.avatarOffsetX == -0.6 && store.profile.avatarOffsetY == 0.3,
                  "Choosing a background preserves the independently edited portrait crop")
            try FileManager.default.removeItem(at: sourceURL)
            let imagesReload = try UserProfileStore(directory: directory)
            check(imagesReload.image(for: .avatar) != nil && imagesReload.image(for: .background) != nil,
                  "Owned profile images survive removal of the selected source file and a new app launch")
            let reopenedPixels = imagesReload.image(for: .avatar)?.cgImage(forProposedRect: nil, context: nil, hints: nil)
            check(reopenedPixels?.width == 2400 && reopenedPixels?.height == 1200,
                  "Reloading a high-resolution portrait retains its original detail after the source file is deleted")
            check(imagesReload.profile.avatarZoom == 2.5 && imagesReload.profile.avatarOffsetX == -0.6
                  && imagesReload.profile.avatarOffsetY == 0.3,
                  "Custom portrait zoom and position survive reopening alongside their image")

            let orientedDirectory = root.appendingPathComponent("OrientedProfile")
            let orientedStore = try UserProfileStore(directory: orientedDirectory)
            let orientedURL = root.appendingPathComponent("rotated.tiff")
            let orientedData = try imageData(width: 1200, height: 600, type: "public.tiff", orientation: 6)
            try orientedData.write(to: orientedURL)
            try orientedStore.importImage(from: orientedURL, kind: .avatar)
            let orientedPixels = orientedStore.image(for: .avatar)?.cgImage(forProposedRect: nil, context: nil, hints: nil)
            check(orientedPixels?.width == 1200 && orientedPixels?.height == 600
                  && orientedStore.imageOrientation(for: .avatar) == 6,
                  "EXIF orientation is retained separately so the renderer can orient only its visible crop")
            let orientedManaged = orientedDirectory.appendingPathComponent("Images").appendingPathComponent(orientedStore.profile.avatarFilename!)
            check(try Data(contentsOf: orientedManaged) == orientedData,
                  "Rotated portraits preserve source pixels and metadata without a full-size normalization copy")
            let orientedReload = try UserProfileStore(directory: orientedDirectory)
            check(orientedReload.imageOrientation(for: .avatar) == 6
                  && orientedReload.imageOrientation(for: .background) == 1,
                  "Portrait orientation survives reopening while normalized backgrounds use upright orientation")
            try orientedStore.update { $0.avatarZoom = 10; $0.avatarOffsetX = 0.45; $0.avatarOffsetY = -0.35 }
            try orientedStore.importImage(from: orientedURL, kind: .avatar, preserveAvatarCrop: true)
            check(orientedStore.profile.avatarZoom == 10 && orientedStore.profile.avatarOffsetX == 0.45
                  && orientedStore.profile.avatarOffsetY == -0.35
                  && !FileManager.default.fileExists(atPath: orientedManaged.path),
                  "Explicit source restoration preserves the existing crop while removing only the replaced managed asset")

            let legacyImageDirectory = root.appendingPathComponent("LegacyImageProfile")
            let legacyImageStore = try UserProfileStore(directory: legacyImageDirectory)
            let legacyImages = legacyImageDirectory.appendingPathComponent("Images")
            try FileManager.default.createDirectory(at: legacyImages, withIntermediateDirectories: true)
            let legacyImageName = UUID().uuidString + ".png"
            try imageData(width: 512, height: 256).write(to: legacyImages.appendingPathComponent(legacyImageName))
            try legacyImageStore.update { $0.avatarFilename = legacyImageName; $0.avatarZoom = 10 }
            let legacyImageReload = try UserProfileStore(directory: legacyImageDirectory)
            let legacyPixels = legacyImageReload.image(for: .avatar)?.cgImage(forProposedRect: nil, context: nil, hints: nil)
            check(legacyPixels?.width == 512 && legacyPixels?.height == 256
                  && legacyImageReload.profile.avatarZoom == 10,
                  "Existing reduced PNG portraits remain readable without fabricated detail or a crop reset")

            let oversizedURL = root.appendingPathComponent("too-wide.png")
            try imageData(width: 32_769, height: 1).write(to: oversizedURL)
            let beforeOversized = store.profile
            check(rejected { try store.importImage(from: oversizedURL, kind: .avatar) } && store.profile == beforeOversized,
                  "Unbounded source dimensions are rejected without replacing a valid portrait")
            let beforeBadImage = store.profile
            let badURL = root.appendingPathComponent("invalid.png")
            try Data("not an image".utf8).write(to: badURL)
            check(rejected { try store.importImage(from: badURL, kind: .avatar) } && store.profile == beforeBadImage,
                  "An invalid image cannot alter the committed profile")
            try store.update {
                $0.backgroundWidth = 750; $0.backgroundOffsetX = 150; $0.backgroundOffsetY = -60
                $0.thumbnailOffsetX = 0.4; $0.thumbnailOffsetY = -0.3
            }
            try store.removeImage(kind: .avatar)
            check(store.profile.avatarFilename == nil && store.image(for: .avatar) == nil
                  && !FileManager.default.fileExists(atPath: avatarURL.path)
                  && FileManager.default.fileExists(atPath: backgroundURL.path),
                  "Restoring the original avatar removes only the unused avatar asset")
            check(store.profile.avatarZoom == 1 && store.profile.avatarOffsetX == 0 && store.profile.avatarOffsetY == 0,
                  "Restoring the default portrait clears its crop geometry together with its asset reference")
            check(store.profile.backgroundWidth == 750 && store.profile.backgroundOffsetX == 150
                  && store.profile.backgroundOffsetY == -60 && store.profile.thumbnailOffsetX == 0.4
                  && store.profile.thumbnailOffsetY == -0.3,
                  "Avatar restoration does not change the separate full-card background or identity-card crop")
            try store.update { $0.avatarZoom = 1.5; $0.avatarOffsetX = 0.2; $0.avatarOffsetY = -0.4 }
            try store.removeImage(kind: .background)
            check(store.profile.backgroundFilename == nil && store.image(for: .background) == nil
                  && store.profile.backgroundWidth == 600 && store.profile.backgroundOffsetX == 0
                  && store.profile.backgroundOffsetY == 0 && store.profile.thumbnailOffsetX == 0
                  && store.profile.thumbnailOffsetY == 0 && !FileManager.default.fileExists(atPath: backgroundURL.path),
                  "Restoring the background clears its image and both independent crop positions atomically")
            check(store.profile.avatarZoom == 1.5 && store.profile.avatarOffsetX == 0.2 && store.profile.avatarOffsetY == -0.4,
                  "Restoring the background leaves portrait zoom and position unchanged")
            let resetReload = try UserProfileStore(directory: directory)
            check(resetReload.profile == store.profile, "Background restoration persists its geometry together with the removed asset reference")
            store.removeObserver(token)
            let beforeNoObserver = changes
            try store.update { $0.name = "Final name" }
            check(changes == beforeNoObserver, "Removing an observer stops further profile notifications")

            let stale = try UserProfileStore(directory: directory)
            try store.update { $0.tag = "9999" }
            let staleSnapshot = stale.profile
            check(rejected { try stale.update { $0.name = "stale overwrite" } } && stale.profile == staleSnapshot,
                  "A stale app instance cannot overwrite newer edits or mutate its in-memory state after failure")
            let metadataURL = directory.appendingPathComponent("profile.json")
            let currentData = try Data(contentsOf: metadataURL)
            var document = try JSONSerialization.jsonObject(with: currentData) as! [String: Any]
            document["version"] = 99
            let futureData = try JSONSerialization.data(withJSONObject: document)
            try futureData.write(to: metadataURL)
            check(rejected { _ = try UserProfileStore(directory: directory) }, "Future profile schemas are rejected")
            check(try Data(contentsOf: metadataURL) == futureData, "Unsupported profile data is preserved byte-for-byte")
            let corrupt = Data("keep this damaged personal card".utf8)
            try corrupt.write(to: metadataURL)
            check(rejected { _ = try UserProfileStore(directory: directory) }, "Corrupt metadata is surfaced instead of reset")
            check(try Data(contentsOf: metadataURL) == corrupt, "Failed profile reads cannot replace or remove the saved file")

            let legacyDirectory = root.appendingPathComponent("EndfieldCharge")
            try FileManager.default.createDirectory(at: legacyDirectory, withIntermediateDirectories: true)
            let legacyDate = Date(timeIntervalSince1970: 1_600_000_000)
            try FileManager.default.setAttributes([.creationDate: legacyDate], ofItemAtPath: legacyDirectory.path)
            let actualLegacyDate = try legacyDirectory.resourceValues(forKeys: [.creationDateKey]).creationDate
            let migrated = try UserProfileStore(directory: legacyDirectory.appendingPathComponent("Profile"), now: date)
            check(migrated.profile.awakeningDate == actualLegacyDate,
                  "An upgrade uses the existing app-data creation date as its earliest local evidence of first use")
        } catch { fatalError("Personal profile persistence test failed: \(error)") }
        return count
    }

    private static func dimensions(at url: URL) -> (Int, Int)? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL, nil),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int else { return nil }
        return (width, height)
    }

    private static func imageData(width: Int, height: Int, type: String = "public.png", orientation: Int = 1) throws -> Data {
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                                      bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { throw UserProfileStoreError.invalidImage }
        context.setFillColor(NSColor.red.cgColor)
        context.fill(CGRect(x: 0, y: 0, width: width / 2, height: height))
        context.setFillColor(NSColor.blue.cgColor)
        context.fill(CGRect(x: width / 2, y: 0, width: width - width / 2, height: height))
        guard let image = context.makeImage() else { throw UserProfileStoreError.invalidImage }
        let data = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(data, type as CFString, 1, nil) else {
            throw UserProfileStoreError.invalidImage
        }
        CGImageDestinationAddImage(destination, image, [kCGImagePropertyOrientation: orientation] as CFDictionary)
        guard CGImageDestinationFinalize(destination) else { throw UserProfileStoreError.invalidImage }
        return data as Data
    }
}
