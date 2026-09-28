import AppKit
import ImageIO

struct UserProfile: Codable, Equatable {
    var name = "Endministrator"
    var tag = "0000"
    var introduction = ""
    let uid: String
    var awakeningDate: Date
    var showsBirthday = false
    var birthdayMonth: Int
    var birthdayDay: Int
    var permissionLevel = 60
    var explorationLevel = 7
    var operatorsCount = 24
    var weaponsCount = 54
    var archivesCount = 325
    var avatarFilename: String?
    var avatarZoom: Double = 1
    var avatarOffsetX: Double = 0
    var avatarOffsetY: Double = 0
    /// nil follows the global HUD color; a saved value is local to both personal cards.
    var themeColorHex: String?
    var backgroundFilename: String?
    var backgroundWidth: Double = 600
    var backgroundOffsetX: Double = 0
    var backgroundOffsetY: Double = 0
    var thumbnailOffsetX: Double = 0
    var thumbnailOffsetY: Double = 0
    var accumulatedWorkSeconds: TimeInterval = 0

    init(awakeningDate: Date = Date(), uid: String = String(UInt64.random(in: 1_000_000_000...9_999_999_999))) {
        self.awakeningDate = awakeningDate
        self.uid = uid
        let birthday = Self.defaultBirthday(for: awakeningDate)
        birthdayMonth = birthday.month
        birthdayDay = birthday.day
    }

    private enum CodingKeys: String, CodingKey {
        case name, tag, introduction, uid, awakeningDate, showsBirthday, birthdayMonth, birthdayDay
        case permissionLevel, explorationLevel
        case operatorsCount, weaponsCount, archivesCount, avatarFilename, backgroundFilename
        case avatarZoom, avatarOffsetX, avatarOffsetY, themeColorHex
        case backgroundWidth, backgroundOffsetX, backgroundOffsetY, thumbnailOffsetX, thumbnailOffsetY
        case accumulatedWorkSeconds
    }

    init(from decoder: Decoder) throws {
        let values = try decoder.container(keyedBy: CodingKeys.self)
        name = try values.decode(String.self, forKey: .name)
        tag = try values.decode(String.self, forKey: .tag)
        uid = try values.decode(String.self, forKey: .uid)
        awakeningDate = try values.decode(Date.self, forKey: .awakeningDate)
        showsBirthday = try values.decodeIfPresent(Bool.self, forKey: .showsBirthday) ?? false
        let birthday = Self.defaultBirthday(for: awakeningDate)
        birthdayMonth = try values.decodeIfPresent(Int.self, forKey: .birthdayMonth) ?? birthday.month
        birthdayDay = try values.decodeIfPresent(Int.self, forKey: .birthdayDay) ?? birthday.day
        permissionLevel = try values.decode(Int.self, forKey: .permissionLevel)
        explorationLevel = try values.decode(Int.self, forKey: .explorationLevel)
        operatorsCount = try values.decode(Int.self, forKey: .operatorsCount)
        weaponsCount = try values.decode(Int.self, forKey: .weaponsCount)
        archivesCount = try values.decode(Int.self, forKey: .archivesCount)
        avatarFilename = try values.decodeIfPresent(String.self, forKey: .avatarFilename)
        backgroundFilename = try values.decodeIfPresent(String.self, forKey: .backgroundFilename)
        accumulatedWorkSeconds = try values.decode(TimeInterval.self, forKey: .accumulatedWorkSeconds)
        // Older personal cards keep their identity and contents; newly added
        // appearance fields begin at the original centered presentation.
        introduction = try values.decodeIfPresent(String.self, forKey: .introduction) ?? ""
        themeColorHex = try values.decodeIfPresent(String.self, forKey: .themeColorHex)
        avatarZoom = try values.decodeIfPresent(Double.self, forKey: .avatarZoom) ?? 1
        avatarOffsetX = try values.decodeIfPresent(Double.self, forKey: .avatarOffsetX) ?? 0
        avatarOffsetY = try values.decodeIfPresent(Double.self, forKey: .avatarOffsetY) ?? 0
        backgroundWidth = try values.decodeIfPresent(Double.self, forKey: .backgroundWidth) ?? 600
        backgroundOffsetX = try values.decodeIfPresent(Double.self, forKey: .backgroundOffsetX) ?? 0
        backgroundOffsetY = try values.decodeIfPresent(Double.self, forKey: .backgroundOffsetY) ?? 0
        thumbnailOffsetX = try values.decodeIfPresent(Double.self, forKey: .thumbnailOffsetX) ?? 0
        thumbnailOffsetY = try values.decodeIfPresent(Double.self, forKey: .thumbnailOffsetY) ?? 0
    }

    fileprivate func normalizedEditableValues() -> UserProfile {
        var value = self
        value.name = String(name.trimmingCharacters(in: .whitespacesAndNewlines).prefix(20))
        value.tag = String(tag.trimmingCharacters(in: .whitespacesAndNewlines).prefix(10))
        value.introduction = String(introduction.prefix(150))
        value.permissionLevel = min(60, max(1, permissionLevel))
        value.explorationLevel = min(7, max(1, explorationLevel))
        value.birthdayMonth = min(12, max(1, birthdayMonth))
        value.birthdayDay = min(Self.maximumBirthdayDay(in: value.birthdayMonth), max(1, birthdayDay))
        value.themeColorHex = themeColorHex.flatMap(Self.normalizedThemeHex)
        value.avatarZoom = avatarZoom.isFinite ? min(20, max(1, avatarZoom)) : 1
        value.avatarOffsetX = avatarOffsetX.isFinite ? min(1, max(-1, avatarOffsetX)) : 0
        value.avatarOffsetY = avatarOffsetY.isFinite ? min(1, max(-1, avatarOffsetY)) : 0
        value.backgroundWidth = backgroundWidth.isFinite ? min(900, max(400, backgroundWidth)) : 600
        value.backgroundOffsetX = backgroundOffsetX.isFinite ? min(400, max(-400, backgroundOffsetX)) : 0
        value.backgroundOffsetY = backgroundOffsetY.isFinite ? min(250, max(-250, backgroundOffsetY)) : 0
        value.thumbnailOffsetX = thumbnailOffsetX.isFinite ? min(1, max(-1, thumbnailOffsetX)) : 0
        value.thumbnailOffsetY = thumbnailOffsetY.isFinite ? min(1, max(-1, thumbnailOffsetY)) : 0
        return value
    }

    func resolvedAccent(fallback: NSColor) -> NSColor {
        guard let hex = themeColorHex.flatMap(Self.normalizedThemeHex), let value = UInt32(hex, radix: 16) else { return fallback }
        return NSColor(srgbRed: CGFloat((value >> 16) & 255) / 255,
                       green: CGFloat((value >> 8) & 255) / 255,
                       blue: CGFloat(value & 255) / 255, alpha: 1)
    }

    private static func normalizedThemeHex(_ input: String) -> String? {
        let value = input.trimmingCharacters(in: .whitespacesAndNewlines).replacingOccurrences(of: "#", with: "").uppercased()
        guard value.count == 6, value.unicodeScalars.allSatisfy({ CharacterSet(charactersIn: "0123456789ABCDEF").contains($0) }) else { return nil }
        return value
    }

    /// A birthday is an annual month/day, so February 29 is valid independently
    /// of the current year. No birth year is collected or synthesized.
    static func maximumBirthdayDay(in month: Int) -> Int {
        let days = [31, 29, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31]
        return days[min(12, max(1, month)) - 1]
    }

    private static func defaultBirthday(for date: Date) -> (month: Int, day: Int) {
        guard date.timeIntervalSinceReferenceDate.isFinite else { return (1, 1) }
        let parts = Calendar(identifier: .gregorian).dateComponents([.month, .day], from: date)
        return (min(12, max(1, parts.month ?? 1)), min(31, max(1, parts.day ?? 1)))
    }
}

enum UserProfileImageKind { case avatar, background }

enum UserProfileStoreError: LocalizedError {
    case invalidName, invalidTag, invalidValue, invalidImage, imageTooLarge, imageDimensionsTooLarge
    case invalidRecord, newerVersion, changedOnDisk, persistence(String)

    var errorDescription: String? {
        switch self {
        case .invalidName: return L10n.text("Enter a name of 1–20 characters without line breaks.", "请输入 1–20 个字符的名称，不含换行。")
        case .invalidTag: return L10n.text("Enter a tag of 1–10 characters without spaces or #.", "请输入 1–10 个字符的编号，不含空格或 #。")
        case .invalidValue: return L10n.text("Enter a number within the allowed range.", "请输入允许范围内的数字。")
        case .invalidImage: return L10n.text("Choose a readable image file.", "请选择可读取的图片文件。")
        case .imageTooLarge: return L10n.text("Choose an image smaller than 128 MB.", "请选择小于 128 MB 的图片。")
        case .imageDimensionsTooLarge: return L10n.text("Choose a portrait no larger than 128 megapixels or 32,768 pixels on either side.", "请选择不超过 1.28 亿像素、且单边不超过 32,768 像素的头像。")
        case .invalidRecord: return L10n.text("The personal card could not be read. The saved data has been preserved.", "无法读取个人名片，已保留原始数据。")
        case .newerVersion: return L10n.text("This personal card was saved by a newer version of EndfieldHUD.", "此个人名片由较新版本的 EndfieldHUD 保存。")
        case .changedOnDisk: return L10n.text("The personal card changed in another app instance. Restart before editing.", "个人名片已由另一个应用实例更改，请重启后编辑。")
        case .persistence(let detail): return L10n.text("The personal card could not be saved: ", "无法保存个人名片：") + detail
        }
    }
}

/// An atomic metadata file and two owned image assets. Portraits retain their
/// source pixels with lazy decoding; only the visible crop is rasterized by the
/// portrait renderer. Backgrounds remain bounded thumbnails.
final class UserProfileStore {
    private(set) var profile: UserProfile
    private let fileURL: URL
    private let imagesDirectory: URL
    private var persistedData: Data?
    private var observers: [UUID: () -> Void] = [:]
    private var imageCache: [String: NSImage] = [:]
    private var imageOrientationCache: [String: Int32] = [:]
    private let fileManager = FileManager.default
    private struct Archive: Codable { let version: Int; let profile: UserProfile }
    private struct Version: Decodable { let version: Int }
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldHUD-Profile-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    static func applicationDirectory() -> URL {
        if CommandLine.arguments.contains(where: {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }) { return diagnosticDirectory }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/Profile", isDirectory: true)
    }

    init(directory: URL, now: Date = Date()) throws {
        fileURL = directory.appendingPathComponent("profile.json")
        imagesDirectory = directory.appendingPathComponent("Images", isDirectory: true)
        // The existing app-data directory is the earliest local evidence of use
        // for upgrades. It is not represented as an exact installer timestamp.
        let existingAppDirectory = directory.deletingLastPathComponent()
        let priorUse = existingAppDirectory.lastPathComponent == "EndfieldCharge"
            ? (try? existingAppDirectory.resourceValues(forKeys: [.creationDateKey]))?.creationDate : nil
        profile = UserProfile(awakeningDate: priorUse.map { min(now, $0) } ?? now)
        do {
            try fileManager.createDirectory(at: directory, withIntermediateDirectories: true)
            if fileManager.fileExists(atPath: fileURL.path) {
                let data = try Data(contentsOf: fileURL)
                profile = try Self.decode(data)
                persistedData = data
            } else {
                // Identity is committed on first app use, even if the user never
                // opens the personal-card screen during that launch.
                try commit(profile)
            }
        } catch let error as UserProfileStoreError { throw error }
        catch { throw UserProfileStoreError.persistence(error.localizedDescription) }
    }

    @discardableResult func observe(_ callback: @escaping () -> Void) -> UUID {
        precondition(Thread.isMainThread)
        let id = UUID(); observers[id] = callback; return id
    }
    func removeObserver(_ id: UUID) { precondition(Thread.isMainThread); observers.removeValue(forKey: id) }

    func update(_ mutation: (inout UserProfile) -> Void) throws {
        precondition(Thread.isMainThread)
        var next = profile
        mutation(&next)
        next = next.normalizedEditableValues()
        try Self.validate(next)
        try commit(next)
    }

    /// An absolute cumulative total makes retrying a failed save idempotent.
    func setWorkSeconds(_ seconds: TimeInterval) throws {
        guard seconds.isFinite, seconds >= profile.accumulatedWorkSeconds else { throw UserProfileStoreError.invalidValue }
        try update { $0.accumulatedWorkSeconds = seconds }
    }

    func importImage(from url: URL, kind: UserProfileImageKind, preserveAvatarCrop: Bool = false) throws {
        precondition(Thread.isMainThread)
        guard url.isFileURL else { throw UserProfileStoreError.invalidImage }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let resources = try url.resourceValues(forKeys: [.fileSizeKey, .isRegularFileKey])
        guard resources.isRegularFile == true else { throw UserProfileStoreError.invalidImage }
        if let size = resources.fileSize, size > 128 * 1024 * 1024 { throw UserProfileStoreError.imageTooLarge }
        guard let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
              CGImageSourceGetCount(source) > 0 else { throw UserProfileStoreError.invalidImage }
        if kind == .avatar, !Self.validPortraitDimensions(source) { throw UserProfileStoreError.imageDimensionsTooLarge }
        // A small decode verifies a portrait without turning its entire source
        // into a permanently reduced bitmap. Backgrounds still use their
        // existing bounded representation.
        let options: [CFString: Any] = [kCGImageSourceCreateThumbnailFromImageAlways: true,
                                       kCGImageSourceCreateThumbnailWithTransform: true,
                                       kCGImageSourceThumbnailMaxPixelSize: kind == .avatar ? 256 : 2048,
                                       kCGImageSourceShouldCacheImmediately: true]
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0, options as CFDictionary) else {
            throw UserProfileStoreError.invalidImage
        }
        let encoded: Data
        if kind == .avatar {
            // Own the original encoded file, including its EXIF orientation.
            // The selected file may then be moved or removed by the user.
            encoded = try Data(contentsOf: url, options: .mappedIfSafe)
            guard encoded.count <= 128 * 1024 * 1024 else { throw UserProfileStoreError.imageTooLarge }
        } else {
            let data = NSMutableData()
            guard let destination = CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil) else {
                throw UserProfileStoreError.invalidImage
            }
            CGImageDestinationAddImage(destination, image, nil)
            guard CGImageDestinationFinalize(destination) else { throw UserProfileStoreError.invalidImage }
            encoded = data as Data
        }
        let name = UUID().uuidString + (kind == .avatar ? ".image" : ".png")
        let managedURL = imagesDirectory.appendingPathComponent(name)
        do {
            try fileManager.createDirectory(at: imagesDirectory, withIntermediateDirectories: true)
            try encoded.write(to: managedURL, options: .atomic)
            // Cache before notifying observers, so the just-imported image is
            // never decoded a second time during the commit's synchronous redraw.
            // Load portraits from the managed file, not the external source,
            // because their CGImage data provider is intentionally lazy.
            if kind == .avatar {
                guard let loaded = Self.loadImage(at: managedURL, kind: kind) else { throw UserProfileStoreError.invalidImage }
                imageCache[name] = loaded.image
                imageOrientationCache[name] = loaded.orientation
            } else {
                imageCache[name] = NSImage(cgImage: image, size: .zero)
                imageOrientationCache[name] = 1
            }
            try update {
                if kind == .avatar {
                    $0.avatarFilename = name
                    if !preserveAvatarCrop {
                        $0.avatarZoom = 1
                        $0.avatarOffsetX = 0
                        $0.avatarOffsetY = 0
                    }
                } else { $0.backgroundFilename = name }
            }
        } catch {
            imageCache.removeValue(forKey: name)
            imageOrientationCache.removeValue(forKey: name)
            try? fileManager.removeItem(at: managedURL)
            throw error
        }
    }

    func removeImage(kind: UserProfileImageKind) throws {
        try update {
            if kind == .avatar {
                $0.avatarFilename = nil
                $0.avatarZoom = 1
                $0.avatarOffsetX = 0
                $0.avatarOffsetY = 0
            } else {
                $0.backgroundFilename = nil
                $0.backgroundWidth = 600
                $0.backgroundOffsetX = 0
                $0.backgroundOffsetY = 0
                $0.thumbnailOffsetX = 0
                $0.thumbnailOffsetY = 0
            }
        }
    }

    func image(for kind: UserProfileImageKind) -> NSImage? {
        let name = kind == .avatar ? profile.avatarFilename : profile.backgroundFilename
        guard let name, Self.validImageName(name) else { return nil }
        if let image = imageCache[name] { return image }
        guard let loaded = Self.loadImage(at: imagesDirectory.appendingPathComponent(name), kind: kind) else { return nil }
        imageCache[name] = loaded.image
        imageOrientationCache[name] = loaded.orientation
        return loaded.image
    }

    /// Applied by the crop renderer before framing; backgrounds are already
    /// normalized on import. Keeping this separate avoids a full-size rotated
    /// bitmap merely to display a small high-resolution portrait crop.
    func imageOrientation(for kind: UserProfileImageKind) -> Int32 {
        guard kind == .avatar, let name = profile.avatarFilename else { return 1 }
        _ = image(for: kind)
        return imageOrientationCache[name] ?? 1
    }

    private static func loadImage(at url: URL, kind: UserProfileImageKind) -> (image: NSImage, orientation: Int32)? {
        guard let source = CGImageSourceCreateWithURL(url as CFURL,
            [kCGImageSourceShouldCache: false] as CFDictionary) else { return nil }
        if kind == .avatar {
            guard validPortraitDimensions(source),
                  let image = CGImageSourceCreateImageAtIndex(source, 0,
                    [kCGImageSourceShouldCache: false, kCGImageSourceShouldCacheImmediately: false] as CFDictionary) else { return nil }
            let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any]
            let orientation = (properties?[kCGImagePropertyOrientation] as? NSNumber)?.int32Value ?? 1
            return (NSImage(cgImage: image, size: .zero), (1...8).contains(orientation) ? orientation : 1)
        }
        guard let image = CGImageSourceCreateThumbnailAtIndex(source, 0,
            [kCGImageSourceCreateThumbnailFromImageAlways: true,
             kCGImageSourceCreateThumbnailWithTransform: true,
             kCGImageSourceThumbnailMaxPixelSize: 2048,
             kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
        return (NSImage(cgImage: image, size: .zero), 1)
    }

    private static func validPortraitDimensions(_ source: CGImageSource) -> Bool {
        guard let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = properties[kCGImagePropertyPixelWidth] as? Int,
              let height = properties[kCGImagePropertyPixelHeight] as? Int,
              (1...32_768).contains(width), (1...32_768).contains(height) else { return false }
        return width * height <= 128_000_000
    }

    private func commit(_ next: UserProfile) throws {
        try Self.validate(next)
        let old = profile
        do {
            let current = fileManager.fileExists(atPath: fileURL.path) ? try Data(contentsOf: fileURL) : nil
            guard current == persistedData else { throw UserProfileStoreError.changedOnDisk }
            if persistedData != nil && next == profile { return }
            let encoder = JSONEncoder(); encoder.outputFormatting = [.sortedKeys]
            let data = try encoder.encode(Archive(version: 1, profile: next))
            try data.write(to: fileURL, options: .atomic)
            persistedData = data
            profile = next
            for name in [old.avatarFilename, old.backgroundFilename].compactMap({ $0 })
                where name != next.avatarFilename && name != next.backgroundFilename {
                imageCache.removeValue(forKey: name)
                imageOrientationCache.removeValue(forKey: name)
                try? fileManager.removeItem(at: imagesDirectory.appendingPathComponent(name))
            }
            Array(observers.values).forEach { $0() }
        } catch let error as UserProfileStoreError { throw error }
        catch { throw UserProfileStoreError.persistence(error.localizedDescription) }
    }

    private static func validImageName(_ name: String) -> Bool {
        let suffix = name.hasSuffix(".png") ? ".png" : ".image"
        return name.hasSuffix(suffix) && name.count == 36 + suffix.count
            && UUID(uuidString: String(name.dropLast(suffix.count))) != nil
    }

    private static func validate(_ profile: UserProfile) throws {
        guard !profile.name.isEmpty, profile.name.count <= 20,
              profile.name.rangeOfCharacter(from: .controlCharacters) == nil else { throw UserProfileStoreError.invalidName }
        guard !profile.tag.isEmpty, profile.tag.count <= 10,
              profile.tag.rangeOfCharacter(from: .controlCharacters.union(.whitespacesAndNewlines).union(CharacterSet(charactersIn: "#"))) == nil else {
            throw UserProfileStoreError.invalidTag
        }
        guard profile.introduction.count <= 150,
              (1...60).contains(profile.permissionLevel), (1...7).contains(profile.explorationLevel),
              (1...12).contains(profile.birthdayMonth),
              (1...UserProfile.maximumBirthdayDay(in: profile.birthdayMonth)).contains(profile.birthdayDay),
              (1...20).contains(profile.avatarZoom),
              (-1...1).contains(profile.avatarOffsetX), (-1...1).contains(profile.avatarOffsetY),
              (400...900).contains(profile.backgroundWidth),
              (-400...400).contains(profile.backgroundOffsetX), (-250...250).contains(profile.backgroundOffsetY),
              (-1...1).contains(profile.thumbnailOffsetX), (-1...1).contains(profile.thumbnailOffsetY),
              profile.operatorsCount >= 0, profile.weaponsCount >= 0, profile.archivesCount >= 0,
              profile.accumulatedWorkSeconds.isFinite, profile.accumulatedWorkSeconds >= 0 else { throw UserProfileStoreError.invalidValue }
        guard profile.uid.count == 10, profile.uid.allSatisfy({ $0.isASCII && $0.isNumber }),
              profile.awakeningDate.timeIntervalSinceReferenceDate.isFinite,
              [profile.avatarFilename, profile.backgroundFilename].compactMap({ $0 }).allSatisfy(validImageName) else {
            throw UserProfileStoreError.invalidRecord
        }
    }

    private static func decode(_ data: Data) throws -> UserProfile {
        let decoder = JSONDecoder()
        guard let version = try? decoder.decode(Version.self, from: data).version else { throw UserProfileStoreError.invalidRecord }
        guard version <= 1 else { throw UserProfileStoreError.newerVersion }
        guard version == 1, let archive = try? decoder.decode(Archive.self, from: data) else { throw UserProfileStoreError.invalidRecord }
        let profile = archive.profile.normalizedEditableValues()
        do { try validate(profile) } catch { throw UserProfileStoreError.invalidRecord }
        return profile
    }
}
