import Foundation
import zlib

enum ReaderError: LocalizedError {
    case unsupported, invalidArchive, tooLarge, invalidBook, unavailable, encrypted, changedOnDisk
    var errorDescription: String? {
        switch self {
        case .unsupported: return L10n.text("Choose a PDF, EPUB or TXT book.", "请选择 PDF、EPUB 或 TXT 书籍。")
        case .invalidArchive: return L10n.text("This EPUB archive is damaged or unsafe.", "此 EPUB 压缩包已损坏或不安全。")
        case .tooLarge: return L10n.text("This book exceeds the reader's safety limits.", "此书籍超过阅读器的安全限制。")
        case .invalidBook: return L10n.text("This book could not be read.", "无法读取此书籍。")
        case .unavailable: return L10n.text("The original book is unavailable. Choose it again.", "原始书籍不可用，请重新选择。")
        case .encrypted: return L10n.text("Encrypted books are not supported.", "暂不支持加密书籍。")
        case .changedOnDisk: return L10n.text("The reader library changed on disk. Restart the app before saving.", "阅读器书库已在磁盘上更改，请重新启动应用后保存。")
        }
    }
}

/// Read-only ZIP container: no extraction, shell, link following or remote URL.
/// Central and local headers must agree; every inflated entry is size/CRC checked.
final class ReaderZIP {
    struct Entry { let name: String; let method: UInt16; let flags: UInt16; let crc: UInt32; let compressed: Int; let size: Int; let offset: UInt64 }
    static let maximumEntries = 8_192
    static let maximumEntryBytes = 32 * 1024 * 1024
    static let maximumInflatedBytes = 512 * 1024 * 1024
    private let file: FileHandle
    private(set) var entries: [String: Entry] = [:]
    private let length: UInt64
    private var directoryOffset: UInt64 = 0
    init(url: URL) throws {
        file = try FileHandle(forReadingFrom: url)
        length = try file.seekToEnd()
        guard length >= 22, length <= 512 * 1024 * 1024 else { throw ReaderError.tooLarge }
        let start = length - min(length, 65_557), tail = try Self.read(file, offset: start, count: Int(length - start))
        var footer: Int?
        for index in stride(from: tail.count - 22, through: 0, by: -1) where tail.u32(index) == 0x06054b50 {
            if index + 22 + Int(tail.u16(index + 20)) == tail.count { footer = index; break }
        }
        guard let end = footer, tail.u16(end + 4) == 0, tail.u16(end + 6) == 0,
              tail.u16(end + 8) == tail.u16(end + 10) else { throw ReaderError.invalidArchive }
        let count = Int(tail.u16(end + 10)), directorySize = Int(tail.u32(end + 12))
        directoryOffset = UInt64(tail.u32(end + 16))
        guard count > 0, count <= Self.maximumEntries, directorySize <= 16 * 1024 * 1024,
              directoryOffset + UInt64(directorySize) == start + UInt64(end) else { throw ReaderError.invalidArchive }
        let table = try Self.read(file, offset: directoryOffset, count: directorySize)
        var position = 0, inflated = 0
        for _ in 0..<count {
            guard position + 46 <= table.count, table.u32(position) == 0x02014b50 else { throw ReaderError.invalidArchive }
            let nameLength = Int(table.u16(position + 28)), extra = Int(table.u16(position + 30)), comment = Int(table.u16(position + 32))
            let stop = position + 46 + nameLength + extra + comment
            guard stop <= table.count, nameLength > 0, nameLength <= 4096,
                  table.u16(position + 34) == 0,
                  let name = String(data: table.subdata(in: position + 46..<position + 46 + nameLength), encoding: .utf8),
                  Self.safePath(name), entries[name] == nil else { throw ReaderError.invalidArchive }
            let flags = table.u16(position + 8), method = table.u16(position + 10)
            let compressed = Int(table.u32(position + 20)), size = Int(table.u32(position + 24))
            let offset = UInt64(table.u32(position + 42)), mode = (table.u32(position + 38) >> 16) & 0xf000
            guard flags & 0x2041 == 0 else { throw ReaderError.encrypted }
            guard [UInt16(0), 8].contains(method), mode != 0xa000,
                  size <= Self.maximumEntryBytes, compressed <= Self.maximumEntryBytes,
                  offset + 30 + UInt64(compressed) <= directoryOffset,
                  size <= max(1, compressed) * 2_000 else { throw ReaderError.invalidArchive }
            inflated += size; guard inflated <= Self.maximumInflatedBytes else { throw ReaderError.tooLarge }
            entries[name] = Entry(name: name, method: method, flags: flags, crc: table.u32(position + 16), compressed: compressed, size: size, offset: offset)
            position = stop
        }
        guard position == table.count else { throw ReaderError.invalidArchive }
    }
    deinit { try? file.close() }
    func data(_ name: String, limit: Int = ReaderZIP.maximumEntryBytes) throws -> Data {
        guard let entry = entries[name], entry.size <= limit else { throw ReaderError.invalidArchive }
        let header = try Self.read(file, offset: entry.offset, count: 30)
        guard header.u32(0) == 0x04034b50, header.u16(6) == entry.flags, header.u16(8) == entry.method else { throw ReaderError.invalidArchive }
        let nameSize = Int(header.u16(26)), extraSize = Int(header.u16(28))
        let body = entry.offset + 30 + UInt64(nameSize + extraSize)
        guard body + UInt64(entry.compressed) <= directoryOffset,
              String(data: try Self.read(file, offset: entry.offset + 30, count: nameSize), encoding: .utf8) == entry.name else { throw ReaderError.invalidArchive }
        if entry.flags & 8 == 0 {
            guard header.u32(14) == entry.crc, Int(header.u32(18)) == entry.compressed,
                  Int(header.u32(22)) == entry.size else { throw ReaderError.invalidArchive }
        }
        let source = try Self.read(file, offset: body, count: entry.compressed)
        var output: Data
        if entry.method == 0 {
            guard source.count == entry.size else { throw ReaderError.invalidArchive }; output = source
        } else {
            output = Data(count: max(1, entry.size)); var stream = z_stream()
            guard inflateInit2_(&stream, -MAX_WBITS, ZLIB_VERSION, Int32(MemoryLayout<z_stream>.size)) == Z_OK else { throw ReaderError.invalidArchive }
            defer { inflateEnd(&stream) }
            let status = source.withUnsafeBytes { input in output.withUnsafeMutableBytes { target -> Int32 in
                stream.next_in = UnsafeMutablePointer(mutating: input.bindMemory(to: Bytef.self).baseAddress)
                stream.avail_in = uInt(source.count); stream.next_out = target.bindMemory(to: Bytef.self).baseAddress
                stream.avail_out = uInt(target.count); return inflate(&stream, Z_FINISH)
            } }
            guard status == Z_STREAM_END, stream.total_in == source.count, stream.total_out == entry.size else { throw ReaderError.invalidArchive }
            if entry.size == 0 { output.removeAll() }
        }
        let checksum = output.withUnsafeBytes { crc32(0, $0.bindMemory(to: Bytef.self).baseAddress, uInt($0.count)) }
        guard UInt32(checksum) == entry.crc else { throw ReaderError.invalidArchive }
        return output
    }
    static func safePath(_ name: String) -> Bool {
        !name.isEmpty && !name.hasPrefix("/") && !name.contains("\\") && !name.contains("\0") && !name.contains(":")
            && !name.split(separator: "/", omittingEmptySubsequences: false).contains(where: { $0 == ".." || $0 == "." })
    }
    static func resolve(_ href: String, relativeTo base: String) throws -> String {
        let raw = href.split(separator: "#", maxSplits: 1, omittingEmptySubsequences: false)[0]
        guard let decoded = String(raw).removingPercentEncoding, !decoded.isEmpty,
              !decoded.hasPrefix("/"), !decoded.contains(":"), !decoded.contains("\\"), !decoded.contains("\0"), !decoded.contains("?") else { throw ReaderError.invalidBook }
        var pieces = base.split(separator: "/").dropLast().map(String.init)
        for part in decoded.split(separator: "/") {
            if part == "." { continue }
            if part == ".." { guard !pieces.isEmpty else { throw ReaderError.invalidBook }; pieces.removeLast() }
            else { pieces.append(String(part)) }
        }
        let result = pieces.joined(separator: "/")
        guard safePath(result) else { throw ReaderError.invalidBook }; return result
    }
    private static func read(_ file: FileHandle, offset: UInt64, count: Int) throws -> Data {
        if count == 0 { return Data() }
        try file.seek(toOffset: offset)
        guard let result = try file.read(upToCount: count), result.count == count else { throw ReaderError.invalidArchive }
        return result
    }
}

private extension Data {
    func u16(_ i: Int) -> UInt16 { UInt16(self[i]) | UInt16(self[i + 1]) << 8 }
    func u32(_ i: Int) -> UInt32 { UInt32(self[i]) | UInt32(self[i + 1]) << 8 | UInt32(self[i + 2]) << 16 | UInt32(self[i + 3]) << 24 }
}
