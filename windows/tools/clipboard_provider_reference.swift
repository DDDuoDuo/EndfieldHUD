// Isolated source expression oracle. No NSPasteboard, app, window, or user data.
import Foundation
import CryptoKit
let sourceURL=URL(fileURLWithPath:CommandLine.arguments[1])
let source=try Data(contentsOf:sourceURL)
let body=String(decoding:source,as:UTF8.self)
precondition(body.contains("let flattened = preview.split(whereSeparator: \\.isWhitespace).joined(separator: \" \")"))
precondition(body.contains("let compact = flattened.count > 90 ? String(flattened.prefix(89)) + \"…\" : flattened"))
let inputs=["", "  first\tsecond\r\n third  ","A\u{00a0}B\u{202f}C\u{2003}D", "a \u{301} b", "a\t\u{301}b", "a\u{200b}b\u{feff}c\u{1c}d", String(repeating:"x",count:89),String(repeating:"x",count:90),String(repeating:"x",count:91),String(repeating:"e\u{301}",count:91),String(repeating:"👨‍👩‍👧‍👦",count:91),String(repeating:"🇰🇷",count:91),String(repeating:"한글",count:50),String(repeating:"文字",count:50),String(repeating:"x ",count:46),"e"+String(repeating:"\u{301}",count:40000)]
let rows=inputs.map { preview -> [String:Any] in
    let flattened = preview.split(whereSeparator: \.isWhitespace).joined(separator: " ")
    let compact = flattened.count > 90 ? String(flattened.prefix(89)) + "…" : flattened
    return ["input":preview,"preview":compact]
}
let output:[String:Any]=["source":"Sources/ClipboardStore.swift","sourceSHA256":SHA256.hash(data:source).map{String(format:"%02x",$0)}.joined(),"expression":"make(_:preview:thumbnail:payload:)","rows":rows]
FileHandle.standardOutput.write(try JSONSerialization.data(withJSONObject:output,options:[.sortedKeys,.prettyPrinted,.withoutEscapingSlashes]))
