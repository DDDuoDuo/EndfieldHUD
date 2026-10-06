import AppKit
import CoreImage
import ImageIO

/// Small, allow-listed derivatives of the supplied PhotoMode package. No source
/// scripts, atlases, scene data or shader binaries are loaded or executed.
enum MediaAssemblyAssetCatalog {
    static let cubeDimension = 32
    static let maximumImageCacheBytes = 6 * 1024 * 1024
    private static let lock = NSLock()
    private struct CachedImage { let key: String; let image: CGImage; let bytes: Int }
    private static var images: [CachedImage] = []
    private static var cubes: [(id: MediaAssemblyFilter, data: Data)] = []
    static func resourceURL(_ relative: String) -> URL? {
        // Callers use only fixed directory names and raw values from enums.
        guard !relative.contains(".."), !relative.hasPrefix("/") else { return nil }
        return HUDResources.url(for: "MediaAssembly/" + relative)
    }
    static var retainedImageBytes: Int { lock.lock(); defer { lock.unlock() }; return images.reduce(0) { $0 + $1.bytes } }
    static var retainedCubeCount: Int { lock.lock(); defer { lock.unlock() }; return cubes.count }
    static func clearCaches() { lock.lock(); images.removeAll(); cubes.removeAll(); lock.unlock() }
    static func filterThumbnail(_ filter: MediaAssemblyFilter) -> CGImage? {
        guard filter != .none else { return nil }; return image("filter-icons/\(filter.rawValue).png", maximumDimension: 168)
    }
    static func stickerThumbnail(_ kind: MediaAssemblyStickerKind) -> CGImage? { stickerImage(kind, maximumDimension: 96) }
    static func stickerSize(_ kind: MediaAssemblyStickerKind) -> CGSize { kind.pixelSize }
    static func stickerImage(_ kind: MediaAssemblyStickerKind, maximumDimension: Int? = nil) -> CGImage? {
        image("stickers/\(kind.rawValue).png", maximumDimension: maximumDimension ?? Int(max(kind.pixelSize.width, kind.pixelSize.height)))
    }
    private static func image(_ relative: String, maximumDimension: Int) -> CGImage? {
        let dimension = min(2048, max(1, maximumDimension)), key = "\(dimension):\(relative)"
        lock.lock()
        if let index = images.firstIndex(where: { $0.key == key }) {
            let cached = images.remove(at: index); images.append(cached); lock.unlock(); return cached.image
        }
        lock.unlock()
        guard let url = resourceURL(relative), let source = CGImageSourceCreateWithURL(url as CFURL, [kCGImageSourceShouldCache: false] as CFDictionary),
              let result = CGImageSourceCreateThumbnailAtIndex(source, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceCreateThumbnailWithTransform: true, kCGImageSourceThumbnailMaxPixelSize: dimension,
                kCGImageSourceShouldCacheImmediately: true] as CFDictionary) else { return nil }
        let cost = result.bytesPerRow * result.height
        lock.lock(); defer { lock.unlock() }
        images.removeAll { $0.key == key }
        while !images.isEmpty && images.reduce(0, { $0 + $1.bytes }) + cost > maximumImageCacheBytes { images.removeFirst() }
        if cost <= maximumImageCacheBytes { images.append(CachedImage(key: key, image: result, bytes: cost)) }
        return result
    }
    /// 32³ RGB8 nodes are a lossless storage reduction of the supplied Float32
    /// CUBE files: every node was verified as exactly Float32(byte / 255).
    /// Core Image consumes red-fastest RGBA Float32, with opaque cube alpha.
    static func cubeData(_ filter: MediaAssemblyFilter) throws -> Data {
        guard filter != .none else { throw MediaAssemblyError.invalidAdjustment }
        lock.lock()
        if let index = cubes.firstIndex(where: { $0.id == filter }) {
            let cached = cubes.remove(at: index); cubes.append(cached); lock.unlock(); return cached.data
        }
        lock.unlock()
        guard let url = resourceURL("luts/\(filter.rawValue).rgb8"), let rgb = try? Data(contentsOf: url),
              rgb.count == cubeDimension * cubeDimension * cubeDimension * 3 else { throw MediaAssemblyError.unavailable }
        var values = [Float](repeating: 1, count: cubeDimension * cubeDimension * cubeDimension * 4)
        rgb.withUnsafeBytes { raw in
            let bytes = raw.bindMemory(to: UInt8.self)
            for index in 0..<(cubeDimension * cubeDimension * cubeDimension) {
                for component in 0..<3 { values[index * 4 + component] = Float(bytes[index * 3 + component]) / 255 }
            }
        }
        let data = values.withUnsafeBytes { Data($0) }
        lock.lock(); defer { lock.unlock() }
        cubes.removeAll { $0.id == filter }; cubes.append((filter, data))
        while cubes.count > 2 { cubes.removeFirst() }
        return data
    }
    static func apply(_ filter: MediaAssemblyFilter, to image: CIImage) throws -> CIImage {
        guard filter != .none else { return image }
        guard let operation = CIFilter(name: "CIColorCubeWithColorSpace") else { throw MediaAssemblyError.unsupported }
        operation.setValue(image, forKey: kCIInputImageKey)
        operation.setValue(cubeDimension, forKey: "inputCubeDimension")
        operation.setValue(try cubeData(filter), forKey: "inputCubeData")
        operation.setValue(CGColorSpace(name: CGColorSpace.sRGB)!, forKey: "inputColorSpace")
        guard let result = operation.outputImage else { throw MediaAssemblyError.exportFailed }
        return result.cropped(to: image.extent)
    }
}
