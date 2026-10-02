import Foundation
import CoreGraphics
import simd

enum HUDSourceDesktopIconLayout {
    static func path(_ source: CGPath) -> CGPath {
        let bounds = source.boundingBoxOfPath
        guard !bounds.isEmpty, bounds.width.isFinite, bounds.height.isFinite else { return source }
        let scale = 26 / max(bounds.width, bounds.height)
        var transform = CGAffineTransform(a: scale, b: 0, c: 0, d: scale,
            tx: 16 - bounds.midX * scale, ty: 16 - bounds.midY * scale)
        return source.copy(using: &transform) ?? source
    }
    /// Remove transparent padding once when artwork is bound. All icon images
    /// then fit the same visible box, irrespective of source canvas dimensions.
    static func image(_ source: CGImage) -> CGImage {
        let side = 96
        var bytes = [UInt8](repeating: 0, count: side * side * 4)
        return bytes.withUnsafeMutableBytes { raw in
            guard let context = CGContext(data: raw.baseAddress, width: side, height: side, bitsPerComponent: 8,
                bytesPerRow: side * 4, space: CGColorSpace(name: CGColorSpace.sRGB)!,
                bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue | CGBitmapInfo.byteOrder32Big.rawValue) else { return source }
            let scale = CGFloat(side) / CGFloat(max(source.width, source.height))
            let size = CGSize(width: CGFloat(source.width) * scale, height: CGFloat(source.height) * scale)
            context.interpolationQuality = .high
            context.draw(source, in: CGRect(x: (CGFloat(side) - size.width) / 2,
                y: (CGFloat(side) - size.height) / 2, width: size.width, height: size.height))
            let pixels = raw.bindMemory(to: UInt8.self)
            var minX = side, minY = side, maxX = -1, maxY = -1
            for y in 0..<side { for x in 0..<side where pixels[(y * side + x) * 4 + 3] > 8 {
                minX = min(minX, x); minY = min(minY, y); maxX = max(maxX, x); maxY = max(maxY, y)
            } }
            guard minX <= maxX, minY <= maxY, let image = context.makeImage() else { return source }
            return image.cropping(to: CGRect(x: minX, y: minY, width: maxX - minX + 1, height: maxY - minY + 1)) ?? image
        }
    }
}

/// Reuse the authored finite ColorTint fades for desktop feedback. Profile
/// decoration and the quit background are separate from photographs, shadows,
/// and side-button faces; none of those need a new animation clock.
struct HUDSourceDesktopHoverFeedback {
    static let sideEdgeOpacity: Float = 0.18
    let sideEdgeIDs: Set<HUDSourceID>
    let profileButtonIDs: Set<HUDSourceID>
    let profileRootID: HUDSourceID?
    private let profileBinding: HUDSourceSelectableColor.Binding?
    private let profileDecorationAlpha: [HUDSourceID: Float]
    private let quitBindings: [(background: HUDSourceID, binding: HUDSourceSelectableColor.Binding)]

    init(document: HUDSourceWatchDocument, selectable: HUDSourceSelectableColor) {
        sideEdgeIDs = Set(document.scene.nodes.filter { node in
            node.path.hasSuffix("/HoverHint/NaviHint/Img")
                && document.buttons.contains { node.path.hasPrefix($0.path + "/") }
        }.map(\.id))
        let card = document.desktopProfileCard
        profileButtonIDs = card?.buttonIDs ?? []
        profileRootID = card?.scene.rootID
        profileBinding = selectable.bindings.first { $0.buttonNodeID == card?.scene.rootID }
        profileDecorationAlpha = Dictionary(uniqueKeysWithValues: (card?.scene.nodes ?? []).compactMap { node in
            guard node.path.contains("/PlayerInfo/DecoNode/"),
                  ["LeftLineImage", "RightLineImage", "LineImage", "LeftBottomImage"].contains(node.name),
                  let image = document.component("UIImage", on: node.id) else { return nil }
            return (node.id, image["m_Color"].color.w)
        })
        quitBindings = selectable.bindings.compactMap { binding in
            guard let node = document.scene.node(binding.buttonNodeID), node.name == "QuitBtn",
                  let background = document.scene.nodes.first(where: { $0.path == node.path + "/Bg" }) else { return nil }
            return (background.id, binding)
        }
    }

    /// Every original profile hit region opens the same desktop profile.
    /// Its root ColorTint also provides consistent feedback across those regions.
    func groupedButton(_ id: HUDSourceID?) -> HUDSourceID? {
        guard let id, profileButtonIDs.contains(id) else { return id }
        return profileRootID
    }

    func opacities(selectableTints: [HUDSourceID: SIMD4<Float>]) -> [HUDSourceID: Float] {
        func progress(_ binding: HUDSourceSelectableColor.Binding) -> Float {
            let normal = binding.colors.color(for: .normal).w
            let range = binding.colors.color(for: .highlighted).w - normal
            guard range > 0, let tint = selectableTints[binding.targetNodeID], tint.w.isFinite else { return 0 }
            return min(1, max(0, (tint.w - normal) / range))
        }
        var result: [HUDSourceID: Float] = [:]
        if let profileBinding {
            let amount = progress(profileBinding)
            for (id, alpha) in profileDecorationAlpha where alpha > 0 {
                // A restrained outline response, without restoring the broad
                // additive Light layers that wash out personal artwork.
                result[id] = 1 + max(0, 0.62 - alpha) / alpha * amount
            }
        }
        for (background, binding) in quitBindings {
            result[background] = 1 + 0.25 * progress(binding)
        }
        return result
    }
}

/// Finite scroll response on the Watch's existing clock. The closed-form
/// spring is independent of frame cadence and returns an exact settled value.
struct HUDSourceDesktopScrollMotion {
    private(set) var position: Double = 1
    private(set) var target: Double = 1
    private var velocity: Double = 0
    private var lastTime: Double?
    private var edgeLimit: Double = 0.08
    private var epsilon: Double = 0.0001
    enum Phase { case none, began, changed, ended, cancelled }
    private(set) var isGestureActive = false
    private(set) var ownsMomentum = false
    private var suppressesMomentum = false
    private var rawPosition: Double = 1
    var acceptsGestureContinuation: Bool { isGestureActive || ownsMomentum }
    var requiresFrames: Bool { isGestureActive || isAnimating }
    func canScroll(_ direction: Int) -> Bool { direction < 0 ? target < 1 - 1e-9 : target > 1e-9 }
    var isAnimating: Bool { !isGestureActive && (abs(position - target) > epsilon || abs(velocity) > epsilon * 18) }

    mutating func reset(to value: Double, at time: Double) {
        position = min(1, max(0, value.isFinite ? value : 1)); target = position
        velocity = 0; lastTime = time.isFinite ? time : nil
        rawPosition = position; isGestureActive = false; ownsMomentum = false; suppressesMomentum = false
    }
    mutating func scroll(by delta: Double, hiddenLength: Double, at time: Double, reduceMotion: Bool) {
        guard delta.isFinite, hiddenLength.isFinite, hiddenLength > 0, time.isFinite else { return }
        _ = advance(at: time)
        isGestureActive = false; ownsMomentum = false; suppressesMomentum = false
        edgeLimit = min(0.08, 36 / hiddenLength); epsilon = min(0.0001, 0.25 / hiddenLength)
        let requested = target + delta
        target = min(1, max(0, requested))
        if reduceMotion { position = target; velocity = 0; return }
        let overflow = requested - target
        if overflow != 0 {
            position += min(edgeLimit * 0.5, max(-edgeLimit * 0.5, overflow * 0.32))
            position = min(1 + edgeLimit, max(-edgeLimit, position))
        }
    }
    /// Precise input follows the finger with the same bounded rubber-band
    /// formula as the native strip. Only release/edge rebound uses our clock.
    mutating func gesture(by delta: Double, hiddenLength: Double, at time: Double,
                          phase: Phase, momentum: Phase, reduceMotion: Bool) {
        guard delta.isFinite, hiddenLength.isFinite, hiddenLength > 0, time.isFinite else { return }
        if phase == .none && momentum == .none {
            scroll(by: delta, hiddenLength: hiddenLength, at: time, reduceMotion: reduceMotion); return
        }
        let isMomentum = momentum != .none
        let ended = isMomentum ? momentum == .ended : phase == .ended
        if phase == .cancelled || momentum == .cancelled { reset(to: position, at: time); return }
        if phase == .began && !isMomentum { ownsMomentum = false; suppressesMomentum = false }
        if isMomentum && suppressesMomentum {
            if ended { ownsMomentum = false; suppressesMomentum = false }
            return
        }
        edgeLimit = min(0.08, 58 / hiddenLength); epsilon = min(0.0001, 0.25 / hiddenLength)
        if !isGestureActive {
            _ = advance(at: time)
            let bound = min(1, max(0, position)), excess = position - bound
            rawPosition = bound + excess / max(0.001, 1 - abs(excess) / edgeLimit)
            isGestureActive = true; velocity = 0
        }
        ownsMomentum = true; lastTime = time
        rawPosition = min(1 + 10_000 / hiddenLength, max(-10_000 / hiddenLength, rawPosition + delta))
        target = min(1, max(0, rawPosition))
        let excess = rawPosition - target
        position = reduceMotion ? target : target + excess / (1 + abs(excess) / edgeLimit)
        let beyond = position < 0 || position > 1
        if ended || (isMomentum && beyond) {
            isGestureActive = false; rawPosition = target
            if beyond { suppressesMomentum = !isMomentum || !ended }
        }
        if isMomentum && ended { ownsMomentum = false; suppressesMomentum = false }
    }
    @discardableResult mutating func advance(at time: Double) -> Double {
        guard time.isFinite else { return position }
        defer { lastTime = time }
        guard let lastTime, time > lastTime, isAnimating else { return position }
        let dt = min(2, time - lastTime), decay = 16.0, frequency = 12.0
        let offset = position - target, b = (velocity + decay * offset) / frequency
        let e = exp(-decay * dt), c = cos(frequency * dt), s = sin(frequency * dt)
        position = target + e * (offset * c + b * s)
        velocity = e * ((b * frequency - decay * offset) * c - (offset * frequency + decay * b) * s)
        position = min(1 + edgeLimit, max(-edgeLimit, position))
        if !isAnimating || dt >= 1 { position = target; velocity = 0 }
        return position
    }
}

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
    private let captionIDs: [HUDSourceID: HUDSourceID]

    init(document: HUDSourceWatchDocument, entryCount: Int) throws {
        let scene = document.scene
        let buttons = document.buttons.filter { $0.path.contains("/RightBottomNode/") }
        captionIDs = Dictionary(uniqueKeysWithValues: buttons.compactMap { button in
            button.label.map { (button.nodeID, $0.nodeID) }
        })
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
                // Some game slots (notably BackPack) hide their own caption.
                // Recycled desktop slots must always display the saved name,
                // while their parent still controls visibility and scrolling.
                if let caption = captionIDs[button] {
                    var text = pose.transforms[caption] ?? HUDSourceTransformOverride()
                    text.active = item.active; pose.transforms[caption] = text
                }
            }
        }
    }
}
