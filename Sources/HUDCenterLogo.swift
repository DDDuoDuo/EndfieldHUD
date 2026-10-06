import AppKit
import ImageIO

/// The default keeps the exact authored Endfield source sprite. Other choices
/// supply bounded, transparent artwork for the source view's existing glow.
enum HUDCenterLogo: String, Codable, CaseIterable {
    case endfield, rhodesIsland, babel, rhineLab, custom

    var title: String {
        switch self {
        case .endfield: return HUDApplicationIcon.endfield.title
        case .rhodesIsland: return HUDApplicationIcon.rhodesIsland.title
        case .babel: return HUDApplicationIcon.babel.title
        case .rhineLab: return HUDApplicationIcon.rhineLab.title
        case .custom: return L10n.text("Custom", "自定")
        }
    }

    private static var presetImages: [HUDCenterLogo: CGImage] = [:]

    /// Call on the main thread when the selected logo/revision changes. Repeated
    /// requests reuse immutable pixels, including failed custom-file lookups.
    func image(revision: String? = nil, store: HUDCenterLogoStore = .shared) -> CGImage? {
        precondition(Thread.isMainThread)
        if self == .endfield { return nil }
        if self == .custom { return store.image(revision: revision) }
        if let cached = Self.presetImages[self] { return cached }
        let icon: HUDApplicationIcon
        switch self {
        case .rhodesIsland: icon = .rhodesIsland
        case .babel: icon = .babel
        case .rhineLab: icon = .rhineLab
        case .endfield, .custom: return nil
        }
        // The existing template rendering removes the supplied black sheet;
        // application-icon rendering would bake in an unwanted rounded plate.
        let template = icon.image(size: 256, menuBar: true)
        guard let source = template.cgImage(forProposedRect: nil, context: nil, hints: nil),
              let context = CGContext(data: nil, width: source.width, height: source.height,
                bitsPerComponent: 8, bytesPerRow: source.width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        let bounds = CGRect(x: 0, y: 0, width: source.width, height: source.height)
        context.draw(source, in: bounds)
        context.setBlendMode(.sourceIn)
        context.setFillColor(CGColor(gray: 1, alpha: 1)); context.fill(bounds)
        guard let image = context.makeImage() else { return nil }
        // Exactly three fixed 512px entries; this cannot grow with user input.
        Self.presetImages[self] = image
        return image
    }
}

enum HUDCenterLogoError: LocalizedError {
    case invalidImage, imageTooLarge, imageDimensionsTooLarge, persistence(String)

    var errorDescription: String? {
        switch self {
        case .invalidImage:
            return L10n.text("Choose a readable image file.", "请选择可读取的图片文件。")
        case .imageTooLarge:
            return L10n.text("Choose an image no larger than 32 MB.", "请选择不超过 32 MB 的图片。")
        case .imageDimensionsTooLarge:
            return L10n.text("Choose an image no larger than 64 megapixels or 16,384 pixels on either side.", "请选择不超过 6400 万像素、且单边不超过 16,384 像素的图片。")
        case .persistence(let detail):
            return L10n.text("The center logo could not be saved: ", "无法保存中心标志：") + detail
        }
    }
}

/// Owns only custom center-logo assets. Import is an explicit action, with no
/// watchers or idle work. Keep the newly imported revision and the prior saved
/// revision so the caller can finish updating its preferences safely.
final class HUDCenterLogoStore {
    static let maximumEncodedBytes = 32 * 1024 * 1024
    static let maximumPixels = 64_000_000
    static let maximumSourceDimension = 16_384
    static let maximumDimension = 768
    static let shared = HUDCenterLogoStore(directory: applicationDirectory())
    private static let diagnosticDirectory = FileManager.default.temporaryDirectory
        .appendingPathComponent("EndfieldHUD-CenterLogo-\(ProcessInfo.processInfo.processIdentifier)-\(UUID().uuidString)", isDirectory: true)

    private let directory: URL
    private let importLock = NSLock()
    private let cacheLock = NSLock()
    private struct CachedImage { let image: CGImage? }
    private var cache: [String: CachedImage] = [:]
    private var cacheOrder: [String] = []

    init(directory: URL) { self.directory = directory }

    static func applicationDirectory() -> URL {
        if CommandLine.arguments.contains(where: {
            $0 == "--ui-test" || $0.hasSuffix("smoke-test") || $0.hasPrefix("--render-")
        }) { return diagnosticDirectory }
        let support = FileManager.default.urls(for: .applicationSupportDirectory, in: .userDomainMask).first
            ?? FileManager.default.homeDirectoryForCurrentUser.appendingPathComponent("Library/Application Support", isDirectory: true)
        return support.appendingPathComponent("EndfieldCharge/CenterLogo", isDirectory: true)
    }

    /// No user-selected file remains required after this succeeds. Only its
    /// first frame is imported, oriented and downsampled before PNG encoding.
    @discardableResult func importImage(from url: URL, replacing revision: String? = nil) throws -> String {
        // Imports can run on a utility queue. Serialize ownership changes, but
        // never hold the image-cache lock while reading or decoding a source.
        importLock.lock(); defer { importLock.unlock() }
        guard url.isFileURL else { throw HUDCenterLogoError.invalidImage }
        let scoped = url.startAccessingSecurityScopedResource()
        defer { if scoped { url.stopAccessingSecurityScopedResource() } }
        let encoded: Data
        do {
            let values = try url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey])
            guard values.isRegularFile == true else { throw HUDCenterLogoError.invalidImage }
            guard let size = values.fileSize, size > 0 else { throw HUDCenterLogoError.invalidImage }
            guard size <= Self.maximumEncodedBytes else { throw HUDCenterLogoError.imageTooLarge }
            encoded = try Data(contentsOf: url, options: .mappedIfSafe)
            guard encoded.count <= Self.maximumEncodedBytes else { throw HUDCenterLogoError.imageTooLarge }
        } catch let error as HUDCenterLogoError { throw error }
        catch { throw HUDCenterLogoError.invalidImage }
        guard let source = CGImageSourceCreateWithData(encoded as CFData,
            [kCGImageSourceShouldCache: false] as CFDictionary), CGImageSourceGetCount(source) > 0,
              let dimensions = Self.dimensions(source) else { throw HUDCenterLogoError.invalidImage }
        guard dimensions.width <= Self.maximumSourceDimension, dimensions.height <= Self.maximumSourceDimension,
              Int64(dimensions.width) * Int64(dimensions.height) <= Self.maximumPixels else {
            throw HUDCenterLogoError.imageDimensionsTooLarge
        }
        guard let thumbnail = CGImageSourceCreateThumbnailAtIndex(source, 0, [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceCreateThumbnailWithTransform: true,
            kCGImageSourceThumbnailMaxPixelSize: Self.maximumDimension,
            kCGImageSourceShouldCacheImmediately: true
        ] as CFDictionary), thumbnail.width <= Self.maximumDimension, thumbnail.height <= Self.maximumDimension,
              let image = Self.normalizedImage(thumbnail) else {
            throw HUDCenterLogoError.invalidImage
        }
        let png = NSMutableData()
        guard let destination = CGImageDestinationCreateWithData(png, "public.png" as CFString, 1, nil) else {
            throw HUDCenterLogoError.invalidImage
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw HUDCenterLogoError.invalidImage }
        let next = UUID().uuidString
        do {
            try FileManager.default.createDirectory(at: directory, withIntermediateDirectories: true)
            try (png as Data).write(to: fileURL(for: next), options: .atomic)
        } catch { throw HUDCenterLogoError.persistence(error.localizedDescription) }
        let retained = Set([next, revision.flatMap(Self.validRevision)].compactMap { $0 })
        discardUnreferencedImages(keeping: retained)
        remember(image, revision: next)
        return next
    }

    func image(revision: String?) -> CGImage? {
        guard let revision = revision.flatMap(Self.validRevision) else { return nil }
        cacheLock.lock()
        if let cached = cache[revision] {
            touch(revision); cacheLock.unlock(); return cached.image
        }
        cacheLock.unlock()
        let image = load(revision: revision)
        remember(image, revision: revision)
        return image
    }

    private func load(revision: String) -> CGImage? {
        let url = fileURL(for: revision)
        // Managed PNGs fit comfortably below 4 MiB. Check before reading so a
        // replaced/corrupt file cannot cause an unbounded startup decode.
        guard let values = try? url.resourceValues(forKeys: [.isRegularFileKey, .fileSizeKey]),
              values.isRegularFile == true, let bytes = values.fileSize, bytes > 0, bytes <= 4 * 1024 * 1024,
              let data = try? Data(contentsOf: url, options: .mappedIfSafe), data.count <= 4 * 1024 * 1024,
              let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
              let size = Self.dimensions(source), size.width <= Self.maximumDimension, size.height <= Self.maximumDimension else { return nil }
        return CGImageSourceCreateImageAtIndex(source, 0, [kCGImageSourceShouldCacheImmediately: true] as CFDictionary)
    }

    private static func dimensions(_ source: CGImageSource) -> (width: Int, height: Int)? {
        guard let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
              let width = (properties[kCGImagePropertyPixelWidth] as? NSNumber)?.intValue,
              let height = (properties[kCGImagePropertyPixelHeight] as? NSNumber)?.intValue,
              width > 0, height > 0 else { return nil }
        return (width, height)
    }

    private static func normalizedImage(_ image: CGImage) -> CGImage? {
        // Normalize HDR/16-bit inputs to a known display footprint and color
        // space. A 768px RGBA image occupies at most 2.25 MiB decoded.
        guard let context = CGContext(data: nil, width: image.width, height: image.height,
            bitsPerComponent: 8, bytesPerRow: image.width * 4,
            space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { return nil }
        context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
        return context.makeImage()
    }

    private static func validRevision(_ value: String) -> String? { UUID(uuidString: value)?.uuidString }
    private func fileURL(for revision: String) -> URL { directory.appendingPathComponent(revision + ".png") }
    private func touch(_ revision: String) {
        cacheOrder.removeAll { $0 == revision }; cacheOrder.append(revision)
    }
    private func remember(_ image: CGImage?, revision: String) {
        cacheLock.lock(); defer { cacheLock.unlock() }
        cache[revision] = CachedImage(image: image); touch(revision)
        while cacheOrder.count > 2 { cache.removeValue(forKey: cacheOrder.removeFirst()) }
    }
    private func discardUnreferencedImages(keeping retained: Set<String>) {
        cacheLock.lock()
        for key in Array(cache.keys) where !retained.contains(key) { cache.removeValue(forKey: key) }
        cacheOrder.removeAll { !retained.contains($0) }
        cacheLock.unlock()
        guard let files = try? FileManager.default.contentsOfDirectory(at: directory,
            includingPropertiesForKeys: nil, options: [.skipsHiddenFiles]) else { return }
        for url in files where url.pathExtension == "png" {
            let stem = url.deletingPathExtension().lastPathComponent
            guard let key = Self.validRevision(stem), stem == key, !retained.contains(key) else { continue }
            try? FileManager.default.removeItem(at: url)
        }
    }
}
