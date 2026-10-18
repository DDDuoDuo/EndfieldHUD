// Isolated ClipboardStore history-identity oracle (tools/clipboard_matches_reference.sh).
// Evaluates the unchanged ClipboardPayload.matches(_:) and
// ClipboardFileReferences source: ClipboardStore.capture moves a payload that
// matches an existing item to the front instead of adding it. No NSPasteboard,
// app, window, network or user data is touched.
//
// rows: file items, one POSIX path each, as a pasteboard file URL string reads
// (file:// plus the percent-encoded path, bridged from NSURL). No input or
// result names an existing file except the root, so the file system cannot
// change a result.
//   path         the POSIX path
//   standardized standardizedFileURL.path(percentEncoded: false)
//   directory    standardizedFileURL.hasDirectoryPath
//   lexical      the same standardization with every name kept as given: the
//                path's non-ASCII scalars are masked as private-use scalars
//                (never decomposed) and restored afterwards
//   decomposed   standardized differs from lexical: Foundation also applied
//                its file-system (HFS+) decomposition to the names
//   match        the first row whose one-file payload matches this row's
// The relation is checked to be an equivalence over every pair of rows, so the
// match classes carry the whole matrix.
// lists: multi-file payload pairs (order and count).
// texts: .text and .url payload pairs: Swift String equality of the strings.
import AppKit

@main
struct ClipboardMatchesReference {
    static func url(_ path: String) -> URL {
        let encoded = path.addingPercentEncoding(withAllowedCharacters: .urlPathAllowed)!
        return NSURL(string: "file://" + encoded)! as URL
    }
    static func payload(_ paths: [String]) -> ClipboardPayload { .files(ClipboardFileReferences(paths.map(url))) }
    static func standardized(_ path: String) -> String { url(path).standardizedFileURL.path(percentEncoded: false) }
    static func lexical(_ path: String) -> String {
        var table: [Unicode.Scalar] = []
        var masked = String.UnicodeScalarView()
        for scalar in path.unicodeScalars {
            if scalar.isASCII { masked.append(scalar); continue }
            if !table.contains(scalar) { table.append(scalar) }
            masked.append(Unicode.Scalar(0xe000 + UInt32(table.firstIndex(of: scalar)!))!)
        }
        var restored = String.UnicodeScalarView()
        for scalar in standardized(String(masked)).unicodeScalars {
            precondition(scalar.isASCII || (0xe000..<0xe000 + UInt32(table.count)).contains(scalar.value), "Masked names stay as given")
            restored.append(scalar.isASCII ? scalar : table[Int(scalar.value - 0xe000)])
        }
        return String(restored)
    }

    static func main() throws {
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        let o = "/EndfieldClipboardOracle"
        var paths: [String] = [
            // The root and dot segments at the root (Foundation keeps "/./" for a root followed only by ".").
            "/", "//", "///", "/.", "/..", "/./", "/../", "/./.", "//.", "/.//", "/.//.", "/../..", "/.././.", "/./..", "/./../.",
            // Separators and dot segments.
            o, o + "/", o + "//", o + "/.", o + "/./", o + "/..", o + "/../", "/" + o, "/" + o + "//a", "/." + o, "/.." + o,
            "/../.." + o + "/a", o + "/a/b.txt", o + "/a/./b.txt", o + "/a//b.txt", o + "/a///b.txt", o + "//a/b.txt",
            o + "/a/../a/b.txt", o + "/x/../a/b.txt", o + "/x/y/../../a/b.txt", o + "/a/b/../../a/b.txt", o + "/a/x/./../b.txt",
            o + "/a/../../../../a/b.txt", o + "/../../a/b.txt", o + "/a/b.txt/", o + "/a/b.txt//", o + "/a/b.txt/.",
            o + "/a/b.txt/./", o + "/a/b.txt/..", o + "/a/b.txt/../", o + "/a/b.txt/../b.txt", o + "/a/b.txt/x/..", o + "/a",
            o + "/a/", o + "/a/.", o + "/a/x/..", o + "/a/x/../", o + "/a/./.", o + "/a/b", o + "/x/..", o + "/x/../.", o + "/./x/..",
            // Names that only look like dot segments stay names.
            o + "/...", o + "/.../", o + "/.../..", o + "/..a/a../.a./a.b", o + "/. /x", o + "/ ./x", o + "/.. /x", o + "/a/.b/..c",
            // Case, spaces, percent signs, punctuation, Unicode (NFC and NFD), tilde.
            o + "/A/B.txt", o + "/a/B.txt", o + "/a b/c%20d", o + "/a b/./c%20d", o + "/#?;=&+,$@!'()*", o + "/%", o + "/%2F", o + "/%2e%2e",
            o + "/中文/文件.txt", o + "/中文/../中文/文件.txt", o + "/\u{e9}", o + "/e\u{301}", o + "/\u{c5}/x", o + "/\u{212b}/x", o + "/A\u{30a}/x",
            o + "/\u{ac00}", o + "/\u{1100}\u{1161}", o + "/😀/x", o + "/~", o + "/~/x", "/~", o + "/a/~/../b.txt",
        ]
        // By scalars: Swift String equality is canonical, so NFC and NFD names would merge.
        var seen = Set<[Unicode.Scalar]>()
        paths = paths.filter { seen.insert(Array($0.unicodeScalars)).inserted }
        let fileManager = FileManager.default
        for path in paths {
            let result = standardized(path)
            let root = result.split(separator: "/").allSatisfy { $0 == "." } // "/" or Foundation's "/./"
            precondition(root || (!fileManager.fileExists(atPath: path) && !fileManager.fileExists(atPath: result)),
                         "Oracle paths must not exist: \(path)")
            precondition(Array(lexical(path).unicodeScalars) == Array(result.unicodeScalars) || !path.allSatisfy(\.isASCII),
                         "Only names with non-ASCII scalars can be decomposed: \(path)")
        }
        let payloads = paths.map { payload([$0]) }
        var match = [Int](repeating: -1, count: paths.count)
        for i in paths.indices {
            for j in paths.indices where payloads[i].matches(payloads[j]) { match[i] = j; break }
        }
        for i in paths.indices {
            for j in paths.indices {
                precondition(payloads[i].matches(payloads[j]) == (match[i] == match[j]), "matches is an equivalence on the rows")
            }
        }
        let rows = paths.indices.map { i -> [String: Any] in
            let result = url(paths[i]).standardizedFileURL
            let names = lexical(paths[i])
            return ["path": paths[i], "standardized": result.path(percentEncoded: false), "directory": result.hasDirectoryPath, "lexical": names,
                    "decomposed": Array(names.unicodeScalars) != Array(result.path(percentEncoded: false).unicodeScalars), "match": match[i]]
        }
        let lists: [([String], [String])] = [
            ([o + "/a/b.txt", o + "/c.txt"], [o + "/a/./b.txt", o + "/x/../c.txt"]),
            ([o + "/a/b.txt", o + "/c.txt"], [o + "/c.txt", o + "/a/b.txt"]),
            ([o + "/a/b.txt"], [o + "/a/b.txt", o + "/a/b.txt"]),
            ([o + "/a/b.txt", o + "/a/b.txt"], [o + "/a//b.txt", o + "/a/b.txt"]),
            ([o + "/a/b.txt", o + "/c.txt"], [o + "/a/b.txt", o + "/c.txt/"]),
            ([o + "/a", o + "/b"], [o + "/a/.", o + "/b"]),
            ([o + "/a/", o + "/b"], [o + "/a/.", o + "/b/x/.."]),
            (["/", o], ["/..", "/." + o]),
            ([o + "/A.txt"], [o + "/a.txt"]),
        ]
        let listRows = lists.map { first, second -> [String: Any] in
            ["first": first, "second": second, "matches": payload(first).matches(payload(second))]
        }
        // .text and .url (original string) payloads: Swift String == is canonical equivalence.
        let long = String(repeating: "a", count: 2000)
        let texts: [(String, String)] = [
            ("abc", "abc"), ("abc", "abd"), ("abc", "ab"), ("A", "a"), ("caf\u{e9}", "cafe\u{301}"), ("K", "\u{212a}"), ("\u{c5}", "\u{212b}"),
            ("\u{c5}", "A\u{30a}"), ("\u{212b}", "A\u{30a}"), ("a\u{301}\u{328}", "a\u{328}\u{301}"), ("a\u{301}\u{300}", "a\u{300}\u{301}"),
            ("\u{ac00}", "\u{1100}\u{1161}"), ("\u{ac01}", "\u{1100}\u{1161}\u{11a8}"), ("\u{ac01}", "\u{ac00}\u{11a8}"), (";", "\u{37e}"),
            ("\u{3a9}", "\u{2126}"), ("\u{3ac}", "\u{1f71}"), ("\u{958}", "\u{915}\u{93c}"), ("fi", "\u{fb01}"), ("e\u{301}", "e\u{300}"),
            ("x\u{301}", "x"), ("\r\n", "\n"), ("\u{e9}\u{301}", "e\u{301}\u{301}"), (long + "\u{e9}", long + "e\u{301}"),
            (long + "\u{e9}" + long, long + "e\u{301}" + long + "b"), ("\u{1f468}\u{200d}\u{1f469}\u{200d}\u{1f467}", "\u{1f468}\u{200d}\u{1f469}\u{200d}\u{1f467}"),
            ("\u{1f468}\u{200d}\u{1f469}\u{200d}\u{1f467}", "\u{1f468}\u{1f469}\u{1f467}"), ("\u{1d15e}", "\u{1d157}\u{1d165}"), ("\u{2000}", "\u{2002}"),
            ("\u{2000}", " "), ("\u{f900}", "\u{8c48}"), ("\u{2f800}", "\u{4e3d}"), ("Ｋ", "K"), ("\u{340}", "\u{300}"), ("\u{344}", "\u{308}\u{301}"),
            ("q\u{307}\u{323}", "q\u{323}\u{307}"), ("\u{1e0b}\u{323}", "\u{1e0d}\u{307}"), ("\u{1e9b}\u{323}", "\u{17f}\u{323}\u{307}"), ("中文", "中文"),
        ]
        let link = URL(string: "https://example.com/")!, other = URL(string: "https://example.com/b")!
        var textRows: [[String: Any]] = texts.map { first, second in
            ["kind": "text", "first": first, "second": second, "matches": ClipboardPayload.text(first).matches(.text(second))]
        }
        textRows += [("caf\u{e9}", "cafe\u{301}", link), ("K", "\u{212a}", link), ("abc", "abd", link), ("abc", "abc", other)].map { first, second, url in
            ["kind": "url", "first": first, "second": second, "secondURL": url.absoluteString, "firstURL": link.absoluteString,
             "matches": ClipboardPayload.url(link, first).matches(.url(url, second))]
        }
        let provenance = try JSONSerialization.jsonObject(with: Data(contentsOf: output.appendingPathComponent("provenance.json")))
        let document: [String: Any] = ["provenance": provenance, "runtime": ProcessInfo.processInfo.operatingSystemVersionString,
                                       "expression": "ClipboardPayload.matches(_:): standardizedFileURL of pasteboard file URLs and String equality",
                                       "rows": rows, "lists": listRows, "texts": textRows]
        try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes])
            .write(to: output.appendingPathComponent("clipboard_matches.json"))
    }
}
