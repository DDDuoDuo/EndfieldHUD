import AppKit

/// Pen and background preferences shared by the Projection screen of the
/// centre (ProjectionCanvas) and the projection workspace (ProjectionModel).
/// They persist in the app's defaults. Once the host attaches the session
/// model, every change here reaches the workspace at once and `capture()`
/// takes the workspace's own toolbar changes back.
final class ProjectionSettings {
    /// The app's instance; previews and tests replace it with a throwaway domain.
    static var shared = ProjectionSettings()
    /// The six pen presets of the Projection toolbar (spec §3.14); the first
    /// is the theme colour. Any other colour comes from the custom picker.
    static var palette: [UInt32] { [themeColor, 0xFAFAFA, 0x22A6F2, 0xE5383B, 0x8CE03C, 0x111111] }
    static var themeColor: UInt32 { NotesFormatBar.hex(HUDColor.yellow) }
    static let widthRange: ClosedRange<Double> = 4...48

    struct Values: Equatable {
        /// sRGB 0xRRGGBB.
        var color: UInt32 = ProjectionSettings.themeColor
        var width: Double = 12
        var erasing = false
        var background = true
    }

    private(set) var values: Values
    private let defaults: UserDefaults
    private weak var model: ProjectionModel?
    private(set) var mediaInsertRequested = false
    private static let key = "projection.settings.v1"

    init(defaults: UserDefaults = .standard) {
        self.defaults = defaults
        var values = Values()
        if let stored = defaults.dictionary(forKey: Self.key) {
            if let color = (stored["color"] as? NSNumber)?.uint32Value { values.color = color & 0xFFFFFF }
            if let width = (stored["width"] as? NSNumber)?.doubleValue, width.isFinite { values.width = min(80, max(1, width)) }
            values.erasing = stored["erasing"] as? Bool ?? false
            values.background = stored["background"] as? Bool ?? true
        }
        self.values = values
    }

    /// The live session model: it takes the saved values now and follows later changes.
    func attach(_ model: ProjectionModel) {
        self.model = model
        apply(to: model)
    }

    func update(_ change: (inout Values) -> Void) {
        var next = values
        change(&next)
        next.color &= 0xFFFFFF
        next.width = next.width.isFinite ? min(80, max(1, next.width)) : values.width
        guard next != values else { return }
        values = next
        save()
        if let model { apply(to: model) }
    }

    /// Reads the workspace's toolbar choices back from the attached model.
    func capture() {
        guard let model else { return }
        update { values in
            if let rgb = model.color.usingColorSpace(.sRGB) {
                let component = { (value: CGFloat) in UInt32((min(1, max(0, value)) * 255).rounded()) }
                values.color = component(rgb.redComponent) << 16 | component(rgb.greenComponent) << 8 | component(rgb.blueComponent)
            }
            values.width = Double(model.brushWidth)
            values.erasing = model.erasing
            values.background = model.backgroundEnabled
        }
    }

    /// Strokes or media exist in the session drawing.
    var hasDrawing: Bool { model.map { !$0.drawing.strokes.isEmpty || !$0.media.isEmpty } ?? false }

    /// Clears the session drawing (strokes and media references; sources stay intact).
    @discardableResult func clearDrawing() -> Bool { model?.clearContent() ?? false }

    /// Asks the workspace to open its media insert once it is presented.
    func requestMediaInsert() { mediaInsertRequested = true }

    /// The host consumes the request right after presenting the workspace.
    func takeMediaInsertRequest() -> Bool {
        defer { mediaInsertRequested = false }
        return mediaInsertRequested
    }

    var color: NSColor { NSColor(cgColor: HUDColor.rgb(values.color)) ?? .systemYellow }

    private func apply(to model: ProjectionModel) {
        model.color = color
        model.setBrushWidth(CGFloat(values.width))
        model.erasing = values.erasing
        model.backgroundEnabled = values.background
    }

    private func save() {
        defaults.set(["color": NSNumber(value: values.color), "width": values.width,
                      "erasing": values.erasing, "background": values.background], forKey: Self.key)
    }
}
