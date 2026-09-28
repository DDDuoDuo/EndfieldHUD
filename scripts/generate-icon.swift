import AppKit

// Render the user's original Endfield emblem into macOS's native icon sizes.
guard CommandLine.arguments.count == 3,
      let source = NSImage(contentsOfFile: CommandLine.arguments[2]) else {
    fatalError("Usage: generate-icon OUTPUT.icns ENDFIELD_SOURCE.png")
}
let output = URL(fileURLWithPath: CommandLine.arguments[1])
let temporary = FileManager.default.temporaryDirectory.appendingPathComponent(UUID().uuidString)
let iconset = temporary.appendingPathComponent("EndfieldHUD.iconset")
try FileManager.default.createDirectory(at: iconset, withIntermediateDirectories: true)
defer { try? FileManager.default.removeItem(at: temporary) }

for size in [16, 32, 128, 256, 512] {
    for scale in [1, 2] {
        let pixels = size * scale
        let bitmap = NSBitmapImageRep(bitmapDataPlanes: nil, pixelsWide: pixels, pixelsHigh: pixels,
            bitsPerSample: 8, samplesPerPixel: 4, hasAlpha: true, isPlanar: false,
            colorSpaceName: .deviceRGB, bytesPerRow: 0, bitsPerPixel: 0)!
        NSGraphicsContext.saveGraphicsState()
        NSGraphicsContext.current = NSGraphicsContext(bitmapImageRep: bitmap)!
        NSGraphicsContext.current?.imageInterpolation = .high
        let dimension = CGFloat(pixels)
        let plate = NSBezierPath(roundedRect: CGRect(x: dimension * 0.035, y: dimension * 0.035, width: dimension * 0.93, height: dimension * 0.93), xRadius: dimension * 0.19, yRadius: dimension * 0.19)
        plate.addClip(); NSColor(white: 0.055, alpha: 1).setFill(); plate.fill()
        let ratio = min(dimension * 0.86 / source.size.width, dimension * 0.86 / source.size.height)
        let rect = CGRect(x: (dimension - source.size.width * ratio) / 2,
            y: (dimension - source.size.height * ratio) / 2,
            width: source.size.width * ratio, height: source.size.height * ratio)
        source.draw(in: rect, from: .zero, operation: .sourceOver, fraction: 1)
        NSGraphicsContext.restoreGraphicsState()
        let suffix = scale == 2 ? "@2x" : ""
        try bitmap.representation(using: .png, properties: [:])!.write(to: iconset.appendingPathComponent("icon_\(size)x\(size)\(suffix).png"))
    }
}
let tool = Process(); tool.executableURL = URL(fileURLWithPath: "/usr/bin/iconutil")
tool.arguments = ["-c", "icns", iconset.path, "-o", output.path]
try tool.run(); tool.waitUntilExit(); exit(tool.terminationStatus)
