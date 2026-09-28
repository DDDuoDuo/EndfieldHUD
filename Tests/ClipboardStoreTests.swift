import AppKit
import ImageIO

enum ClipboardStoreTests {
    static func run() -> Int {
        var count = 0
        func check(_ value: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !value { fatalError(message, file: file, line: line) }
        }
        let input = NSPasteboard(name: NSPasteboard.Name("ClipboardStoreTests-input-\(UUID().uuidString)"))
        let output = NSPasteboard(name: NSPasteboard.Name("ClipboardStoreTests-output-\(UUID().uuidString)"))
        defer { input.releaseGlobally(); output.releaseGlobally() }
        func text(_ value: String, to store: ClipboardStore) -> Bool {
            input.clearContents(); input.setString(value, forType: .string)
            return store.capture(from: input)
        }
        let fm = FileManager.default
        let root = fm.temporaryDirectory.appendingPathComponent("ClipboardStoreTests-\(UUID().uuidString)", isDirectory: true)
        defer { try? fm.removeItem(at: root) }
        do {
            let store = ClipboardStore()
            check(store.capacity == 10 && store.items.isEmpty, "The session cache defaults to ten items and starts empty")
            check(ClipboardStore(capacity: 0).capacity == 1, "Initial capacity has a valid positive minimum")
            var updates = 0
            let observer = store.observe { updates += 1 }
            let exact = "  完整文本 👩🏽‍💻\nSecond line\tEmbedded NUL: \0 end  "
            check(text(exact, to: store), "Plain Unicode text enters the cache")
            let original = store.items[0]
            check(original.kind == .text && original.thumbnail == nil && !original.isPinned, "Text has the expected type and starts unpinned")
            check(original.preview == "完整文本 👩🏽‍💻 Second line Embedded NUL: \0 end", "The preview flattens whitespace without altering graphemes")
            check(store.copy(id: original.id, to: output) && output.string(forType: .string) == exact,
                  "Copy restores exact text, including newlines, spaces and embedded NUL")
            let beforeDuplicate = updates
            check(text(exact, to: store) && store.items.count == 1 && store.items[0].id == original.id,
                  "Duplicate content reuses the existing history identity")
            check(updates == beforeDuplicate, "Capturing the already-front duplicate does not trigger a redundant redraw")
            check(store.togglePin(id: original.id), "Pinning an existing item succeeds")
            check(text("Later", to: store) && store.items.count == 2, "Distinct content is inserted newest first")
            check(text(exact, to: store) && store.items[0].id == original.id && store.items[0].isPinned
                  && store.items[0].createdAt == original.createdAt, "Deduplication moves old content forward while preserving pin and creation time")
            let cluster = "👨‍👩‍👧‍👦"
            check(text(String(repeating: cluster, count: 110), to: store), "Long grapheme-rich content is accepted")
            check(store.items[0].preview.count == 90 && store.items[0].preview.hasSuffix("…")
                  && String(store.items[0].preview.dropLast()) == String(repeating: cluster, count: 89),
                  "Preview truncation is at most 90 graphemes and never splits emoji clusters")

            let urlText = "https://example.com/a?query=中文#fragment"
            check(text(urlText, to: store) && store.items[0].kind == .url, "A short valid plain-text web URL is recognized")
            let urlID = store.items[0].id
            check(store.copy(id: urlID, to: output) && output.string(forType: .string) == urlText
                  && output.string(forType: .URL) == URL(string: urlText)?.absoluteString,
                  "URL copy exposes both native URL and exact plain-text representations")
            check(text("An ordinary sentence: this is not a URL", to: store) && store.items[0].kind == .text,
                  "A colon in normal prose does not misclassify text as a URL")
            check(text("https://", to: store) && store.items[0].kind == .text, "A URL without a host remains text")
            check(text("mailto:person@example.com", to: store) && store.items[0].kind == .url, "A mailto URL is recognized")
            input.clearContents(); input.setString("customapp://open/item", forType: .URL)
            check(store.capture(from: input) && store.items[0].kind == .url, "Explicit native URL flavors support custom app schemes")
            check(store.copy(id: store.items[0].id, to: output) && output.string(forType: .URL) == "customapp://open/item",
                  "Explicit URL payloads survive copy-back")

            let imageStore = ClipboardStore()
            for type in [NSPasteboard.PasteboardType.png, .tiff] {
                let data = try imageData(width: 512, height: 256, type: type == .png ? "public.png" : "public.tiff")
                input.clearContents(); input.setData(data, forType: type)
                check(imageStore.capture(from: input), "PNG and TIFF clipboard images are accepted")
                let imageItem = imageStore.items[0]
                check(imageItem.kind == .image && imageItem.thumbnail?.width == 96 && imageItem.thumbnail?.height == 48,
                      "The store decodes one bounded 96-pixel image thumbnail")
                check(imageItem.preview.contains("512 × 256"), "Image cards include original dimensions")
                check(imageStore.copy(id: imageItem.id, to: output) && output.data(forType: type) == data,
                      "Copy-back retains original encoded image data, not the small preview")
                check(imageStore.capture(from: input) && imageStore.items[0].id == imageItem.id,
                      "An identical image representation deduplicates by payload")
            }
            input.clearContents(); input.setData(Data("not an image".utf8), forType: .png)
            let imageCount = imageStore.items.count
            check(!imageStore.capture(from: input) && imageStore.items.count == imageCount, "Malformed image bytes cannot create a broken card")
            input.clearContents(); input.setData(Data(repeating: 0, count: 64 * 1_048_576 + 1), forType: .png)
            check(!imageStore.capture(from: input) && imageStore.statusMessage != nil && imageStore.items.count == imageCount,
                  "Oversized image payloads are skipped with visible status and no history mutation")
            // Valid PNGs with trailing bytes exercise the encoded-memory bound
            // without allocating enormous decoded images just for a test.
            let imageBudget = ClipboardStore()
            let smallPNG = try imageData(width: 32, height: 16, type: "public.png")
            var bigIDs: [UUID] = []
            for marker: UInt8 in [1, 2, 3] {
                var encoded = smallPNG
                encoded.append(Data(repeating: 0, count: 44 * 1_048_576 - encoded.count - 1))
                encoded.append(marker)
                input.clearContents(); input.setData(encoded, forType: .png)
                check(imageBudget.capture(from: input), "Images above 12 MB remain supported within the 64 MB individual bound")
                bigIDs.append(imageBudget.items[0].id)
            }
            check(imageBudget.items.map(\.id) == [bigIDs[2], bigIDs[1]],
                  "The 128 MB aggregate budget evicts the oldest unpinned payload even when item slots remain")
            imageBudget.items.map(\.id).forEach { imageBudget.togglePin(id: $0) }
            var another = smallPNG
            another.append(Data(repeating: 0, count: 44 * 1_048_576 - another.count - 1)); another.append(4)
            input.clearContents(); input.setData(another, forType: .png)
            check(!imageBudget.capture(from: input) && imageBudget.items.map(\.id) == [bigIDs[2], bigIDs[1]]
                  && imageBudget.statusMessage != nil, "Pinned payloads cannot be evicted by the aggregate memory budget")
            imageBudget.items.map(\.id).forEach { _ = imageBudget.remove(id: $0) }
            input.clearContents()

            try fm.createDirectory(at: root, withIntermediateDirectories: true)
            let file = root.appendingPathComponent("reference.txt")
            let folder = root.appendingPathComponent("Folder", isDirectory: true)
            let contents = Data("Do not change or copy this original.".utf8)
            try contents.write(to: file)
            try fm.createDirectory(at: folder, withIntermediateDirectories: true)
            let fileStore = ClipboardStore()
            input.clearContents(); input.writeObjects([file as NSURL, folder as NSURL])
            check(fileStore.capture(from: input) && fileStore.items.count == 1 && fileStore.items[0].kind == .files,
                  "A multi-file selection is one clipboard item containing native URL references")
            check(fileStore.items[0].preview == "reference.txt, Folder" && fileStore.items[0].thumbnail == nil,
                  "File previews use compact filenames without decoding source contents")
            let fileID = fileStore.items[0].id
            check(fileStore.copy(id: fileID, to: output), "Native file references can be copied back")
            let copiedFiles = output.readObjects(forClasses: [NSURL.self], options: [.urlReadingFileURLsOnly: true]) as? [URL]
            check(copiedFiles == [file, folder], "File copy-back restores original URLs in their original order")
            check(fileStore.capture(from: input) && fileStore.items[0].id == fileID && fileStore.items.count == 1,
                  "Repeated native file selections deduplicate")
            let mixed = NSPasteboardItem()
            mixed.setString(file.absoluteString, forType: .fileURL)
            mixed.setString("Fallback text", forType: .string)
            input.clearContents(); input.writeObjects([mixed])
            check(fileStore.capture(from: input) && fileStore.items[0].kind == .files,
                  "Native file URLs take priority over fallback text or image representations")
            fileStore.clearUnpinned()
            check(try Data(contentsOf: file) == contents && fm.contentsOfDirectory(atPath: root.path).sorted() == ["Folder", "reference.txt"],
                  "Capture, copy and clear never duplicate, move or delete file originals")

            let bounded = ClipboardStore(capacity: 3)
            for value in ["A", "B", "C"] { check(text(value, to: bounded), "Capacity fixture captures text") }
            let a = bounded.items[2].id
            bounded.togglePin(id: a)
            check(text("D", to: bounded) && bounded.items.map(\.preview) == ["D", "C", "A"],
                  "Eviction removes the oldest unpinned item while preserving a pinned oldest item")
            bounded.items.map(\.id).forEach { id in if bounded.items.first(where: { $0.id == id })?.isPinned == false { bounded.togglePin(id: id) } }
            let pinnedIDs = bounded.items.map(\.id)
            check(!text("E", to: bounded) && bounded.items.map(\.id) == pinnedIDs && bounded.statusMessage != nil,
                  "A full all-pinned cache skips new content with a visible explanation")
            check(text("A", to: bounded) && bounded.items[0].id == a && bounded.items.count == 3,
                  "A duplicate can still move forward when every slot is pinned")
            check(!bounded.setCapacity(2) && bounded.capacity == 3 && bounded.items.count == 3,
                  "Reducing capacity below the pinned count is refused without losing protected items")
            check(!bounded.clearUnpinned() && bounded.items.count == 3, "Clear unpinned cannot remove pinned items")
            bounded.togglePin(id: bounded.items[1].id)
            check(bounded.setCapacity(2) && bounded.items.count == 2 && bounded.items.allSatisfy(\.isPinned),
                  "Capacity reduction evicts only eligible unpinned items")
            check(bounded.setCapacity(5) && bounded.capacity == 5, "Capacity is configurable for future settings")
            check(!bounded.setCapacity(0) && bounded.capacity == 5, "Invalid zero capacity is rejected")
            check(bounded.remove(id: a) && !bounded.items.contains(where: { $0.id == a }), "Explicit remove can delete a pinned cache item")
            check(!bounded.remove(id: UUID()) && !bounded.togglePin(id: UUID()), "Stale UI identities are harmless")
            output.clearContents(); output.setString("Preserve destination", forType: .string)
            check(!bounded.copy(id: UUID(), to: output) && output.string(forType: .string) == "Preserve destination",
                  "Copying a nonexistent item does not clear the destination clipboard")

            let ignored = ClipboardStore()
            for marker in ["org.nspasteboard.ConcealedType", "org.nspasteboard.TransientType", "org.nspasteboard.AutoGeneratedType", "de.petermaurer.TransientPasteboardType", "com.typeit4me.clipping"] {
                let item = NSPasteboardItem()
                item.setString("Private content", forType: .string)
                item.setData(Data(), forType: NSPasteboard.PasteboardType(marker))
                input.clearContents(); input.writeObjects([item])
                check(!ignored.capture(from: input) && ignored.items.isEmpty && ignored.statusMessage == nil,
                      "Concealed, transient and autogenerated clipboard markers are ignored")
            }
            input.clearContents()
            check(!ignored.capture(from: input), "An empty pasteboard creates no cache card")
            input.setData(Data([1, 2, 3]), forType: NSPasteboard.PasteboardType("application/x-unknown"))
            check(!ignored.capture(from: input), "Unsupported custom pasteboard data is ignored")
            check(!text(String(repeating: "x", count: 1_048_577), to: ignored) && ignored.items.isEmpty && ignored.statusMessage != nil,
                  "Text beyond the memory bound is skipped with status")
            check(text("Accepted again", to: ignored) && ignored.statusMessage == nil, "A successful capture clears stale skip status")
            check(ClipboardStore().items.isEmpty, "A new session store cannot resurrect raw clipboard history from disk")
            let updateCount = updates
            store.removeObserver(observer)
            _ = text("No callback now", to: store)
            check(updates == updateCount, "Removing an observer stops callback delivery")
        } catch { fatalError("Clipboard store test failed: \(error)") }
        return count
    }

    private static func imageData(width: Int, height: Int, type: String) throws -> Data {
        let space = CGColorSpaceCreateDeviceRGB()
        guard let context = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8,
                                      bytesPerRow: width * 4, space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue) else {
            throw NSError(domain: "ClipboardStoreTests", code: 1)
        }
        context.setFillColor(CGColor(red: 0.2, green: 0.6, blue: 0.7, alpha: 1))
        context.fill(CGRect(x: 0, y: 0, width: width, height: height))
        let data = NSMutableData()
        guard let image = context.makeImage(), let destination = CGImageDestinationCreateWithData(data, type as CFString, 1, nil) else {
            throw NSError(domain: "ClipboardStoreTests", code: 2)
        }
        CGImageDestinationAddImage(destination, image, nil)
        guard CGImageDestinationFinalize(destination) else { throw NSError(domain: "ClipboardStoreTests", code: 3) }
        return data as Data
    }
}
