import AppKit
import QuartzCore

struct PersonalProfileCanvasAction {
    let id: String
    let label: String
    let rect: CGRect
}

struct PersonalProfileSliderValue {
    let field: PersonalProfileField
    let label: String
    let rect: CGRect
    let value: Double
    let minimum: Double
    let maximum: Double
    let step: Double
    let valueDescription: String
}

enum PersonalProfileField: String, CaseIterable {
    case name, tag, introduction, awakeningDate, birthday, permissionLevel, explorationLevel, operatorsCount, weaponsCount, archivesCount
    case backgroundWidth, backgroundOffsetX, backgroundOffsetY, thumbnailOffsetX, thumbnailOffsetY
    case avatarZoom, avatarOffsetX, avatarOffsetY
    var title: String {
        switch self {
        case .name: return L10n.text("Name", "名称")
        case .tag: return "#"
        case .introduction: return L10n.text("Introduction", "个人介绍")
        case .awakeningDate: return L10n.text("Awakening day", "苏醒日")
        case .birthday: return L10n.text("Birthday", "生日")
        case .permissionLevel: return L10n.text("Authority level", "权限等级")
        case .explorationLevel: return L10n.text("Exploration level", "探索等级")
        case .operatorsCount: return L10n.text("Operators", "干员")
        case .weaponsCount: return L10n.text("Weapons", "武器")
        case .archivesCount: return L10n.text("Archives", "档案")
        case .backgroundWidth: return L10n.text("Background width", "背景宽度")
        case .backgroundOffsetX: return L10n.text("Background X", "背景 X")
        case .backgroundOffsetY: return L10n.text("Background Y", "背景 Y")
        case .thumbnailOffsetX: return L10n.text("Thumbnail X", "缩略图 X")
        case .thumbnailOffsetY: return L10n.text("Thumbnail Y", "缩略图 Y")
        case .avatarZoom: return L10n.text("Zoom", "缩放")
        case .avatarOffsetX: return L10n.text("Portrait X", "头像 X")
        case .avatarOffsetY: return L10n.text("Portrait Y", "头像 Y")
        }
    }
    var isNumeric: Bool { self != .name && self != .tag && self != .introduction && !isDate }
    var isDate: Bool { self == .awakeningDate || self == .birthday }
    var isGeometry: Bool { Self.geometryFields.contains(self) || Self.portraitFields.contains(self) }
    var textLimit: Int? { self == .name ? 20 : self == .tag ? 10 : self == .introduction ? 150 : nil }
    static let geometryFields: [Self] = [.backgroundWidth, .backgroundOffsetX, .backgroundOffsetY, .thumbnailOffsetX, .thumbnailOffsetY]
    static let portraitFields: [Self] = [.avatarZoom, .avatarOffsetX, .avatarOffsetY]
    static let counters: [Self] = [.operatorsCount, .weaponsCount, .archivesCount]
}

/// A retained personal card, its contextual controls and an independently
/// positioned backdrop share the HUD's spatial projection.
final class PersonalProfileCanvas: NSObject, HUDModuleContentFactory {
    let layer = CALayer()
    /// Root owns this unclipped host's placement beside the central viewport.
    let backgroundLayer = CALayer()
    var onBackgroundChange: (() -> Void)?
    var onVisibilityChange: (() -> Void)?
    var onChange: (() -> Void)?
    var onEditField: ((PersonalProfileField, CGRect, String) -> Void)?
    var onChooseImage: ((UserProfileImageKind) -> Void)?
    var onChooseColor: ((NSColor) -> Void)?
    var onGeometryPreview: ((UserProfile) -> Void)?
    private let store: UserProfileStore?
    private let workSeconds: () -> TimeInterval
    private var observer: UUID?
    private var timer: Timer?
    private var active = false
    // Constructing the HUD must not prepare a second portrait crop or a hidden
    // backdrop. The bottom-left identity card owns the always-visible image.
    private var artworkEnabled = false
    private var imagesNeedUpdate = true
    private var dark = true
    private var hudAccent = HUDRuntimeAppearance.accent
    private var accent: NSColor { profile.resolvedAccent(fallback: hudAccent) }
    var resolvedAccent: NSColor { accent }
    private var scale: CGFloat = 2
    private var profile: UserProfile
    private var errorMessage: String?
    private var shownHours = ""
    private let background = CALayer()
    private let backgroundShade = CAGradientLayer()
    private let artwork = CALayer()
    private let toolbar = CALayer()
    private let popoverLayer = CALayer()
    private var departingPopover: CALayer?
    private enum Popover { case identity, background, portrait, themeColor }
    private var popover: Popover?
    private var draggedSlider: PersonalProfileField?
    private var selectedSlider: PersonalProfileField?
    var isDragging: Bool { draggedSlider != nil }
    private(set) var isTextHidden = false
    var isPopoverOpen: Bool { popover != nil }
    var popoverBounds: CGRect? {
        guard let popover else { return nil }
        switch popover {
        case .identity: return CGRect(x: 18, y: 136, width: 188, height: 139)
        case .background: return CGRect(x: 126, y: Self.backgroundRect.minY - 230, width: 264, height: 224)
        case .themeColor: return CGRect(x: 126, y: Self.backgroundRect.minY - 136, width: 264, height: 130)
        case .portrait: return CGRect(x: 100, y: 111, width: 288, height: 146)
        }
    }
    func containsPopoverPoint(_ point: CGPoint) -> Bool {
        guard let popoverBounds else { return false }
        return popoverBounds.contains(point) || accessibleActions.contains { $0.rect.contains(point) }
    }
    private var valueLayers: [PersonalProfileField: CALayer] = [:]
    private var workValue: CATextLayer?
    private var dateCaption: CATextLayer?
    private let shouldReduceMotion: () -> Bool
    static let menuRect = CGRect(x: 20, y: 116, width: 19, height: 19)
    static let backgroundRect = CGRect(x: 243, y: 294, width: 109, height: 24)
    static let visibilityRect = CGRect(x: 364, y: 294, width: 25, height: 24)
    static let dateLabelRect = CGRect(x: 98, y: 77, width: 67, height: 16)
    static let dateValueRect = CGRect(x: 165, y: 77, width: 90, height: 16)
    static let introductionRect = CGRect(x: 259, y: 172, width: 128, height: 77)
    static let introductionActionRect = CGRect(x: 257, y: 157, width: 132, height: 101)
    // The selected source frame extends beyond its square photo. Reserve its
    // full outer extent between the heading, identity labels and edit button.
    static let portraitRect = CGRect(x: 28, y: 44, width: 55, height: 55)
    var accessibilityStatus: String { errorMessage ?? L10n.text("Personal profile", "个人名片") }
    var profileValue: UserProfile { profile }
    var activeAnimationCount: Int {
        func count(_ node: CALayer) -> Int { (node.animationKeys()?.count ?? 0) + (node.sublayers ?? []).reduce(0) { $0 + count($1) } }
        return count(layer) + count(backgroundLayer)
    }

    init(store: UserProfileStore?, error: String? = nil,
         workSeconds: @escaping () -> TimeInterval = { 0 },
         reduceMotion: @escaping () -> Bool = { HUDRuntimeAppearance.reduceMotion }) {
        self.store = store; self.workSeconds = workSeconds; self.errorMessage = error
        self.profile = store?.profile ?? UserProfile()
        self.shouldReduceMotion = reduceMotion
        super.init()
        layer.name = "module.profile.canvas"; layer.frame = CGRect(x: 0, y: 0, width: 400, height: 334)
        layer.allowsGroupOpacity = false
        backgroundLayer.name = "profile.backgroundHost"; backgroundLayer.frame = layer.bounds
        backgroundLayer.masksToBounds = false; backgroundLayer.allowsGroupOpacity = false
        background.name = "profile.background"; background.frame = layer.bounds
        background.contentsGravity = .resizeAspectFill; background.masksToBounds = true
        backgroundLayer.addSublayer(background)
        backgroundShade.frame = layer.bounds; backgroundShade.startPoint = CGPoint(x: 0, y: 0.5)
        backgroundShade.endPoint = CGPoint(x: 1, y: 0.5); backgroundShade.locations = [0, 0.60, 1]
        background.addSublayer(backgroundShade)
        artwork.name = "profile.fields"; artwork.frame = layer.bounds; artwork.allowsGroupOpacity = false
        layer.addSublayer(artwork)
        toolbar.name = "profile.toolbar"; toolbar.frame = layer.bounds; layer.addSublayer(toolbar)
        popoverLayer.name = "profile.popover"; popoverLayer.frame = layer.bounds; layer.addSublayer(popoverLayer)
        observer = store?.observe { [weak self] in self?.refreshFromStore() }
    }
    deinit { timer?.invalidate(); if let observer { store?.removeObserver(observer) } }

    func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        dark = style.dark; hudAccent = style.accent; scale = style.contentsScale
        artworkEnabled = true
        updateBackgroundGeometry()
        repaint(); return layer
    }
    func updateRenderScale(_ value: CGFloat) {
        let next = value.isFinite ? min(8, max(1, value)) : 2
        guard scale != next else { return }; scale = next; repaint()
    }
    func activate() {
        guard !active else { return }; active = true; artworkEnabled = true; refreshFromStore()
        let clock = Timer(timeInterval: 30, repeats: true) { [weak self] _ in self?.refreshWorkDuration() }
        clock.tolerance = 5; timer = clock; RunLoop.main.add(clock, forMode: .common)
    }
    func deactivate() {
        artworkEnabled = false
        mouseUp()
        active = false; timer?.invalidate(); timer = nil
        popover = nil; departingPopover?.removeFromSuperlayer(); departingPopover = nil
        popoverLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        removeAnimations(in: layer); removeAnimations(in: backgroundLayer)
    }
    func refreshFromStore() {
        let previous = profile
        if let store { profile = store.profile }
        if let draggedSlider { applyGeometry(draggedSlider, value: geometryValue(draggedSlider, in: previous), to: &profile) }
        let imageChanged = previous.avatarFilename != profile.avatarFilename || previous.backgroundFilename != profile.backgroundFilename
        if imageChanged { imagesNeedUpdate = true }
        updateBackgroundGeometry()
        repaint()
        if active && !isDragging {
            for field in PersonalProfileField.allCases where value(field, in: previous) != value(field, in: profile) {
                if let item = valueLayers[field] { animateUpdate(item) }
            }
            if previous.showsBirthday != profile.showsBirthday {
                if let dateCaption { animateUpdate(dateCaption) }
                if let dateValue = valueLayers[profile.showsBirthday ? .birthday : .awakeningDate] { animateUpdate(dateValue) }
            }
            let portraitChanged = previous.avatarZoom != profile.avatarZoom || previous.avatarOffsetX != profile.avatarOffsetX || previous.avatarOffsetY != profile.avatarOffsetY
            if portraitChanged, let portrait = artwork.sublayers?.first(where: { $0.name == "profile.portrait" }) { animateUpdate(portrait) }
            if imageChanged { animateUpdate(background); animateUpdate(artwork) }
            if previous.themeColorHex != profile.themeColorHex { animateUpdate(artwork); animateUpdate(toolbar) }
        }
        onBackgroundChange?(); onChange?()
    }
    func refreshWorkDuration() {
        let next = formattedHours
        guard shownHours != next else { return }
        shownHours = next
        CATransaction.begin(); CATransaction.setDisableActions(true); workValue?.string = next; CATransaction.commit()
        if active, let workValue { animateUpdate(workValue) }; onChange?()
    }
    private var formattedHours: String {
        let seconds = max(profile.accumulatedWorkSeconds, workSeconds())
        return String(format: "%.2f", seconds.isFinite ? max(0, seconds) / 3600 : 0)
    }
    func showError(_ message: String) { errorMessage = message; repaint(); onChange?() }

    var accessibleActions: [PersonalProfileCanvasAction] {
        if let popover { return popoverActions(popover) }
        var actions = [PersonalProfileCanvasAction(id: "profile:backgroundMenu", label: L10n.text("Change card theme", "更换名片主题"), rect: Self.backgroundRect),
                       PersonalProfileCanvasAction(id: "profile:visibility", label: isTextHidden ? L10n.text("Show profile text", "显示名片文字") : L10n.text("Hide profile text", "隐藏名片文字"), rect: Self.visibilityRect)]
        guard !isTextHidden else { return actions }
        let dateField: PersonalProfileField = profile.showsBirthday ? .birthday : .awakeningDate
        actions.append(PersonalProfileCanvasAction(id: "profile:toggleDateLabel", label: profile.showsBirthday ? L10n.text("Show awakening day", "切换为苏醒日") : L10n.text("Show birthday", "切换为生日"), rect: Self.dateLabelRect))
        actions.append(PersonalProfileCanvasAction(id: "profile:" + dateField.rawValue, label: dateField.title + ": " + value(dateField, in: profile), rect: Self.dateValueRect))
        actions.append(PersonalProfileCanvasAction(id: "profile:menu", label: L10n.text("Edit personal profile", "编辑个人名片"), rect: Self.menuRect))
        actions.append(PersonalProfileCanvasAction(id: "profile:introduction", label: L10n.text("Edit introduction", "编辑个人介绍"), rect: Self.introductionActionRect))
        for field in [PersonalProfileField.permissionLevel, .explorationLevel] + PersonalProfileField.counters {
            actions.append(PersonalProfileCanvasAction(id: "profile:" + field.rawValue,
                label: field.title + ": " + value(field, in: profile), rect: fieldRect(field)))
        }
        return actions
    }
    var accessibleSliders: [PersonalProfileSliderValue] {
        let fields: [PersonalProfileField]
        switch popover {
        case .portrait: fields = PersonalProfileField.portraitFields
        case .background: fields = PersonalProfileField.geometryFields
        default: return []
        }
        return fields.map { field in
            let row = fieldRect(field)
            let range: ClosedRange<Double>
            switch field {
            case .avatarZoom: range = 1...20
            case .backgroundWidth: range = 400...900
            case .backgroundOffsetX: range = -400...400
            case .backgroundOffsetY: range = -250...250
            default: range = -100...100
            }
            let portrait = popover == .portrait
            let suffix = field == .avatarZoom ? "×" : [.avatarOffsetX, .avatarOffsetY, .thumbnailOffsetX, .thumbnailOffsetY].contains(field) ? "%" : ""
            return PersonalProfileSliderValue(field: field, label: field.title,
                rect: CGRect(x: portrait ? 112 : 138, y: row.minY + 12, width: portrait ? 262 : 240, height: 17),
                value: geometryValue(field, in: profile), minimum: range.lowerBound, maximum: range.upperBound,
                step: field == .avatarZoom ? 0.1 : 1, valueDescription: value(field, in: profile) + suffix)
        }
    }
    private func geometryValue(_ field: PersonalProfileField, in profile: UserProfile) -> Double {
        switch field {
        case .avatarZoom: return profile.avatarZoom
        case .avatarOffsetX: return profile.avatarOffsetX * 100
        case .avatarOffsetY: return profile.avatarOffsetY * 100
        case .backgroundWidth: return profile.backgroundWidth
        case .backgroundOffsetX: return profile.backgroundOffsetX
        case .backgroundOffsetY: return profile.backgroundOffsetY
        case .thumbnailOffsetX: return profile.thumbnailOffsetX * 100
        case .thumbnailOffsetY: return profile.thumbnailOffsetY * 100
        default: return 0
        }
    }
    private func applyGeometry(_ field: PersonalProfileField, value: Double, to profile: inout UserProfile) {
        switch field {
        case .avatarZoom: profile.avatarZoom = value
        case .avatarOffsetX: profile.avatarOffsetX = value / 100
        case .avatarOffsetY: profile.avatarOffsetY = value / 100
        case .backgroundWidth: profile.backgroundWidth = value
        case .backgroundOffsetX: profile.backgroundOffsetX = value
        case .backgroundOffsetY: profile.backgroundOffsetY = value
        case .thumbnailOffsetX: profile.thumbnailOffsetX = value / 100
        case .thumbnailOffsetY: profile.thumbnailOffsetY = value / 100
        default: break
        }
    }
    @discardableResult func setSlider(field: PersonalProfileField, value: Double) -> Bool {
        guard value.isFinite, let slider = accessibleSliders.first(where: { $0.field == field }) else { return false }
        let precision = field == .avatarZoom ? 100.0 : 1.0
        let bounded = (min(slider.maximum, max(slider.minimum, value)) * precision).rounded() / precision
        selectedSlider = field
        if draggedSlider == field {
            guard geometryValue(field, in: profile) != bounded else { return true }
            applyGeometry(field, value: bounded, to: &profile)
            updateBackgroundGeometry(); repaint()
            onGeometryPreview?(profile); onBackgroundChange?(); onChange?()
            return true
        }
        return commit(field: field, text: String(bounded))
    }
    @discardableResult func nudgeSlider(_ direction: Double) -> Bool {
        guard let selectedSlider, let slider = accessibleSliders.first(where: { $0.field == selectedSlider }) else { return false }
        return setSlider(field: selectedSlider, value: slider.value + direction * slider.step)
    }
    func mouseDragged(to point: CGPoint) { if isDragging { moveSlider(to: point) } }
    private func moveSlider(to point: CGPoint) {
        guard point.x.isFinite, let draggedSlider, let slider = accessibleSliders.first(where: { $0.field == draggedSlider }) else { return }
        let fraction = min(1, max(0, (point.x - slider.rect.minX - 4) / (slider.rect.width - 8)))
        _ = setSlider(field: draggedSlider, value: slider.minimum + fraction * (slider.maximum - slider.minimum))
    }
    func mouseUp() {
        guard let field = draggedSlider else { return }
        let value = geometryValue(field, in: profile)
        // A drag previews both cards in memory and performs one durable save.
        // Clear drag ownership before notifying store observers.
        draggedSlider = nil
        if !commit(field: field, text: String(value)) { refreshFromStore() }
        onGeometryPreview?(profile)
    }
    @discardableResult func mouseDown(at point: CGPoint) -> Bool {
        guard layer.bounds.contains(point) else {
            if popover != nil { dismissPopover(); return true }
            return false
        }
        if let slider = accessibleSliders.first(where: { $0.rect.contains(point) }) {
            draggedSlider = slider.field; selectedSlider = slider.field; moveSlider(to: point)
        }
        else if let action = accessibleActions.first(where: { $0.rect.contains(point) }) { perform(actionID: action.id) }
        else if popover != nil { dismissPopover() }
        return true
    }
    func dismissPopover() {
        guard popover != nil else { return }
        mouseUp()
        let pieces = popoverLayer.sublayers ?? []
        popover = nil; repaint()
        departingPopover?.removeFromSuperlayer(); departingPopover = nil
        if active, !shouldReduceMotion(), !pieces.isEmpty {
            let departing = CALayer(); departing.frame = layer.bounds; departing.opacity = 0
            pieces.forEach { departing.addSublayer($0) }; layer.addSublayer(departing); departingPopover = departing
            let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 1; fade.toValue = 0
            let move = CABasicAnimation(keyPath: "transform.translation.y"); move.fromValue = 0; move.toValue = -5
            let group = CAAnimationGroup(); group.animations = [fade, move]; group.duration = 0.14
            group.timingFunction = CAMediaTimingFunction(name: .easeOut); departing.add(group, forKey: "profile.popoverClose")
            DispatchQueue.main.asyncAfter(deadline: .now() + 0.15) { [weak self, weak departing] in
                guard let self, let departing, self.departingPopover === departing else { return }
                departing.removeFromSuperlayer(); self.departingPopover = nil
            }
        }
        onChange?()
    }
    func perform(actionID: String) {
        prepareArtworkForInteraction()
        switch actionID {
        case "profile:menu": setPopover(.identity)
        case "profile:backgroundMenu": setPopover(.background)
        case "profile:themeMenu": setPopover(.themeColor)
        case "profile:portraitMenu": setPopover(.portrait)
        case "profile:popoverClose": dismissPopover()
        case "profile:avatar": dismissPopover(); onChooseImage?(.avatar)
        case "profile:background": onChooseImage?(.background)
        case "profile:restoreAvatar": dismissPopover(); restoreImage(.avatar)
        case "profile:resetBackground": restoreImage(.background)
        case "profile:themeDefault": setThemeColor(nil)
        case "profile:themeCustom": onChooseColor?(accent)
        case "profile:toggleDateLabel":
            guard let store else { return }
            do { try store.update { $0.showsBirthday.toggle() } }
            catch { showError(error.localizedDescription) }
        case "profile:visibility":
            isTextHidden.toggle(); popover = nil; repaint()
            if !shouldReduceMotion() {
                let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = isTextHidden ? 1 : 0; fade.toValue = isTextHidden ? 0 : 1
                fade.duration = 0.18; artwork.add(fade, forKey: "profile.visibility")
                let shadeFade = CABasicAnimation(keyPath: "opacity"); shadeFade.fromValue = isTextHidden ? 1 : 0; shadeFade.toValue = isTextHidden ? 0 : 1
                shadeFade.duration = 0.18; backgroundShade.add(shadeFade, forKey: "profile.visibility")
            }
            onVisibilityChange?(); onChange?()
        default:
            if actionID.hasPrefix("profile:theme:") { setThemeColor(String(actionID.dropFirst("profile:theme:".count))); return }
            guard actionID.hasPrefix("profile:"), let field = PersonalProfileField(rawValue: String(actionID.dropFirst("profile:".count))) else { return }
            guard !field.isGeometry else { return }
            dismissPopover()
            onEditField?(field, fieldRect(field), value(field, in: profile))
        }
    }
    private func setPopover(_ next: Popover) {
        mouseUp(); selectedSlider = nil
        departingPopover?.removeFromSuperlayer(); departingPopover = nil
        popover = next; repaint()
        if !shouldReduceMotion() { animateUpdate(popoverLayer) }
        onChange?()
    }
    @discardableResult func commit(field: PersonalProfileField, text: String) -> Bool {
        guard let store else { showError(L10n.text("Profile storage is unavailable.", "个人名片存储不可用。")); return false }
        let input = text.trimmingCharacters(in: .whitespacesAndNewlines)
        let parsedDate = field == .awakeningDate ? Self.parseAwakeningDate(input) : nil
        let parsedBirthday = field == .birthday ? Self.parseBirthday(input) : nil
        if field == .awakeningDate && parsedDate == nil {
            showError(L10n.text("Use YYYY/MM/DD for awakening day.", "苏醒日格式为 YYYY/MM/DD。")); return false
        }
        if field == .birthday && parsedBirthday == nil {
            showError(L10n.text("Use MM/DD for birthday.", "生日格式为 MM/DD。")); return false
        }
        let previous = profile
        let hadError = errorMessage != nil
        errorMessage = nil
        do {
            try store.update { profile in
                switch field {
                case .name: profile.name = String(input.prefix(20))
                case .tag: profile.tag = String((input.hasPrefix("#") ? String(input.dropFirst()) : input).prefix(10))
                case .introduction: profile.introduction = String(input.prefix(150))
                case .awakeningDate: if let parsedDate { profile.awakeningDate = parsedDate }
                case .birthday: if let parsedBirthday { profile.birthdayMonth = parsedBirthday.month; profile.birthdayDay = parsedBirthday.day }
                case .permissionLevel: profile.permissionLevel = min(60, max(1, Int(input) ?? 60))
                case .explorationLevel: profile.explorationLevel = min(7, max(1, Int(input) ?? 7))
                case .operatorsCount: profile.operatorsCount = max(0, Int(input) ?? profile.operatorsCount)
                case .weaponsCount: profile.weaponsCount = max(0, Int(input) ?? profile.weaponsCount)
                case .archivesCount: profile.archivesCount = max(0, Int(input) ?? profile.archivesCount)
                case .backgroundWidth: profile.backgroundWidth = finiteNumber(input, fallback: profile.backgroundWidth, range: 400...900)
                case .backgroundOffsetX: profile.backgroundOffsetX = finiteNumber(input, fallback: profile.backgroundOffsetX, range: -400...400)
                case .backgroundOffsetY: profile.backgroundOffsetY = finiteNumber(input, fallback: profile.backgroundOffsetY, range: -250...250)
                case .thumbnailOffsetX: profile.thumbnailOffsetX = finiteNumber(input, fallback: profile.thumbnailOffsetX * 100, range: -100...100) / 100
                case .thumbnailOffsetY: profile.thumbnailOffsetY = finiteNumber(input, fallback: profile.thumbnailOffsetY * 100, range: -100...100) / 100
                case .avatarZoom: profile.avatarZoom = finiteNumber(input, fallback: profile.avatarZoom, range: 1...20)
                case .avatarOffsetX: profile.avatarOffsetX = finiteNumber(input, fallback: profile.avatarOffsetX * 100, range: -100...100) / 100
                case .avatarOffsetY: profile.avatarOffsetY = finiteNumber(input, fallback: profile.avatarOffsetY * 100, range: -100...100) / 100
                }
            }
            if profile == previous && hadError { repaint(); onChange?() }
            return true
        } catch { showError(error.localizedDescription); return false }
    }
    func setCustomColor(_ color: NSColor) {
        guard let rgb = color.usingColorSpace(.sRGB) else { return }
        setThemeColor(String(format: "%02X%02X%02X", Int((rgb.redComponent * 255).rounded()),
                             Int((rgb.greenComponent * 255).rounded()), Int((rgb.blueComponent * 255).rounded())))
    }
    private func setThemeColor(_ hex: String?) {
        guard let store else { return }
        do { try store.update { $0.themeColorHex = hex } }
        catch { showError(error.localizedDescription) }
    }
    private static func parseAwakeningDate(_ input: String) -> Date? {
        let parts = input.replacingOccurrences(of: "-", with: "/").split(separator: "/", omittingEmptySubsequences: false)
        guard parts.count == 3, let year = Int(parts[0]), let month = Int(parts[1]), let day = Int(parts[2]),
              (1...9999).contains(year), (1...12).contains(month), (1...31).contains(day) else { return nil }
        let calendar = Calendar(identifier: .gregorian)
        guard let date = calendar.date(from: DateComponents(year: year, month: month, day: day, hour: 12)) else { return nil }
        let actual = calendar.dateComponents([.year, .month, .day], from: date)
        return actual.year == year && actual.month == month && actual.day == day ? date : nil
    }
    private static func parseBirthday(_ input: String) -> (month: Int, day: Int)? {
        let parts = input.replacingOccurrences(of: "-", with: "/").split(separator: "/", omittingEmptySubsequences: false)
        guard parts.count == 2, let month = Int(parts[0]), let day = Int(parts[1]),
              (1...12).contains(month), (1...UserProfile.maximumBirthdayDay(in: month)).contains(day) else { return nil }
        return (month, day)
    }
    private static func awakeningString(_ date: Date) -> String {
        let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX")
        formatter.calendar = Calendar(identifier: .gregorian); formatter.dateFormat = "yyyy/MM/dd"
        return formatter.string(from: date)
    }
    private func finiteNumber(_ input: String, fallback: Double, range: ClosedRange<Double>) -> Double {
        guard let number = Double(input), number.isFinite else { return fallback }
        return min(range.upperBound, max(range.lowerBound, number))
    }
    private func restoreImage(_ kind: UserProfileImageKind) {
        guard let store else { return }
        let previous = profile; let hadError = errorMessage != nil
        errorMessage = nil
        do {
            try store.removeImage(kind: kind)
            if previous == profile && hadError { repaint(); onChange?() }
        }
        catch { showError(error.localizedDescription) }
    }
    @discardableResult func importImage(from url: URL, kind: UserProfileImageKind) -> Bool {
        guard let store else { showError(L10n.text("Profile storage is unavailable.", "个人名片存储不可用。")); return false }
        errorMessage = nil
        do { try store.importImage(from: url, kind: kind); return true }
        catch { showError(error.localizedDescription); return false }
    }
    private func value(_ field: PersonalProfileField, in value: UserProfile) -> String {
        switch field {
        case .name: return value.name
        case .tag: return value.tag
        case .introduction: return value.introduction
        case .awakeningDate: return Self.awakeningString(value.awakeningDate)
        case .birthday: return String(format: "%02d/%02d", value.birthdayMonth, value.birthdayDay)
        case .permissionLevel: return String(value.permissionLevel)
        case .explorationLevel: return String(value.explorationLevel)
        case .operatorsCount: return String(value.operatorsCount)
        case .weaponsCount: return String(value.weaponsCount)
        case .archivesCount: return String(value.archivesCount)
        case .backgroundWidth: return String(format: "%.0f", value.backgroundWidth)
        case .backgroundOffsetX: return String(format: "%.0f", value.backgroundOffsetX)
        case .backgroundOffsetY: return String(format: "%.0f", value.backgroundOffsetY)
        case .thumbnailOffsetX: return String(format: "%.0f", value.thumbnailOffsetX * 100)
        case .thumbnailOffsetY: return String(format: "%.0f", value.thumbnailOffsetY * 100)
        case .avatarZoom: return String(format: "%.2f", value.avatarZoom)
        case .avatarOffsetX: return String(format: "%.0f", value.avatarOffsetX * 100)
        case .avatarOffsetY: return String(format: "%.0f", value.avatarOffsetY * 100)
        }
    }
    func fieldRect(_ field: PersonalProfileField) -> CGRect {
        switch field {
        case .name, .tag: return CGRect(x: 97, y: 37, width: 286, height: 26)
        case .introduction: return Self.introductionRect.insetBy(dx: 5, dy: 9)
        case .awakeningDate, .birthday: return Self.dateValueRect
        case .permissionLevel: return CGRect(x: 180, y: 145, width: 57, height: 25)
        case .explorationLevel: return CGRect(x: 180, y: 177, width: 57, height: 25)
        case .operatorsCount: return CGRect(x: 18, y: 219, width: 74, height: 42)
        case .weaponsCount: return CGRect(x: 96, y: 219, width: 74, height: 42)
        case .archivesCount: return CGRect(x: 174, y: 219, width: 74, height: 42)
        case .backgroundWidth, .backgroundOffsetX, .backgroundOffsetY, .thumbnailOffsetX, .thumbnailOffsetY:
            let index = PersonalProfileField.geometryFields.firstIndex(of: field) ?? 0
            return CGRect(x: 260, y: Self.backgroundRect.minY - 162 + CGFloat(index) * 30, width: 55, height: 23)
        case .avatarZoom, .avatarOffsetX, .avatarOffsetY:
            let index = PersonalProfileField.portraitFields.firstIndex(of: field) ?? 0
            return CGRect(x: 260, y: 151 + CGFloat(index) * 30, width: 55, height: 23)
        }
    }
    private func updateImages() {
        background.contents = cgImage(store?.image(for: .background))
    }
    private func prepareArtworkForInteraction() {
        guard !artworkEnabled else { return }
        artworkEnabled = true
        updateBackgroundGeometry()
        repaint()
    }
    private func cgImage(_ value: NSImage?) -> CGImage? {
        guard let value else { return nil }; var rect = CGRect(origin: .zero, size: value.size)
        return value.cgImage(forProposedRect: &rect, context: nil, hints: nil)
    }
    private func repaint() {
        guard artworkEnabled else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        defer { CATransaction.commit() }
        if imagesNeedUpdate { updateImages(); imagesNeedUpdate = false }
        artwork.sublayers?.forEach { $0.removeFromSuperlayer() }; valueLayers.removeAll()
        toolbar.sublayers?.forEach { $0.removeFromSuperlayer() }
        popoverLayer.sublayers?.forEach { $0.removeFromSuperlayer() }
        artwork.opacity = isTextHidden ? 0 : 1
        backgroundShade.opacity = isTextHidden ? 0 : 1
        let hasBackdrop = background.contents != nil
        let ink = hasBackdrop ? NSColor.white : NSColor(white: dark ? 0.96 : 0.10, alpha: 1)
        let muted = ink.withAlphaComponent(0.65)
        if hasBackdrop {
            // A moved image must not leave white identity labels over a light
            // desktop. This fixed wash belongs to the hideable text surface.
            let contrast = CAGradientLayer(); contrast.name = "profile.textContrast"
            contrast.frame = CGRect(x: 0, y: 0, width: 272, height: 334)
            contrast.startPoint = CGPoint(x: 0, y: 0.5); contrast.endPoint = CGPoint(x: 1, y: 0.5)
            contrast.colors = [NSColor.black.withAlphaComponent(0.52).cgColor, NSColor.black.withAlphaComponent(0.46).cgColor, NSColor.clear.cgColor]
            contrast.locations = [0, 0.80, 1]
            let fade = CAGradientLayer(); fade.frame = contrast.bounds
            fade.colors = [NSColor.clear.cgColor, NSColor.black.cgColor, NSColor.black.cgColor, NSColor.clear.cgColor]
            fade.locations = [0, 0.04, 0.95, 1]; contrast.mask = fade
            artwork.addSublayer(contrast)
        }
        background.backgroundColor = NSColor.clear.cgColor
        backgroundShade.colors = hasBackdrop ? [NSColor.black.withAlphaComponent(0.70).cgColor, NSColor.black.withAlphaComponent(0.38).cgColor, NSColor.black.withAlphaComponent(0.03).cgColor] : [NSColor.clear.cgColor, NSColor.clear.cgColor, NSColor.clear.cgColor]
        text(L10n.text("PERSONAL PROFILE", "个人名片"), rect: CGRect(x: 18, y: 4, width: 360, height: 21), size: 13, weight: .bold, color: ink, parent: artwork)
        let portrait = HUDPortraitArtwork.makeLayer(image: cgImage(store?.image(for: .avatar)), profile: profile,
            size: Self.portraitRect.size, ink: ink, accent: accent, contentsScale: scale,
            orientation: store?.imageOrientation(for: .avatar) ?? 1)
        portrait.frame = Self.portraitRect
        artwork.addSublayer(portrait)
        let name = text(profile.name + "#" + profile.tag, rect: CGRect(x: 97, y: 38, width: 286, height: 28), size: 15, weight: .semibold, color: ink, parent: artwork)
        valueLayers[.name] = name; valueLayers[.tag] = name
        // A small registry block and crossed brackets sit beneath the name.
        let nameMark = CGMutablePath(); nameMark.move(to: CGPoint(x: 99, y: 61)); nameMark.addLine(to: CGPoint(x: 99, y: 69)); nameMark.addLine(to: CGPoint(x: 106, y: 69))
        nameMark.move(to: CGPoint(x: 136, y: 61)); nameMark.addLine(to: CGPoint(x: 143, y: 61)); nameMark.addLine(to: CGPoint(x: 143, y: 69))
        nameMark.move(to: CGPoint(x: 109, y: 61)); nameMark.addLine(to: CGPoint(x: 118, y: 69)); nameMark.move(to: CGPoint(x: 118, y: 61)); nameMark.addLine(to: CGPoint(x: 109, y: 69))
        shape(nameMark, stroke: muted.withAlphaComponent(0.45), width: 0.9, parent: artwork)
        fill(CGRect(x: 98, y: 72, width: 20, height: 3), color: muted.withAlphaComponent(0.75), parent: artwork)
        fill(Self.dateLabelRect, color: NSColor.black.withAlphaComponent(0.73), parent: artwork)
        for x: CGFloat in [102, 109] {
            let triangle = CGMutablePath(); triangle.move(to: CGPoint(x: x, y: 81)); triangle.addLine(to: CGPoint(x: x + 5, y: 85)); triangle.addLine(to: CGPoint(x: x, y: 89)); triangle.closeSubpath()
            shape(triangle, fill: accent, parent: artwork)
        }
        let dateField: PersonalProfileField = profile.showsBirthday ? .birthday : .awakeningDate
        let caption = profile.showsBirthday ? L10n.text("Birthday", "生日") : L10n.text("Awakening", "苏醒日")
        let captionFont = NSFont.systemFont(ofSize: 8, weight: .bold)
        let captionHeight = ceil(captionFont.ascender - captionFont.descender)
        dateCaption = text(caption, rect: CGRect(x: 117, y: Self.dateLabelRect.midY - captionHeight / 2, width: 46, height: captionHeight), size: 8, weight: .bold, color: .white, parent: artwork)
        dateCaption?.name = "profile.dateCaption"
        fill(Self.dateValueRect, color: NSColor.white.withAlphaComponent(0.90), parent: artwork)
        let valueFont = NSFont.systemFont(ofSize: 10, weight: .semibold)
        let valueHeight = ceil(valueFont.ascender - valueFont.descender)
        valueLayers[dateField] = text(value(dateField, in: profile), rect: CGRect(x: 169, y: Self.dateValueRect.midY - valueHeight / 2, width: 84, height: valueHeight), size: 10, weight: .semibold, color: NSColor(white: 0.16, alpha: 1), parent: artwork)
        HUDControlHighlightLayer.add(to: artwork, rect: Self.dateLabelRect)
        HUDControlHighlightLayer.add(to: artwork, rect: Self.dateValueRect)
        text("UID: " + profile.uid, rect: CGRect(x: 98, y: 99, width: 260, height: 18), size: 11, color: muted, parent: artwork)
        let menu = CALayer(); menu.frame = Self.menuRect; menu.cornerRadius = Self.menuRect.height / 2
        menu.backgroundColor = ink.withAlphaComponent(0.85).cgColor; artwork.addSublayer(menu)
        for x: CGFloat in [5, 9.5, 14] { fill(CGRect(x: x - 1, y: 8.5, width: 2, height: 2), color: NSColor(white: dark || hasBackdrop ? 0.12 : 0.94, alpha: 1), parent: menu) }
        HUDControlHighlightLayer.add(to: menu, rect: menu.bounds, shape: .ellipse)
        level(.permissionLevel, y: 145, ink: ink)
        level(.explorationLevel, y: 177, ink: ink)
        marker(y: 211)
        for field in [PersonalProfileField.operatorsCount, .weaponsCount, .archivesCount] {
            let rect = fieldRect(field)
            fill(rect, color: NSColor(white: dark || hasBackdrop ? 0.01 : 1, alpha: 0.29), parent: artwork)
            HUDControlHighlightLayer.add(to: artwork, rect: rect)
            let item = text(value(field, in: profile), rect: CGRect(x: rect.minX + 6, y: rect.minY + 1, width: rect.width - 12, height: 25), size: 21, weight: .medium, color: accent, parent: artwork)
            valueLayers[field] = item
            text("· " + field.title, rect: CGRect(x: rect.minX + 6, y: rect.minY + 26, width: rect.width - 9, height: 15), size: 9, color: accent.withAlphaComponent(0.88), parent: artwork)
        }
        marker(y: 272)
        text(L10n.text("REGION CONSTRUCTION", "地区建设概况"), rect: CGRect(x: 18, y: 269, width: 187, height: 17), size: 10, weight: .bold, color: accent, parent: artwork)
        fill(CGRect(x: 18, y: 290, width: 190, height: 31), color: ink.withAlphaComponent(0.13), parent: artwork)
        text(L10n.text("Work Mode", "工作模式"), rect: CGRect(x: 25, y: 299, width: 75, height: 16), size: 10, color: ink, parent: artwork)
        shownHours = formattedHours
        workValue = text(shownHours, rect: CGRect(x: 105, y: 292, width: 70, height: 28), size: 21, weight: .medium, color: ink, parent: artwork, alignment: .right)
        text(L10n.text("h", "小时"), rect: CGRect(x: 180, y: 302, width: 25, height: 13), size: 8, color: muted, parent: artwork)
        drawIntroduction(ink: ink, muted: muted)
        drawToolbar(ink: ink)
        if let popover {
            drawPopover(popover, ink: ink)
            func suppressUnderlyingFeedback(_ node: CALayer) {
                (node as? HUDControlHighlightLayer)?.setEnabled(false)
                node.sublayers?.forEach(suppressUnderlyingFeedback)
            }
            suppressUnderlyingFeedback(artwork); suppressUnderlyingFeedback(toolbar)
        }
        func applyLocalAccent(_ node: CALayer) {
            (node as? HUDControlHighlightLayer)?.accentOverride = accent
            node.sublayers?.forEach(applyLocalAccent)
        }
        applyLocalAccent(layer)
        if let errorMessage {
            text(errorMessage, rect: CGRect(x: 18, y: 322, width: 372, height: 12), size: 9, color: .systemRed, parent: toolbar, wrapped: true)
        }
    }
    private func updateBackgroundGeometry() {
        guard artworkEnabled else { return }
        CATransaction.begin(); CATransaction.setDisableActions(true)
        let previous = background.frame
        let width = CGFloat(profile.backgroundWidth)
        background.frame = CGRect(x: (400 - width) / 2 + CGFloat(profile.backgroundOffsetX),
                                  y: CGFloat(profile.backgroundOffsetY), width: width, height: 334)
        backgroundShade.frame = background.bounds
        let horizontal = CAGradientLayer(); horizontal.frame = background.bounds
        horizontal.startPoint = CGPoint(x: 0, y: 0.5); horizontal.endPoint = CGPoint(x: 1, y: 0.5)
        horizontal.colors = [NSColor.clear.cgColor, NSColor.black.cgColor, NSColor.black.cgColor, NSColor.clear.cgColor]
        horizontal.locations = [0, 0.08, 0.92, 1]
        let vertical = CAGradientLayer(); vertical.frame = background.bounds
        vertical.startPoint = CGPoint(x: 0.5, y: 0); vertical.endPoint = CGPoint(x: 0.5, y: 1)
        vertical.colors = horizontal.colors; vertical.locations = [0, 0.08, 0.90, 1]
        horizontal.mask = vertical; background.mask = horizontal
        if active, !isDragging, !shouldReduceMotion(), previous != background.frame {
            let position = CABasicAnimation(keyPath: "position")
            position.fromValue = NSValue(point: CGPoint(x: previous.midX, y: previous.midY)); position.toValue = NSValue(point: background.position)
            let bounds = CABasicAnimation(keyPath: "bounds")
            bounds.fromValue = NSValue(rect: CGRect(origin: .zero, size: previous.size)); bounds.toValue = NSValue(rect: background.bounds)
            let move = CAAnimationGroup(); move.animations = [position, bounds]; move.duration = 0.18
            move.timingFunction = CAMediaTimingFunction(name: .easeOut); background.add(move, forKey: "profile.backgroundGeometry")
        }
        CATransaction.commit()
    }
    private func popoverActions(_ value: Popover) -> [PersonalProfileCanvasAction] {
        if value == .identity {
            let rows: [(String, String)] = [("name", L10n.text("Edit name", "修改名称")), ("tag", L10n.text("Edit #", "修改 #")),
                                           ("avatar", L10n.text("Change profile picture", "更换头像")), ("portraitMenu", L10n.text("Adjust portrait", "调整头像")),
                                           ("restoreAvatar", L10n.text("Restore default picture", "恢复默认头像"))]
            return rows.enumerated().map { index, row in PersonalProfileCanvasAction(id: "profile:" + row.0, label: row.1,
                rect: CGRect(x: 23, y: 141 + CGFloat(index) * 26, width: 178, height: 24)) }
                + [PersonalProfileCanvasAction(id: "profile:popoverClose", label: L10n.text("Close", "关闭"), rect: Self.menuRect)]
        }
        if value == .themeColor {
            var choices = [PersonalProfileCanvasAction(id: "profile:backgroundMenu", label: L10n.text("Back to card theme", "返回名片主题"), rect: CGRect(x: 136, y: 164, width: 23, height: 23)),
                           PersonalProfileCanvasAction(id: "profile:popoverClose", label: L10n.text("Close", "关闭"), rect: CGRect(x: 361, y: 164, width: 23, height: 23)),
                           PersonalProfileCanvasAction(id: "profile:themeDefault", label: L10n.text("Follow HUD theme", "跟随浮层主题"), rect: CGRect(x: 136, y: 250, width: 242, height: 25))]
            for (index, hex) in HUDSettingsController.presetAccentHexes.enumerated() {
                choices.append(PersonalProfileCanvasAction(id: "profile:theme:" + hex, label: L10n.text("Card color ", "名片颜色 ") + hex,
                    rect: CGRect(x: 138 + CGFloat(index) * 40, y: 199, width: 32, height: 32)))
            }
            choices.append(PersonalProfileCanvasAction(id: "profile:themeCustom", label: L10n.text("Custom card color", "自定名片颜色"), rect: CGRect(x: 338, y: 199, width: 32, height: 32)))
            return choices
        }
        var result: [PersonalProfileCanvasAction]
        if value == .portrait {
            result = [PersonalProfileCanvasAction(id: "profile:menu", label: L10n.text("Back to profile menu", "返回名片菜单"), rect: CGRect(x: 110, y: 117, width: 23, height: 23)),
                      PersonalProfileCanvasAction(id: "profile:popoverClose", label: L10n.text("Close", "关闭"), rect: CGRect(x: 360, y: 117, width: 23, height: 23))]
        } else {
            result = [PersonalProfileCanvasAction(id: "profile:popoverClose", label: L10n.text("Close", "关闭"), rect: CGRect(x: 361, y: 69, width: 23, height: 23)),
                      PersonalProfileCanvasAction(id: "profile:background", label: L10n.text("Choose background", "选择背景"), rect: CGRect(x: 136, y: 97, width: 116, height: 23)),
                      PersonalProfileCanvasAction(id: "profile:resetBackground", label: L10n.text("Restore default", "恢复默认"), rect: CGRect(x: 259, y: 97, width: 119, height: 23)),
                      PersonalProfileCanvasAction(id: "profile:themeMenu", label: L10n.text("Card color", "名片颜色"), rect: CGRect(x: 331, y: 69, width: 23, height: 23))]
        }
        return result
    }
    private func drawPopover(_ value: Popover, ink: NSColor) {
        let rect = popoverBounds ?? .zero
        let backing = CALayer(); backing.frame = rect.offsetBy(dx: -3, dy: 4)
        backing.backgroundColor = NSColor.black.withAlphaComponent(0.30).cgColor; popoverLayer.addSublayer(backing)
        let face = CALayer(); face.frame = rect; face.backgroundColor = NSColor(white: dark ? 0.08 : 0.92, alpha: 0.98).cgColor
        face.borderWidth = 0.7; face.borderColor = accent.withAlphaComponent(0.7).cgColor; popoverLayer.addSublayer(face)
        let color = NSColor(white: dark ? 0.96 : 0.10, alpha: 1)
        if value == .background || value == .portrait {
            let portrait = value == .portrait
            let header = portrait ? L10n.text("PORTRAIT", "调整头像") : L10n.text("CARD THEME", "名片主题")
            text(header, rect: portrait ? CGRect(x: 143, y: 121, width: 204, height: 20) : CGRect(x: 138, y: 72, width: 182, height: 20), size: 12, weight: .bold, color: color, parent: popoverLayer)
            for slider in accessibleSliders {
                let row = fieldRect(slider.field), track = slider.rect.insetBy(dx: 4, dy: 0)
                text(slider.label, rect: CGRect(x: slider.rect.minX, y: row.minY, width: slider.rect.width - 62, height: 13), size: 10, color: color, parent: popoverLayer)
                text(slider.valueDescription, rect: CGRect(x: slider.rect.maxX - 61, y: row.minY, width: 61, height: 13), size: 10, weight: .semibold, color: color, parent: popoverLayer, alignment: .right)
                let y = track.midY
                let fraction = CGFloat((slider.value - slider.minimum) / (slider.maximum - slider.minimum))
                let x = track.minX + track.width * fraction
                fill(CGRect(x: track.minX, y: y - 1, width: track.width, height: 2), color: color.withAlphaComponent(0.19), parent: popoverLayer)
                fill(CGRect(x: track.minX, y: y - 1, width: track.width * fraction, height: 2), color: accent.withAlphaComponent(0.85), parent: popoverLayer)
                if slider.minimum < 0 {
                    fill(CGRect(x: track.midX - 0.5, y: y - 3, width: 1, height: 6), color: color.withAlphaComponent(0.40), parent: popoverLayer)
                }
                let handle = CGRect(x: x - 3.5, y: y - 4, width: 7, height: 8)
                shape(CGPath(roundedRect: handle, cornerWidth: 1, cornerHeight: 1, transform: nil), fill: accent, stroke: color.withAlphaComponent(0.65), width: 0.6, parent: popoverLayer)
                HUDControlHighlightLayer.add(to: popoverLayer, rect: slider.rect, shape: .cutCorner)
            }
        }
        if value == .themeColor {
            text(L10n.text("CARD COLOR", "名片颜色"), rect: CGRect(x: 169, y: 168, width: 182, height: 20), size: 12, weight: .bold, color: color, parent: popoverLayer)
        }
        for action in popoverActions(value) {
            guard action.id != "profile:popoverClose" || value != .identity else { continue }
            if action.id.hasPrefix("profile:theme:") || action.id == "profile:themeMenu" {
                var swatchProfile = profile
                if action.id.hasPrefix("profile:theme:") { swatchProfile.themeColorHex = String(action.id.dropFirst("profile:theme:".count)) }
                let swatch = swatchProfile.resolvedAccent(fallback: hudAccent)
                fill(action.rect.insetBy(dx: 4, dy: 4), color: swatch, parent: popoverLayer)
                HUDControlHighlightLayer.add(to: popoverLayer, rect: action.rect, shape: .cutCorner, framed: true)
                if action.id == "profile:theme:" + (profile.themeColorHex ?? "") {
                    shape(CGPath(rect: action.rect, transform: nil), stroke: color, width: 1.2, parent: popoverLayer)
                }
                continue
            }
            let label: String
            if action.id == "profile:popoverClose" { label = "×" }
            else if (action.id == "profile:menu" && value == .portrait) || (action.id == "profile:backgroundMenu" && value == .themeColor) { label = "‹" }
            else if action.id == "profile:themeCustom" { label = "◉" }
            else { label = action.label }
            fill(action.rect, color: color.withAlphaComponent(0.07), parent: popoverLayer)
            HUDControlHighlightLayer.add(to: popoverLayer, rect: action.rect, shape: .cutCorner, framed: true)
            text(label, rect: action.rect.insetBy(dx: 6, dy: 5), size: 10, weight: .semibold, color: color, parent: popoverLayer,
                 alignment: value == .identity ? .left : .center)
        }
    }
    private func drawIntroduction(ink: NSColor, muted: NSColor) {
        let quoteInk = (dark || background.contents != nil) ? NSColor.white.withAlphaComponent(0.96) : ink
        text("“", rect: CGRect(x: 257, y: 152, width: 25, height: 30), size: 31, weight: .bold, color: quoteInk, parent: artwork)
        text("”", rect: CGRect(x: 366, y: 241, width: 25, height: 30), size: 31, weight: .bold, color: quoteInk, parent: artwork)
        let item = text(profile.introduction.isEmpty ? "…" : profile.introduction,
                        rect: Self.introductionRect.insetBy(dx: 5, dy: 9), size: 10, weight: .medium, color: ink, parent: artwork, wrapped: true)
        valueLayers[.introduction] = item
        let pencil = CGMutablePath(); pencil.move(to: CGPoint(x: 373, y: 171)); pencil.addLine(to: CGPoint(x: 383, y: 161));
        pencil.addLine(to: CGPoint(x: 386, y: 164)); pencil.addLine(to: CGPoint(x: 376, y: 174)); pencil.addLine(to: CGPoint(x: 372, y: 175)); pencil.closeSubpath()
        pencil.move(to: CGPoint(x: 371, y: 177)); pencil.addLine(to: CGPoint(x: 387, y: 177))
        shape(pencil, stroke: ink, width: 1.1, parent: artwork)
        HUDControlHighlightLayer.add(to: artwork, rect: Self.introductionActionRect)
    }
    private func drawToolbar(ink: NSColor) {
        HUDControlHighlightLayer.add(to: toolbar, rect: Self.backgroundRect, shape: .cutCorner)
        let eyeRect = Self.visibilityRect
        let plate = CGRect(x: eyeRect.midX - 12, y: eyeRect.midY - 12, width: 24, height: 24)
        shape(CGPath(ellipseIn: plate, transform: nil), fill: NSColor(white: dark || background.contents != nil ? 0 : 1, alpha: 0.26), parent: toolbar)
        HUDControlHighlightLayer.add(to: toolbar, rect: eyeRect, shape: .ellipse)
        text(L10n.text("Card theme", "更换名片主题"), rect: CGRect(x: 246, y: 300, width: 83, height: 17), size: 10, weight: .medium, color: ink, parent: toolbar)
        let theme = CGMutablePath(); theme.addRect(CGRect(x: 333, y: 299, width: 13, height: 14))
        theme.move(to: CGPoint(x: 335, y: 309)); theme.addLine(to: CGPoint(x: 339, y: 302)); theme.addLine(to: CGPoint(x: 343, y: 307))
        theme.move(to: CGPoint(x: 342, y: 314)); theme.addLine(to: CGPoint(x: 349, y: 305))
        shape(theme, stroke: ink, width: 1.2, parent: toolbar)
        fill(CGRect(x: 357, y: 298, width: 1.3, height: 17), color: ink.withAlphaComponent(0.74), parent: toolbar)
        let eye = CGMutablePath(); eye.move(to: CGPoint(x: 366, y: 307)); eye.addQuadCurve(to: CGPoint(x: 387, y: 307), control: CGPoint(x: 377, y: 294))
        eye.addQuadCurve(to: CGPoint(x: 366, y: 307), control: CGPoint(x: 376, y: 319)); eye.addEllipse(in: CGRect(x: 374, y: 303, width: 6, height: 7))
        if !isTextHidden { eye.move(to: CGPoint(x: 368, y: 317)); eye.addLine(to: CGPoint(x: 385, y: 296)) }
        shape(eye, stroke: ink, width: 1.4, parent: toolbar)
    }

    private func level(_ field: PersonalProfileField, y: CGFloat, ink: NSColor) {
        fill(CGRect(x: 18, y: y, width: 219, height: 25), color: NSColor.black.withAlphaComponent(0.56), parent: artwork)
        artwork.addSublayer(HUDProfileLevelArtwork.makeLayer(field == .permissionLevel ? .authority : .exploration,
            frame: CGRect(x: 21, y: y + 2, width: 22, height: 21), color: .white, contentsScale: scale))
        text(field.title, rect: CGRect(x: 46, y: y + 4, width: 133, height: 19), size: 12, color: .white, parent: artwork)
        let rect = fieldRect(field); HUDControlHighlightLayer.add(to: artwork, rect: rect)
        valueLayers[field] = text(value(field, in: profile), rect: rect.insetBy(dx: 5, dy: 1), size: 20, weight: .medium, color: .white, parent: artwork, alignment: .right)
    }
    private func marker(y: CGFloat) { fill(CGRect(x: 7, y: y + 2, width: 6, height: 3), color: accent, parent: artwork) }
    private func control(_ value: String, rect: CGRect, ink: NSColor) {
        fill(rect, color: ink.withAlphaComponent(0.09), parent: artwork)
        HUDControlHighlightLayer.add(to: artwork, rect: rect, shape: .cutCorner, framed: true)
        text(value, rect: rect.insetBy(dx: 5, dy: 5), size: 10, weight: .semibold, color: ink, parent: artwork, alignment: .center)
    }
    @discardableResult private func text(_ value: String, rect: CGRect, size: CGFloat, weight: NSFont.Weight = .regular,
                                        color: NSColor, parent: CALayer, alignment: CATextLayerAlignmentMode = .left, wrapped: Bool = false) -> CATextLayer {
        let item = CATextLayer(); item.frame = rect; item.string = value
        item.font = NSFont.systemFont(ofSize: size, weight: weight); item.fontSize = size
        item.foregroundColor = color.cgColor; item.alignmentMode = alignment; item.truncationMode = .end
        item.isWrapped = wrapped; item.contentsScale = HUDRenderScale.contentScale(for: item, baseScale: scale)
        parent.addSublayer(item); return item
    }
    private func fill(_ rect: CGRect, color: NSColor, parent: CALayer) { shape(CGPath(rect: rect, transform: nil), fill: color, parent: parent) }
    private func shape(_ path: CGPath, fill: NSColor? = nil, stroke: NSColor? = nil, width: CGFloat = 1, parent: CALayer) {
        let item = CAShapeLayer(); item.path = path; item.fillColor = fill?.cgColor; item.strokeColor = stroke?.cgColor; item.lineWidth = width
        item.contentsScale = scale; parent.addSublayer(item)
    }
    private func animateUpdate(_ item: CALayer) {
        guard !shouldReduceMotion() else { return }
        let opacity = CABasicAnimation(keyPath: "opacity"); opacity.fromValue = 0.3; opacity.toValue = 1
        let movement = CABasicAnimation(keyPath: "transform.translation.y"); movement.fromValue = 3; movement.toValue = 0
        let group = CAAnimationGroup(); group.animations = [opacity, movement]; group.duration = 0.18
        group.timingFunction = CAMediaTimingFunction(name: .easeOut); item.add(group, forKey: "profile.update")
    }
    private func removeAnimations(in node: CALayer) { node.removeAllAnimations(); node.sublayers?.forEach { removeAnimations(in: $0) } }
}
