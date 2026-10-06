import AppKit
import ImageIO

enum HUDCenterLogoTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        let root = FileManager.default.temporaryDirectory.appendingPathComponent("EndfieldHUD-CenterLogoTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? FileManager.default.removeItem(at: root) }
        do {
            let directory = root.appendingPathComponent("CenterLogo", isDirectory: true)
            let store = HUDCenterLogoStore(directory: directory)
            check(HUDCenterLogo.allCases == [.endfield, .rhodesIsland, .babel, .rhineLab, .custom],
                  "Center-logo choices have stable saved values and the authored default first")
            for preset in HUDCenterLogo.allCases {
                let encoded = try JSONEncoder().encode(preset)
                check(try JSONDecoder().decode(HUDCenterLogo.self, from: encoded) == preset,
                      "Each center-logo choice round trips independently of its image data")
                check(!preset.title.isEmpty, "Every logo choice has a readable localized label")
            }
            check(HUDCenterLogo.endfield.image(store: store) == nil && HUDCenterLogo.custom.image(store: store) == nil
                  && !FileManager.default.fileExists(atPath: directory.path),
                  "Default and unconfigured custom artwork do no disk writes and leave the authored logo intact")
            for preset in [HUDCenterLogo.rhodesIsland, .babel, .rhineLab] {
                guard let image = preset.image(store: store) else { fatalError("Bundled logo must resolve") }
                let rep = NSBitmapImageRep(cgImage: image)
                check(image.width <= 768 && image.height <= 768 && preset.image(store: store) === image,
                      "Fixed preset images are bounded and cached")
                check((rep.colorAt(x: 0, y: 0)?.alphaComponent ?? 1) < 0.05,
                      "Faction logos have transparent corners rather than application-icon plates")
                let visible = stride(from: 0, to: rep.pixelsHigh, by: 4).contains { y in
                    stride(from: 0, to: rep.pixelsWide, by: 4).contains { x in
                        guard let color = rep.colorAt(x: x, y: y)?.usingColorSpace(.sRGB) else { return false }
                        return color.alphaComponent > 0.4 && color.redComponent > 0.9
                            && color.greenComponent > 0.9 && color.blueComponent > 0.9
                    }
                }
                check(visible, "Preset silhouettes are white artwork ready for the existing logo glow")
                let presentation = HUDCenterLogoPresentation.image(image)
                check(presentation?.width == 600 && presentation?.height == 130,
                      "Each new center wordmark occupies the authored 300 by 65 slot at 2x")
            }

            // File margins and aspect ratio must not shrink a user's center
            // logo. Generate square, wide and tall fixtures with asymmetric
            // transparent padding and compare their visible presentation.
            var referencePixels: Data?
            for size in [(200, 200), (768, 128), (128, 768)] {
                let context = CGContext(data: nil, width: size.0, height: size.1,
                    bitsPerComponent: 8, bytesPerRow: size.0 * 4,
                    space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                context.setFillColor(NSColor(srgbRed: 0.15, green: 0.8, blue: 0.35, alpha: 1).cgColor)
                context.fill(CGRect(x: 11, y: 23, width: size.0 - 36, height: size.1 - 47))
                let presentation = HUDCenterLogoPresentation.image(context.makeImage()!)!
                check(presentation.width == 600 && presentation.height == 130,
                      "Custom square, wide and tall artwork use the default logo's exact footprint")
                let pixels = presentation.dataProvider!.data! as Data
                if let referencePixels {
                    check(pixels == referencePixels, "Transparent file margins do not change the visible logo size")
                } else { referencePixels = pixels }
                let color = NSBitmapImageRep(cgImage: presentation).colorAt(x: 300, y: 65)!.usingColorSpace(.sRGB)!
                check(color.greenComponent > color.redComponent && color.greenComponent > color.blueComponent,
                      "Logo normalization retains custom artwork colors")
            }
            let transparent = CGContext(data: nil, width: 64, height: 64,
                bitsPerComponent: 8, bytesPerRow: 256, space: CGColorSpaceCreateDeviceRGB(),
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
            check(HUDCenterLogoPresentation.image(transparent.makeImage()!) == nil,
                  "Empty artwork falls back safely without invalid crop geometry")

            try FileManager.default.createDirectory(at: root, withIntermediateDirectories: true)
            let source = root.appendingPathComponent("source.png")
            let sourceData = try png(width: 2400, height: 1200)
            try sourceData.write(to: source)
            let first = try store.importImage(from: source)
            check(UUID(uuidString: first) != nil, "An import returns a safe opaque revision for preferences")
            let owned = directory.appendingPathComponent(first + ".png")
            guard let firstImage = store.image(revision: first) else { fatalError("Imported image must resolve") }
            check(firstImage.width == 768 && firstImage.height == 384
                  && firstImage.bitsPerComponent == 8 && firstImage.bitsPerPixel == 32,
                  "Large imports downsample before storage and preserve their aspect ratio")
            let ownedData = try Data(contentsOf: owned)
            check(ownedData.count < 4 * 1024 * 1024 && ownedData != sourceData,
                  "Only the normalized bounded PNG is retained, not the source file")
            let preview = NSBitmapImageRep(cgImage: firstImage)
            check((preview.colorAt(x: 0, y: 0)?.alphaComponent ?? 1) < 0.05
                  && (preview.colorAt(x: 384, y: 192)?.alphaComponent ?? 0) > 0.4,
                  "Import retains transparent margins and visible custom-color artwork")
            try FileManager.default.removeItem(at: source)
            let reopened = HUDCenterLogoStore(directory: directory)
            check(reopened.image(revision: first)?.width == 768
                  && HUDCenterLogo.custom.image(revision: first, store: reopened)?.height == 384,
                  "Custom artwork survives source removal and a fresh store instance")
            check(store.image(revision: first.lowercased()) === firstImage,
                  "Equivalent UUID spelling shares the revision cache")
            check(store.image(revision: "../source") == nil && store.image(revision: "/tmp/logo.png") == nil,
                  "Invalid revision strings never become filesystem paths")

            let oriented = root.appendingPathComponent("rotated.png")
            try png(width: 120, height: 60, orientation: 6).write(to: oriented)
            let second = try store.importImage(from: oriented, replacing: first)
            check(store.image(revision: second)?.width == 60 && store.image(revision: second)?.height == 120,
                  "EXIF orientation is applied before saving the bounded custom image")
            check(FileManager.default.fileExists(atPath: owned.path),
                  "Import retains the prior saved revision while preferences switch to the new image")
            let third = try store.importImage(from: oriented, replacing: second)
            let managed = try FileManager.default.contentsOfDirectory(atPath: directory.path)
            check(Set(managed) == Set([second + ".png", third + ".png"])
                  && store.image(revision: first) == nil && store.image(revision: second) != nil,
                  "Repeated imports retain only the new and prior image, including in-memory cache pruning")

            let unchanged = try Data(contentsOf: directory.appendingPathComponent(third + ".png"))
            func rejects(_ url: URL, matching expected: String) -> Bool {
                do { _ = try store.importImage(from: url, replacing: third); return false }
                catch HUDCenterLogoError.invalidImage { return expected == "invalid" }
                catch HUDCenterLogoError.imageTooLarge { return expected == "bytes" }
                catch HUDCenterLogoError.imageDimensionsTooLarge { return expected == "pixels" }
                catch { return false }
            }
            let bad = root.appendingPathComponent("bad.png"); try Data("invalid".utf8).write(to: bad)
            check(rejects(URL(string: "https://example.invalid/image.png")!, matching: "invalid")
                  && rejects(root, matching: "invalid") && rejects(source, matching: "invalid")
                  && rejects(bad, matching: "invalid"), "Remote URLs, directories, missing and malformed files cannot replace a logo")
            let oversized = root.appendingPathComponent("oversized.png")
            FileManager.default.createFile(atPath: oversized.path, contents: nil)
            let handle = try FileHandle(forWritingTo: oversized)
            try handle.truncate(atOffset: UInt64(HUDCenterLogoStore.maximumEncodedBytes + 1)); try handle.close()
            check(rejects(oversized, matching: "bytes"), "The encoded-size limit rejects oversized files before decoding")
            let tooWide = root.appendingPathComponent("too-wide.png")
            try png(width: HUDCenterLogoStore.maximumSourceDimension + 1, height: 1).write(to: tooWide)
            check(rejects(tooWide, matching: "pixels"), "Unreasonable one-dimensional sources are rejected before thumbnail decoding")
            let tooManyPixels = root.appendingPathComponent("too-many-pixels.jpg")
            try jpegHeader(width: 8192, height: 8192).write(to: tooManyPixels)
            check(rejects(tooManyPixels, matching: "pixels"), "Pixel-count limit rejects oversized image metadata without allocating the full image")
            check(try Data(contentsOf: directory.appendingPathComponent(third + ".png")) == unchanged
                  && Set(FileManager.default.contentsOfDirectory(atPath: directory.path)) == Set(managed),
                  "Rejected imports preserve the prior assets byte-for-byte without partial managed files")

            let occupied = root.appendingPathComponent("occupied")
            try Data("keep".utf8).write(to: occupied)
            do {
                _ = try HUDCenterLogoStore(directory: occupied).importImage(from: oriented)
                check(false, "An unwritable destination must report failure")
            } catch HUDCenterLogoError.persistence { check(true, "Destination errors are surfaced without returning an unusable revision") }
            check(try Data(contentsOf: occupied) == Data("keep".utf8), "Failed writes cannot replace an existing destination file")

            let cacheRoot = root.appendingPathComponent("Cache", isDirectory: true)
            try FileManager.default.createDirectory(at: cacheRoot, withIntermediateDirectories: true)
            let cacheStore = HUDCenterLogoStore(directory: cacheRoot)
            let missing = UUID().uuidString
            check(cacheStore.image(revision: missing) == nil, "A missing custom file resolves to the original logo fallback")
            try png(width: 40, height: 20).write(to: cacheRoot.appendingPathComponent(missing + ".png"))
            check(cacheStore.image(revision: missing) == nil
                  && HUDCenterLogoStore(directory: cacheRoot).image(revision: missing) != nil,
                  "Missing revisions are cached so repeated renders never poll the filesystem")
            let cachedRevision = try cacheStore.importImage(from: oriented)
            let cachedImage = cacheStore.image(revision: cachedRevision)
            try FileManager.default.removeItem(at: cacheRoot.appendingPathComponent(cachedRevision + ".png"))
            check(cacheStore.image(revision: cachedRevision) === cachedImage,
                  "Resolved images are reused without per-frame filesystem reads")
            let invalidManaged = UUID().uuidString
            try pngHeader(width: 2000, height: 2000).write(to: cacheRoot.appendingPathComponent(invalidManaged + ".png"))
            check(cacheStore.image(revision: invalidManaged) == nil,
                  "Corrupt or externally replaced managed assets cannot bypass the bounded startup loader")

            let concurrent = HUDCenterLogoStore(directory: root.appendingPathComponent("Concurrent", isDirectory: true))
            let retainedRevision = try concurrent.importImage(from: oriented)
            DispatchQueue.concurrentPerform(iterations: 12) { index in
                if index == 0 {
                    do { _ = try concurrent.importImage(from: oriented, replacing: retainedRevision) }
                    catch { preconditionFailure("Utility-queue import failed: \(error)") }
                } else {
                    for _ in 0..<200 {
                        precondition(concurrent.image(revision: retainedRevision) != nil,
                                     "A concurrent import cannot invalidate the currently selected revision")
                    }
                }
            }
            check(concurrent.image(revision: retainedRevision) != nil,
                  "Utility imports and concurrent render-cache reads preserve the selected image safely")
        } catch { fatalError("Center logo fixture failed: \(error)") }
        return count
    }

    private static func png(width: Int, height: Int, orientation: Int = 1) throws -> Data {
        let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
            bytesPerRow: width * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
            bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
        context.setFillColor(NSColor(srgbRed: 0.2, green: 0.8, blue: 0.6, alpha: 0.65).cgColor)
        context.fill(CGRect(x: CGFloat(width) * 0.15, y: 0, width: CGFloat(width) * 0.7, height: CGFloat(height)))
        let data = NSMutableData()
        let destination = CGImageDestinationCreateWithData(data, "public.png" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, context.makeImage()!, [kCGImagePropertyOrientation: orientation] as CFDictionary)
        precondition(CGImageDestinationFinalize(destination)); return data as Data
    }

    /// Valid dimensions/CRC let ImageIO inspect the header, but no huge bitmap
    /// is generated; validation must reject this before decoding its tiny IDAT.
    private static func pngHeader(width: UInt32, height: UInt32) throws -> Data {
        var bytes = [UInt8](try png(width: 1, height: 1))
        for (start, value) in [(16, width), (20, height)] {
            for index in 0..<4 { bytes[start + index] = UInt8((value >> (24 - index * 8)) & 255) }
        }
        var crc: UInt32 = 0xFFFF_FFFF
        for byte in bytes[12..<29] {
            crc ^= UInt32(byte)
            for _ in 0..<8 { crc = (crc >> 1) ^ ((crc & 1) == 0 ? 0 : 0xEDB8_8320) }
        }
        crc ^= 0xFFFF_FFFF
        for index in 0..<4 { bytes[29 + index] = UInt8((crc >> (24 - index * 8)) & 255) }
        return Data(bytes)
    }

    private static func jpegHeader(width: UInt16, height: UInt16) throws -> Data {
        let original = CGImageSourceCreateWithData(try png(width: 1, height: 1) as CFData, nil)!
        let data = NSMutableData()
        let destination = CGImageDestinationCreateWithData(data, "public.jpeg" as CFString, 1, nil)!
        CGImageDestinationAddImage(destination, CGImageSourceCreateImageAtIndex(original, 0, nil)!, nil)
        precondition(CGImageDestinationFinalize(destination))
        var bytes = [UInt8](data as Data)
        guard let marker = (0..<(bytes.count - 8)).first(where: {
            bytes[$0] == 0xFF && [UInt8(0xC0), 0xC1, 0xC2].contains(bytes[$0 + 1])
        }) else { preconditionFailure("JPEG fixture must contain a start-of-frame marker") }
        bytes[marker + 5] = UInt8(height >> 8); bytes[marker + 6] = UInt8(height & 255)
        bytes[marker + 7] = UInt8(width >> 8); bytes[marker + 8] = UInt8(width & 255)
        return Data(bytes)
    }
}
