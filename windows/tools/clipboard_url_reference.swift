// Isolated ClipboardStore link oracle (tools/clipboard_url_reference.sh).
// Evaluates the unchanged parsedURL(_:explicit:) source on fixed strings: no
// NSPasteboard, app, window, network or user data. Per row:
//   parsed   parsedURL(text, explicit: false)?.absoluteString
//   explicit parsedURL(text, explicit: true)?.absoluteString (the .URL type)
//   detected the string path of read(): a link only up to 2048 UTF-8 bytes
import Foundation

@main
struct ClipboardURLReference {
    static func main() throws {
        let output = URL(fileURLWithPath: CommandLine.arguments[1])
        let long = String(repeating: "a", count: 63)
        var inputs: [String] = [
            // Common links, whitespace trimming and the 2048-byte detection bound.
            "https://example.com", "https://example.com/a?b=c#d", " https://example.com/a?b=c#d\n", "\thttps://example.com\r\n",
            "https://example.com/\u{3000}", "\u{feff}https://example.com", "https://a.com\u{85}", "https://a.com/\u{a0}", "https://a.com/\u{180e}",
            "https://zh.wikipedia.org/wiki/中国", "https://ja.wikipedia.org/wiki/日本語?x=1#節", "https://ko.wikipedia.org/wiki/한국어",
            "https://example.com/" + String(repeating: "a", count: 2100), "https://example.com/" + String(repeating: "中", count: 700),
            "", " ", "\n", "www.example.com", "example.com/path", "C:\\Users\\me\\file.txt", "localhost:3000", "user@example.com",
            "about:blank", "data:text/plain,hi", "http://127.0.0.1:8080/x", "ftp://files.example.com/pub/file.zip",
            "MAILTO:someone@example.com?subject=Hi", "tel:+1-555-0100", "file:///C:/Users/me/My%20File.txt", "file://server/share/doc.pdf",
            "file:///C:/Program%20Files/", "file://localhost/C:/x", "file:C:/x",
            // Parser edge cases.
            "https://例子.com/路径", "https://a.com/é", "https://a.com/a b", "https://a.com/<x>", "https://a.com/%zz", "https://a.com:abc/",
            "https://a.com:99999/", "http://:80/", "http://@/", "http://user@/", "http://[::1]/", "http://[zz]/", "http:///x", "http://",
            "http:", "https:/a.com", "https:a.com", "HTTPS://A.COM", "ftp://h", "mailto:", "mailto:a@b", "tel:+1", "sms:1", "file:",
            "file://", "file:///", "file:///C:/x.txt", "file://host", "file://host/x", "javascript:alert(1)", "x-y:abc", "1abc:x", "a_b:x",
            "abc", "http://a.com/|", "http://a.com/{}", "http://a.com/\\x", "http://a.com/\"q\"", "http://a.com/^", "http://a.com/`",
            "http://a%20b.com/", "http://a b.com", "http://a.com#frag#2", "http://a.com/?q=[1]", "http://[::1]:8080/p", "http://a..b/",
            "http://-a.com/", "http://a.com./", "http://a\u{200b}.com/", "news:comp", "urn:isbn:1", "http://a<b>.com/", "http://a|b.com/",
            "http://a_b.com/", "http://例子.com:80/x", "http://ü.com", "http://Ü.COM", "http://a.com:80:90/", "http://a@b@c/",
            "http://a%zz.com/", "http://[::1/", "http://a]b/", "http://a[b/", "http://xn--abc/", "http://a.com:/", "http://user:pa ss@a.com",
            "http://us<er@a.com/", "http://üser@a.com/", "http://a.com/%E4%BD%A0", "http://a.com/%e4%bd%a0", "http://a.com/?q=你好&x=1",
            "http://a.com/#片段", "mailto:你@例子.com", "mailto:a b", "tel:+1 555", "custom:你好", "http://a.com/a%", "http://a.com/a%2",
            "http://a.com/\u{7f}", "http://a.com/x\u{1}", "http://a.com/x\u{a0}y", "http://a.com/\u{200b}", "http://a\u{ad}b.com/",
            "http://ＡＢＣ.com/", "http://a.com/ＡＢＣ", "https://a.com/😀", "https://😀.com/", "http://a.com?x#y?z", "http://a.com/;p=1",
            "http://a.com/x'y", "http://a.com/x!y$z&(a)*+,=", "http://a.com/~_.-", "http://a.com/@:", "http://a:b:c@d.com/",
            "http://a.com:8a/", "http://a.com:-1/", "http://:@a.com/", "http://a.com\\b", "http:\\\\a.com", "https://a.com/\u{2028}x",
            // Hosts converted by IDNA (UTS #46) and percent-checked hosts.
            "http://faß.de/", "http://βόλος.com/", "http://-ü.com/", "http://ü-.com/", "http://ü_b.com/", "http://üb--c.com/",
            "http://ab--ü.com/", "http://xn--zz/", "http://xn--ü/", "http://ü..com/", "http://ü.com./", "http://مثال.com/",
            "http://مثال1.com/", "http://1مثال.com/", "ftp://ü.com", "custom://ü.com/", "file://ü/x", "ws://ü.com/", "http://a.ü/",
            "http://ü:8080/", "http://user@ü.com/", "http://[ü]/", "http://%C3%BC.com/", "http://ü%41.com/", "http://a%41.com/",
            "http://ü.com:abc", "http://a.com/x?y=ü#ü", "http://ä\u{200b}b.com/", "http://a\u{200d}b.com/", "http://ü\u{200c}b.com/",
            "http://a!$&'()*+,;=.com/", "http://ü!b.com/", "http://Ⅻ.com/", "http://ﬁ.com/", "http://ǅ.com/", "http://İ.com/",
            "http://ü<b.com/", "http://ü|b.com/", "http://ü%zz.com/", "http://ü^b.com/", "http://ü\"b.com/", "http://ü~b.com/",
            "http://[a:b]/", "http://[::1%25eth0]/", "http://[a<b]/", "http://[a]:80/", "http://[a]x/", "http://[v1.x]/", "http://[]/",
            "http://%FF/", "http://%41/", "http://ü%C3%BC.com/", "http://ü.com%2F/", "http://ü.%41/", "http://ü" + long + ".com/",
            "http://a" + long + "a.com/", "http://例子。中国/", "https://中文.example/",
            // Paths, opaque URLs and fragments.
            "file:///%FF", "file:%FF", "file:x", "file:?q", "file:#f", "file://h?q", "http://u%zz@a.com/", "http://u%41@a.com/",
            "http://u:p:q@a.com/", "http://u#x@a.com/", "http://u?x@a.com/", "http://a.com/x#y#z", "http://a.com/?a?b", "http://a.com/x%2Fy",
            "mailto:%", "mailto:a%zz", "x:", "x:/", "x://", "x:///", "x://ü", "x://a<b", "x:a<b", "custom:a b", "a+b.c-d:x", "A1:x",
            "x:#f", "x:a#f", "mailto:a#f", "x:?q#f", "x:/#f", "http://a.com#", "http://a.com?", "x:?", "x:#", "file:#", "http://a.com/#a#b",
            "x:a?b#c#d", "x:a/b:c@d", "x:%41", "x:a%zz#%zz", "http://a.com/?%zz", "http://a.com/#%zz", "x://h#f", "x://#f", "x://?q",
            "x:///#f", "file:///#f", "http:#f", "http:?q", "mailto:#f", "mailto:?subject=x", "tel:#1", "x:a[b]", "http://a.com/[x]",
        ]
        var seen = Set<String>()
        inputs = inputs.filter { seen.insert($0).inserted }
        func value(_ url: URL?) -> Any { url.map { $0.absoluteString as Any } ?? NSNull() }
        let rows = inputs.map { text -> [String: Any] in
            let parsed = ClipboardStoreReference.parsedURL(text, explicit: false)
            let explicitURL = ClipboardStoreReference.parsedURL(text, explicit: true)
            let detected = text.utf8.count <= 2048 ? parsed : nil
            return ["text": text, "parsed": value(parsed), "explicit": value(explicitURL), "detected": value(detected)]
        }
        let provenance = try JSONSerialization.jsonObject(with: Data(contentsOf: output.appendingPathComponent("provenance.json")))
        let document: [String: Any] = ["provenance": provenance, "runtime": ProcessInfo.processInfo.operatingSystemVersionString,
                                       "expression": "ClipboardStore.parsedURL(_:explicit:) and read() string-path detection", "rows": rows]
        try JSONSerialization.data(withJSONObject: document, options: [.sortedKeys, .prettyPrinted, .withoutEscapingSlashes])
            .write(to: output.appendingPathComponent("clipboard_urls.json"))
    }
}
