import AppKit
import QuartzCore
import UniformTypeIdentifiers

/// The personal card's editable values and its background / portrait geometry.
enum PersonalProfileField: String, CaseIterable {
    case name, tag, playerID, introduction, awakeningDate, birthday, permissionLevel, explorationLevel, operatorsCount, weaponsCount, archivesCount
    case backgroundWidth, backgroundZoom, backgroundOffsetX, backgroundOffsetY, thumbnailZoom, thumbnailOffsetX, thumbnailOffsetY
    case avatarZoom, avatarOffsetX, avatarOffsetY
    var title: String {
        switch self {
        case .name: return L10n.text("Name", "名称")
        case .playerID: return L10n.text("Player ID", "玩家 ID")
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
        case .backgroundZoom: return L10n.text("Background zoom", "背景缩放")
        case .backgroundOffsetX: return L10n.text("Background X", "背景 X")
        case .backgroundOffsetY: return L10n.text("Background Y", "背景 Y")
        case .thumbnailZoom: return L10n.text("Thumbnail zoom", "缩略图缩放")
        case .thumbnailOffsetX: return L10n.text("Thumbnail X", "缩略图 X")
        case .thumbnailOffsetY: return L10n.text("Thumbnail Y", "缩略图 Y")
        case .avatarZoom: return L10n.text("Zoom", "缩放")
        case .avatarOffsetX: return L10n.text("Portrait X", "头像 X")
        case .avatarOffsetY: return L10n.text("Portrait Y", "头像 Y")
        }
    }
    var isNumeric: Bool { [.permissionLevel, .explorationLevel, .operatorsCount, .weaponsCount, .archivesCount].contains(self) }
    var isGeometry: Bool { Self.geometryFields.contains(self) || Self.portraitFields.contains(self) }
    /// Inline editor limits (spec §3.20.3).
    var textLimit: Int? {
        switch self {
        case .name: return 20
        case .tag: return 11
        case .playerID: return 64
        case .introduction: return 150
        case .awakeningDate, .birthday: return 10
        case .permissionLevel, .explorationLevel: return 2
        case .operatorsCount, .weaponsCount, .archivesCount: return 5
        default: return nil
        }
    }
    /// The Account field chip that lets an Endfield sync own this value.
    var syncField: HUDAccountPresentation.SyncField? {
        switch self {
        case .name: return .name
        case .tag: return .tag
        case .playerID: return .uid
        case .awakeningDate: return .awakening
        case .permissionLevel, .explorationLevel: return .levels
        case .operatorsCount, .weaponsCount, .archivesCount: return .counts
        default: return nil
        }
    }
    static let geometryFields: [Self] = [.backgroundWidth, .backgroundZoom, .backgroundOffsetX, .backgroundOffsetY, .thumbnailZoom, .thumbnailOffsetX, .thumbnailOffsetY]
    static let portraitFields: [Self] = [.avatarZoom, .avatarOffsetX, .avatarOffsetY]
    static let counters: [Self] = [.operatorsCount, .weaponsCount, .archivesCount]
    /// Values drawn on the card (a change re-renders it and plays vChg).
    static let shown: [Self] = [.name, .tag, .playerID, .introduction, .awakeningDate, .birthday, .permissionLevel, .explorationLevel] + counters
}

/// Personal ID (spec §3.20): the in-game 个人名片 card. The chosen background
/// art spans the whole disc behind the semi-transparent card under a radial
/// mask; the card carries the tinted avatar frame, name and #, registration
/// mark, date chip, UID, level bars, counters, Work Mode hours, the minigame
/// best score, the squared
/// quotation marks and the introduction, every value editable inline. Card
/// theme (colour, art, thumbnail) and portrait pop-overs adjust it live.
final class PersonalProfileCanvas: HUDDesignCanvas {
    /// Live drag previews for the shell's identity card (one save on release).
    var onGeometryPreview: ((UserProfile) -> Void)?
    /// An Endfield sync owns the card's game fields (see `syncedFields`).
    var gameSyncActive = false { didSet { if gameSyncActive != oldValue { editabilityChanged() } } }
    /// The fields that sync writes; nil while the host reports only
    /// `gameSyncActive`, which then locks every game field.
    var syncedFields: Set<HUDAccountPresentation.SyncField>? { didSet { if syncedFields != oldValue { editabilityChanged() } } }
    private let store: UserProfileStore?
    private let workSeconds: () -> TimeInterval
    private let minigameBest: () -> Int
    private var observer: UUID?
    private var profile: UserProfile
    /// In-memory geometry while a slider drags; saved once on release.
    private var preview: UserProfile?
    private var displayed: UserProfile { preview ?? profile }
    private var hudAccent = HUDRuntimeAppearance.accent
    private var errorMessage: String?
    private(set) var isTextHidden = false
    private var lastThemeHex = "C9A2FF"
    private var refs = Refs()
    private var accentBinders: [(CGColor) -> Void] = []
    private var popoverBinders: [() -> Void] = []
    private var editing: (field: PersonalProfileField, text: HUDDesignTextField)?
    private var needsRerender = false
    private var hoursTimer: Timer?
    private var images = ImageCache()
    private var chooser: NSOpenPanel?
    private var colorPanel: NSColorPanel?
    private var colorObserver: NSObjectProtocol?
    private var dialogGeneration = 0

    static let presetHexes = HUDSettingsController.presetAccentHexes
    static let cardOrigin = CGPoint(x: 0, y: HUDMetric.headerHeight)
    static let cardSize = CGSize(width: 590, height: 422)
    /// The art layer (spec): body(−180, −120), 950 × 650, masked by the
    /// centre backdrop disc (430 px around its (475, 343), the disc centre).
    static let artFrame = CGRect(x: -180, y: -120, width: 950, height: 650)
    /// The mask disc's square in the art layer's wrapper.
    static let maskFrame = HUDCentreBackdrop.frame.offsetBy(dx: -cardOrigin.x, dy: -cardOrigin.y)
    /// The art layer covers the whole mask circle, so a wide or moved image
    /// fades out at the ring instead of ending at the 950 × 650 edge.
    static let artHostFrame = artFrame.union(maskFrame)
    static let wellFrame = CGRect(x: 30, y: 52, width: 82, height: 82)
    static let avatarFrame = CGRect(x: 12.5, y: 34.2, width: 117.6, height: 117.6)
    /// ⋯ under the portrait, on the left edge (x 22) the title, level bars,
    /// counters and readout plates share, centred between the portrait's
    /// visible bottom (134) and the first level bar (180).
    static let moreFrame = CGRect(x: 22, y: 143, width: 28, height: 28)
    /// Left column rows below ⋯: the two level bars, the counters, the
    /// REGION CONSTRUCTION label, then the Work Mode and minigame best plates.
    static let levelYs: [CGFloat] = [180, 220], countersY: CGFloat = 268, regionY: CGFloat = 336
    static let readoutFrames = [CGRect(x: 22, y: 352, width: 300, height: 28), CGRect(x: 22, y: 384, width: 300, height: 28)]
    static let introFrame = CGRect(x: 376, y: 196, width: 182, height: 112)

    private var resolvedAccent: NSColor { displayed.resolvedAccent(fallback: hudAccent) }
    private var accent: CGColor { resolvedAccent.usingColorSpace(.sRGB)?.cgColor ?? HUDColor.yellow }
    /// A file chooser or the colour panel is up (the host keeps the HUD open).
    override var isPresentingPanel: Bool { chooser != nil || colorPanel != nil }
    override var isInputLocked: Bool { super.isInputLocked || isPresentingPanel }

    init(context: HUDDesignContext, store: UserProfileStore?, error: String? = nil,
         workSeconds: @escaping () -> TimeInterval = { 0 }, minigameBest: @escaping () -> Int = { 0 }) {
        self.store = store; self.workSeconds = workSeconds; self.minigameBest = minigameBest; errorMessage = error
        profile = store?.profile ?? UserProfile()
        super.init(module: .profile, context: context)
        observer = store?.observe { [weak self] in self?.storeChanged() }
    }

    deinit {
        hoursTimer?.invalidate()
        if let observer { store?.removeObserver(observer) }
        clearColorPanel()
    }

    override func makeContent(for module: HUDModule, style: HUDModuleContentStyle) -> CALayer {
        if !style.accent.isEqual(hudAccent) {
            hudAccent = style.accent
            if renderedLanguage != nil { applyAccent() }
        }
        return super.makeContent(for: module, style: style)
    }

    // MARK: Lifecycle

    override func didActivate() {
        let timer = Timer(timeInterval: 30, repeats: true) { [weak self] _ in self?.refreshWorkDuration() }
        timer.tolerance = 5
        RunLoop.main.add(timer, forMode: .common)
        hoursTimer = timer
        refreshWorkDuration()
        refreshMinigameBest()
        if store == nil, let errorMessage { toast(icon: .warn, "Notice", "提示", detail: errorMessage) }
    }

    override func didDeactivate() {
        hoursTimer?.invalidate(); hoursTimer = nil
        dialogGeneration += 1
        dismissColorPanel()
        let panel = chooser; chooser = nil; panel?.cancel(nil)
        revertPreview(animated: false)
    }

    /// An interrupted slider drag (pop-over closed, module left) is not saved.
    override func closePopover() {
        super.closePopover()
        revertPreview(animated: true)
    }

    private func revertPreview(animated: Bool) {
        guard preview != nil else { return }
        preview = nil
        layoutGeometry(animated: animated)
        onGeometryPreview?(profile)
    }

    override func willLeave() { didDeactivate() }

    override func recordingModeChanged() {
        if editing?.field == .playerID { endEditing(commit: false) }
        guard renderedLanguage != nil else { return }
        rerenderWhenIdle()
        if context.recordingMode, let uid = refs.uid { HUDRedaction.playEntrance(in: [uid], env: env) }
    }

    /// The Work Mode hours: a live readout, updated in place.
    func refreshWorkDuration() {
        guard let hours = refs.hours else { return }
        let next = formattedHours
        guard hours.value.text != next else { return }
        hours.set(next)
        if isActive { Self.playValueChange(hours.value, env: env) }
    }

    /// The minigame's best score, read once per visit (the game cannot run
    /// while this module shows).
    func refreshMinigameBest() {
        guard let best = refs.best else { return }
        let next = formattedBest
        guard best.value.text != next else { return }
        best.set(next)
        if isActive { Self.playValueChange(best.value, env: env) }
    }

    private var formattedBest: String {
        let score = minigameBest()
        return score > 0 ? HUDFormat.grouped(score) : "—"
    }

    private var formattedHours: String {
        let seconds = max(displayed.accumulatedWorkSeconds, workSeconds())
        return String(format: "%.2f", seconds.isFinite ? max(0, seconds) / 3600 : 0)
    }

    private func showError(_ message: String) {
        errorMessage = message
        if isActive { toast(icon: .warn, "Notice", "提示", detail: message) }
    }

    func canEdit(_ field: PersonalProfileField) -> Bool {
        guard store != nil else { return false }
        guard gameSyncActive, let owner = field.syncField else { return true }
        return !(syncedFields ?? Set(HUDAccountPresentation.SyncField.allCases)).contains(owner)
    }

    private func editabilityChanged() {
        if let field = editing?.field, !canEdit(field) { endEditing(commit: false) }
        if renderedLanguage != nil { rerenderWhenIdle() }
    }

    private func rerenderWhenIdle() {
        if editing != nil { needsRerender = true } else { rerender() }
    }

    // MARK: Store

    private func storeChanged() {
        guard let store else { return }
        let old = displayed
        profile = store.profile
        if old.avatarFilename != profile.avatarFilename { images.avatar = nil }
        if old.backgroundFilename != profile.backgroundFilename { images.background = nil }
        guard renderedLanguage != nil else { return }
        let next = displayed
        let changed = PersonalProfileField.shown.filter { Self.value($0, in: old) != Self.value($0, in: next) }
        let structural = !changed.isEmpty || old.showsBirthday != next.showsBirthday
            || old.avatarFilename != next.avatarFilename || old.backgroundFilename != next.backgroundFilename
        if structural {
            guard editing == nil else { needsRerender = true; return }
            rerender()
            for field in changed { if let layer = refs.values[field] { Self.playValueChange(layer, env: env) } }
            if old.showsBirthday != next.showsBirthday {
                for layer in [refs.dateKind, refs.values[next.showsBirthday ? .birthday : .awakeningDate]].compactMap({ $0 }) {
                    Self.playValueChange(layer, env: env)
                }
            }
        } else {
            if old.themeColorHex != next.themeColorHex { applyAccent() }
            layoutGeometry(animated: true)
        }
        popoverBinders.forEach { $0() }
        refreshWorkDuration()
    }

    // MARK: Page

    override func buildPage(_ page: HUDDesignPage) {
        refs = Refs(); accentBinders = []
        let p = displayed
        header.configure(crumbs: ["PERSONAL ID"], code: "MOD-15", title: HUDModuleTitle("Personal ID", "个人名片"),
                         counter: "#" + p.tag, showsBack: false)
        let wrapper = HUDLayer.plain("profile.wrapper")
        let card = HUDLayer.plain("profile.card"), main = HUDLayer.plain("profile.main")
        let bounds = CGRect(origin: .zero, size: Self.cardSize)
        HUDMotion.perform {
            wrapper.frame = CGRect(origin: Self.cardOrigin, size: Self.cardSize)
            card.frame = bounds; card.masksToBounds = true
            main.frame = bounds
        }
        if let art = backgroundImage() { buildArt(art, in: wrapper) }
        buildPlate(in: card, art: refs.artBox != nil)
        refs.card = card; refs.main = main
        var controls: [HUDControl] = []
        buildIdentity(in: main, controls: &controls)
        buildLevels(in: main, controls: &controls)
        buildIntroduction(in: main, controls: &controls)
        HUDMotion.perform { card.addSublayer(main); wrapper.addSublayer(card) }
        buildToolbar(in: card, controls: &controls)
        HUDMotion.perform {
            page.layer.addSublayer(wrapper)
            main.opacity = isTextHidden ? 0 : 1
        }
        page.stagger(wrapper)
        page.add(controls)
        applyAccent()
        layoutGeometry(animated: false)
        renderVisibility(animated: false)
    }

    /// The art spanning the disc: radial-gradient(circle 430px at 475px 343px,
    /// #000 0 52%, rgba(0,0,0,.55) 74%, transparent).
    private func buildArt(_ image: CGImage, in wrapper: CALayer) {
        let host = HUDLayer.plain("profile.art"), box = HUDLayer.plain("profile.art.box"), picture = HUDLayer.plain("profile.art.image")
        let mask = HUDLayer.plain("profile.art.mask")
        HUDMotion.perform {
            host.frame = Self.artHostFrame
            mask.frame = Self.maskFrame.offsetBy(dx: -Self.artHostFrame.minX, dy: -Self.artHostFrame.minY)
            mask.contents = HUDCentreBackdrop.mask; mask.contentsGravity = .resize
            host.mask = mask
            box.masksToBounds = true
            picture.contents = image; picture.contentsGravity = .resizeAspectFill
            box.addSublayer(picture); host.addSublayer(box)
            wrapper.addSublayer(host)
        }
        refs.artHost = host; refs.artBox = box; refs.artImage = picture
    }

    /// Card fill rgba(12,12,12,.38) with chamfer 18 and the 1 px #2e2e2e ring,
    /// and (with art) the darkening overlay.
    private func buildPlate(in card: CALayer, art: Bool) {
        let bounds = CGRect(origin: .zero, size: Self.cardSize)
        let fill = HUDLayer.shape("profile.plate"), ring = HUDLayer.shape("profile.ring")
        HUDMotion.perform {
            fill.frame = bounds; fill.path = HUDShape.chamfer(bounds, cut: 18); fill.fillColor = HUDColor.rgb(0x0C0C0C, 0.38)
            ring.frame = bounds; ring.fillColor = nil; ring.strokeColor = HUDColor.rgb(0x2E2E2E); ring.lineWidth = 1
            let edge = CGMutablePath(), r = bounds.insetBy(dx: 0.5, dy: 0.5)
            edge.move(to: CGPoint(x: r.maxX - 17.5, y: r.minY)); edge.addLine(to: CGPoint(x: r.minX, y: r.minY))
            edge.addLine(to: CGPoint(x: r.minX, y: r.maxY)); edge.addLine(to: CGPoint(x: r.maxX, y: r.maxY))
            edge.addLine(to: CGPoint(x: r.maxX, y: r.minY + 17.5))
            ring.path = edge
            card.addSublayer(fill); card.addSublayer(ring)
        }
        refs.plate = [fill, ring]
        guard art else { return }
        let overlay = HUDLayer.plain("profile.overlay")
        HUDMotion.perform {
            overlay.frame = bounds
            overlay.showArtwork(Self.overlayArtwork(size: bounds.size, scale: env.renderScale), scale: env.renderScale)
            card.addSublayer(overlay)
        }
        refs.overlay = overlay
    }

    private func buildIdentity(in main: CALayer, controls: inout [HUDControl]) {
        let p = displayed
        // Title: PERSONAL PROFILE + 个人名片 (flipped for CJK UIs).
        let lines = HUDModuleTitle("PERSONAL PROFILE", "个人名片").lines()
        let title = HUDTextLayer(lines.title, style: Style.title), subtitle = HUDTextLayer(lines.subtitle, style: Style.titleSmall)
        title.language = lines.titleLanguage; subtitle.language = lines.subtitleLanguage
        let titleWidth = ceil(title.preferredWidth)
        title.lineFrame = CGRect(x: 22, y: 16, width: titleWidth + 1, height: 12)
        subtitle.lineFrame = CGRect(x: 22 + titleWidth + 10, y: 16.5, width: ceil(subtitle.preferredWidth) + 1, height: 11)
        // Portrait well and the tinted frame.
        let well = HUDLayer.plain("profile.well"), frame = HUDLayer.plain("profile.frame")
        HUDMotion.perform {
            well.frame = Self.wellFrame; well.backgroundColor = HUDColor.rgb(0x383838, 0.85); well.masksToBounds = true
            frame.frame = Self.avatarFrame; frame.contentsGravity = .resize
            [title, subtitle, well, frame].forEach(main.addSublayer)
        }
        refs.frame = frame
        if let avatar = avatarImage() {
            let picture = HUDLayer.plain("profile.portrait")
            HUDMotion.perform { picture.contentsGravity = .resize; well.addSublayer(picture) }
            refs.portrait = picture; refs.portraitSource = avatar
        } else {
            let silhouette = HUDLayer.shape("profile.silhouette")
            HUDMotion.perform {
                silhouette.frame = well.bounds
                let path = CGMutablePath(), k = Self.wellFrame.width / 55
                path.addEllipse(in: CGRect(x: (28.05 - 8.25) * k, y: (19.5 - 8.5) * k, width: 16.5 * k, height: 17 * k))
                path.move(to: CGPoint(x: 9.9 * k, y: 47.3 * k))
                path.addCurve(to: CGPoint(x: 45.65 * k, y: 47.3 * k), control1: CGPoint(x: 13.2 * k, y: 24.75 * k),
                              control2: CGPoint(x: 42.35 * k, y: 24.75 * k))
                path.closeSubpath()
                silhouette.path = path; silhouette.fillColor = HUDColor.rgb(0xF5F5F5, 0.63)
                well.addSublayer(silhouette)
            }
            refs.silhouette = silhouette
        }
        // Name + #tag (max-width 300, padding 0 4).
        let name = editable(.name, in: main, controls: &controls)
        let nameText = HUDTextLayer(p.name, style: Style.name), tagText = HUDTextLayer("#" + p.tag, style: Style.tag)
        let tagWidth = ceil(tagText.preferredWidth), nameWidth = min(ceil(nameText.preferredWidth), 292 - 2 - tagWidth)
        HUDMotion.perform {
            nameText.lineFrame = CGRect(x: 4, y: 0, width: nameWidth + 1, height: 30)
            tagText.lineFrame = CGRect(x: 4 + nameWidth + 2, y: 0, width: tagWidth + 1, height: 30)
            name.setFrame(CGRect(x: 132, y: 50, width: 4 + nameWidth + 2 + tagWidth + 4, height: 30), content: [nameText, tagText])
        }
        name.label = L10n.text("Edit name", "修改名称") + ", " + p.name + " #" + p.tag
        refs.values[.tag] = name.layer
        // Registration mark.
        let mark = HUDLayer.shape("profile.registration"), bar = HUDLayer.plain("profile.registration.bar")
        HUDMotion.perform {
            mark.frame = CGRect(x: 138, y: 84, width: 64, height: 16)
            let path = CGMutablePath()
            path.move(to: CGPoint(x: 1, y: 1)); path.addLine(to: CGPoint(x: 1, y: 10)); path.addLine(to: CGPoint(x: 9, y: 10))
            path.move(to: CGPoint(x: 48, y: 1)); path.addLine(to: CGPoint(x: 57, y: 1)); path.addLine(to: CGPoint(x: 57, y: 10))
            path.move(to: CGPoint(x: 14, y: 1)); path.addLine(to: CGPoint(x: 25, y: 10))
            path.move(to: CGPoint(x: 25, y: 1)); path.addLine(to: CGPoint(x: 14, y: 10))
            mark.path = path; mark.fillColor = nil; mark.strokeColor = HUDColor.rgb(0xF5F5F5, 0.45); mark.lineWidth = 1
            bar.frame = CGRect(x: 138, y: 96, width: 26, height: 3.5); bar.backgroundColor = HUDColor.rgb(0xF5F5F5, 0.75)
            main.addSublayer(mark); main.addSublayer(bar)
        }
        buildDateChip(in: main, controls: &controls)
        buildUID(in: main, controls: &controls)
        // ⋯ menu.
        let more = ProfileCircleButton(id: "profile:menu", label: L10n.text("Edit personal profile", "编辑个人名片"), style: .more, env: env)
        HUDMotion.perform { more.layer.frame = Self.moreFrame; main.addSublayer(more.layer) }
        more.onActivate = { [weak self, weak more] in if let more { self?.openMoreMenu(more) } }
        accentBinders.append { [weak more] color in more?.accent = color }
        controls.append(more)
        refs.more = more
    }

    private func buildDateChip(in main: CALayer, controls: inout [HUDControl]) {
        let p = displayed
        let kind = HUDControl(id: "profile:toggleDateLabel", label: p.showsBirthday ? L10n.text("Show awakening day", "切换为苏醒日")
                                                                                  : L10n.text("Show birthday", "切换为生日"), env: env)
        let kindText = HUDTextLayer(p.showsBirthday ? L10n.text("Birthday", "生日") : L10n.text("Awakening", "苏醒日"), style: Style.dateKind)
        let chevrons = HUDLayer.shape("profile.date.chevrons")
        let kindWidth = 6 + 12 + 6 + ceil(kindText.preferredWidth) + 8
        HUDMotion.perform {
            kind.layer.frame = CGRect(x: 138, y: 98, width: kindWidth, height: 20)
            kind.layer.backgroundColor = HUDColor.black(0.73)
            chevrons.frame = CGRect(x: 6, y: 5, width: 12, height: 10)
            let path = CGMutablePath()
            path.addLines(between: [CGPoint(x: 0, y: 0), CGPoint(x: 5, y: 5), CGPoint(x: 0, y: 10)]); path.closeSubpath()
            path.addLines(between: [CGPoint(x: 6, y: 0), CGPoint(x: 11, y: 5), CGPoint(x: 6, y: 10)]); path.closeSubpath()
            chevrons.path = path
            kindText.lineFrame = CGRect(x: 24, y: 5, width: ceil(kindText.preferredWidth) + 1, height: 10)
            kind.layer.addSublayer(chevrons); kind.layer.addSublayer(kindText)
            main.addSublayer(kind.layer)
        }
        kind.setTarget(kind.layer.bounds)
        kind.onActivate = { [weak self] in self?.toggleDateKind() }
        accentBinders.append { [weak chevrons] color in chevrons?.fillColor = color }
        controls.append(kind)
        let field: PersonalProfileField = p.showsBirthday ? .birthday : .awakeningDate
        let value = editable(field, in: main, controls: &controls)
        let valueText = HUDTextLayer(Self.value(field, in: p), style: Style.dateValue)
        let valueWidth = ceil(valueText.preferredWidth)
        HUDMotion.perform {
            valueText.lineFrame = CGRect(x: 10, y: 4, width: valueWidth + 1, height: 12)
            value.setFrame(CGRect(x: 138 + kindWidth, y: 98, width: valueWidth + 20, height: 20), content: [valueText])
            value.layer.backgroundColor = HUDColor.white(0.92)
        }
        refs.dateKind = kind.layer
    }

    private func buildUID(in main: CALayer, controls: inout [HUDControl]) {
        let prefix = HUDTextLayer("UID ", style: Style.uid)
        let prefixWidth = ceil(prefix.preferredWidth)
        prefix.lineFrame = CGRect(x: 138, y: 126, width: prefixWidth + 1, height: 13)
        HUDMotion.perform { main.addSublayer(prefix) }
        if context.recordingMode {
            // Recording mode (spec §3.26): the Personal ID UID becomes a 120 px bar.
            let bar = HUDRedaction.bar(replacing: CGRect(x: 138 + prefixWidth, y: 126, width: 120, height: 13), style: Style.uid,
                                       width: 120, env: env)
            HUDMotion.perform { main.addSublayer(bar) }
            refs.uid = bar
            return
        }
        let uid = editable(.playerID, in: main, controls: &controls)
        let value = HUDTextLayer(displayed.displayedUID, style: Style.uid)
        let width = ceil(value.preferredWidth)
        HUDMotion.perform {
            value.lineFrame = CGRect(x: 4, y: 4, width: width + 1, height: 13)
            uid.setFrame(CGRect(x: 138 + prefixWidth - 4, y: 122, width: width + 8, height: 21), content: [value])
        }
        refs.uid = uid.layer
    }

    private func buildLevels(in main: CALayer, controls: inout [HUDControl]) {
        let p = displayed
        let marks = HUDLayer.shape("profile.marks")
        HUDMotion.perform {
            marks.frame = CGRect(origin: .zero, size: Self.cardSize)
            let path = CGMutablePath()
            for y in [Self.levelYs[0] - 14, Self.countersY - 6, Self.regionY - 2] { path.addRect(CGRect(x: 8, y: y, width: 8, height: 3)) }
            marks.path = path
            main.addSublayer(marks)
        }
        accentBinders.append { [weak marks] color in marks?.fillColor = color }
        for (field, y, kind) in [(PersonalProfileField.permissionLevel, Self.levelYs[0], HUDProfileLevelArtwork.Kind.authority),
                                 (.explorationLevel, Self.levelYs[1], .exploration)] {
            let bar = HUDLayer.plain("profile.level")
            let label = HUDTextLayer(field.title, style: Style.levelLabel)
            HUDMotion.perform {
                bar.frame = CGRect(x: 22, y: y, width: 300, height: 34); bar.backgroundColor = HUDColor.black(0.56)
                bar.addSublayer(HUDProfileLevelArtwork.makeLayer(kind, frame: CGRect(x: 8, y: 5, width: 26, height: 24),
                                                                 color: .white, contentsScale: env.renderScale))
                label.lineFrame = CGRect(x: 44, y: 10, width: 180, height: 14)
                bar.addSublayer(label)
                main.addSublayer(bar)
            }
            let value = editable(field, in: main, controls: &controls)
            let text = HUDTextLayer(Self.value(field, in: p), style: Style.levelValue, alignment: .right)
            let width = ceil(text.preferredWidth) + 8
            HUDMotion.perform {
                text.lineFrame = CGRect(x: 4, y: 2, width: width - 8, height: 25)
                value.setFrame(CGRect(x: 22 + 300 - 12 - width, y: y + 2.5, width: width, height: 29), content: [text])
            }
        }
        for (index, field) in PersonalProfileField.counters.enumerated() {
            let counter = editable(field, in: main, controls: &controls)
            let value = HUDTextLayer(Self.value(field, in: p), style: Style.counterValue)
            let label = HUDTextLayer(field.title, style: Style.counterLabel)
            let bullet = HUDLayer.plain("profile.counter.bullet")
            HUDMotion.perform {
                value.lineFrame = CGRect(x: 9, y: 6, width: 78, height: 27 * 1.05)
                bullet.frame = CGRect(x: 9, y: 6 + 27 * 1.05 + 3 + 4, width: 3, height: 3)
                label.lineFrame = CGRect(x: 17, y: 6 + 27 * 1.05 + 3, width: 70, height: 11)
                counter.setFrame(CGRect(x: 22 + CGFloat(index) * 102, y: Self.countersY, width: 96, height: 58), content: [value, bullet, label])
                counter.layer.backgroundColor = HUDColor.rgb(0x030303, 0.32)
            }
            accentBinders.append { [weak value, weak label, weak bullet] color in
                value?.color = color; label?.color = color.copy(alpha: 0.9) ?? color; bullet?.backgroundColor = color.copy(alpha: 0.9)
            }
        }
        let region = HUDTextLayer(L10n.text("REGION CONSTRUCTION", "地区建设概况"), style: Style.region)
        region.lineFrame = CGRect(x: 22, y: Self.regionY, width: 300, height: 11.5)
        accentBinders.append { [weak region] color in region?.color = color }
        let hours = PlateReadout(id: "profile.work", label: L10n.text("Work Mode", "工作模式"), unit: L10n.text("h", "小时"),
                                 frame: Self.readoutFrames[0])
        let best = PlateReadout(id: "profile.minigameBest", label: L10n.text("OrbiPom! MERGE! best", "融合！山团团！最高分"), unit: nil,
                                frame: Self.readoutFrames[1])
        HUDMotion.perform { [region, hours.layer, best.layer].forEach(main.addSublayer) }
        hours.set(formattedHours); best.set(formattedBest)
        refs.hours = hours; refs.best = best
    }

    private func buildIntroduction(in main: CALayer, controls: inout [HUDControl]) {
        let marks = HUDLayer.shape("profile.quotes"), bars = HUDLayer.shape("profile.quoteBars"), pen = HUDLayer.shape("profile.pen")
        HUDMotion.perform {
            let bounds = CGRect(origin: .zero, size: Self.cardSize)
            marks.frame = bounds; bars.frame = bounds; pen.frame = bounds
            let glyphs = CGMutablePath(), accentBars = CGMutablePath()
            // The opening mark sits over the introduction's left edge, the
            // closing one flush with its right edge below it.
            Self.addQuote(at: CGPoint(x: Self.introFrame.minX, y: 166), closing: false, glyphs: glyphs, bars: accentBars)
            Self.addQuote(at: CGPoint(x: Self.introFrame.maxX - Self.quoteBox.width * Self.quoteScale, y: 305), closing: true,
                          glyphs: glyphs, bars: accentBars)
            marks.path = glyphs; marks.fillColor = HUDColor.white(0.94)
            bars.path = accentBars
            let path = CGMutablePath()
            path.addLines(between: [CGPoint(x: 544, y: 182), CGPoint(x: 556, y: 170), CGPoint(x: 560, y: 174),
                                    CGPoint(x: 548, y: 186), CGPoint(x: 543, y: 187)])
            path.closeSubpath()
            path.move(to: CGPoint(x: 541, y: 190)); path.addLine(to: CGPoint(x: 561, y: 190))
            pen.path = path; pen.fillColor = nil; pen.strokeColor = HUDColor.rgb(0xF5F5F5); pen.lineWidth = 1.3
            [marks, bars, pen].forEach(main.addSublayer)
        }
        accentBars(bars)
        let intro = editable(.introduction, in: main, controls: &controls)
        let text = HUDTextLayer(displayed.introduction.isEmpty ? "…" : displayed.introduction, style: Style.intro)
        HUDMotion.perform {
            text.maximumLines = Int((Self.introFrame.height - 8) / (13 * 1.5))
            text.lineFrame = CGRect(x: 6, y: 4, width: Self.introFrame.width - 12, height: CGFloat(text.maximumLines) * 13 * 1.5)
            intro.setFrame(Self.introFrame, content: [text])
        }
        intro.label = L10n.text("Edit introduction", "编辑个人介绍")
    }

    private func accentBars(_ layer: CAShapeLayer) { accentBinders.append { [weak layer] color in layer?.fillColor = color } }

    /// A slim squared quotation mark (the demo's QMARK, narrowed, flattened
    /// and scaled down): two 4.5-unit strokes with a short tail in a 13.5 × 10
    /// box, × 1.4. The opening mark is the closing one turned 180° about the
    /// box centre. Each has a 6 × 1.5 accent bar at y 13 (x 0 opening, flush
    /// right closing).
    static let quoteBox = CGSize(width: 13.5, height: 10), quoteScale: CGFloat = 1.4
    static func addQuote(at origin: CGPoint, closing: Bool, glyphs: CGMutablePath, bars: CGMutablePath) {
        let k = quoteScale, box = quoteBox
        let points: [CGPoint] = [CGPoint(x: 1, y: 0), CGPoint(x: 5.5, y: 0), CGPoint(x: 5.5, y: 6), CGPoint(x: 3.2, y: 10),
                                 CGPoint(x: 1.6, y: 10), CGPoint(x: 2.9, y: 6), CGPoint(x: 1, y: 6)]
        func place(_ p: CGPoint) -> CGPoint { CGPoint(x: origin.x + p.x * k, y: origin.y + p.y * k) }
        for shift: CGFloat in [0, 7] {
            let shape = points.map { p -> CGPoint in
                let moved = CGPoint(x: p.x + shift, y: p.y)
                return place(closing ? moved : CGPoint(x: box.width - moved.x, y: box.height - moved.y))
            }
            glyphs.addLines(between: shape); glyphs.closeSubpath()
        }
        bars.addRect(CGRect(x: origin.x + (closing ? box.width - 6 : 0) * k, y: origin.y + 13 * k, width: 6 * k, height: 1.5 * k))
    }

    private func buildToolbar(in card: CALayer, controls: inout [HUDControl]) {
        // right 20, bottom 16, gap 10: Card theme, divider, eye.
        let eye = ProfileCircleButton(id: "profile:visibility", label: "", style: .eye, env: env)
        let eyeFrame = CGRect(x: Self.cardSize.width - 20 - 32, y: Self.cardSize.height - 16 - 32, width: 32, height: 32)
        let divider = HUDLayer.plain("profile.toolbar.divider")
        let theme = ProfileThemeButton(id: "profile:backgroundMenu", title: L10n.text("Card theme", "更换名片主题"), env: env)
        let themeWidth = theme.layer.bounds.width
        HUDMotion.perform {
            eye.layer.frame = eyeFrame
            divider.frame = CGRect(x: eyeFrame.minX - 10 - 1.3, y: eyeFrame.midY - 9, width: 1.3, height: 18)
            divider.backgroundColor = HUDColor.rgb(0xF5F5F5, 0.74)
            theme.layer.frame = CGRect(x: divider.frame.minX - 10 - themeWidth, y: eyeFrame.midY - 15, width: themeWidth, height: 30)
            [theme.layer, divider, eye.layer].forEach(card.addSublayer)
        }
        eye.onActivate = { [weak self] in self?.toggleTextHidden() }
        theme.onActivate = { [weak self, weak theme] in
            guard let self, let theme else { return }
            if self.isPopoverOpen { self.closePopover() } else { self.openThemePopover(theme) }
        }
        refs.eye = eye; refs.theme = theme
        controls += [theme, eye]
    }

    private func editable(_ field: PersonalProfileField, in main: CALayer, controls: inout [HUDControl]) -> ProfileEditable {
        let control = ProfileEditable(id: "profile:" + field.rawValue, label: field.title + ": " + Self.value(field, in: displayed), env: env)
        control.isEnabled = canEdit(field) && !isTextHidden
        control.onActivate = { [weak self] in self?.beginEdit(field) }
        HUDMotion.perform { main.addSublayer(control.layer) }
        accentBinders.append { [weak control] color in control?.accent = color }
        refs.values[field] = control.layer
        refs.editables[field] = control
        controls.append(control)
        return control
    }

    // MARK: Accent, geometry, visibility

    /// Follow HUD uses the HUD accent; otherwise the card's own colour. It tints
    /// the frame, marks, counters, region label, quote bars, chevrons and editors.
    private func applyAccent() {
        let color = accent
        HUDMotion.perform {
            refs.frame?.contents = HUDPortraitArtwork.frameImage(accent: resolvedAccent)
            accentBinders.forEach { $0(color) }
        }
        popoverBinders.forEach { $0() }
    }

    private func layoutGeometry(animated: Bool) { layoutArt(animated: animated); layoutPortrait(animated: animated) }

    /// Art box: w = width × 1.58, h = w × .685, centred in the 950 × 650 layer
    /// and offset by X × 1.58, Y × 1.3; zoom scales the image inside it (.2 s E).
    private func layoutArt(animated: Bool) {
        guard let box = refs.artBox, let picture = refs.artImage else { return }
        let p = displayed, duration = animated ? 0.2 : 0
        let frame = Self.artBoxFrame(p).offsetBy(dx: Self.artFrame.minX - Self.artHostFrame.minX, dy: Self.artFrame.minY - Self.artHostFrame.minY)
        let zoom = CGFloat(min(20, max(1, p.backgroundZoom)))
        HUDMotion.animate(box, "position", to: NSValue(point: CGPoint(x: frame.midX, y: frame.midY)), duration: duration, env: env)
        HUDMotion.animate(box, "bounds", to: NSValue(rect: CGRect(origin: .zero, size: frame.size)), duration: duration, env: env)
        HUDMotion.animate(picture, "position", to: NSValue(point: CGPoint(x: frame.width / 2, y: frame.height / 2)), duration: duration, env: env)
        HUDMotion.animate(picture, "bounds", to: NSValue(rect: CGRect(origin: .zero, size: frame.size)), duration: duration, env: env)
        HUDMotion.animate(picture, "transform.scale", to: zoom, duration: duration, env: env)
    }

    static func artBoxFrame(_ p: UserProfile) -> CGRect {
        let width = CGFloat(p.backgroundWidth) * 1.58, height = width * 0.685
        return CGRect(x: (artFrame.width - width) / 2 + CGFloat(p.backgroundOffsetX) * 1.58,
                      y: (artFrame.height - height) / 2 + CGFloat(p.backgroundOffsetY) * 1.3, width: width, height: height)
    }

    /// The portrait pans and zooms with a layer transform (no re-crop per
    /// slider tick) using the shell card's crop semantics; the default
    /// silhouette moves as the demo's translate(X·.41, Y·.41) scale(Z).
    private func layoutPortrait(animated: Bool) {
        let p = displayed, side = Self.wellFrame.width, duration = animated ? 0.2 : 0
        let zoom = CGFloat(min(20, max(1, p.avatarZoom)))
        let x = CGFloat(min(1, max(-1, p.avatarOffsetX))), y = CGFloat(min(1, max(-1, p.avatarOffsetY)))
        let scale = CATransform3DMakeScale(zoom, zoom, 1)
        if let picture = refs.portrait, let source = refs.portraitSource {
            guard let fill = preparePortrait(picture, source: source, zoom: zoom) else { return }
            let shift = CGPoint(x: -x * (zoom * fill.width - 1) * side / 2, y: -y * (zoom * fill.height - 1) * side / 2)
            let transform = CATransform3DConcat(scale, CATransform3DMakeTranslation(shift.x, shift.y, 0))
            HUDMotion.animate(picture, "transform", to: NSValue(caTransform3D: transform), duration: duration, env: env)
        } else if let silhouette = refs.silhouette {
            let transform = CATransform3DConcat(scale, CATransform3DMakeTranslation(x * 41, y * 41, 0))
            HUDMotion.animate(silhouette, "transform", to: NSValue(caTransform3D: transform), duration: duration, env: env)
        }
    }

    /// The whole avatar, downsampled once per zoom step; returns its
    /// aspect-fill size in well sides.
    private func preparePortrait(_ picture: CALayer, source: (image: CGImage, orientation: Int32), zoom: CGFloat) -> CGSize? {
        let step = Int(ceil(zoom - 0.001)), key = "\(step)@\(env.renderScale)"
        if images.portraitKey != key || images.portrait == nil {
            images.portrait = HUDPortraitArtwork.panningImage(source.image, side: Self.wellFrame.width, zoom: Double(step),
                                                              contentsScale: env.renderScale, orientation: source.orientation)
            images.portraitKey = key
        }
        guard let rendered = images.portrait else { return nil }
        let side = Self.wellFrame.width
        HUDMotion.perform {
            if picture.contents as! CGImage? !== rendered.image { picture.contents = rendered.image }
            picture.bounds = CGRect(x: 0, y: 0, width: rendered.fill.width * side, height: rendered.fill.height * side)
            picture.position = CGPoint(x: side / 2, y: side / 2)
        }
        return rendered.fill
    }

    /// Hide text (eye): .main fades (.22 s), the overlay (.2 s) and the card
    /// fill and ring (.24 s); only the art and the toolbar remain.
    private func renderVisibility(animated: Bool) {
        let hidden = isTextHidden
        HUDMotion.animate(refs.main ?? CALayer(), "opacity", to: Float(hidden ? 0 : 1), duration: animated ? 0.22 : 0,
                          timing: HUDEasing.ease, env: env)
        if let overlay = refs.overlay {
            HUDMotion.animate(overlay, "opacity", to: Float(hidden ? 0 : 1), duration: animated ? 0.2 : 0, timing: HUDEasing.ease, env: env)
        }
        for layer in refs.plate {
            HUDMotion.animate(layer, "opacity", to: Float(hidden ? 0 : 1), duration: animated ? 0.24 : 0, timing: HUDEasing.ease, env: env)
        }
        refs.eye?.slashed = !hidden
        refs.eye?.label = hidden ? L10n.text("Show text", "显示文字") : L10n.text("Show background", "显示背景")
        for (field, control) in refs.editables { control.isEnabled = !hidden && canEdit(field) }
        refs.more?.isEnabled = !hidden
    }

    private func toggleTextHidden() {
        endEditing(commit: true); closePopups()
        isTextHidden.toggle()
        renderVisibility(animated: true)
        context.onInputStateChange?()
    }

    private func toggleDateKind() {
        guard let store, !isTextHidden else { return }
        do { try store.update { $0.showsBirthday.toggle() } } catch { showError(error.localizedDescription) }
    }

    // MARK: Inline editors (spec §3.20.3)

    /// Opens the inline editor exactly over the element: #212121, accent ring
    /// and bar, at least 220 wide for name and #, 60 otherwise.
    private func beginEdit(_ field: PersonalProfileField) {
        guard !isTextHidden, canEdit(field), field != .playerID || !context.recordingMode,
              let card = refs.card, let anchor = refs.editables[field == .tag ? .name : field] else { return }
        endEditing(commit: true); closePopups()
        let rect = anchor.layer.frame
        let wide = field == .name || field == .tag, intro = field == .introduction
        let size = CGSize(width: max(rect.width, wide ? 220 : 60), height: rect.height)
        var options = HUDDesignTextField.Options()
        options.multiline = intro
        options.maximumLength = field.textLimit
        options.selectsAllOnBegin = true
        options.style = HUDTextStyle(size: intro ? 13 : min(22, max(12, rect.height * 0.55)), weight: .bold,
                                     color: HUDColor.rgb(0xF5F5F5), lineHeight: intro ? 1.5 : 1)
        options.inset = CGSize(width: 8, height: intro ? 6 : 0)
        options.accent = accent; options.background = HUDColor.rgb(0x212121)
        options.alignment = field.isNumeric ? .right : .left
        let initial = field == .tag ? displayed.tag : Self.value(field, in: displayed)
        let editor = HUDDesignTextField(id: "edit:" + field.rawValue, label: field.title, text: initial, size: size,
                                        options: options, env: env)
        HUDMotion.perform {
            editor.layer.frame = CGRect(origin: rect.origin, size: size)
            editor.layer.zPosition = HUDLayerOrder.menu
            editor.layer.opacity = 0
            card.addSublayer(editor.layer)
        }
        env.applyScale(to: editor.layer)
        HUDMotion.animate(editor.layer, "opacity", to: Float(1), duration: 0.14, timing: HUDEasing.easeOut, env: env)
        editor.onChange = { [weak editor] text in editor?.setInvalid(!Self.isValid(field, text)) }
        editor.onCommit = { [weak self] text in self?.finishEdit(field, text) }
        editor.onCancel = { [weak self] in self?.finishEdit(field, nil) }
        editing = (field, editor)
        register(editor)
        refreshRouter()
        editor.beginEditing()
    }

    /// Enter, Tab and a press elsewhere commit; Esc cancels. Invalid values
    /// are discarded; a saved value re-renders the card and plays vChg.
    private func finishEdit(_ field: PersonalProfileField, _ text: String?) {
        guard let current = editing, current.field == field else { return }
        editing = nil
        HUDMotion.perform { current.text.layer.removeFromSuperlayer() }
        refreshRouter()
        if let text, Self.isValid(field, text), text != (field == .tag ? displayed.tag : Self.value(field, in: displayed)) {
            commit(field, text)
        }
        if needsRerender { needsRerender = false; rerender() }
        context.onInputStateChange?()
    }

    static func isValid(_ field: PersonalProfileField, _ raw: String) -> Bool {
        let value = raw.trimmingCharacters(in: .whitespacesAndNewlines)
        func number(_ range: ClosedRange<Int>) -> Bool {
            !value.isEmpty && value.unicodeScalars.allSatisfy { (48...57).contains($0.value) } && Int(value).map(range.contains) == true
        }
        switch field {
        case .name: return !value.isEmpty && value.count <= 20 && value.rangeOfCharacter(from: .controlCharacters) == nil
        case .tag:
            let digits = value.hasPrefix("#") ? String(value.dropFirst()) : value
            return (1...10).contains(digits.count) && digits.unicodeScalars.allSatisfy { (48...57).contains($0.value) }
        case .playerID: return value.count <= 64
        case .introduction: return raw.count <= 150
        case .awakeningDate: return parseAwakeningDate(value) != nil
        case .birthday: return parseBirthday(value) != nil
        case .permissionLevel: return number(1...60)
        case .explorationLevel: return number(1...7)
        case .operatorsCount, .weaponsCount, .archivesCount: return number(0...99_999)
        default: return false
        }
    }

    private func commit(_ field: PersonalProfileField, _ text: String) {
        guard canEdit(field) else { return }
        guard let store else { showError(L10n.text("Profile storage is unavailable.", "个人名片存储不可用。")); return }
        let input = text.trimmingCharacters(in: .whitespacesAndNewlines)
        errorMessage = nil
        do {
            try store.update { profile in
                switch field {
                case .playerID: profile.playerIDOverride = input
                case .name: profile.name = input
                case .tag: profile.tag = input.hasPrefix("#") ? String(input.dropFirst()) : input
                case .introduction: profile.introduction = input
                case .awakeningDate:
                    if let date = Self.parseAwakeningDate(input) { profile.awakeningDate = date; profile.hasManualAwakeningDate = true }
                case .birthday:
                    if let day = Self.parseBirthday(input) { profile.birthdayMonth = day.month; profile.birthdayDay = day.day }
                case .permissionLevel: profile.permissionLevel = Int(input) ?? profile.permissionLevel
                case .explorationLevel: profile.explorationLevel = Int(input) ?? profile.explorationLevel
                case .operatorsCount: profile.operatorsCount = Int(input) ?? profile.operatorsCount
                case .weaponsCount: profile.weaponsCount = Int(input) ?? profile.weaponsCount
                case .archivesCount: profile.archivesCount = Int(input) ?? profile.archivesCount
                default: break
                }
            }
        } catch { showError(error.localizedDescription) }
    }

    static func parseAwakeningDate(_ input: String) -> Date? {
        let parts = input.replacingOccurrences(of: "-", with: "/").split(separator: "/", omittingEmptySubsequences: false)
        guard parts.count == 3, let year = Int(parts[0]), let month = Int(parts[1]), let day = Int(parts[2]),
              (1...9999).contains(year), (1...12).contains(month), (1...31).contains(day) else { return nil }
        let calendar = Calendar(identifier: .gregorian)
        guard let date = calendar.date(from: DateComponents(year: year, month: month, day: day, hour: 12)) else { return nil }
        let actual = calendar.dateComponents([.year, .month, .day], from: date)
        return actual.year == year && actual.month == month && actual.day == day ? date : nil
    }

    static func parseBirthday(_ input: String) -> (month: Int, day: Int)? {
        let parts = input.replacingOccurrences(of: "-", with: "/").split(separator: "/", omittingEmptySubsequences: false)
        guard parts.count == 2, let month = Int(parts[0]), let day = Int(parts[1]),
              (1...12).contains(month), (1...UserProfile.maximumBirthdayDay(in: month)).contains(day) else { return nil }
        return (month, day)
    }

    static func value(_ field: PersonalProfileField, in value: UserProfile) -> String {
        switch field {
        case .name: return value.name
        case .tag: return value.tag
        case .playerID: return value.displayedUID
        case .introduction: return value.introduction
        case .awakeningDate:
            let formatter = DateFormatter(); formatter.locale = Locale(identifier: "en_US_POSIX")
            formatter.calendar = Calendar(identifier: .gregorian); formatter.dateFormat = "yyyy/MM/dd"
            return formatter.string(from: value.awakeningDate)
        case .birthday: return String(format: "%02d/%02d", value.birthdayMonth, value.birthdayDay)
        case .permissionLevel: return String(value.permissionLevel)
        case .explorationLevel: return String(value.explorationLevel)
        case .operatorsCount: return String(value.operatorsCount)
        case .weaponsCount: return String(value.weaponsCount)
        case .archivesCount: return String(value.archivesCount)
        default: return String(format: "%.2f", geometryValue(field, in: value))
        }
    }

    /// vChg .2 s E: the committed value fades up 3 px.
    static func playValueChange(_ layer: CALayer, env: HUDDesignEnvironment) {
        guard !env.reducesMotion else { return }
        let fade = CABasicAnimation(keyPath: "opacity"); fade.fromValue = 0.3 * layer.opacity; fade.toValue = layer.opacity
        let rise = CABasicAnimation(keyPath: "transform.translation.y"); rise.fromValue = 3; rise.toValue = 0; rise.isAdditive = true
        let group = CAAnimationGroup(); group.animations = [fade, rise]; group.duration = 0.2; group.timingFunction = HUDEasing.standard
        layer.add(group, forKey: "hud.valueChange")
    }

    // MARK: Menus and pop-overs (spec §3.20.4)

    private func openMoreMenu(_ anchor: HUDControl) {
        let items = [
            HUDMenu.Item(id: "name", title: L10n.text("Edit name", "修改名称"), enabled: canEdit(.name)),
            HUDMenu.Item(id: "tag", title: L10n.text("Edit #", "修改 #"), enabled: canEdit(.tag)),
            HUDMenu.Item(id: "picture", title: L10n.text("Change profile picture", "更换头像"), enabled: store != nil),
            HUDMenu.Item(id: "portrait", title: L10n.text("Adjust portrait", "调整头像")),
            HUDMenu.Item(id: "restore", title: L10n.text("Restore default picture", "恢复默认头像"), enabled: store != nil)
        ]
        openMenu(id: "profile.more", items: items, anchor: anchor) { [weak self, weak anchor] item in
            guard let self else { return }
            switch item.id {
            case "name": self.beginEdit(.name)
            case "tag": self.beginEdit(.tag)
            case "picture": self.chooseImage(.avatar)
            case "portrait": if let anchor { self.openPortraitPopover(anchor) }
            default: self.restoreAvatar()
            }
        }
    }

    private func restoreAvatar() {
        guard let store else { return }
        do {
            try store.removeImage(kind: .avatar)
            toast(icon: .reset, "Default picture", "已恢复默认头像")
        } catch { showError(error.localizedDescription) }
    }

    /// Card theme + colour (350 wide, above the button): presets, custom
    /// colour, Follow HUD, Background / Default, then the art and thumbnail sliders.
    private func openThemePopover(_ anchor: HUDControl) {
        let lines = HUDModuleTitle("CARD THEME", "名片主题").lines()
        let width: CGFloat = 350, inner = width - 24
        let rows: [PersonalProfileField] = PersonalProfileField.geometryFields
        let height: CGFloat = 36 + 32 + CGFloat(rows.count) * 34 + 17
        let popover = HUDPopover(title: lines.title, subtitle: lines.subtitle, width: width, contentHeight: height, env: env)
        var controls: [HUDControl] = []
        popoverBinders = []
        HUDMotion.perform {
            for (index, hex) in Self.presetHexes.enumerated() {
                let swatch = HUDSwatch(id: "theme:" + hex, label: L10n.text("Card color ", "名片颜色 ") + "#" + hex,
                                       color: Self.color(hex), env: env)
                swatch.layer.frame.origin = CGPoint(x: CGFloat(index) * 32, y: 2)
                swatch.onActivate = { [weak self] in self?.setThemeColor(hex) }
                popover.content.addSublayer(swatch.layer)
                popoverBinders.append { [weak self, weak swatch] in swatch?.isSelected = self?.displayed.themeColorHex == hex }
                controls.append(swatch)
            }
            let custom = HUDSwatch(id: "theme:custom", label: L10n.text("Custom card color", "自定名片颜色"), color: nil, env: env)
            custom.layer.frame.origin = CGPoint(x: CGFloat(Self.presetHexes.count) * 32, y: 2)
            custom.onActivate = { [weak self] in guard let self else { return }; self.chooseColor(self.resolvedAccent) }
            popover.content.addSublayer(custom.layer)
            popoverBinders.append { [weak self, weak custom] in
                custom?.isSelected = self?.displayed.themeColorHex.map { !Self.presetHexes.contains($0) } ?? false
            }
            controls.append(custom)
            for kind in [HUDButton.Kind.primary, .secondary] {
                let follow = HUDButton(id: "theme:follow", title: L10n.text("Follow HUD", "跟随浮层"), kind: kind, size: .small, env: env)
                follow.place(at: .zero)
                follow.layer.frame.origin = CGPoint(x: inner - follow.layer.bounds.width, y: 1)
                follow.onActivate = { [weak self] in self?.toggleFollow() }
                popover.content.addSublayer(follow.layer)
                popoverBinders.append { [weak self, weak follow] in
                    HUDMotion.perform { follow?.layer.isHidden = (self?.displayed.themeColorHex == nil) != (kind == .primary) }
                }
                controls.append(follow)
            }
            let half = (inner - 6) / 2
            let background = HUDButton(id: "theme:background", title: L10n.text("Background", "背景"), icon: .image, kind: .secondary,
                                       size: .small, env: env)
            background.place(at: CGPoint(x: 0, y: 36), width: half)
            background.onActivate = { [weak self] in self?.chooseImage(.background) }
            let reset = HUDButton(id: "theme:default", title: L10n.text("Default", "默认"), icon: .reset, kind: .secondary, size: .small, env: env)
            reset.place(at: CGPoint(x: half + 6, y: 36), width: half)
            reset.onActivate = { [weak self] in self?.resetBackground() }
            popover.content.addSublayer(background.layer); popover.content.addSublayer(reset.layer)
            controls += [background, reset]
        }
        var y: CGFloat = 68
        for (index, field) in rows.enumerated() {
            if index == 4 {
                let separator = HUDLayer.plain("popover.separator")
                HUDMotion.perform {
                    separator.frame = CGRect(x: 0, y: y + 8, width: inner, height: 1); separator.backgroundColor = HUDColor.separator
                    popover.content.addSublayer(separator)
                }
                y += 17
            }
            controls.append(sliderRow(field, y: y, width: inner, in: popover))
            y += 34
        }
        popoverBinders.forEach { $0() }
        openPopover(popover, anchor: anchor, controls: controls)
    }

    /// The portrait pop-up (330 wide). It opens below ⋯ so the portrait stays visible.
    private func openPortraitPopover(_ anchor: HUDControl) {
        let lines = HUDModuleTitle("PORTRAIT", "调整头像").lines()
        let width: CGFloat = 330
        let popover = HUDPopover(title: lines.title, subtitle: lines.subtitle, width: width, contentHeight: 3 * 34, env: env)
        popoverBinders = []
        let controls = PersonalProfileField.portraitFields.enumerated().map { index, field in
            sliderRow(field, y: CGFloat(index) * 34, width: width - 24, in: popover)
        }
        openPopover(popover, anchor: anchor, controls: controls)
        let more = anchor.rect(in: root)
        HUDMotion.perform { popover.layer.frame.origin = CGPoint(x: 22, y: min(more.maxY + 8, HUDMetric.box.height - popover.layer.bounds.height)) }
    }

    /// `.prow`: label 118, dark slider, `[ value ]` (min-width 52).
    private func sliderRow(_ field: PersonalProfileField, y: CGFloat, width: CGFloat, in popover: HUDPopover) -> HUDSlider {
        let label = HUDTextLayer(field.title, style: Style.popoverLabel)
        let spec = Self.sliderSpec(field)
        let slider = HUDSlider(id: "profile:" + field.rawValue, label: field.title, value: Self.geometryValue(field, in: displayed),
                               range: spec.range, format: spec.format, width: width - 126, dark: true, env: env)
        HUDMotion.perform {
            label.lineFrame = CGRect(x: 0, y: y + 11, width: 118, height: 12)
            slider.layer.frame.origin = CGPoint(x: 126, y: y + 2)
            popover.content.addSublayer(label); popover.content.addSublayer(slider.layer)
        }
        env.applyScale(to: label)
        hostBubble(slider)
        slider.onChange = { [weak self] value, final in self?.sliderChanged(field, value, final: final) }
        popoverBinders.append { [weak self, weak slider] in
            guard let self, let slider, !slider.isDragging else { return }
            slider.setValue(Self.geometryValue(field, in: self.displayed))
        }
        return slider
    }

    static func sliderSpec(_ field: PersonalProfileField) -> (range: HUDSlider.Range, format: HUDValueFormat) {
        switch field {
        case .backgroundWidth: return (HUDSlider.Range(minimum: 400, maximum: 900, step: 1), .integer)
        case .backgroundZoom: return (HUDSlider.Range(minimum: 1, maximum: 20, step: 0.01), .scale)
        case .backgroundOffsetX: return (HUDSlider.Range(minimum: -400, maximum: 400, step: 1, zero: 0), .signed)
        case .backgroundOffsetY: return (HUDSlider.Range(minimum: -250, maximum: 250, step: 1, zero: 0), .signed)
        case .thumbnailZoom, .avatarZoom: return (HUDSlider.Range(minimum: 1, maximum: 5, step: 0.01), .scale)
        default: return (HUDSlider.Range(minimum: -100, maximum: 100, step: 1, zero: 0), .signedPercent)
        }
    }

    /// Sliders repaint live from an in-memory preview and save once on release.
    private func sliderChanged(_ field: PersonalProfileField, _ value: Double, final: Bool) {
        var next = displayed
        Self.applyGeometry(field, value: value, to: &next)
        preview = next
        layoutGeometry(animated: true)
        onGeometryPreview?(next)
        guard final else { return }
        preview = nil
        guard let store else { return }
        do {
            try store.update { saved in
                for field in PersonalProfileField.geometryFields + PersonalProfileField.portraitFields {
                    Self.applyGeometry(field, value: Self.geometryValue(field, in: next), to: &saved)
                }
            }
        } catch { showError(error.localizedDescription); layoutGeometry(animated: true); onGeometryPreview?(profile) }
    }

    static func geometryValue(_ field: PersonalProfileField, in profile: UserProfile) -> Double {
        switch field {
        case .avatarZoom: return profile.avatarZoom
        case .avatarOffsetX: return profile.avatarOffsetX * 100
        case .avatarOffsetY: return profile.avatarOffsetY * 100
        case .backgroundWidth: return profile.backgroundWidth
        case .backgroundZoom: return profile.backgroundZoom
        case .backgroundOffsetX: return profile.backgroundOffsetX
        case .backgroundOffsetY: return profile.backgroundOffsetY
        case .thumbnailZoom: return profile.thumbnailZoom
        case .thumbnailOffsetX: return profile.thumbnailOffsetX * 100
        case .thumbnailOffsetY: return profile.thumbnailOffsetY * 100
        default: return 0
        }
    }

    static func applyGeometry(_ field: PersonalProfileField, value: Double, to profile: inout UserProfile) {
        switch field {
        case .avatarZoom: profile.avatarZoom = value
        case .avatarOffsetX: profile.avatarOffsetX = value / 100
        case .avatarOffsetY: profile.avatarOffsetY = value / 100
        case .backgroundWidth: profile.backgroundWidth = value
        case .backgroundZoom: profile.backgroundZoom = value
        case .backgroundOffsetX: profile.backgroundOffsetX = value
        case .backgroundOffsetY: profile.backgroundOffsetY = value
        case .thumbnailZoom: profile.thumbnailZoom = value
        case .thumbnailOffsetX: profile.thumbnailOffsetX = value / 100
        case .thumbnailOffsetY: profile.thumbnailOffsetY = value / 100
        default: break
        }
    }

    private func setThemeColor(_ hex: String?) {
        guard let store else { return }
        if let current = displayed.themeColorHex { lastThemeHex = current }
        do { try store.update { $0.themeColorHex = hex } } catch { showError(error.localizedDescription) }
    }

    /// On: the HUD accent; off: the card's last own colour (default #C9A2FF).
    private func toggleFollow() { setThemeColor(displayed.themeColorHex == nil ? lastThemeHex : nil) }

    private func setCustomColor(_ color: NSColor) {
        guard let rgb = color.usingColorSpace(.sRGB) else { return }
        setThemeColor(String(format: "%02X%02X%02X", Int((rgb.redComponent * 255).rounded()),
                             Int((rgb.greenComponent * 255).rounded()), Int((rgb.blueComponent * 255).rounded())))
    }

    /// Default: the art is removed and every background and thumbnail value reset.
    private func resetBackground() {
        guard let store else { return }
        do { try store.removeImage(kind: .background) } catch { showError(error.localizedDescription) }
    }

    private func importImage(from url: URL, kind: UserProfileImageKind) {
        guard let store else { showError(L10n.text("Profile storage is unavailable.", "个人名片存储不可用。")); return }
        do {
            try store.importImage(from: url, kind: kind)
            if kind == .avatar { toast(icon: .user, "Picture changed", "已更换头像") }
        } catch { showError(error.localizedDescription) }
    }

    // MARK: File chooser and colour panel

    private func chooseImage(_ kind: UserProfileImageKind) {
        guard chooser == nil, colorPanel == nil, let host = context.host, let window = host.window else { return }
        endEditing(commit: true)
        context.onLockParallax?()
        let panel = NSOpenPanel()
        panel.title = kind == .avatar ? L10n.text("Choose profile picture", "选择头像") : L10n.text("Choose profile background", "选择名片背景")
        panel.prompt = L10n.text("Choose", "选择")
        panel.canChooseFiles = true; panel.canChooseDirectories = false; panel.allowsMultipleSelection = false
        panel.allowsOtherFileTypes = false; panel.resolvesAliases = true
        panel.allowedContentTypes = [.image]
        panel.level = NSWindow.Level(rawValue: max(panel.level.rawValue, window.level.rawValue + 1))
        dialogGeneration += 1
        let token = dialogGeneration
        chooser = panel
        context.onInputStateChange?()
        panel.beginSheetModal(for: window) { [weak self, weak panel, weak host] response in
            guard let self, self.dialogGeneration == token else { return }
            self.chooser = nil
            self.context.onInputStateChange?()
            guard self.isActive, let host, host.window != nil else { return }
            host.window?.makeKeyAndOrderFront(nil)
            if response == .OK, let url = panel?.url { self.importImage(from: url, kind: kind) }
            host.window?.makeFirstResponder(host)
        }
    }

    private func chooseColor(_ initial: NSColor) {
        guard colorPanel == nil, chooser == nil, let window = context.host?.window else { return }
        endEditing(commit: true)
        context.onLockParallax?()
        let panel = NSColorPanel.shared
        colorPanel = panel
        panel.setTarget(nil); panel.setAction(nil)
        panel.color = initial; panel.showsAlpha = false; panel.mode = .wheel; panel.isContinuous = true
        panel.title = L10n.text("Card color", "名片颜色")
        panel.level = NSWindow.Level(rawValue: max(NSWindow.Level.floating.rawValue, window.level.rawValue + 1))
        panel.setTarget(self); panel.setAction(#selector(colorChanged(_:)))
        colorObserver = NotificationCenter.default.addObserver(forName: NSWindow.willCloseNotification, object: panel, queue: .main) { [weak self] _ in
            guard let self else { return }
            self.clearColorPanel()
            self.context.onInputStateChange?()
            if self.isActive, let host = self.context.host { host.window?.makeKeyAndOrderFront(nil); host.window?.makeFirstResponder(host) }
        }
        context.onInputStateChange?()
        panel.makeKeyAndOrderFront(nil)
    }

    @objc private func colorChanged(_ sender: NSColorPanel) {
        guard isActive, sender === colorPanel else { return }
        setCustomColor(sender.color)
    }

    private func dismissColorPanel() {
        guard let panel = colorPanel else { return }
        clearColorPanel(); panel.orderOut(nil)
    }

    private func clearColorPanel() {
        colorPanel?.setTarget(nil); colorPanel?.setAction(nil); colorPanel = nil
        if let colorObserver { NotificationCenter.default.removeObserver(colorObserver) }
        colorObserver = nil
    }

    // MARK: Images

    private func backgroundImage() -> CGImage? {
        guard let store, let name = profile.backgroundFilename else { return nil }
        if images.backgroundName == name, let image = images.background { return image }
        images.background = Self.cgImage(store.image(for: .background)); images.backgroundName = name
        return images.background
    }

    private func avatarImage() -> (image: CGImage, orientation: Int32)? {
        guard let store, let name = profile.avatarFilename else { return nil }
        if images.avatarName != name || images.avatar == nil {
            images.avatar = Self.cgImage(store.image(for: .avatar)).map { ($0, store.imageOrientation(for: .avatar)) }
            images.avatarName = name; images.portrait = nil; images.portraitKey = nil
        }
        return images.avatar
    }

    private static func cgImage(_ value: NSImage?) -> CGImage? {
        guard let value else { return nil }
        var rect = CGRect(origin: .zero, size: value.size)
        return value.cgImage(forProposedRect: &rect, context: nil, hints: nil)
    }

    static func color(_ hex: String) -> CGColor { UInt32(hex, radix: 16).map { HUDColor.rgb($0) } ?? HUDColor.yellow }

    /// The art's radial mask, drawn once at half resolution (a smooth ramp):
    /// opaque to r 224, 55 % at r 318, clear at r 430.
    /// linear-gradient(90deg, rgba(0,0,0,.74) 0, .52 52%, .08 100%) clipped to the chamfer.
    private static func overlayArtwork(size: CGSize, scale: CGFloat) -> CGImage? {
        let pixels = CGSize(width: ceil(size.width * scale), height: ceil(size.height * scale))
        guard let space = CGColorSpace(name: CGColorSpace.sRGB),
              let ctx = CGContext(data: nil, width: Int(pixels.width), height: Int(pixels.height), bitsPerComponent: 8, bytesPerRow: 0,
                                  space: space, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue),
              let gradient = CGGradient(colorsSpace: space, colors: [HUDColor.black(0.74), HUDColor.black(0.52), HUDColor.black(0.08)] as CFArray,
                                        locations: [0, 0.52, 1]) else { return nil }
        ctx.translateBy(x: 0, y: pixels.height); ctx.scaleBy(x: scale, y: -scale)
        ctx.addPath(HUDShape.chamfer(CGRect(origin: .zero, size: size), cut: 18)); ctx.clip()
        ctx.drawLinearGradient(gradient, start: .zero, end: CGPoint(x: size.width, y: 0), options: [])
        return ctx.makeImage()
    }

    // MARK: Types

    private struct Refs {
        var card: CALayer?, main: CALayer?, plate: [CALayer] = [], overlay: CALayer?
        var artHost: CALayer?, artBox: CALayer?, artImage: CALayer?
        var frame: CALayer?, portrait: CALayer?, silhouette: CAShapeLayer?
        var portraitSource: (image: CGImage, orientation: Int32)?
        var dateKind: CALayer?, uid: CALayer?, hours: PlateReadout?, best: PlateReadout?
        var more: ProfileCircleButton?, eye: ProfileCircleButton?, theme: ProfileThemeButton?
        var values: [PersonalProfileField: CALayer] = [:]
        var editables: [PersonalProfileField: ProfileEditable] = [:]
    }

    private struct ImageCache {
        var background: CGImage?, backgroundName: String?
        var avatar: (image: CGImage, orientation: Int32)?, avatarName: String?
        var portrait: (image: CGImage, fill: CGSize)?, portraitKey: String?
    }

    private enum Style {
        static let ink = HUDColor.rgb(0xF5F5F5)
        static let title = HUDTextStyle(size: 12, weight: .bold, color: ink, tracking: 0.16)
        static let titleSmall = HUDTextStyle(size: 11, weight: .medium, color: HUDColor.rgb(0xF5F5F5, 0.55), tracking: 0.2)
        static let name = HUDTextStyle(size: 22, weight: .bold, color: ink, lineHeight: 30 / 22)
        static let tag = HUDTextStyle(size: 22, weight: .regular, color: HUDColor.rgb(0xF5F5F5, 0.6), lineHeight: 30 / 22)
        static let dateKind = HUDTextStyle(size: 10, weight: .bold, color: HUDColor.white(1), tracking: 0.06, uppercase: true)
        static let dateValue = HUDTextStyle(size: 12, weight: .bold, color: HUDColor.rgb(0x292929), tabular: true)
        static let uid = HUDTextStyle(size: 13, weight: .regular, color: HUDColor.rgb(0xF5F5F5, 0.65), tabular: true)
        static let levelLabel = HUDTextStyle(size: 14, weight: .regular, color: HUDColor.white(1))
        static let levelValue = HUDTextStyle(size: 25, weight: .medium, color: HUDColor.white(1), tabular: true)
        static var counterValue: HUDTextStyle { HUDTextStyle(size: 27, weight: .medium, color: HUDColor.yellow, lineHeight: 1.05, tabular: true) }
        static var counterLabel: HUDTextStyle { HUDTextStyle(size: 11, weight: .regular, color: HUDColor.yellow) }
        static var region: HUDTextStyle { HUDTextStyle(size: 11.5, weight: .bold, color: HUDColor.yellow, tracking: 0.08) }
        static let workLabel = HUDTextStyle(size: 12, weight: .regular, color: ink, lineHeight: 1.2)
        static let workValue = HUDTextStyle(size: 24, weight: .medium, color: ink, tabular: true)
        static let workUnit = HUDTextStyle(size: 10, weight: .regular, color: HUDColor.rgb(0xF5F5F5, 0.65), lineHeight: 1.2)
        static let intro = HUDTextStyle(size: 13, weight: .medium, color: ink, lineHeight: 1.5)
        static let toolbar = HUDTextStyle(size: 12, weight: .medium, color: ink)
        static let popoverLabel = HUDTextStyle(size: 12, weight: .regular, color: HUDColor.rgb(0xDDDDDD))
    }

    /// A left-column readout plate: label on the left, then the value
    /// (24 px tabular) and an optional unit flush right, e.g. `25.59 h`.
    private final class PlateReadout {
        let layer: CALayer
        let value = HUDTextLayer(style: Style.workValue)
        private let unit: HUDTextLayer?
        init(id: String, label: String, unit: String?, frame: CGRect) {
            layer = HUDLayer.plain(id)
            self.unit = unit.map { HUDTextLayer($0, style: Style.workUnit) }
            let title = HUDTextLayer(label, style: Style.workLabel)
            HUDMotion.perform {
                layer.frame = frame; layer.backgroundColor = HUDColor.rgb(0xF5F5F5, 0.13)
                title.lineFrame = CGRect(x: 12, y: (frame.height - 14.4) / 2, width: 160, height: 14.4)
                ([title, value] + [self.unit].compactMap { $0 }).forEach(layer.addSublayer)
            }
        }
        func set(_ text: String) {
            HUDMotion.perform {
                value.text = text
                let width = layer.bounds.width, height = layer.bounds.height
                var right = width - 12
                if let unit {
                    let unitWidth = ceil(unit.preferredWidth)
                    unit.lineFrame = CGRect(x: right - unitWidth, y: (height - 12) / 2, width: unitWidth + 1, height: 12)
                    right -= unitWidth + 6
                }
                let valueWidth = ceil(value.preferredWidth)
                value.lineFrame = CGRect(x: right - valueWidth, y: (height - 24) / 2, width: valueWidth + 1, height: 24)
            }
        }
    }
}

/// An editable card element (spec §3.20.3): hover draws a 1 px accent ring
/// with a soft accent glow (.22 s); a press opens the inline editor.
private final class ProfileEditable: HUDControl {
    private let ring = HUDLayer.plain("profile.editable.ring")
    private var content: [CALayer] = []
    var accent: CGColor = HUDColor.yellow {
        didSet { HUDMotion.perform { ring.borderColor = accent; ring.shadowColor = accent } }
    }

    init(id: String, label: String, env: HUDDesignEnvironment) {
        super.init(id: id, label: label, env: env)
        HUDMotion.perform {
            ring.borderWidth = 1; ring.opacity = 0; ring.zPosition = 1
            ring.shadowOpacity = 0.3; ring.shadowRadius = 7; ring.shadowOffset = .zero
            layer.addSublayer(ring)
        }
    }

    func setFrame(_ frame: CGRect, content layers: [CALayer]) {
        content.forEach { $0.removeFromSuperlayer() }
        content = layers
        layer.frame = frame
        let bounds = CGRect(origin: .zero, size: frame.size)
        ring.frame = bounds
        // The glow hugs the ring: a 1 px outline as the shadow path.
        let outline = CGMutablePath()
        outline.addRect(bounds)
        outline.addLines(between: [CGPoint(x: 1, y: 1), CGPoint(x: 1, y: bounds.height - 1),
                                   CGPoint(x: bounds.width - 1, y: bounds.height - 1), CGPoint(x: bounds.width - 1, y: 1)])
        outline.closeSubpath()
        ring.shadowPath = outline
        layers.forEach { layer.insertSublayer($0, below: ring) }
        setTarget(bounds)
        env.applyScale(to: layer)
    }

    override func hoverChanged() {
        HUDMotion.animate(ring, "opacity", to: Float(isHovered && isEnabled ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
    }
    override func enabledChanged() { hoverChanged() }
}

/// The card's round buttons: ⋯ (light, 3 dots; hover scale 1.08 + a 3 px
/// accent ring at 40 %) and the eye (dark; hover .5 + scale 1.08).
private final class ProfileCircleButton: HUDControl {
    enum Style { case more, eye }
    let style: Style
    private let disc = HUDLayer.plain("profile.circle")
    private let halo = HUDLayer.plain("profile.circle.ring")
    private let glyph = HUDLayer.shape("profile.circle.glyph")
    private let slash = HUDLayer.shape("profile.circle.slash")
    var accent: CGColor = HUDColor.yellow { didSet { HUDMotion.perform { halo.borderColor = accent.copy(alpha: 0.4) } } }
    var slashed = true { didSet { HUDMotion.perform { slash.isHidden = !slashed } } }

    init(id: String, label: String, style: Style, env: HUDDesignEnvironment) {
        self.style = style
        super.init(id: id, label: label, env: env)
        let side: CGFloat = style == .more ? 28 : 32
        let bounds = CGRect(x: 0, y: 0, width: side, height: side)
        HUDMotion.perform {
            layer.bounds = bounds
            disc.frame = bounds; disc.cornerRadius = side / 2
            disc.backgroundColor = style == .more ? HUDColor.rgb(0xF5F5F5, 0.88) : HUDColor.black(0.3)
            halo.frame = bounds.insetBy(dx: -3, dy: -3); halo.cornerRadius = side / 2 + 3; halo.borderWidth = 3; halo.opacity = 0
            layer.addSublayer(halo); layer.addSublayer(disc)
            if style == .more {
                // 16 × 4 strip of r 1.6 dots every 5.5 px.
                let path = CGMutablePath()
                for index in 0..<3 {
                    path.addEllipse(in: CGRect(x: 6 + 2.75 + CGFloat(index) * 5.5 - 1.6, y: 14 - 1.6, width: 3.2, height: 3.2))
                }
                glyph.frame = bounds; glyph.path = path; glyph.fillColor = HUDColor.rgb(0x1F1F1F)
                layer.addSublayer(glyph)
            } else {
                // The custom 20 px eye: almond + iris, slashed while the text shows.
                var scale = CGAffineTransform(scaleX: 20 / 24, y: 20 / 24).concatenating(CGAffineTransform(translationX: 6, y: 6))
                let eye = CGMutablePath()
                eye.move(to: CGPoint(x: 2, y: 12)); eye.addQuadCurve(to: CGPoint(x: 22, y: 12), control: CGPoint(x: 12, y: 1))
                eye.addQuadCurve(to: CGPoint(x: 2, y: 12), control: CGPoint(x: 12, y: 23)); eye.closeSubpath()
                eye.addEllipse(in: CGRect(x: 12 - 3.2, y: 12 - 3.6, width: 6.4, height: 7.2))
                let line = CGMutablePath(); line.move(to: CGPoint(x: 4, y: 21)); line.addLine(to: CGPoint(x: 20, y: 3))
                for (shape, path) in [(glyph, eye as CGPath), (slash, line as CGPath)] {
                    shape.frame = bounds; shape.path = path.copy(using: &scale)
                    shape.fillColor = nil; shape.strokeColor = HUDColor.rgb(0xF5F5F5); shape.lineWidth = 1.6 * 20 / 24
                    layer.addSublayer(shape)
                }
            }
        }
        setTarget(bounds, path: CGPath(ellipseIn: bounds, transform: nil))
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(layer, "transform.scale", to: on ? 1.08 : 1, duration: HUDDuration.hover, env: env)
        if style == .more {
            HUDMotion.animate(halo, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        } else {
            HUDMotion.animate(disc, "backgroundColor", to: HUDColor.black(on ? 0.5 : 0.3), duration: HUDDuration.hover,
                              timing: HUDEasing.ease, env: env)
        }
    }
    override func enabledChanged() { hoverChanged() }
}

/// `Card theme` + image icon: transparent, hover rgba(245,245,245,.1) and a 1 px lift.
private final class ProfileThemeButton: HUDControl {
    private let fill = HUDLayer.plain("profile.theme.fill")

    init(id: String, title: String, env: HUDDesignEnvironment) {
        super.init(id: id, label: title, env: env)
        let text = HUDTextLayer(title, style: HUDTextStyle(size: 12, weight: .medium, color: HUDColor.rgb(0xF5F5F5)))
        let icon = HUDIconLayer(.image, size: 15, color: HUDColor.rgb(0xF5F5F5))
        let textWidth = ceil(text.preferredWidth)
        let bounds = CGRect(x: 0, y: 0, width: 10 + textWidth + 8 + 15 + 10, height: 30)
        HUDMotion.perform {
            layer.bounds = bounds
            fill.frame = bounds; fill.backgroundColor = HUDColor.rgb(0xF5F5F5, 0.1); fill.opacity = 0
            text.lineFrame = CGRect(x: 10, y: 9, width: textWidth + 1, height: 12)
            icon.frame = CGRect(x: 10 + textWidth + 8, y: 7.5, width: 15, height: 15)
            [fill, text, icon].forEach(layer.addSublayer)
        }
        setTarget(bounds)
    }

    override func hoverChanged() {
        let on = isHovered && isEnabled
        HUDMotion.animate(fill, "opacity", to: Float(on ? 1 : 0), duration: HUDDuration.hover, timing: HUDEasing.ease, env: env)
        HUDMotion.animate(layer, "transform.translation.y", to: on ? -1 : 0, duration: HUDDuration.hover, env: env)
    }
}
