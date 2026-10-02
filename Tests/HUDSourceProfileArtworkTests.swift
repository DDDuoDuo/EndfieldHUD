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
            let artwork = try HUDSourceProfileArtwork.backgroundArtwork(bgra: source, textureWidth: 3, textureHeight: 2,
                spriteRect: CGRect(x: 0, y: 0, width: 3, height: 2))
            check(stride(from: 3, to: bytes(artwork).count, by: 4).map { bytes(artwork)[$0] } == [128, 192, 255, 0, 32, 64],
                  "The original bottom-origin alpha is flipped once for a top-origin user bitmap")
            check(Array(bytes(artwork)[0..<4]) == [56, 34, 12, 128], "The authored border RGB is retained, not reduced to an alpha mask")
            let subart = try HUDSourceProfileArtwork.backgroundArtwork(bgra: source, textureWidth: 3, textureHeight: 2,
                spriteRect: CGRect(x: 1, y: 0, width: 2, height: 2))
            check(stride(from: 3, to: bytes(subart).count, by: 4).map { bytes(subart)[$0] } == [192, 255, 32, 64],
                  "A packed source offset crops artwork without shifting or mirroring it")

            let photo = image(Array(repeating: [UInt8(100), 150, 200, 255], count: 530 * 204).flatMap { $0 }, width: 530, height: 204)
            let sourceArt = image(Array(repeating: [UInt8(24), 72, 120, 255], count: 530 * 204).flatMap { $0 }, width: 530, height: 204)
            let composite = try HUDSourceProfileArtwork.compositedBackground(photo, artwork: sourceArt)
            let compositeBytes = bytes(composite)
            func pixel(_ x: Int, _ y: Int) -> [UInt8] {
                let start = y * composite.bytesPerRow + x * 4
                return Array(compositeBytes[start..<(start + 4)])
            }
            check(pixel(5, 100) == [24, 72, 120, 255] && pixel(520, 100) == [24, 72, 120, 255]
                && pixel(260, 5) == [24, 72, 120, 255] && pixel(260, 199) == [24, 72, 120, 255],
                "Custom artwork preserves all four authored outer decorations")
            check(pixel(20, 22) == [24, 72, 120, 255], "Rounded panel corners preserve their original source decoration")
            check(zip(pixel(260, 100), [52, 78, 104, 255]).allSatisfy { abs(Int($0.0) - $0.1) <= 1 },
                "Only the custom background interior is darkened; its hue and crop stay intact")
            check(Array(bytes(photo)[0..<4]) == [100, 150, 200, 255], "Compositing does not mutate the uploaded photo")
            let clearArtwork = image([0, 0, 0, 0], width: 1, height: 1)
            let clipped = try HUDSourceProfileArtwork.compositedBackground(photo, artwork: clearArtwork)
            let clippedBytes = bytes(clipped)
            check(stride(from: 3, to: clippedBytes.count, by: 4).allSatisfy { clippedBytes[$0] == 0 },
                "Custom artwork cannot fill pixels outside the source alpha silhouette")

            let avatar = image([64, 128, 192, 255, 32, 96, 160, 128], width: 1, height: 2)
            let upload = try HUDSourceProfileArtwork.texturePixels(avatar)
            let rgba = Array(upload.rgba)
            check(upload.width == 1 && upload.height == 2, "The avatar upload retains the crop dimensions")
            check(Array(rgba[4..<8]) == [64, 128, 192, 255], "Opaque avatar RGB is unfiltered and flipped once for original bottom-origin UVs")
            check(zip(Array(rgba[0..<4]), [32, 96, 160, 128]).allSatisfy { abs(Int($0.0) - $0.1) <= 1 },
                "Transparent avatar RGB is straight alpha for the source shader's single premultiplication")
            check(bytes(avatar) == [64, 128, 192, 255, 32, 96, 160, 128], "Uploading never changes the stored user image")
            for rectangle in [CGRect(x: 0, y: 0, width: 4, height: 2), CGRect(x: 0.5, y: 0, width: 2, height: 2)] {
                do {
                    _ = try HUDSourceProfileArtwork.backgroundArtwork(bgra: source, textureWidth: 3, textureHeight: 2, spriteRect: rectangle)
                    check(false, "Invalid source bounds must fail")
                } catch HUDSourceProfileArtwork.Failure.invalidMask { check(true, "Invalid source bounds rejected") }
            }
        } catch { fatalError("Profile artwork fixture: \(error)") }
        return count
    }
}
