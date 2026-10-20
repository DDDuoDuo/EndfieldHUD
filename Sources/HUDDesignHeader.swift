import AppKit
import QuartzCore

/// A module's bilingual title. English UIs show the English title with the
/// Simplified Chinese subtitle; zh, ja and ko flip it (CJK title, English
/// subtitle), as the game does (spec §1.1, guide §4).
struct HUDModuleTitle: Equatable {
    let english: String
    let simplifiedChinese: String
    /// The UI-language title (L10n of the pair); equal to `english` in English.
    let localized: String

    struct Lines: Equatable {
        let title: String, titleLanguage: String?
        let subtitle: String, subtitleLanguage: String?
    }

    func lines(for language: AppLanguage = L10n.resolvedLanguage) -> Lines {
        let resolved = language == .system ? L10n.resolvedLanguage : language
        guard resolved != .english, resolved != .system else {
            return Lines(title: english, titleLanguage: nil,
                         subtitle: simplifiedChinese, subtitleLanguage: HUDTextLanguage.simplifiedChinese)
        }
        return Lines(title: localized, titleLanguage: HUDTextLanguage.tag(for: resolved), subtitle: english, subtitleLanguage: nil)
    }
}

/// The shared module header (spec §2.1): `// HUD / …` breadcrumb with the mono
/// code and hatch tail, the bilingual title with an optional back button and
/// side actions, and the rule with its yellow lead, tick ladder and counter.
/// 590 × 81 including the bottom margin.
final class HUDModuleHeader {
    let layer = HUDLayer.plain("header")
    let env: HUDDesignEnvironment
    private(set) var back: HUDIconButton?
    private let crumbs = HUDLayer.plain("header.crumbs")
    private let codeText = HUDTextLayer(style: HUDType.breadcrumb)
    private let tail = HUDLayer.plain("header.tail")
    private let titleGroup = HUDLayer.plain("header.title")
    private let title = HUDTextLayer(style: HUDType.moduleTitle)
    private let subtitle = HUDTextLayer(style: HUDType.titleSubtitle)
    private let actions = HUDLayer.plain("header.actions")
    private let rule = HUDLayer.plain("header.rule")
    private let ruleLine = HUDLayer.plain()
    private let lead = HUDLayer.plain()
    private let ladder = HUDLayer.shape()
    private let counterPlate = HUDLayer.plain()
    private let counter = HUDTextLayer(style: HUDType.ruleCounter)
    private var actionControls: [HUDControl] = []
    private var crumbLayers: [HUDTextLayer] = []

    static let width = HUDMetric.box.width

    init(env: HUDDesignEnvironment) {
        self.env = env
        HUDMotion.perform {
            layer.frame = CGRect(x: 0, y: 0, width: Self.width, height: HUDMetric.headerHeight)
            [crumbs, codeText, tail, titleGroup, actions, rule].forEach(layer.addSublayer)
            titleGroup.addSublayer(title); titleGroup.addSublayer(subtitle)
            rule.anchorPoint = CGPoint(x: 0, y: 0.5)
            rule.frame = CGRect(x: 0, y: 60, width: Self.width, height: 9)
            ruleLine.frame = CGRect(x: 0, y: 4, width: Self.width, height: 1); ruleLine.backgroundColor = HUDColor.white(0.22)
            lead.frame = CGRect(x: 0, y: 3, width: 56, height: 3); lead.backgroundColor = HUDColor.yellow
            ladder.frame = CGRect(x: Self.width - 46 - 90, y: 0, width: 90, height: 9)
            ladder.path = HUDShape.tickLadder(width: 90, height: 9, pitch: 9); ladder.fillColor = HUDColor.white(0.35)
            counterPlate.backgroundColor = HUDColor.counterBackground
            [ruleLine, lead, ladder, counterPlate, counter].forEach(rule.addSublayer)
            tail.frame = CGRect(x: Self.width - 34, y: 3.5, width: 34, height: 7)
            tail.showArtwork(HUDArtwork.hatch(.light, size: CGSize(width: 34, height: 7), scale: env.renderScale), scale: env.renderScale)
            codeText.alignment = .right
        }
    }

    /// - crumbs: after `// HUD` (uppercased); the last is highlighted.
    func configure(crumbs path: [String], code: String, title moduleTitle: HUDModuleTitle, counter value: String,
                   showsBack: Bool, actions controls: [HUDControl] = []) {
        HUDMotion.perform {
            crumbLayers.forEach { $0.removeFromSuperlayer() }
            crumbLayers = []
            var x: CGFloat = 0
            let parts = ["// HUD"] + path.flatMap { ["/", $0] }
            for (index, part) in parts.enumerated() {
                let current = index == parts.count - 1 && index > 0
                let text = HUDTextLayer(part, style: current ? HUDType.breadcrumb.colored(HUDColor.crumbCurrent) : HUDType.breadcrumb)
                let width = text.preferredWidth
                text.lineFrame = CGRect(x: x, y: 1.75, width: width + 1, height: 10.5)
                crumbs.addSublayer(text); crumbLayers.append(text)
                x += width + 6
            }
            crumbs.frame = CGRect(x: 0, y: 0, width: Self.width, height: 14)
            codeText.text = code
            let codeWidth = codeText.preferredWidth
            codeText.lineFrame = CGRect(x: Self.width - 34 - 6 - codeWidth - 1, y: 1.75, width: codeWidth + 1, height: 10.5)

            if showsBack, back == nil {
                let button = HUDIconButton(id: "back", label: L10n.text("Back", "返回"), icon: .back, style: .back, env: env)
                back = button; layer.addSublayer(button.layer)
            } else if !showsBack, let button = back { button.layer.removeFromSuperlayer(); back = nil }
            back?.layer.frame = CGRect(x: 0, y: 20, width: 30, height: 30)

            let lines = moduleTitle.lines()
            title.text = lines.title; title.language = lines.titleLanguage
            subtitle.text = lines.subtitle; subtitle.language = lines.subtitleLanguage
            let titleX: CGFloat = showsBack ? 40 : 0
            let titleWidth = title.preferredWidth, subtitleWidth = subtitle.preferredWidth
            // Baselines align: both line boxes share the title row's baseline.
            let titleMetrics = HUDText.metrics(HUDType.moduleTitle, language: lines.titleLanguage ?? HUDTextLanguage.tag())
            let subMetrics = HUDText.metrics(HUDType.titleSubtitle, language: lines.subtitleLanguage ?? HUDTextLanguage.tag())
            let baseline = 35 - titleMetrics.height / 2 + titleMetrics.baseline
            titleGroup.frame = CGRect(x: titleX, y: 20, width: titleWidth + 12 + subtitleWidth + 4, height: 30)
            title.lineFrame = CGRect(x: 0, y: 15 - titleMetrics.height / 2, width: titleWidth + 1, height: titleMetrics.height)
            subtitle.lineFrame = CGRect(x: titleWidth + 12, y: baseline - 20 - subMetrics.baseline,
                                        width: subtitleWidth + 1, height: subMetrics.height)

            actionControls.forEach { $0.layer.removeFromSuperlayer() }
            actionControls = controls
            var right = Self.width
            for control in controls.reversed() {
                let size = control.layer.bounds.size
                right -= size.width
                control.layer.frame = CGRect(x: right, y: 35 - size.height / 2, width: size.width, height: size.height)
                actions.addSublayer(control.layer)
                right -= 8
            }
            actions.frame = layer.bounds
        }
        setCounter(value)
        env.applyScale(to: layer)
    }

    /// Live counters (TODO count, volume, pins, export %) change in place.
    func setCounter(_ value: String) {
        HUDMotion.perform {
            counter.text = value
            let width = counter.preferredWidth
            counter.lineFrame = CGRect(x: Self.width - width - 1, y: -1.5, width: width + 1, height: 11)
            counterPlate.frame = CGRect(x: Self.width - width - 6 - 1, y: -2, width: width + 6 + 1, height: 12)
            counter.contentsScale = env.scale(for: counter)
        }
    }

    var controls: [HUDControl] { (back.map { [$0] } ?? []) + actionControls }

    /// Title wipe with fade (.45 s E) and the rule wipe (.42 s E after .05 s).
    func playEntrance() {
        HUDMotion.wipe(titleGroup, from: .left, duration: HUDDuration.titleWipe, fadeFrom: 0, env: env)
        HUDMotion.grow(rule, duration: HUDDuration.ruleWipe, delay: HUDDuration.ruleDelay, env: env)
    }

    var titleTextForTesting: (String, String) { (title.text, subtitle.text) }
    var counterTextForTesting: String { counter.text }
    var crumbTextsForTesting: [String] { crumbLayers.map(\.text) }
}
