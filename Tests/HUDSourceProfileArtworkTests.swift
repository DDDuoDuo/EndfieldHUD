import AppKit

enum HUDSourceProfileArtworkTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String) { count += 1; precondition(value, message) }
        func bytes(_ image: CGImage) -> [UInt8] { Array(image.dataProvider!.data! as Data) }
        func image(_ rgba: [UInt8], width: Int, height: Int) -> CGImage {
            CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32, bytesPerRow: width * 4,
                space: CGColorSpace(name: CGColorSpace.sRGB)!, bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.last.rawValue),
                provider: CGDataProvider(data: Data(rgba) as CFData)!, decode: nil, shouldInterpolate: false, intent: .defaultIntent)!
        }
        do {
            let alpha: [UInt8] = [0, 32, 64, 128, 192, 255]
            let source = Data(alpha.flatMap { [UInt8(12), 34, 56, $0] })
            let mask = try HUDSourceProfileArtwork.backgroundMask(bgra: source, textureWidth: 3, textureHeight: 2,
                spriteRect: CGRect(x: 0, y: 0, width: 3, height: 2))
            check(stride(from: 3, to: bytes(mask).count, by: 4).map { bytes(mask)[$0] } == [128, 192, 255, 0, 32, 64],
                  "The original bottom-origin alpha is flipped once for a top-origin user bitmap")
            let submask = try HUDSourceProfileArtwork.backgroundMask(bgra: source, textureWidth: 3, textureHeight: 2,
                spriteRect: CGRect(x: 1, y: 0, width: 2, height: 2))
            check(stride(from: 3, to: bytes(submask).count, by: 4).map { bytes(submask)[$0] } == [192, 255, 32, 64],
                  "A packed source offset crops alpha without shifting or mirroring user artwork")
            let stencilAlpha: [UInt8] = [0, 0, 0, 0, 255, 0, 0, 0, 0]
            let stencil = try HUDSourceProfileArtwork.backgroundMask(bgra: Data(stencilAlpha.flatMap { [0, 0, 0, $0] }),
                textureWidth: 3, textureHeight: 3, spriteRect: CGRect(x: 0, y: 0, width: 3, height: 3))
            let photo = image(Array(repeating: [UInt8(32), 96, 192, 255], count: 9).flatMap { $0 }, width: 3, height: 3)
            let clipped = try HUDSourceProfileArtwork.applyingBackgroundMask(stencil, to: photo)
            let clippedBytes = bytes(clipped)
            let center = clipped.bytesPerRow + 4
            check(Array(clippedBytes[center..<(center + 4)]) == [32, 96, 192, 255],
                  "The opaque interior keeps user RGB bytes rather than applying a brightness or color filter")
            check((0..<3).allSatisfy { row in (0..<3).allSatisfy { column in
                (row == 1 && column == 1) || clippedBytes[row * clipped.bytesPerRow + column * 4 + 3] == 0
            } }, "An opaque rectangular upload cannot draw beyond the source silhouette")
            check(bytes(photo) == Array(repeating: [UInt8(32), 96, 192, 255], count: 9).flatMap { $0 },
                  "Clipping never mutates the uploaded user image")
            for rectangle in [CGRect(x: 0, y: 0, width: 4, height: 2), CGRect(x: 0.5, y: 0, width: 2, height: 2)] {
                do {
                    _ = try HUDSourceProfileArtwork.backgroundMask(bgra: source, textureWidth: 3, textureHeight: 2, spriteRect: rectangle)
                    check(false, "Invalid source bounds must fail")
                } catch HUDSourceProfileArtwork.Failure.invalidMask { check(true, "Invalid mask rejected") }
            }
        } catch { fatalError("Profile artwork fixture: \(error)") }
        return count
    }
}
