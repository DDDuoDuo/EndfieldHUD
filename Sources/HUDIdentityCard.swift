import AppKit
import QuartzCore

/// Lower-left identity artwork in the shell's 1000 × 640 design coordinates.
/// The host supplies native accessibility controls and owns both actions:
/// `.close` dismisses only the HUD; `.profile` opens the retained profile editor.
final class HUDIdentityCard {
    enum Target: Equatable { case close, profile }

    let layer = CALayer()
    let closeRect = CGRect(x: 22, y: 591, width: 35, height: 35)
    var powerRect: CGRect { closeRect }
    let profileRect = CGRect(x: 78, y: 564, width: 240, height: 82)
    private var details: UserProfile?
    private var avatarImage: CGImage?
    private var avatarOrientation: Int32 = 1
    private var backgroundImage: CGImage?

    private let profile = CALayer()
    private let backing = CALayer()
    private let face = CALayer()
    private let foreground = CALayer()
    private let close = CALayer()
    private let closeFace = CALayer()
    private let closeGlyph = CALayer()
    private var hovered: Target?
    private var dark = true
    private var accent = NSColor(srgbRed: 0.98, green: 0.83, blue: 0.12, alpha: 1)
    private var contentsScale: CGFloat = 2
    private var hoverGeneration = 0
    private var cleanup: DispatchWorkItem?
    private var profileRevision: UInt64 = 0
    private var renderedState: RenderedState?

    /// Layout and opening both refresh shell appearance. Retain the artwork
    /// until its actual inputs change instead of recreating its portrait,
    /// lettering, masks and power glyph on every same-style refresh.
    private struct RenderedState: Equatable {
        let dark: Bool
        let accent: CGColor
        let contentsScale: CGFloat
        let language: AppLanguage
        let profileRevision: UInt64
        let profileBounds: CGRect
        let closeBounds: CGRect
    }

    init() {
        withoutActions {
            layer.name = "hud.identity"
            layer.frame = CGRect(x: 0, y: 0, width: 1000, height: 640)
            layer.allowsGroupOpacity = false; layer.masksToBounds = false
            profile.name = "identity.profile"; profile.frame = profileRect
            profile.allowsGroupOpacity = false
            backing.name = "identity.glassBacking"; backing.frame = profile.bounds
            face.name = "identity.face"; face.frame = profile.bounds
            foreground.name = "identity.lettering"; foreground.frame = profile.bounds
            for item in [backing, face, foreground] { item.allowsGroupOpacity = false; profile.addSublayer(item) }
            close.name = "identity.close"; close.frame = closeRect; close.allowsGroupOpacity = false
            closeFace.name = "identity.closePlate"; closeFace.frame = close.bounds
            closeGlyph.name = "identity.closeGlyph"; closeGlyph.frame = close.bounds
            close.addSublayer(closeFace); close.addSublayer(closeGlyph)
            layer.addSublayer(close); layer.addSublayer(profile)
        }
        update(dark: true, accent: accent, contentsScale: 2)
    }

    deinit { cleanup?.cancel() }

    func update(dark: Bool, accent: NSColor, contentsScale: CGFloat) {
        let scale = contentsScale.isFinite ? min(8, max(1, contentsScale)) : 2
        let nextState = RenderedState(dark: dark,
            accent: (accent.usingColorSpace(.sRGB) ?? accent).cgColor,
            contentsScale: scale, language: L10n.resolvedLanguage, profileRevision: profileRevision,
            profileBounds: profile.bounds, closeBounds: close.bounds)
        guard nextState != renderedState else { return }
        self.dark = dark; self.accent = accent
        self.contentsScale = scale
        withoutActions {
            for item in [backing, face, foreground, closeFace, closeGlyph] { item.sublayers?.forEach { $0.removeFromSuperlayer() } }
            renderProfile(); renderClose(); applyPose(animated: false)
        }
        renderedState = nextState
    }

    func setHovered(_ target: Target?, animated: Bool = true) {
        guard target != hovered else { return }
        hovered = target; applyPose(animated: animated && !HUDRuntimeAppearance.reduceMotion)
    }

    func resetInteraction(animated: Bool = false) {
        hovered = nil; applyPose(animated: animated && !HUDRuntimeAppearance.reduceMotion)
    }

    func rect(for target: Target) -> CGRect { target == .close ? closeRect : profileRect }

    func setProfile(_ value: UserProfile, avatar: NSImage?, background: NSImage?, avatarOrientation: Int32 = 1) {
        details = value
        self.avatarOrientation = avatarOrientation
        avatarImage = avatar?.cgImage(forProposedRect: nil, context: nil, hints: nil)
        backgroundImage = background?.cgImage(forProposedRect: nil, context: nil, hints: nil)
        // Image content/crop may change independently of the global appearance.
        profileRevision &+= 1
        update(dark: dark, accent: accent, contentsScale: contentsScale)
    }

    /// Used after the host has inverted the shared panel-plane projection.
    /// The compact movement envelope keeps the raised card easy to acquire.
    func target(at point: CGPoint) -> Target? {
        if closeRect.insetBy(dx: -4, dy: -5).contains(point) { return .close }
        if profileRect.insetBy(dx: -4, dy: -7).contains(point) { return .profile }
        return nil
    }

    private func renderProfile() {
        let r = CGRect(origin: .zero, size: profileRect.size)
        let white = NSColor(white: dark ? 0.97 : 0.16, alpha: 1)
        let muted = NSColor(white: dark ? 0.79 : 0.27, alpha: 1)
        let cardAccent = details?.resolvedAccent(fallback: accent) ?? accent
        let gold = cardAccent.usingColorSpace(.sRGB) ?? cardAccent
        let outline = cardPath(r)

        let surface = CAGradientLayer(); surface.frame = r
        surface.colors = [NSColor(white: dark ? 0.58 : 0.90, alpha: 0.67).cgColor,
                          NSColor(white: dark ? 0.25 : 0.69, alpha: 0.76).cgColor,
                          NSColor(white: dark ? 0.43 : 0.82, alpha: 0.58).cgColor]
        surface.locations = [0, 0.63, 1]; surface.startPoint = CGPoint(x: 0.08, y: 0); surface.endPoint = CGPoint(x: 0.9, y: 1)
        let clip = CAShapeLayer(); clip.path = outline; clip.fillColor = NSColor.black.cgColor; surface.mask = clip
        face.addSublayer(surface)
        if let backgroundImage {
            let photo = CALayer(); photo.frame = r; photo.contents = backgroundImage
            photo.contentsRect = Self.thumbnailCrop(imageSize: CGSize(width: backgroundImage.width, height: backgroundImage.height),
                targetSize: r.size, offset: CGPoint(x: details?.thumbnailOffsetX ?? 0, y: details?.thumbnailOffsetY ?? 0))
            photo.contentsGravity = .resize; photo.masksToBounds = true; photo.opacity = 0.62
            let mask = CAShapeLayer(); mask.path = outline; photo.mask = mask
            face.addSublayer(photo)
            let shade = shape(outline, fill: NSColor.black.withAlphaComponent(0.35))
            face.addSublayer(shade)
        }
        let pattern = CALayer(); pattern.frame = r
        let patternClip = CAShapeLayer(); patternClip.path = outline; patternClip.fillColor = NSColor.black.cgColor; pattern.mask = patternClip
        face.addSublayer(pattern)
        for (points, opacity) in [
            ([CGPoint(x: 15, y: 0), CGPoint(x: 99, y: 0), CGPoint(x: 36, y: 82), CGPoint(x: -15, y: 82)], CGFloat(0.13)),
            ([CGPoint(x: 112, y: -4), CGPoint(x: 148, y: -4), CGPoint(x: 99, y: 86), CGPoint(x: 70, y: 86)], CGFloat(0.12)),
            ([CGPoint(x: 198, y: -5), CGPoint(x: 232, y: -5), CGPoint(x: 157, y: 84), CGPoint(x: 145, y: 84)], CGFloat(0.09))
        ] {
            pattern.addSublayer(shape(polygon(points), fill: NSColor(white: dark ? 0.02 : 1, alpha: opacity)))
        }
        let contours = CGMutablePath()
        for index in 0..<4 {
            let shift = CGFloat(index) * 7
            contours.move(to: CGPoint(x: 97 + shift, y: 0))
            contours.addLine(to: CGPoint(x: 74 + shift, y: 25))
            contours.addLine(to: CGPoint(x: 141 + shift, y: 46))
            contours.addLine(to: CGPoint(x: 119 + shift, y: 83))
        }
        pattern.addSublayer(shape(contours, fill: nil, stroke: white.withAlphaComponent(0.065), width: 0.7))
        face.addSublayer(shape(outline, fill: nil, stroke: white.withAlphaComponent(0.38), width: 0.8))
        let topGold = CGMutablePath(); topGold.move(to: CGPoint(x: 88, y: 0.8)); topGold.addLine(to: CGPoint(x: 228, y: 0.8))
        topGold.addQuadCurve(to: CGPoint(x: 239.2, y: 12), control: CGPoint(x: 239.2, y: 0.8))
        topGold.addLine(to: CGPoint(x: 239.2, y: 70))
        topGold.addQuadCurve(to: CGPoint(x: 228, y: 81.2), control: CGPoint(x: 239.2, y: 81.2))
        face.addSublayer(shape(topGold, fill: nil, stroke: gold.withAlphaComponent(0.82), width: 1))

        renderAvatar(in: foreground, white: white, gold: gold)
        text(details?.name ?? "Endministrator", in: foreground, rect: CGRect(x: 82, y: 10, width: 146, height: 24), size: 16.5,
             color: white, weight: .medium, italic: true)
        text("#" + (details?.tag ?? "0000"), in: foreground, rect: CGRect(x: 84, y: 30, width: 106, height: 10), size: 6.5,
             color: muted.withAlphaComponent(0.85), weight: .semibold)
        let level = min(60, max(1, details?.permissionLevel ?? 60))
        text(String(level), in: foreground, rect: CGRect(x: 82, y: 40, width: 51, height: 25), size: 21,
             color: white, weight: .semibold)
        let pill = CGRect(x: 143, y: 37, width: 89, height: 30)
        let capsule = CAGradientLayer(); capsule.name = "identity.authorityShade"; capsule.frame = pill
        capsule.startPoint = CGPoint(x: 0, y: 0.5); capsule.endPoint = CGPoint(x: 1, y: 0.5)
        let shade = NSColor(white: dark ? 0.035 : 0.10, alpha: 1)
        capsule.colors = [shade.withAlphaComponent(0).cgColor, shade.withAlphaComponent(0.10).cgColor,
                          shade.withAlphaComponent(0.65).cgColor, shade.withAlphaComponent(0.90).cgColor]
        capsule.locations = [0, 0.37, 0.72, 1]
        let capsuleMask = CAShapeLayer()
        capsuleMask.path = CGPath(roundedRect: capsule.bounds, cornerWidth: 15, cornerHeight: 15, transform: nil)
        capsule.mask = capsuleMask; foreground.addSublayer(capsule)
        text(L10n.text("Authority", "权限等级"), in: foreground,
             rect: CGRect(x: 147, y: 41, width: 50, height: 12), size: 8.2,
             color: NSColor(white: 0.98, alpha: 0.94), weight: .medium, alignment: .right)
        if level == 60 {
            text(L10n.text("MAX", "满级"), in: foreground,
                 rect: CGRect(x: 175, y: 52, width: 22, height: 10), size: 7.4,
                 color: gold, weight: .medium, alignment: .right)
        }
        foreground.addSublayer(HUDProfileLevelArtwork.makeLayer(.authority,
            frame: CGRect(x: 201, y: 41, width: 17, height: 21), color: gold, contentsScale: contentsScale))
        let arrow = polygon([CGPoint(x: 221, y: 47), CGPoint(x: 227, y: 51.5), CGPoint(x: 221, y: 56)])
        foreground.addSublayer(shape(arrow, fill: gold))
        let progress = CGRect(x: 84, y: 65, width: 112, height: 2.5)
        foreground.addSublayer(shape(CGPath(roundedRect: progress, cornerWidth: 1.25, cornerHeight: 1.25, transform: nil),
                                    fill: NSColor.black.withAlphaComponent(0.35)))
        var completed = progress; completed.size.width *= CGFloat(level) / 60
        foreground.addSublayer(shape(CGPath(roundedRect: completed, cornerWidth: 1.25, cornerHeight: 1.25, transform: nil), fill: gold))
        text("UID: " + (details?.uid ?? "—"), in: foreground, rect: CGRect(x: 84, y: 71, width: 120, height: 10), size: 6.7,
             color: muted.withAlphaComponent(0.77), weight: .medium)
        for index in 0..<4 {
            let mark = CGRect(x: 216 + CGFloat(index) * 3.3, y: 73, width: 1.2, height: index.isMultiple(of: 2) ? 3.1 : 1.7)
            foreground.addSublayer(shape(CGPath(rect: mark, transform: nil), fill: white.withAlphaComponent(0.55)))
        }
    }

    /// A normalized aspect-fill crop moves only within the available image,
    /// so adjusting the compact card never exposes empty image edges.
    static func thumbnailCrop(imageSize: CGSize, targetSize: CGSize, offset: CGPoint) -> CGRect {
        guard imageSize.width > 0, imageSize.height > 0, targetSize.width > 0, targetSize.height > 0,
              imageSize.width.isFinite, imageSize.height.isFinite,
              targetSize.width.isFinite, targetSize.height.isFinite else {
            return CGRect(x: 0, y: 0, width: 1, height: 1)
        }
        let fit = max(targetSize.width / imageSize.width, targetSize.height / imageSize.height)
        let width = min(1, targetSize.width / (imageSize.width * fit))
        let height = min(1, targetSize.height / (imageSize.height * fit))
        let x = offset.x.isFinite ? min(1, max(-1, offset.x)) : 0
        let y = offset.y.isFinite ? min(1, max(-1, offset.y)) : 0
        return CGRect(x: (1 - width) * (x + 1) / 2, y: (1 - height) * (y + 1) / 2,
                      width: width, height: height)
    }

    private func renderAvatar(in host: CALayer, white: NSColor, gold: NSColor) {
        let rect = CGRect(x: 13, y: 12, width: 52, height: 56)
        let portrait = HUDPortraitArtwork.makeLayer(image: avatarImage, profile: details,
            size: rect.size, ink: white, accent: gold, contentsScale: contentsScale, orientation: avatarOrientation)
        portrait.frame = rect
        host.addSublayer(portrait)
    }

    private func renderClose() {
        let red = NSColor(srgbRed: 0.91, green: 0.20, blue: 0.20, alpha: 1)
        let r = close.bounds
        let outline = CGPath(roundedRect: r, cornerWidth: 4, cornerHeight: 4, transform: nil)
        closeFace.addSublayer(shape(outline, fill: red.withAlphaComponent(dark ? 0.14 : 0.10)))
        let clip = CAShapeLayer(); clip.path = outline; clip.fillColor = NSColor.black.cgColor
        let hatch = CAShapeLayer(); hatch.frame = r; hatch.mask = clip
        let path = CGMutablePath()
        for index in -7...10 {
            let x = CGFloat(index) * 5.5
            path.move(to: CGPoint(x: x, y: r.maxY)); path.addLine(to: CGPoint(x: x + r.height, y: 0))
        }
        hatch.path = path; hatch.lineWidth = 1.7; hatch.strokeColor = red.withAlphaComponent(0.72).cgColor; hatch.fillColor = nil; closeFace.addSublayer(hatch)
        let power = CGMutablePath()
        let center = CGPoint(x: 17.5, y: 18.5)
        power.addArc(center: center, radius: 10.3, startAngle: -.pi * 0.31, endAngle: .pi * 1.31, clockwise: false)
        power.move(to: CGPoint(x: 17.5, y: 4.2)); power.addLine(to: CGPoint(x: 17.5, y: 17))
        let glyph = shape(power, fill: nil, stroke: NSColor(white: dark ? 0.97 : 0.15, alpha: 0.94), width: 4)
        glyph.lineCap = .round; closeGlyph.addSublayer(glyph)
    }

    private func applyPose(animated: Bool) {
        hoverGeneration += 1; let generation = hoverGeneration
        cleanup?.cancel(); cleanup = nil
        let profileActive = hovered == .profile, closeActive = hovered == .close
        let targets: [(CALayer, CGFloat, CGFloat)] = [
            (backing, profileActive ? -0.8 : 0, profileActive ? 0.6 : 0),
            (face, profileActive ? -2.4 : 0, profileActive ? 5 : 0),
            (foreground, profileActive ? -4.3 : 0, profileActive ? 9 : 0),
            (closeFace, closeActive ? -1 : 0, closeActive ? 2 : 0),
            (closeGlyph, closeActive ? -2.8 : 0, closeActive ? 7 : 0)
        ]
        withoutActions {
            for (item, y, z) in targets {
                let previous = item.presentation()?.transform ?? item.transform
                item.removeAnimation(forKey: "identity.hover")
                item.transform = CATransform3DMakeTranslation(0, y, z)
                if animated {
                    let motion = CABasicAnimation(keyPath: "transform"); motion.fromValue = NSValue(caTransform3D: previous)
                    motion.toValue = NSValue(caTransform3D: item.transform); motion.duration = 0.18
                    motion.timingFunction = CAMediaTimingFunction(controlPoints: 0.18, 0.74, 0.25, 1)
                    item.add(motion, forKey: "identity.hover")
                }
            }
            face.opacity = profileActive ? 1 : 0.91; closeFace.opacity = closeActive ? 1 : 0.82
        }
        if animated {
            let work = DispatchWorkItem { [weak self] in
                guard let self, self.hoverGeneration == generation else { return }
                for (item, _, _) in targets { item.removeAnimation(forKey: "identity.hover") }
                self.cleanup = nil
            }
            cleanup = work; DispatchQueue.main.asyncAfter(deadline: .now() + 0.19, execute: work)
        }
    }

    private func cardPath(_ r: CGRect) -> CGPath {
        CGPath(roundedRect: r, cornerWidth: 12, cornerHeight: 12, transform: nil)
    }
    private func polygon(_ points: [CGPoint]) -> CGPath {
        let path = CGMutablePath(); guard let first = points.first else { return path }
        path.move(to: first); for point in points.dropFirst() { path.addLine(to: point) }; path.closeSubpath(); return path
    }
    private func shape(_ path: CGPath, fill: NSColor?, stroke: NSColor? = nil, width: CGFloat = 1) -> CAShapeLayer {
        let result = CAShapeLayer(); result.path = path; result.fillColor = fill?.cgColor; result.strokeColor = stroke?.cgColor
        result.lineWidth = width; result.lineJoin = .miter; result.contentsScale = contentsScale; return result
    }
    private func text(_ value: String, in host: CALayer, rect: CGRect, size: CGFloat, color: NSColor, weight: NSFont.Weight, italic: Bool = false, alignment: CATextLayerAlignmentMode = .left) {
        let item = CATextLayer(); item.frame = rect; item.string = value; item.fontSize = size
        item.alignmentMode = alignment
        item.font = italic ? NSFont(name: "HelveticaNeue-MediumItalic", size: size) ?? NSFont.systemFont(ofSize: size, weight: weight) : NSFont.systemFont(ofSize: size, weight: weight)
        item.foregroundColor = color.cgColor; item.truncationMode = .end
        item.contentsScale = HUDRenderScale.contentScale(for: item, baseScale: contentsScale); host.addSublayer(item)
    }
    private func withoutActions(_ body: () -> Void) { CATransaction.begin(); CATransaction.setDisableActions(true); body(); CATransaction.commit() }
}
