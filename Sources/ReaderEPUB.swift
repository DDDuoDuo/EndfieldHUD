import Foundation

enum ReaderBlock { case text(NSString), image(String) }

final class ReaderEPUB {
    let zip: ReaderZIP
    let title: String
    let spine: [String]
    private var cached: [(Int, [ReaderBlock])] = []
    init(url: URL) throws {
        let archive = try ReaderZIP(url: url); zip = archive
        guard String(data: try archive.data("mimetype", limit: 100), encoding: .utf8)?.trimmingCharacters(in: .whitespacesAndNewlines) == "application/epub+zip" else { throw ReaderError.invalidBook }
        let container = try ReaderXML.parse(archive.data("META-INF/container.xml", limit: 256 * 1024))
        guard let root = container.rootFile, ReaderZIP.safePath(root) else { throw ReaderError.invalidBook }
        let package = try ReaderXML.parse(archive.data(root, limit: 2 * 1024 * 1024))
        guard !package.spine.isEmpty, package.spine.count <= 4_096 else { throw ReaderError.invalidBook }
        spine = try package.spine.map { id in
            guard let item = package.manifest[id], ["application/xhtml+xml", "text/html", "image/jpeg", "image/png", "image/webp", "image/gif"].contains(item.type) else { throw ReaderError.invalidBook }
            let path = try ReaderZIP.resolve(item.href, relativeTo: root)
            guard archive.entries[path] != nil else { throw ReaderError.invalidBook }; return path
        }
        title = package.title.trimmingCharacters(in: .whitespacesAndNewlines)
    }
    func blocks(_ section: Int) throws -> [ReaderBlock] {
        guard spine.indices.contains(section) else { throw ReaderError.invalidBook }
        if let found = cached.first(where: { $0.0 == section }) { return found.1 }
        let path = spine[section], result: [ReaderBlock]
        if ["jpg", "jpeg", "png", "gif", "webp"].contains(URL(fileURLWithPath: path).pathExtension.lowercased()) { result = [.image(path)] }
        else {
            let parsed = try ReaderXML.parse(zip.data(path, limit: 8 * 1024 * 1024))
            result = try parsed.blocks.map { block in
                if case .image(let href) = block { return .image(try ReaderZIP.resolve(href, relativeTo: path)) }
                return block
            }
        }
        guard !result.isEmpty else { throw ReaderError.invalidBook }
        cached.append((section, result)); if cached.count > 2 { cached.removeFirst() }
        return result
    }
}

/// XML only, never HTML/WebKit loading. No external entity resolution, document
/// scripts, network images, CSS, font installation or hyperlink execution.
private final class ReaderXML: NSObject, XMLParserDelegate {
    struct Item { let href: String; let type: String }
    var rootFile: String?, manifest: [String: Item] = [:], spine: [String] = [], title = "", blocks: [ReaderBlock] = []
    private var text = "", depth = 0, nodes = 0, ignoredDepth: Int?, bodyDepth: Int?, titleDepth: Int?
    private var rejected = false
    static func parse(_ data: Data) throws -> ReaderXML {
        let text: String?
        if data.starts(with: [0xff, 0xfe]) || data.starts(with: [0xfe, 0xff]) { text = String(data: data, encoding: .utf16) }
        else { text = String(data: data, encoding: .utf8) }
        guard let text, !text.contains("\0"), !text.uppercased().contains("<!ENTITY") else { throw ReaderError.invalidBook }
        var source = text.replacingOccurrences(of: "&nbsp;", with: "&#160;")
        if source.first == "\u{feff}" { source.removeFirst() }
        // The bounded XML string is normalized to UTF-8 below. Keep its
        // declaration consistent when the original archive entry used UTF-16.
        if source.hasPrefix("<?xml"), let end = source.range(of: "?>"),
           source.distance(from: source.startIndex, to: end.upperBound) <= 1_024 {
            let range = source.startIndex..<end.upperBound
            let declaration = String(source[range]).replacingOccurrences(
                of: #"(?i)\bencoding\s*=\s*(["'])[^"']*\1"#,
                with: "encoding=\"UTF-8\"", options: .regularExpression)
            source.replaceSubrange(range, with: declaration)
        }
        let parser = XMLParser(data: Data(source.utf8)), result = ReaderXML()
        parser.shouldResolveExternalEntities = false; parser.externalEntityResolvingPolicy = .never
        parser.delegate = result
        guard parser.parse(), !result.rejected else { throw ReaderError.invalidBook }
        result.flush(); return result
    }
    private func flush() {
        let clean = text.trimmingCharacters(in: .whitespacesAndNewlines)
        if !clean.isEmpty { blocks.append(.text(clean as NSString)) }; text = ""
    }
    func parser(_ parser: XMLParser, didStartElement name: String, namespaceURI: String?, qualifiedName: String?, attributes: [String: String]) {
        depth += 1; nodes += 1
        guard depth <= 128, nodes <= 100_000, blocks.count <= 8_192 else { rejected = true; parser.abortParsing(); return }
        let tag = name.split(separator: ":").last.map(String.init)?.lowercased() ?? name
        if tag == "rootfile", rootFile == nil { rootFile = attributes["full-path"] }
        if tag == "item", let id = attributes["id"], let href = attributes["href"], let type = attributes["media-type"] {
            guard manifest[id] == nil else { rejected = true; parser.abortParsing(); return }; manifest[id] = Item(href: href, type: type)
        }
        if tag == "itemref", let id = attributes["idref"], attributes["linear"] != "no" { spine.append(id) }
        if tag == "title", bodyDepth == nil { titleDepth = depth }
        if tag == "body" { bodyDepth = depth }
        if ["script", "style", "iframe", "object", "audio", "video"].contains(tag), ignoredDepth == nil { ignoredDepth = depth }
        guard bodyDepth != nil, ignoredDepth == nil else { return }
        if ["p", "div", "br", "h1", "h2", "h3", "li", "section"].contains(tag), !text.isEmpty { text += "\n" }
        if ["img", "image"].contains(tag), let href = attributes["src"] ?? attributes["href"] ?? attributes["xlink:href"] {
            flush(); blocks.append(.image(href))
        }
    }
    func parser(_ parser: XMLParser, foundCharacters string: String) {
        if titleDepth != nil { title += string }
        if bodyDepth != nil, ignoredDepth == nil { text += string }
    }
    func parser(_ parser: XMLParser, didEndElement name: String, namespaceURI: String?, qualifiedName: String?) {
        if depth == ignoredDepth { ignoredDepth = nil }
        if depth == titleDepth { titleDepth = nil }
        if depth == bodyDepth { flush(); bodyDepth = nil }
        depth -= 1
    }
    func parser(_ parser: XMLParser, resolveExternalEntityName name: String, systemID: String?) -> Data? { nil }
    func parser(_ parser: XMLParser, foundInternalEntityDeclarationWithName name: String, value: String?) { rejected = true; parser.abortParsing() }
}
