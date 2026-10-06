import AppKit

struct NotesShelfMediaChoice {
    let id: UUID
    let title: String
    let detail: String
    let isSupported: Bool
    let isAvailable: Bool
}

/// Visible rows alone have retained artwork; scrolling never resolves a bookmark
/// or reads a file. The transparent adapter shares the native HUD menu routing.
final class NotesShelfMediaPicker: NotesRetainedMenu {
    var onSelect: ((UUID) -> Void)?
    private let choices: [NotesShelfMediaChoice]
    private var filtered: [NotesShelfMediaChoice]
    private var mediaOnly = true
    private var firstRow = 0
    private var selectedID: UUID?
    private var scrollRemainder: CGFloat = 0
    init(choices: [NotesShelfMediaChoice], dark: Bool) {
        self.choices = choices; filtered = choices.filter(\.isSupported)
        super.init(size: CGSize(width: 340, height: 260), dark: dark)
        setAccessibilityLabel(L10n.text("Shelf media", "暂存架媒体")); refresh()
    }
    required init?(coder: NSCoder) { nil }
    private func refresh() {
        items = [Item(id: "close", title: "×", rect: CGRect(x: 307, y: 7, width: 23, height: 23)),
                 Item(id: "filter", title: (mediaOnly ? "✓  " : "□  ") + L10n.text("Media only", "仅显示媒体"), rect: CGRect(x: 8, y: 33, width: 280, height: 23)),
                 Item(id: "use", title: L10n.text("Use selected media", "使用所选媒体"), rect: CGRect(x: 174, y: 225, width: 156, height: 27), enabled: selectedID != nil)]
        for row in firstRow..<min(filtered.count, firstRow + 4) {
            let choice = filtered[row]
            items.append(Item(id: "row:\(row)", title: choice.title + " · " + choice.detail,
                rect: CGRect(x: 8, y: 62 + (row - firstRow) * 39, width: 316, height: 35),
                enabled: choice.isSupported && choice.isAvailable, selected: selectedID == choice.id))
        }
        paint()
    }
    override func paintContent(on layer: CALayer) {
        text(L10n.text("SHELF · IMAGE/VIDEO", "暂存架 · 图片/视频"), rect: CGRect(x: 10, y: 10, width: 285, height: 20), size: 12, parent: layer)
        if filtered.count > 4 {
            let track = CALayer(); track.frame = CGRect(x: 329, y: 62, width: 2, height: 152)
            track.backgroundColor = ink.withAlphaComponent(0.12).cgColor; layer.addSublayer(track)
            let height = max(10, 152 * 4 / CGFloat(filtered.count))
            let thumb = CALayer(); thumb.frame = CGRect(x: 329, y: 62 + CGFloat(firstRow) / CGFloat(filtered.count - 4) * (152 - height), width: 2, height: height)
            thumb.backgroundColor = ink.withAlphaComponent(0.6).cgColor; layer.addSublayer(thumb)
        }
    }
    override func scrollWheel(with event: NSEvent) {
        scroll(delta: -event.scrollingDeltaY * (event.hasPreciseScrollingDeltas ? 1 : 12))
    }
    func scroll(delta: CGFloat) {
        guard delta.isFinite else { return }
        scrollRemainder += min(256, max(-256, delta))
        let rows = Int(scrollRemainder / 30)
        guard rows != 0 else { return }
        scrollRemainder -= CGFloat(rows) * 30
        let next = min(max(0, filtered.count - 4), max(0, firstRow + rows))
        if next != firstRow { firstRow = next; refresh() }
    }
    override func perform(_ id: String) {
        if id == "filter" {
            mediaOnly.toggle(); filtered = mediaOnly ? choices.filter(\.isSupported) : choices
            firstRow = 0; selectedID = nil; refresh()
        } else if id == "use", let selectedID { onSelect?(selectedID) }
        else if id.hasPrefix("row:"), let row = Int(id.dropFirst(4)), filtered.indices.contains(row) {
            let choice = filtered[row]
            guard choice.isSupported, choice.isAvailable else { return }
            selectedID = choice.id; refresh(); onSelect?(choice.id)
        } else { super.perform(id) }
    }
}
