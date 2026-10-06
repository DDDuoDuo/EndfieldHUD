import AppKit
import PDFKit
import CoreText
import ImageIO

struct ReaderPage {
    let location: ReaderLocation
    let next: ReaderLocation?
    let previous: ReaderLocation?
    let progress: Double
    let image: CGImage
    let summary: String
    let isIllustration: Bool
}

/// Owned exclusively by the controller's one worker. PDFKit documents, ZIP file
/// handles and chapter caches never cross threads. Only bounded page images do.
struct ReaderImageView: Equatable {
    var zoom: CGFloat = 1
    var pan = CGPoint.zero
    var isValid: Bool { zoom.isFinite && (1...12).contains(zoom) && pan.x.isFinite && pan.y.isFinite }
    func clamped(to size: CGSize) -> ReaderImageView {
        let safe = zoom.isFinite ? min(12, max(1, zoom)) : 1
        let limit = CGSize(width: size.width * (safe - 1) / 2, height: size.height * (safe - 1) / 2)
        return ReaderImageView(zoom: safe, pan: CGPoint(x: min(limit.width, max(-limit.width, pan.x.isFinite ? pan.x : 0)),
            y: min(limit.height, max(-limit.height, pan.y.isFinite ? pan.y : 0))))
    }
}

final class ReaderDocument {
    static let extensions = ["pdf", "epub", "txt"]
    let title: String
    private let access: ReaderFileAccess
    private let pdf: PDFDocument?
    private let epub: ReaderEPUB?
    private let text: NSString?
    var sectionCount: Int { pdf?.pageCount ?? epub?.spine.count ?? 1 }
    init(access: ReaderFileAccess) throws {
        self.access = access
        let ext = access.url.pathExtension.lowercased()
        switch ext {
        case "pdf":
            guard let length = try access.url.resourceValues(forKeys: [.fileSizeKey]).fileSize, length <= 1_024 * 1_024 * 1_024 else { throw ReaderError.tooLarge }
            guard let document = PDFDocument(url: access.url) else { throw ReaderError.invalidBook }
            guard !document.isLocked else { throw ReaderError.encrypted }
            guard document.pageCount > 0, document.pageCount < 100_000 else { throw ReaderError.tooLarge }
            pdf = document; epub = nil; text = nil
            title = (document.documentAttributes?[PDFDocumentAttribute.titleAttribute] as? String)
                .flatMap { $0.isEmpty ? nil : $0 } ?? access.url.deletingPathExtension().lastPathComponent
        case "epub":
            let document = try ReaderEPUB(url: access.url); epub = document; pdf = nil; text = nil
            title = document.title.isEmpty ? access.url.deletingPathExtension().lastPathComponent : document.title
        case "txt":
            let handle = try FileHandle(forReadingFrom: access.url); defer { try? handle.close() }
            let data = try handle.read(upToCount: 32 * 1024 * 1024 + 1) ?? Data()
            guard data.count <= 32 * 1024 * 1024 else { throw ReaderError.tooLarge }
            let decoded: String?
            if data.starts(with: [0xff, 0xfe]) || data.starts(with: [0xfe, 0xff]) { decoded = String(data: data, encoding: .utf16) }
            else { decoded = String(data: data, encoding: .utf8) ?? String(data: data, encoding: .init(rawValue: CFStringConvertEncodingToNSStringEncoding(CFStringEncoding(CFStringEncodings.GB_18030_2000.rawValue)))) }
            guard let decoded, !decoded.isEmpty, !decoded.contains("\0") else { throw ReaderError.invalidBook }
            text = decoded as NSString; pdf = nil; epub = nil; title = access.url.deletingPathExtension().lastPathComponent
        default: throw ReaderError.unsupported
        }
    }
    private func blocks(_ section: Int) throws -> [ReaderBlock] {
        if let text { return [.text(text)] }; return try epub?.blocks(section) ?? []
    }
    func normalized(_ location: ReaderLocation) throws -> ReaderLocation {
        let section = min(sectionCount - 1, max(0, location.section))
        if pdf != nil { return ReaderLocation(section: section) }
        let contents = try blocks(section), index = min(contents.count - 1, max(0, location.block))
        var character = 0
        if case .text(let text) = contents[index] {
            character = min(max(0, text.length - 1), max(0, location.character))
            if character > 0, (0xdc00...0xdfff).contains(text.character(at: character)) { character -= 1 }
        }
        return ReaderLocation(section: section, block: index, character: character)
    }
    func location(at progress: Double) throws -> ReaderLocation {
        let amount = min(0.999999, max(0, progress)), section = min(sectionCount - 1, Int(amount * Double(sectionCount)))
        if pdf != nil { return ReaderLocation(section: section) }
        let contents = try blocks(section), fraction = amount * Double(sectionCount) - Double(section)
        let index = min(contents.count - 1, Int(fraction * Double(contents.count)))
        var location = ReaderLocation(section: section, block: index)
        if case .text(let text) = contents[index] {
            let string = text as NSString
            location.character = Int((fraction * Double(contents.count) - Double(index)) * Double(string.length))
            // Jump to a nearby paragraph boundary, keeping all following text.
            if location.character > 0 {
                let range = NSRange(location: max(0, location.character - 200), length: min(200, location.character))
                let found = string.range(of: "\n", options: .backwards, range: range)
                if found.location != NSNotFound { location.character = found.location + 1 }
            }
        }
        return try normalized(location)
    }
    func render(at requested: ReaderLocation, preferences: ReaderPreferences, size: CGSize, dark: Bool, imageView: ReaderImageView = ReaderImageView()) throws -> ReaderPage {
        let location = try normalized(requested)
        let dimensions = CGSize(width: min(900, max(200, size.width * 2)), height: min(1000, max(200, size.height * 2)))
        guard let context = CGContext(data: nil, width: Int(dimensions.width), height: Int(dimensions.height), bitsPerComponent: 8,
            bytesPerRow: 0, space: CGColorSpaceCreateDeviceRGB(), bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else { throw ReaderError.tooLarge }
        context.scaleBy(x: dimensions.width / size.width, y: dimensions.height / size.height)
        context.setFillColor(NSColor(white: dark ? 0.07 : 0.95, alpha: 1).cgColor); context.fill(CGRect(origin: .zero, size: size))
        let rect = CGRect(origin: .zero, size: size).insetBy(dx: preferences.margin, dy: preferences.margin)
        var next: ReaderLocation?, summary = "", illustration = false
        let imageView = imageView.clamped(to: size)
        func applyImageView() {
            // Rasterize only the visible viewport at every zoom. The fixed
            // bitmap budget is independent of the PDF's dimensions or zoom.
            context.translateBy(x: size.width / 2 + imageView.pan.x, y: size.height / 2 - imageView.pan.y)
            context.scaleBy(x: imageView.zoom, y: imageView.zoom)
            context.translateBy(x: -size.width / 2, y: -size.height / 2)
        }
        let progress: Double
        if let pdf {
            illustration = true; applyImageView()
            guard let page = pdf.page(at: location.section) else { throw ReaderError.invalidBook }
            let bounds = page.bounds(for: .cropBox)
            guard bounds.width > 0, bounds.height > 0, bounds.width.isFinite, bounds.height.isFinite else { throw ReaderError.invalidBook }
            let target = Self.fit(bounds.size, in: rect)
            context.saveGState(); context.translateBy(x: target.minX, y: target.minY)
            context.scaleBy(x: target.width / bounds.width, y: target.height / bounds.height)
            context.translateBy(x: -bounds.minX, y: -bounds.minY)
            context.setFillColor(NSColor.white.cgColor); context.fill(bounds)
            page.draw(with: .cropBox, to: context); context.restoreGState()
            next = location.section + 1 < pdf.pageCount ? ReaderLocation(section: location.section + 1) : nil
            progress = Double(location.section) / Double(max(1, pdf.pageCount - 1))
            summary = "\(location.section + 1) / \(pdf.pageCount)"
        } else {
            let contents = try blocks(location.section)
            var portion: Double = 0
            switch contents[location.block] {
            case .image(let path):
                illustration = true; applyImageView()
                guard let epub else { throw ReaderError.invalidBook }
                let data = try epub.zip.data(path)
                guard let source = CGImageSourceCreateWithData(data as CFData, [kCGImageSourceShouldCache: false] as CFDictionary),
                      let metadata = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [CFString: Any],
                      let width = metadata[kCGImagePropertyPixelWidth] as? Int, let height = metadata[kCGImagePropertyPixelHeight] as? Int,
                      width > 0, height > 0, width <= 65_536, height <= 65_536, Int64(width) * Int64(height) <= 64_000_000,
                      let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [kCGImageSourceCreateThumbnailFromImageAlways: true,
                        kCGImageSourceThumbnailMaxPixelSize: min(2048, Int(1200 * imageView.zoom)), kCGImageSourceCreateThumbnailWithTransform: true] as CFDictionary) else { throw ReaderError.invalidBook }
                context.draw(image, in: Self.fit(CGSize(width: image.width, height: image.height), in: rect))
                next = advance(location, blockCount: contents.count)
            case .text(let value):
                let string = value as NSString, chunk = Self.chunk(string, from: location.character)
                let attributed = Self.attributed(chunk, preferences: preferences, dark: dark)
                let setter = CTFramesetterCreateWithAttributedString(attributed)
                let frame = CTFramesetterCreateFrame(setter, CFRange(location: 0, length: 0), CGPath(rect: rect, transform: nil), nil)
                let visible = CTFrameGetVisibleStringRange(frame).length
                guard visible > 0 else { throw ReaderError.invalidBook }
                CTFrameDraw(frame, context)
                let end = location.character + visible
                next = end < string.length ? ReaderLocation(section: location.section, block: location.block, character: end) : advance(location, blockCount: contents.count)
                portion = Double(location.character) / Double(max(1, string.length))
                summary = String(chunk.prefix(80)).replacingOccurrences(of: "\n", with: " ")
            }
            progress = next == nil ? 1 : (Double(location.section) + (Double(location.block) + portion) / Double(contents.count)) / Double(sectionCount)
        }
        guard let image = context.makeImage() else { throw ReaderError.invalidBook }
        return ReaderPage(location: location, next: next, previous: try previous(location, preferences: preferences, size: size),
                          progress: progress, image: image, summary: summary, isIllustration: illustration)
    }
    private func advance(_ location: ReaderLocation, blockCount: Int) -> ReaderLocation? {
        if location.block + 1 < blockCount { return ReaderLocation(section: location.section, block: location.block + 1) }
        if location.section + 1 < sectionCount { return ReaderLocation(section: location.section + 1) }; return nil
    }
    private func previous(_ location: ReaderLocation, preferences: ReaderPreferences, size: CGSize) throws -> ReaderLocation? {
        if pdf != nil { return location.section > 0 ? ReaderLocation(section: location.section - 1) : nil }
        var section = location.section, block = location.block, end = location.character
        if end == 0 {
            if block > 0 { block -= 1 }
            else if section > 0 { section -= 1; block = try blocks(section).count - 1 }
            else { return nil }
            if case .text(let text) = try blocks(section)[block] { end = (text as NSString).length }
        }
        guard case .text(let value) = try blocks(section)[block], end > 0 else { return ReaderLocation(section: section, block: block) }
        let string = value as NSString, available = max(1, size.height - preferences.margin * 2)
        var low = max(0, end - 16_384), high = end - 1
        while low < high {
            let mid = (low + high) / 2
            let text = string.substring(with: NSRange(location: mid, length: end - mid))
            let setter = CTFramesetterCreateWithAttributedString(Self.attributed(text, preferences: preferences, dark: true))
            let fit = CTFramesetterSuggestFrameSizeWithConstraints(setter, CFRange(location: 0, length: 0), nil,
                CGSize(width: max(1, size.width - preferences.margin * 2), height: .greatestFiniteMagnitude), nil)
            if fit.height <= available { high = mid } else { low = mid + 1 }
        }
        if low > 0, (0xdc00...0xdfff).contains(string.character(at: low)) { low -= 1 }
        return ReaderLocation(section: section, block: block, character: low)
    }
    private static func chunk(_ value: NSString, from start: Int) -> String {
        var count = min(16_384, value.length - start)
        if count > 0, start + count < value.length, (0xd800...0xdbff).contains(value.character(at: start + count - 1)) { count -= 1 }
        return value.substring(with: NSRange(location: start, length: count))
    }
    private static func attributed(_ value: String, preferences: ReaderPreferences, dark: Bool) -> NSAttributedString {
        let paragraph = NSMutableParagraphStyle(); paragraph.lineSpacing = preferences.lineSpacing
        paragraph.baseWritingDirection = .natural
        return NSAttributedString(string: value, attributes: [.font: NSFont(name: preferences.fontName, size: preferences.fontSize) ?? .systemFont(ofSize: preferences.fontSize),
            .foregroundColor: NSColor(white: dark ? 0.92 : 0.08, alpha: 1), .paragraphStyle: paragraph])
    }
    private static func fit(_ size: CGSize, in bounds: CGRect) -> CGRect {
        let ratio = min(bounds.width / max(1, size.width), bounds.height / max(1, size.height))
        return CGRect(x: bounds.midX - size.width * ratio / 2, y: bounds.midY - size.height * ratio / 2, width: size.width * ratio, height: size.height * ratio)
    }
}
