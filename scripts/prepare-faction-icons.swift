import AppKit
import ImageIO

// Build-time extraction of the existing atlas selections. Coordinates come
// from the runtime enum, keeping one source of truth and unchanged artwork.
guard CommandLine.arguments.count == 4 else { fatalError("Usage: prepare-faction-icons atlas enum-source output-directory") }
let atlasURL = URL(fileURLWithPath: CommandLine.arguments[1])
let sourceText = try String(contentsOfFile: CommandLine.arguments[2], encoding: .utf8)
let output = URL(fileURLWithPath: CommandLine.arguments[3], isDirectory: true)
let pattern = #"case \.([A-Za-z0-9]+): return \((\d+), (\d+)\)"#
let matches = try NSRegularExpression(pattern: pattern).matches(in: sourceText, range: NSRange(sourceText.startIndex..., in: sourceText))
guard !matches.isEmpty, let source = CGImageSourceCreateWithURL(atlasURL as CFURL, nil),
      let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
        kCGImageSourceCreateThumbnailFromImageAlways: true,
        kCGImageSourceThumbnailMaxPixelSize: 512 * 14,
        kCGImageSourceCreateThumbnailWithTransform: true,
        kCGImageSourceShouldCacheImmediately: true
      ] as CFDictionary) else { fatalError("Cannot read faction atlas") }
try FileManager.default.createDirectory(at: output, withIntermediateDirectories: true)
let unit = CGFloat(image.width) / 10
let inset = unit * 5 / 649.7
for match in matches {
    func field(_ n: Int) -> String { String(sourceText[Range(match.range(at: n), in: sourceText)!]) }
    let name = field(1), column = Int(field(2))!, row = Int(field(3))!
    let rect = CGRect(x: CGFloat(column) * unit + inset, y: CGFloat(row) * unit + inset,
                      width: unit - 2 * inset, height: unit - 2 * inset).integral
    guard let crop = image.cropping(to: rect) else { fatalError("Invalid atlas cell: \(name)") }
    let scale = min(1, 512 / CGFloat(max(crop.width, crop.height)))
    let width = max(1, Int(CGFloat(crop.width) * scale)), height = max(1, Int(CGFloat(crop.height) * scale))
    guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
        bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { fatalError("Cannot prepare \(name)") }
    context.interpolationQuality = .high
    context.draw(crop, in: CGRect(x: 0, y: 0, width: width, height: height))
    let destinationURL = output.appendingPathComponent(name + ".png")
    guard let pixels = context.makeImage(),
          let destination = CGImageDestinationCreateWithURL(destinationURL as CFURL, "public.png" as CFString, 1, nil)
    else { fatalError("Cannot write \(name)") }
    CGImageDestinationAddImage(destination, pixels, nil)
    guard CGImageDestinationFinalize(destination) else { fatalError("Cannot finish \(name)") }
}
print("Prepared \(matches.count) faction icons without changing their source artwork")
