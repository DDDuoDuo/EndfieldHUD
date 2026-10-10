import Foundation
import PDFKit
// The unchanged Mac ReaderDocument decides every expected title; its sources
// are compiled beside this file by reader_pdf_title_reference.sh.
enum L10n { static func text(_ en: String, _ zh: String) -> String { en } }
func hex(_ text: String) -> String { text.utf16.map { String(format: "%04X", $0) }.joined() }
@main struct ReaderPDFTitleReference {
    static func main() throws {
        guard CommandLine.arguments.count == 3 else { fatalError("Usage: cases-directory output-directory") }
        let cases = URL(fileURLWithPath: CommandLine.arguments[1], isDirectory: true)
        let out = URL(fileURLWithPath: CommandLine.arguments[2], isDirectory: true)
        let files = try FileManager.default.contentsOfDirectory(at: cases, includingPropertiesForKeys: nil)
            .filter { $0.pathExtension == "pdf" }.sorted { $0.lastPathComponent < $1.lastPathComponent }
        var rows: [[String: Any]] = []
        for url in files {
            let data = try Data(contentsOf: url)
            var row: [String: Any] = ["name": url.deletingPathExtension().lastPathComponent, "base64": data.base64EncodedString()]
            // The final Mac library title (metadata title, else the file name).
            do {
                let document = try ReaderDocument(access: ReaderFileAccess(url: url))
                // Big-endian UTF-16 code units in hex: exact even where a title is not valid UTF-8.
                row["titleUTF16BE"] = hex(document.title)
                row["opened"] = true
            } catch { row["opened"] = false }
            // The raw PDFKit attribute, for reading the fixture.
            let attribute = PDFDocument(url: url)?.documentAttributes?[PDFDocumentAttribute.titleAttribute] as? String
            row["pdfKitTitleUTF16BE"] = attribute.map(hex) ?? NSNull()
            rows.append(row)
        }
        var provenance = try JSONSerialization.jsonObject(with: Data(contentsOf: out.appendingPathComponent("provenance.json"))) as! [String: Any]
        provenance["cases"] = rows
        let json = try JSONSerialization.data(withJSONObject: provenance, options: [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes])
        try json.write(to: out.appendingPathComponent("reader-pdf-title-source.json"))
        print("Original ReaderDocument PDF titles: \(rows.count) cases, \(json.count) bytes")
    }
}
