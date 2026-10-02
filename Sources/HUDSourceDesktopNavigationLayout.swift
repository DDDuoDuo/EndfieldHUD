import Foundation
import simd

/// Recycles the authored right-hand button rows rather than cloning the source
/// scene for every saved application. Logical entries remain unlimited; draw,
/// animation and hit-test work stays bounded by the original nine-row pool.
struct HUDSourceDesktopNavigationLayout {
    struct Row {
        let id: HUDSourceID
        let buttons: [HUDSourceID]
        let anchored: HUDSourceVector2
    }
    struct Sample {
        let assignments: [HUDSourceID: Int]
        let logicalRows: [HUDSourceID: Int]
        let contentHeight: Double
    }

    let rows: [Row]
    let contentID: HUDSourceID
    let entryCount: Int
    let viewportHeight: Double
    private let originalContentSize: HUDSourceVector2
    private let cycleHeight: Double
    private let rowScale: Double
    private let columns: Int

    init(document: HUDSourceWatchDocument, entryCount: Int) throws {
        let scene = document.scene
        let buttons = document.buttons.filter { $0.path.contains("/RightBottomNode/") }
        var rowIDs: [HUDSourceID] = []
        var byRow: [HUDSourceID: [HUDSourceID]] = [:]
        for button in buttons {
            guard let parent = scene.node(button.nodeID)?.parentID else { continue }
            if byRow[parent] == nil { rowIDs.append(parent) }
            byRow[parent, default: []].append(button.nodeID)
        }
        let rows = rowIDs.compactMap { id -> Row? in
            guard let rect = scene.node(id)?.transform.rect else { return nil }
            return Row(id: id, buttons: byRow[id] ?? [], anchored: rect.anchoredPosition)
        }
        // A shortcut can itself contain a nested source scroll control. Bind
        // the scroll rect whose content actually owns the shared row pool.
        var rowAncestors = Set<HUDSourceID>()
        var ancestor = rows.first.map(\.id)
        while let id = ancestor {
            rowAncestors.insert(id); ancestor = scene.node(id)?.parentID
        }
        guard rows.count > 1, let first = rows.first, let last = rows.last,
              first.buttons.count > 0, rows.allSatisfy({ $0.buttons.count == first.buttons.count }),
              let scrollID = scene.traversalIDs.first(where: {
                  guard let content = document.component("UIScrollRect", on: $0)?["m_Content"].targetID else { return false }
                  return rowAncestors.contains(content)
              }), let scroll = document.component("UIScrollRect", on: scrollID),
              let contentID = scroll["m_Content"].targetID,
              let size = scene.node(contentID)?.transform.rect?.sizeDelta,
              let viewport = scene.node(scrollID)?.transform.rect?.sizeDelta,
              let rowParent = scene.node(first.id)?.parentID,
              let parent = scene.node(rowParent) else {
            throw HUDSourceError.invalid("Missing desktop source navigation row pool")
        }
        let differences = zip(rows, rows.dropFirst()).map { $0.anchored.y - $1.anchored.y }.sorted()
        let step = differences[differences.count / 2]
        guard step > 0, parent.transform.localScale.y > 0, viewport.y > 0 else {
            throw HUDSourceError.invalid("Invalid desktop source navigation extent")
        }
        self.rows = rows; self.contentID = contentID; self.entryCount = max(0, entryCount)
        self.originalContentSize = size; self.viewportHeight = viewport.y
        self.cycleHeight = first.anchored.y - last.anchored.y + step
        self.rowScale = parent.transform.localScale.y
        self.columns = first.buttons.count
    }

    func sample(normalizedPosition: Double) -> Sample {
        let count = entryCount / columns + (entryCount % columns == 0 ? 0 : 1)
        let lastRowY = count > 0 ? rows[(count - 1) % rows.count].anchored.y
            - Double((count - 1) / rows.count) * cycleHeight : rows[0].anchored.y
        let contentHeight = max(viewportHeight,
            originalContentSize.y + (rows.last!.anchored.y - lastRowY) * rowScale)
        let normalized = normalizedPosition.isFinite ? min(1, max(0, normalizedPosition)) : 1
        let offset = (1 - normalized) * max(0, contentHeight - viewportHeight)
        let cycle = Int(offset / (cycleHeight * rowScale))
        let withinCycle = offset - Double(cycle) * cycleHeight * rowScale
        let localRow = rows.lastIndex(where: { (rows[0].anchored.y - $0.anchored.y) * rowScale <= withinCycle }) ?? 0
        let firstRow = min(max(0, count - rows.count), max(0, cycle * rows.count + localRow - 1))
        var assignments: [HUDSourceID: Int] = [:], logicalRows: [HUDSourceID: Int] = [:]
        for logicalRow in firstRow..<min(count, firstRow + rows.count) {
            let row = rows[logicalRow % rows.count]
            logicalRows[row.id] = logicalRow
            for (column, button) in row.buttons.enumerated() {
                let index = logicalRow * columns + column
                if index < entryCount { assignments[button] = index }
            }
        }
        return Sample(assignments: assignments, logicalRows: logicalRows, contentHeight: contentHeight)
    }

    func apply(to pose: inout HUDSourceWatchPose, normalizedPosition: Double) {
        let sample = sample(normalizedPosition: normalizedPosition)
        var content = pose.transforms[contentID] ?? HUDSourceTransformOverride()
        content.sizeDelta = HUDSourceVector2(originalContentSize.x, sample.contentHeight)
        pose.transforms[contentID] = content
        for row in rows {
            var transform = pose.transforms[row.id] ?? HUDSourceTransformOverride()
            let logical = sample.logicalRows[row.id]
            transform.active = logical != nil
            if let logical {
                // Preserve the source row's X/Y placement pattern and its
                // separately animated Z/depth, including deployment motion.
                let cycle = logical / rows.count
                let existing = transform.anchoredPosition3D
                transform.anchoredPosition3D = HUDSourceVector3(existing?.x ?? row.anchored.x,
                    row.anchored.y - Double(cycle) * cycleHeight, existing?.z ?? 0)
            }
            pose.transforms[row.id] = transform
            for button in row.buttons {
                var item = pose.transforms[button] ?? HUDSourceTransformOverride()
                item.active = sample.assignments[button] != nil
                pose.transforms[button] = item
            }
        }
    }
}
