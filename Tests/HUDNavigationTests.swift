import AppKit
import QuartzCore

/// Windowless checks for the planar tile rendering used below the common 3D
/// panel plane. Native control integration is exercised by the graphical smoke.
enum HUDNavigationTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1
            if !condition { fatalError(message, file: file, line: line) }
        }
        func near(_ a: CGFloat, _ b: CGFloat) -> Bool { abs(a - b) < 0.000001 }
        func colorsEqual(_ first: Any?, _ second: Any?) -> Bool {
            guard let first, let second else { return false }
            let left = first as AnyObject, right = second as AnyObject
            guard CFGetTypeID(left) == CGColor.typeID,
                  CFGetTypeID(right) == CGColor.typeID else { return false }
            return CFEqual(left, right)
        }
        func animationCount(_ layer: CALayer) -> Int {
            (layer.animationKeys()?.count ?? 0)
                + (layer.sublayers ?? []).reduce(0) { $0 + animationCount($1) }
                + (layer.mask.map(animationCount) ?? 0)
        }
        let previousLanguage = L10n.language
        defer { L10n.language = previousLanguage }
        L10n.language = .english
        // The new shell may recycle a finite number of authored plates, but
        // its logical desktop actions must not truncate or change saved data.
        do {
            let shortcuts = (0..<100).map { index in
                HUDAppShortcutPresentation(id: UUID(), name: "Saved app \(index) · 应用",
                                           iconPreset: .original, icon: nil)
            }
            for amount in [0, 1, 6, 7, 24, 100] {
                let saved = Array(shortcuts.prefix(amount))
                let entries = HUDDesktopWatchNavigation.entries(shortcuts: saved)
                let modules = entries.compactMap { $0.target.module }
                check(entries.count == HUDModule.allCases.count + amount
                      && modules.count == HUDModule.allCases.count
                      && Set(modules) == Set(HUDModule.allCases),
                      "The source shell retains each of the sixteen stable modules and every saved app")
                check(entries.prefix(4).compactMap { $0.target.module } == [.system, .display, .hotkeys, .about],
                      "The four left-side source slots retain the stable settings categories")
                check(entries.last?.target == .module(.addApp)
                      && entries.last?.title == HUDModule.addApp.title,
                      "Add App remains the final logical navigation action after any number of shortcuts")
                let apps = entries.filter { $0.target.module == nil }
                check(apps.map(\.target) == saved.map { .appShortcut($0.id) }
                      && apps.map(\.title) == saved.map(\.name),
                      "Application IDs, order and user-edited names survive source-slot recycling")
                check(Set(entries.map(\.target)).count == entries.count,
                      "Source navigation never aliases a saved app to a module or another shortcut")
                check(entries.allSatisfy { entry in
                    entry.target.module.map { entry.title == $0.title } ?? true
                }, "Desktop module names replace game labels without changing stable translations")
            }
            let first = shortcuts[0]
            let duplicate = HUDAppShortcutPresentation(id: first.id, name: "Duplicate should not replace first",
                                                       iconPreset: .star, icon: nil)
            let deduplicated = HUDDesktopWatchNavigation.entries(shortcuts: [first, duplicate, shortcuts[1]])
            check(deduplicated.filter { $0.target == .appShortcut(first.id) }.count == 1
                  && deduplicated.first { $0.target == .appShortcut(first.id) }?.title == first.name,
                  "Repeated presentation IDs keep the first saved action and do not duplicate hit targets")
            for language in [AppLanguage.english, .simplifiedChinese, .traditionalChinese, .japanese] {
                L10n.language = language
                let entries = HUDDesktopWatchNavigation.entries(shortcuts: [first])
                check(entries.allSatisfy { entry in
                    entry.target.module.map { entry.title == $0.title } ?? (entry.title == first.name)
                }, "Changing HUD language translates module names without translating user app names")
            }
            L10n.language = .english
        }
        for size in [CGSize(width: 2400, height: 800), CGSize(width: 800, height: 2400)] {
            let target = CGSize(width: 240, height: 82)
            for x: CGFloat in [-1, 0, 1] {
                for y: CGFloat in [-1, 0, 1] {
                    let crop = HUDIdentityCard.thumbnailCrop(imageSize: size, targetSize: target, offset: CGPoint(x: x, y: y))
                    check(CGRect(x: -0.00001, y: -0.00001, width: 1.00002, height: 1.00002).contains(crop),
                          "Thumbnail repositioning never exposes empty image edges")
                    check(near(crop.width * size.width / (crop.height * size.height), target.width / target.height),
                          "Independent thumbnail crop preserves image proportions on either image orientation")
                }
            }
        }
        check(HUDIdentityCard.thumbnailCrop(imageSize: .zero, targetSize: .zero, offset: .zero)
              == CGRect(x: 0, y: 0, width: 1, height: 1), "Unavailable thumbnail dimensions safely use the full image")
        // A portrait can move to every edge at any supported zoom without
        // exposing empty pixels or stretching the imported source image.
        for source in [CGSize(width: 512, height: 512), CGSize(width: 1024, height: 256), CGSize(width: 256, height: 1024)] {
            for target in [CGSize(width: 66, height: 71), CGSize(width: 52, height: 56)] {
                for zoom: Double in [1, 1.5, 4, 10, 20] {
                    for x: CGFloat in [-1, 0, 1] { for y: CGFloat in [-1, 0, 1] {
                        let crop = HUDPortraitArtwork.crop(imageSize: source, targetSize: target, zoom: zoom, offset: CGPoint(x: x, y: y))
                        check(crop.minX >= 0 && crop.minY >= 0 && crop.maxX <= 1.000001 && crop.maxY <= 1.000001,
                              "Portrait pan and zoom remain inside the source on both profile surfaces")
                        check(near(crop.width * source.width / (crop.height * source.height), target.width / target.height),
                              "Portrait framing preserves source proportions at every zoom")
                    } }
                }
                check(HUDPortraitArtwork.crop(imageSize: source, targetSize: target, zoom: .nan, offset: CGPoint(x: CGFloat.infinity, y: CGFloat.nan))
                      == HUDPortraitArtwork.crop(imageSize: source, targetSize: target, zoom: 1, offset: .zero),
                      "Malformed portrait geometry uses a centered, finite crop")
            }
        }
        // Render native pixels rather than just testing crop geometry: reducing
        // the entire import to 512 px first erases these narrow source features.
        do {
            func portraitFixture(width: Int, height: Int, color: (Int, Int) -> (UInt8, UInt8, UInt8)) -> CGImage {
                var bytes = [UInt8](repeating: 0, count: width * height * 4)
                for y in 0..<height { for x in 0..<width {
                    let value = color(x, y), index = (y * width + x) * 4
                    bytes[index] = value.0; bytes[index + 1] = value.1
                    bytes[index + 2] = value.2; bytes[index + 3] = 255
                } }
                return CGImage(width: width, height: height, bitsPerComponent: 8, bitsPerPixel: 32,
                    bytesPerRow: width * 4, space: CGColorSpaceCreateDeviceRGB(),
                    bitmapInfo: CGBitmapInfo(rawValue: CGImageAlphaInfo.premultipliedLast.rawValue),
                    provider: CGDataProvider(data: Data(bytes) as CFData)!, decode: nil,
                    shouldInterpolate: false, intent: .defaultIntent)!
            }
            func pixel(_ image: CGImage, x: Int, y: Int) -> [UInt8] {
                precondition((0..<image.width).contains(x) && (0..<image.height).contains(y))
                var bytes = [UInt8](repeating: 0, count: image.width * image.height * 4)
                bytes.withUnsafeMutableBytes { buffer in
                    let context = CGContext(data: buffer.baseAddress, width: image.width, height: image.height,
                        bitsPerComponent: 8, bytesPerRow: image.width * 4, space: CGColorSpaceCreateDeviceRGB(),
                        bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
                    context.draw(image, in: CGRect(x: 0, y: 0, width: image.width, height: image.height))
                }
                let index = (y * image.width + x) * 4
                return Array(bytes[index..<(index + 4)])
            }
            let detailed = portraitFixture(width: 4096, height: 1024) { x, _ in
                x % 8 < 4 ? (255, 255, 255) : (0, 0, 0)
            }
            for zoom in [10.0, 20.0] {
                let target = CGSize(width: 64, height: 64)
                guard let rendered = HUDPortraitArtwork.renderedImage(detailed, targetSize: target,
                    zoom: zoom, offset: .zero, contentsScale: 2) else { fatalError("Native portrait crop must render") }
                let crop = HUDPortraitArtwork.crop(imageSize: CGSize(width: 4096, height: 1024),
                    targetSize: target, zoom: zoom, offset: .zero)
                func renderedX(sourceX: CGFloat) -> Int {
                    Int((sourceX - crop.minX * 4096) / (crop.width * 4096) * CGFloat(rendered.width))
                }
                let bright = pixel(rendered, x: renderedX(sourceX: 2050), y: rendered.height / 2)
                let dark = pixel(rendered, x: renderedX(sourceX: 2054), y: rendered.height / 2)
                check(bright[0] > 220 && dark[0] < 35 && bright[3] == 255 && dark[3] == 255,
                      "Native four-pixel portrait details remain distinct at 10× and 20× zoom")
                check(rendered.width >= 128 && rendered.height >= 128 && rendered.width <= 192 && rendered.height <= 192,
                      "A high-resolution source produces only a small oversampled display bitmap")
                let cached = HUDPortraitArtwork.renderedImage(detailed, targetSize: target,
                    zoom: zoom, offset: .zero, contentsScale: 2)
                check(cached === rendered, "An unchanged portrait crop reuses its cached display image")
            }
            // Four distinct corners expose reversed Y offsets and rotated EXIF
            // axes that a square, symmetric portrait cannot reveal.
            let colors: [[UInt8]] = [[255, 0, 0, 255], [0, 255, 0, 255],
                                    [0, 0, 255, 255], [255, 255, 0, 255]]
            let asymmetric = portraitFixture(width: 64, height: 32) { x, y in
                let value = colors[(y < 16 ? 0 : 2) + (x < 32 ? 0 : 1)]
                return (value[0], value[1], value[2])
            }
            for (orientation, order) in [(Int32(1), [0, 1, 2, 3]), (Int32(6), [2, 0, 3, 1]), (Int32(8), [1, 3, 0, 2])] {
                let target = orientation == 1 ? CGSize(width: 64, height: 32) : CGSize(width: 32, height: 64)
                guard let rendered = HUDPortraitArtwork.renderedImage(asymmetric, targetSize: target,
                    zoom: 1, offset: .zero, contentsScale: 1, orientation: orientation) else {
                    fatalError("EXIF-oriented portrait must render")
                }
                for corner in 0..<4 {
                    let right = corner % 2 == 1, bottom = corner >= 2
                    check(pixel(rendered, x: rendered.width * (right ? 3 : 1) / 4,
                                y: rendered.height * (bottom ? 3 : 1) / 4) == colors[order[corner]],
                          "Portrait rotation preserves the expected top-left pixel layout for EXIF 1, 6 and 8")
                    guard let panned = HUDPortraitArtwork.renderedImage(asymmetric, targetSize: target,
                        zoom: 2, offset: CGPoint(x: right ? 1 : -1, y: bottom ? 1 : -1),
                        contentsScale: 1, orientation: orientation) else { fatalError("Portrait edge pan must render") }
                    check(pixel(panned, x: panned.width / 2, y: panned.height / 2) == colors[order[corner]],
                          "Portrait edge pans follow visible X/Y axes after EXIF rotation without flipping Y")
                }
            }
            check(HUDPortraitArtwork.renderedImage(asymmetric, targetSize: .zero,
                zoom: 20, offset: .zero, contentsScale: 2) == nil,
                  "An unavailable portrait layout does not create an invalid display bitmap")
        }
        let navigation = HUDNavigation()
        check(AppShortcutArtwork.gameIcon(for: .grid) == .worldMap,
              "Custom app presets use the earth icon independently of square Map navigation")
        let identity = HUDIdentityCard()
        var identityProfile = UserProfile()
        func labels(in layer: CALayer) -> [String] {
            let own = ((layer as? CATextLayer)?.string as? String).map { [$0] } ?? []
            return own + (layer.sublayers ?? []).flatMap { labels(in: $0) }
        }
        identity.setProfile(identityProfile, avatar: nil, background: nil)
        check(labels(in: identity.layer).contains("Authority") && labels(in: identity.layer).contains("MAX"),
              "Maximum-level personal cards show their authority title and max status")
        func cardArtwork(_ root: CALayer) -> [CALayer] {
            [root] + (root.sublayers ?? []).flatMap(cardArtwork) + (root.mask.map(cardArtwork) ?? [])
        }
        let cardAccent = NSColor(srgbRed: 0.23, green: 0.77, blue: 0.64, alpha: 1)
        identity.update(dark: true, accent: cardAccent, contentsScale: 2)
        let retainedCardArtwork = cardArtwork(identity.layer)
        let retainedCardIDs = retainedCardArtwork.map(ObjectIdentifier.init)
        for _ in 0..<8 { identity.update(dark: true, accent: cardAccent, contentsScale: 2) }
        check(cardArtwork(identity.layer).map(ObjectIdentifier.init) == retainedCardIDs,
              "Repeated same-style layout and opening refreshes retain every identity card artwork layer")
        // Keep detached-layer animation registrations alive until inspected.
        CATransaction.begin(); CATransaction.setDisableActions(true)
        identity.setHovered(.profile)
        let hoverTracks = animationCount(identity.layer)
        identity.update(dark: true, accent: cardAccent, contentsScale: 2)
        check((hoverTracks > 0 || HUDRuntimeAppearance.reduceMotion) && animationCount(identity.layer) == hoverTracks,
              "An unchanged appearance refresh does not cancel active identity hover motion")
        identity.resetInteraction()
        CATransaction.commit()
        identity.update(dark: false, accent: cardAccent, contentsScale: 2)
        check(cardArtwork(identity.layer).map(ObjectIdentifier.init) != retainedCardIDs,
              "Changing light/dark appearance repaints the identity card")
        let lightCardArtwork = cardArtwork(identity.layer)
        identity.update(dark: false, accent: .systemPink, contentsScale: 2)
        check(cardArtwork(identity.layer).map(ObjectIdentifier.init) != lightCardArtwork.map(ObjectIdentifier.init),
              "Changing theme color repaints the card instead of retaining stale accent artwork")
        let coloredCardArtwork = cardArtwork(identity.layer)
        identity.update(dark: false, accent: .systemPink, contentsScale: 3)
        check(cardArtwork(identity.layer).map(ObjectIdentifier.init) != coloredCardArtwork.map(ObjectIdentifier.init),
              "A backing-scale change regenerates crisp identity text and portrait artwork")
        L10n.language = .simplifiedChinese
        identity.update(dark: false, accent: .systemPink, contentsScale: 3)
        check(labels(in: identity.layer).contains("权限等级") && labels(in: identity.layer).contains("满级"),
              "The cached card invalidates when its displayed language changes")
        L10n.language = .english
        identity.update(dark: true, accent: cardAccent, contentsScale: 2)
        let beforeProfileArtwork = cardArtwork(identity.layer)
        identity.setProfile(identityProfile, avatar: nil, background: nil, avatarOrientation: 6)
        check(cardArtwork(identity.layer).map(ObjectIdentifier.init) != beforeProfileArtwork.map(ObjectIdentifier.init),
              "Explicit profile/image updates invalidate artwork even when global appearance is unchanged")
        func textLayers(in layer: CALayer) -> [CATextLayer] {
            ((layer as? CATextLayer).map { [$0] } ?? [])
                + (layer.sublayers ?? []).flatMap { textLayers(in: $0) }
        }
        let identityLabels = textLayers(in: identity.layer)
        guard let authorityTitle = identityLabels.first(where: { ($0.string as? String) == "Authority" }),
              let maximumLabel = identityLabels.first(where: { ($0.string as? String) == "MAX" }) else {
            fatalError("Maximum-level card needs both aligned status labels")
        }
        check(authorityTitle.alignmentMode == .right && maximumLabel.alignmentMode == .right
              && near(authorityTitle.frame.maxX, maximumLabel.frame.maxX),
              "Authority and MAX share the same right edge beside the badge icon")
        identityProfile.permissionLevel = 59
        identity.setProfile(identityProfile, avatar: nil, background: nil)
        check(labels(in: identity.layer).contains("Authority") && !labels(in: identity.layer).contains("MAX"),
              "Lower authority levels keep their title but omit the max-level status")
        check(navigation.entries.count == 16 && Set(navigation.entries.compactMap(\.module)) == Set(HUDModule.allCases),
              "The rendered navigation keeps all sixteen stable module identities")
        check(navigation.entries.filter { $0.module == .power || $0.module == .profile }.allSatisfy { $0.layer.isHidden },
              "Power and Profile retain navigation metadata while dedicated shell renderers draw their controls")
        let layerIdentities = navigation.entries.map { ObjectIdentifier($0.layer) }
        let addApp = navigation.entries.first { $0.module == .addApp }!
        check(navigation.rightViewport.height > 350 && navigation.visibleEntries.filter { $0.group == .right }.count == 8
              && navigation.clippedRect(for: addApp) != nil,
              "A taller viewport exposes all four compact rows with a feathered lower edge")
        check(!navigation.canScrollUp && navigation.canScrollDown,
              "Scroll indicators describe the first viewport's available direction")

        for entry in navigation.entries {
            for selected in [false, true] {
                let other: HUDModule = entry.module == .power ? .notes : .power
                navigation.select(entry.module!, animated: false)
                navigation.select(selected ? entry.module! : other, animated: false)
                let expectedScale: CGFloat = entry.group == .bottom ? 1 : (selected ? 1.045 : 1)
                let current = entry.faceLayer.transform
                check(near(hypot(current.m11, current.m12), expectedScale)
                      && near(current.m11 * current.m21 + current.m12 * current.m22, 0)
                      && near(current.m41, 0) && near(current.m42, 0),
                      "Navigation feedback changes uniform scale only; sectors never change scale, position or shear")
                check(CATransform3DIsAffine(current) && current.m34 == 0 && !entry.layer.shouldRasterize && !entry.faceLayer.shouldRasterize,
                      "Every card keeps a crisp affine face without a separate perspective or rasterized surface")
                check(near(entry.projectedRect.midX, entry.rect.midX) && near(entry.projectedRect.midY, entry.rect.midY),
                      "Selected cards expand around their original center without lifting or sliding")
                let center = CGPoint(x: entry.rect.midX, y: entry.rect.midY)
                check(entry.isSelected == selected && navigation.hitTest(point: center) == entry.module,
                      "Every visible card center remains hittable before and after selection")
            }
        }
        let liftedNote = navigation.entries.first { $0.module == .notes }!
        let noteBacking = liftedNote.layer.sublayers!.first { $0.name == "navigation.backingPlate" }!
        if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            navigation.select(.notes, animated: true)
            let expansion = liftedNote.faceLayer.animation(forKey: "navigation.transform") as? CABasicAnimation
            check(expansion != nil && !(expansion is CASpringAnimation) && expansion?.duration == HUDNavigation.selectionTransitionDuration
                  && expansion?.autoreverses == false && expansion?.repeatCount == 0 && expansion?.isRemovedOnCompletion == true,
                  "Selection uses one finite basic interpolation with no keyframe press, spring or bounce")
            guard let from = (expansion?.fromValue as? NSValue)?.caTransform3DValue,
                  let to = (expansion?.toValue as? NSValue)?.caTransform3DValue else { fatalError("Expansion must supply both endpoints") }
            check(near(hypot(from.m11, from.m12), 1) && near(hypot(to.m11, to.m12), 1.045)
                  && from.m42 == 0 && to.m42 == 0 && from.m21 == 0 && to.m21 == 0,
                  "Selection endpoints contain only the starting and expanded face sizes")
            let backingMotion = noteBacking.animation(forKey: "navigation.backingTransform") as? CABasicAnimation
            check(backingMotion != nil && !(backingMotion is CASpringAnimation) && near(noteBacking.transform.m41, 0)
                  && near(noteBacking.transform.m42, 0) && near(hypot(noteBacking.transform.m11, noteBacking.transform.m12), 1.012),
                  "Backing feedback is a smaller smooth expansion around its fixed offset")
            navigation.hover(.notes)
            check(near(hypot(liftedNote.faceLayer.transform.m11, liftedNote.faceLayer.transform.m12), 1.045)
                  && near(hypot(noteBacking.transform.m11, noteBacking.transform.m12), 1.012)
                  && near(liftedNote.faceLayer.transform.m42, -3) && near(noteBacking.transform.m42, -0.8),
                  "Hovering a selected card preserves both scales while the face lifts farther than its backing")
            let began = liftedNote.faceLayer.animation(forKey: "navigation.transform")?.beginTime
            navigation.hover(.notes)
            check(liftedNote.faceLayer.animation(forKey: "navigation.transform")?.beginTime == began,
                  "Repeated pointer events on the same face do not restart its expansion")
            navigation.hover(nil); navigation.cancelAnimations()

            let staged = HUDNavigation()
            for entry in staged.entries where entry.group != .bottom {
                staged.select(entry.module == .power ? .notes : .power, animated: false)
                staged.select(entry.module!, animated: true)
                guard let motion = entry.faceLayer.animation(forKey: "navigation.transform") as? CABasicAnimation,
                      let from = (motion.fromValue as? NSValue)?.caTransform3DValue,
                      let to = (motion.toValue as? NSValue)?.caTransform3DValue else { fatalError("Each side card needs basic scale feedback") }
                let reserved = staged.nativeHitRect(for: entry)
                staged.cancelAnimations()
                for fraction in [CGFloat(0), 0.25, 0.5, 0.75, 1] {
                    var pose = from
                    pose.m11 = from.m11 + (to.m11 - from.m11) * fraction
                    pose.m12 = from.m12 + (to.m12 - from.m12) * fraction
                    pose.m21 = from.m21 + (to.m21 - from.m21) * fraction
                    pose.m22 = from.m22 + (to.m22 - from.m22) * fraction
                    CATransaction.begin(); CATransaction.setDisableActions(true); entry.faceLayer.transform = pose; CATransaction.commit()
                    check(reserved.insetBy(dx: -0.001, dy: -0.001).contains(staged.clippedRect(for: entry)!),
                          "Native envelopes cover the complete monotonic scale interpolation")
                    check(staged.hitTest(point: CGPoint(x: entry.rect.midX, y: entry.rect.midY)) == entry.module,
                          "Pointer hit inversion follows the expanding face at every tested interpolation point")
                }
            }

            let cleanup = HUDNavigation()
            let entry = cleanup.entries.first { $0.module == .notes }!
            let backing = entry.layer.sublayers!.first { $0.name == "navigation.backingPlate" }!
            entry.faceLayer.speed = 0; backing.speed = 0
            func retainTracks() {
                for (layer, key) in [(entry.faceLayer, "navigation.transform"), (backing, "navigation.backingTransform")] {
                    guard let copy = layer.animation(forKey: key)?.copy() as? CAAnimation else { fatalError("Expected bounded transform track") }
                    copy.isRemovedOnCompletion = false; layer.add(copy, forKey: key)
                }
            }
            cleanup.select(.notes, animated: true); retainTracks()
            RunLoop.main.run(until: Date().addingTimeInterval(HUDNavigation.selectionTransitionDuration + 0.08))
            check(animationCount(cleanup.layer) == 0 && near(entry.faceLayer.transform.m11, 1.045),
                  "Deadline cleanup removes finished tracks even from paused layers without changing the final expanded pose")
            cleanup.select(.power, animated: false); cleanup.select(.notes, animated: true); retainTracks()
            RunLoop.main.run(until: Date().addingTimeInterval(0.10))
            cleanup.select(.power, animated: false); cleanup.select(.notes, animated: true); retainTracks()
            RunLoop.main.run(until: Date().addingTimeInterval(0.16))
            check(entry.faceLayer.animation(forKey: "navigation.transform") != nil,
                  "A superseded animation deadline cannot remove a newer expansion")
            cleanup.cancelAnimations()
            check(animationCount(cleanup.layer) == 0 && animationCount(cleanup.bottomLayer) == 0,
                  "Closing removes finite feedback from both navigation roots")
            var temporary: HUDNavigation? = HUDNavigation()
            weak var released = temporary?.entries.first { $0.module == .notes }
            temporary?.select(.notes); temporary = nil
            check(released == nil, "Cleanup deadlines never retain hidden navigation")
        }
        navigation.hover(nil); navigation.cancelAnimations(); navigation.select(.power, animated: false)

        let depth = HUDNavigation()
        for entry in depth.entries where entry.group == .left || entry.group == .right {
            depth.select(.power, animated: false)
            let before = entry.faceLayer.transform
            guard let contentClip = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.contentClip" }),
                  let contents = contentClip.sublayers?.first(where: { $0.name == "navigation.contents" }),
                  let clip = contentClip.mask as? CAShapeLayer,
                  let plate = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.plate" }) as? CAShapeLayer,
                  let lamp = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.hoverLight" }),
                  let backing = entry.layer.sublayers?.first(where: { $0.name == "navigation.backingPlate" }) else {
                fatalError("Side navigation needs separately clipped content, a lamp highlight and a restrained backing")
            }
            let glyphs = (contents.sublayers ?? []).map(\.transform)
            depth.hover(entry.module)
            check(near(hypot(before.m11, before.m12), hypot(entry.faceLayer.transform.m11, entry.faceLayer.transform.m12))
                  && near(contents.transform.m11, 1) && near(backing.transform.m11, before.m11),
                  "Hover never scales the face, backing, text or icon")
            check(abs(backing.transform.m42) < abs(entry.faceLayer.transform.m42)
                  && abs(contents.transform.m42) > abs(entry.faceLayer.transform.m42)
                  && near(entry.faceLayer.transform.m42 + contents.transform.m42, -6.8),
                  "Backing lifts least, face next and the contained icon/text plane lifts farthest")
            check(clip.path == plate.path && CATransform3DIsIdentity(contentClip.transform)
                  && zip(contents.sublayers ?? [], glyphs).allSatisfy { CATransform3DEqualToTransform($0.0.transform, $0.1) },
                  "A stationary face-shaped mask contains the raised glyphs while preserving their individual sizes")
            check(lamp.opacity == 0.72 && plate.fillColor == NSColor(white: 1, alpha: 1).cgColor,
                  "Hover commits a steady opaque white face and lamp after its finite activation")
            let activation = plate.animation(forKey: "navigation.fillColor") as? CAKeyframeAnimation
            if !HUDRuntimeAppearance.reduceMotion {
                let colors = activation?.values
                check(activation?.duration == 1.0 / 6.0
                      && activation?.keyTimes == [0, 0.2, 0.4, 0.6, 1]
                      && activation?.timingFunctions?.count == 4
                      && activation?.repeatCount == 0 && activation?.autoreverses == false
                      && colors?.count == 5 && colorsEqual(colors?[1], colors?[3]) && colorsEqual(colors?[3], colors?[4])
                      && colorsEqual(colors?[0], colors?[2]) && !colorsEqual(colors?[0], colors?[1]),
                      "Highlighted replays two 30 Hz brightness activations over one sixth second and then holds")
                check(lamp.animation(forKey: "navigation.opacity")?.duration == HUDNavigation.hoverTransitionDuration,
                      "The lamp joins the finite activation instead of changing opacity abruptly")
            } else {
                check(activation == nil && (lamp.animationKeys() ?? []).isEmpty,
                      "Reduce Motion commits the steady highlight without either brightness activation")
            }
            let liftBegan = entry.faceLayer.animation(forKey: "navigation.transform")?.beginTime
            let activationBegan = activation?.beginTime
            depth.hover(entry.module)
            check(plate.animation(forKey: "navigation.fillColor")?.beginTime == activationBegan
                  && entry.faceLayer.animation(forKey: "navigation.transform")?.beginTime == liftBegan,
                  "Repeated pointer samples restart neither the brightness activation nor the lift")
            guard let border = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.outerBorder" }) as? CAShapeLayer,
                  let faceBounds = plate.path?.boundingBoxOfPath, let borderBounds = border.path?.boundingBoxOfPath else {
                fatalError("Side buttons need a detached outer border")
            }
            check(borderBounds.contains(faceBounds) && (2.8...3.2).contains(faceBounds.minX - borderBounds.minX)
                  && near(faceBounds.minY - borderBounds.minY, 3) && plate.lineWidth == 0,
                  "The only face outline sits outside the filled tile with a three-point clear gap")
            depth.hover(nil)
            check(lamp.animation(forKey: "navigation.hoverBlink") == nil && lamp.opacity == 0
                  && CATransform3DIsIdentity(contents.transform), "Leaving commits the resting lamp and glyph depth without a repeating blink")
            check(HUDRuntimeAppearance.reduceMotion
                  ? lamp.animation(forKey: "navigation.opacity") == nil
                  : lamp.animation(forKey: "navigation.opacity")?.duration == HUDNavigation.hoverExitDuration,
                  "Leaving follows the controller's 0.1 second Normal blend, respecting Reduce Motion")
            depth.cancelAnimations()
        }
        let stableNote = depth.entries.first { $0.module == .notes }!
        let edge = CGPoint(x: stableNote.rect.midX, y: stableNote.rect.maxY - 2.5)
        check(depth.hitTest(point: edge) == .notes, "The lower face edge accepts the pointer before lift")
        depth.hover(.notes)
        check(depth.hoverHitTest(point: edge) == .notes && depth.hitTest(point: edge) == nil,
              "Hover remains stable over the original edge while exact clicks follow the lifted face")
        check(depth.hoverHitTest(point: CGPoint(x: stableNote.rect.midX, y: stableNote.rect.maxY + 8)) != .notes,
              "The hover corridor is bounded and does not hold unrelated surrounding space")
        let lamp = stableNote.faceLayer.sublayers!.first { $0.name == "navigation.hoverLight" }!
        depth.hover(nil); depth.hover(.notes)
        RunLoop.main.run(until: Date().addingTimeInterval(0.30))
        check(animationCount(depth.layer) == 0 && lamp.opacity == 0.72,
              "Reentering settles to a steady highlight after the finite brightness activation")
        depth.hover(nil); depth.hover(.notes); depth.cancelAnimations()
        check(animationCount(depth.layer) == 0 && lamp.opacity == 0 && near(stableNote.faceLayer.transform.m42, 0),
              "Hiding removes all pending feedback and commits the resting pose")

        if !HUDRuntimeAppearance.reduceMotion {
            let plate = stableNote.faceLayer.sublayers!.first { $0.name == "navigation.plate" } as! CAShapeLayer
            let border = stableNote.faceLayer.sublayers!.first { $0.name == "navigation.outerBorder" } as! CAShapeLayer
            let backing = stableNote.layer.sublayers!.first { $0.name == "navigation.backingPlate" } as! CAShapeLayer
            depth.hover(.notes)
            let shownColor = plate.presentation()?.fillColor ?? plate.fillColor!
            let shownLamp = lamp.presentation()?.opacity ?? lamp.opacity
            depth.hover(nil)
            let exit = plate.animation(forKey: "navigation.fillColor") as? CABasicAnimation
            let lampExit = lamp.animation(forKey: "navigation.opacity") as? CABasicAnimation
            check((exit == nil ? shownColor == plate.fillColor : colorsEqual(exit?.fromValue, shownColor))
                  && (lampExit == nil ? shownLamp == lamp.opacity : lampExit?.fromValue as? Float == shownLamp),
                  "A fast exit retargets the rendered color and opacity instead of restarting from an idle value")
            for _ in 0..<16 {
                depth.hover(.notes); depth.hover(nil)
                check((plate.animationKeys() ?? []).count <= 2
                      && (lamp.animationKeys() ?? []).count <= 1
                      && plate.fillColor != NSColor(white: 1, alpha: 1).cgColor && lamp.opacity == 0,
                      "Rapid enter and exit replace finite feedback tracks without accumulating animations")
            }
            depth.hover(.notes)
            check(border.animation(forKey: "navigation.strokeColor") != nil
                  && backing.animation(forKey: "navigation.fillColor") != nil,
                  "The detached outline and subdued backing smoothly join the hover state")
            let appearance = HUDRuntimeAppearance.configuration
            HUDRuntimeAppearance.configuration.reduceMotion = true
            depth.update(dark: true, accent: HUDRuntimeAppearance.accent, contentsScale: 2)
            check(animationCount(depth.layer) == 0 && lamp.opacity == 0.72
                  && plate.fillColor == NSColor(white: 1, alpha: 1).cgColor,
                  "Enabling Reduce Motion cancels in-flight feedback and preserves the steady hovered state")
            depth.hover(nil)
            check(animationCount(depth.layer) == 0 && lamp.opacity == 0,
                  "Reduce Motion also makes hover exit immediate without retaining finite tracks")
            HUDRuntimeAppearance.configuration = appearance
            depth.cancelAnimations()
        }

        check(navigation.hitTest(point: CGPoint(x: CGFloat.nan, y: 0)) == nil
              && navigation.hitTest(point: CGPoint(x: -1000, y: -1000)) == nil,
              "Invalid and outside pointer locations cannot activate a tile")

        navigation.scrollPixels(-10_000, animated: false)
        let right = navigation.entries.filter { $0.group == .right }
        check(right.allSatisfy { $0.rect.size == CGSize(width: 78, height: 74) && near($0.faceLayer.transform.m12, 0) },
              "Right cards use an upright near-square face while their positions follow the arc")
        check(near(right[2].rect.minY - right[0].rect.minY, 90)
              && near(navigation.maxRightScrollOffset, 16),
              "Smaller right cards have closer spacing and retain bounded continuous scroll travel")
        check(right.compactMap { navigation.clippedRect(for: $0) }.allSatisfy(navigation.rightViewport.contains),
              "Native control geometry is clipped to the taller visual viewport")
        let left = navigation.entries.filter { $0.group == .left }
        check(left[0].rect.minX > left[1].rect.minX && left[3].rect.minX > left[2].rect.minX
              && left[0].faceLayer.transform.m12 < 0 && left[3].faceLayer.transform.m12 > 0
              && left.allSatisfy { abs(atan2($0.faceLayer.transform.m12, $0.faceLayer.transform.m11)) < 0.025 && $0.rect.height >= 84 },
              "Left cards retain a curved arrangement with gentler rotation and taller readable faces")
        check(zip(left, left.dropFirst()).allSatisfy { near($0.1.rect.minY - $0.0.rect.minY, 95) },
              "Left cards leave clearance for detached outlines without changing their readable face size")
        check(navigation.entries.allSatisfy { entry in
            entry.layer.sublayers?.first { $0.name == "navigation.backingPlate" }.map {
                near($0.position.x - entry.layer.bounds.midX, entry.group == .right ? -8 : 8)
                    && near($0.position.y - entry.layer.bounds.midY, 10)
            } == true
        }, "Backings have a stronger eight-point side and ten-point downward offset, with right backings extending left")
        check(navigation.entries.allSatisfy { entry in
            guard let side = entry.layer.sublayers?.first(where: { $0.name == "navigation.backingPlate" }) as? CAShapeLayer else { return false }
            return (side.fillColor?.alpha ?? 1) <= 0.21 && (side.strokeColor?.alpha ?? 1) <= 0.4
                && side.lineWidth < 0.7 && side.shadowOpacity == 0
                && !(entry.layer.sublayers ?? []).contains { $0.name == "navigation.backingOutline" }
        }, "Cards retain one translucent thin underplate without opaque stacked outlines or blurred shadows")
        for entry in left {
            guard let strands = entry.layer.sublayers?.first(where: { $0.name == "navigation.connectors" }) as? CAShapeLayer,
                  let path = strands.path else { fatalError("Each left card needs its own connector strands") }
            var starts: [CGPoint] = [], ends: [CGPoint] = []
            path.applyWithBlock { pointer in
                if pointer.pointee.type == .moveToPoint { starts.append(pointer.pointee.points[0]) }
                if pointer.pointee.type == .addLineToPoint { ends.append(pointer.pointee.points[0]) }
            }
            check(starts.count == 5 && ends.count == 15 && starts.allSatisfy { $0.x < entry.rect.width }
                  && near(ends.map(\.x).max()! + entry.rect.minX, 300) && strands.superlayer === entry.layer
                  && (strands.strokeColor?.alpha ?? 1) < 0.5 && (strands.animationKeys() ?? []).isEmpty,
                  "Five extended gray strands reach design x300 under the left base without inheriting the raised face transform")
            let fade = strands.mask as? CAGradientLayer
            check(fade?.locations?.count == 3
                  && near(CGFloat(fade!.locations![1].doubleValue) * fade!.bounds.width, fade!.bounds.width - 16)
                  && fade?.filters?.isEmpty != false,
                  "The last sixteen points of each extended connector fade into the ring without a blur")
        }
        check(navigation.entries.filter { $0.group != .left }.allSatisfy {
            !($0.layer.sublayers ?? []).contains { $0.name == "navigation.connectors" }
        }, "Only the left navigation owns the inward connector strands")
        guard let rightClip = navigation.layer.sublayers?.first(where: { $0.name == "hud.navigation.rightViewport" }),
              let feather = rightClip.mask as? CAGradientLayer,
              let colors = feather.colors, colors.count == 4 else { fatalError("Right scrolling needs a retained alpha-gradient mask") }
        let alpha = colors.map { ($0 as! CGColor).alpha }
        let featherIdentity = ObjectIdentifier(feather)
        check(feather.frame == navigation.rightViewport && alpha == [0, 1, 1, 0]
              && feather.startPoint == CGPoint(x: 0.5, y: 0) && feather.endPoint == CGPoint(x: 0.5, y: 1)
              && (feather.locations?[1].doubleValue ?? 0) > 0
              && (feather.locations?[2].doubleValue ?? 1) < 1
              && rightClip.filters?.isEmpty != false && !rightClip.shouldRasterize,
              "The strip fades vertically at both edges using a small gradient mask, with an opaque readable center and no blur")
        var arrowPoints: [[CGPoint]] = []
        for (name, rect) in [("navigation.scrollUp", navigation.scrollUpRect), ("navigation.scrollDown", navigation.scrollDownRect)] {
            guard let arrow = navigation.layer.sublayers?.first(where: { $0.name == name }),
                  let glyph = arrow.sublayers?.first(where: { $0.name == "navigation.scrollChevron" }) as? CAShapeLayer,
                  let echo = arrow.sublayers?.first(where: { $0.name == "navigation.scrollEcho" }) as? CAShapeLayer,
                  let baseline = arrow.sublayers?.first(where: { $0.name == "navigation.scrollBaseline" }) as? CAShapeLayer else {
                fatalError("Scroll arrows need a chevron pair, offset echo and baseline")
            }
            var points: [CGPoint] = []
            glyph.path?.applyWithBlock { pointer in points.append(pointer.pointee.points[0]) }
            arrowPoints.append(points)
            check(arrow.frame == rect && points.count == 6 && (echo.strokeColor?.alpha ?? 1) < 0.25
                  && echo.frame.origin != glyph.frame.origin && (baseline.path?.boundingBox.width ?? 0) == navigation.rightViewport.width
                  && baseline.lineWidth < 1 && arrow.shadowOpacity == 0,
                  "Each scroll arrow keeps its original hit rectangle with a double chevron, faint offset echo and long hairline")
        }
        check(zip(arrowPoints[0], arrowPoints[1]).allSatisfy { near($0.0.x, $0.1.x) && near($0.0.y + $0.1.y, navigation.scrollUpRect.height) },
              "Up and down chevrons are exact vertical counterparts")
        let sectors = navigation.entries.filter { $0.group == .bottom }
        check(sectors.allSatisfy { $0.rect.minY == 463 && $0.rect.size == CGSize(width: 144, height: 65)
            && $0.layer.superlayer === navigation.bottomLayer }
              && navigation.bottomLayer !== navigation.layer && navigation.bottomLayer.superlayer == nil
              && navigation.entries.first { $0.module == .power }?.rect == HUDChargeBadge.compactHitRect
              && navigation.entries.first { $0.module == .profile }?.rect == CGRect(x: 78, y: 564, width: 240, height: 82),
              "Bottom sectors occupy the circular instrument and expose an independent root for its stronger parallax plane")
        check(near(sectors[0].rect.maxX + 12, sectors[1].rect.minX)
              && near(sectors[0].rect.minX + sectors[1].rect.maxX, 1000),
              "The mirrored sectors retain a narrow twelve-point central gap")
        let centerButtonSpan = sectors[1].rect.maxX - sectors[0].rect.minX
        check((0.55...0.58).contains(HUDChargeBadge.compactHitRect.width / centerButtonSpan)
              && HUDChargeBadge.compactHitRect.maxY < sectors[0].rect.minY,
              "The smaller battery bar spans slightly over half the unchanged center-button pair and clears their top edge")
        var sectorPaths: [CGPath] = []
        for entry in sectors {
            guard let plate = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.plate" }) as? CAShapeLayer,
                  let path = plate.path,
                  let stripe = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.bottomStripe" }) as? CAShapeLayer,
                  let border = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.innerBorder" }) as? CAShapeLayer,
                  let technical = entry.faceLayer.sublayers?.first(where: { $0.name == "navigation.bottomTechnical" }) as? CAShapeLayer else {
                fatalError("Each bottom sector needs its own vector face, stripe, inset border and technical marks")
            }
            sectorPaths.append(path)
            let bottomIcon = entry.faceLayer.sublayers!.first { $0.name == "navigation.icon" } as! CAShapeLayer
            check(bottomIcon.fillRule == .nonZero && bottomIcon.shadowOpacity > 0 && bottomIcon.shadowOpacity < 0.35
                  && bottomIcon.shadowRadius < 1 && entry.faceLayer.shadowOpacity == 0 && entry.layer.shadowOpacity == 0,
                  "Only bottom glyphs have a restrained drop shadow and solid overlap fill")
            check(path.boundingBoxOfPath.minY + entry.rect.minY > 463
                  && path.boundingBoxOfPath.maxY + entry.rect.minY <= 528,
                  "Smaller sectors remain below timer controls and within the central lower arc")
            var stripeTop: [CGPoint] = []
            stripe.path?.applyWithBlock { if stripeTop.count < 2 && $0.pointee.type != .closeSubpath { stripeTop.append($0.pointee.points[0]) } }
            check(stripeTop.count == 2 && near(stripeTop[0].y, stripeTop[1].y)
                  && abs(stripeTop[0].x - stripeTop[1].x) > 75,
                  "The bright lower band has a wide flat top and tapers into the circular edge")
            var curveCount = 0
            path.applyWithBlock { if $0.pointee.type == .addCurveToPoint { curveCount += 1 } }
            check(curveCount > 0 && (plate.fillColor?.alpha ?? 1) < 0.65
                  && (stripe.fillColor?.alpha ?? 0) > 0.85 && border.path != nil && technical.path != nil,
                  "The translucent sector has a true circular lower edge, brighter lower stripe, double border and sparse technical markings")
            let point = CGPoint(x: entry.rect.midX, y: entry.rect.midY)
            check(navigation.hitTestBottom(point: point) == entry.module
                  && navigation.hitTest(point: point, includingBottom: false) == nil,
                  "Core-plane hit routing distinguishes bottom sectors from side-plane buttons")
            let outerX = entry.module == .storage ? entry.rect.minX + 2 : entry.rect.maxX - 2
            check(navigation.hitTestBottom(point: CGPoint(x: outerX, y: entry.rect.maxY - 8)) == nil,
                  "Transparent corners below the circular arc cannot steal central content clicks")
        }
        for x in stride(from: CGFloat(5), to: 140, by: 13) {
            for y in stride(from: CGFloat(5), to: 64, by: 11) {
                check(sectorPaths[0].contains(CGPoint(x: x, y: y)) == sectorPaths[1].contains(CGPoint(x: 144 - x, y: y)),
                      "Both circular sector outlines are exact mirrors")
            }
        }
        let sectorRects = sectors.map(\.projectedRect)
        navigation.select(.storage, animated: true); navigation.hover(.activityMonitor)
        check(sectors.map(\.projectedRect) == sectorRects && sectors.allSatisfy { CATransform3DIsIdentity($0.faceLayer.transform)
            && $0.faceLayer.animation(forKey: "navigation.transform") == nil
            && $0.layer.sublayers!.first { $0.name == "navigation.backingPlate" }!.animation(forKey: "navigation.backingTransform") == nil },
              "Selecting or hovering a bottom sector cannot scale, lift or animate its backing")
        check(NSWorkspace.shared.accessibilityDisplayShouldReduceMotion || animationCount(navigation.bottomLayer) > 0,
              "Bottom sectors animate their fill highlight while retaining fixed shape and geometry")
        navigation.cancelAnimations()
        check(animationCount(navigation.bottomLayer) == 0, "Closing removes finite tracks from the separate core-plane navigation root")
        let activitySector = sectors.first { $0.module == .activityMonitor }!
        let activityPlate = activitySector.faceLayer.sublayers!.first { $0.name == "navigation.plate" } as! CAShapeLayer
        navigation.select(.activityMonitor, animated: false)
        let selectedGold = activityPlate.fillColor!
        navigation.hover(.activityMonitor)
        check((activityPlate.fillColor?.alpha ?? 0) > selectedGold.alpha && (activityPlate.fillColor?.alpha ?? 0) >= 0.90
              && CATransform3DIsIdentity(activitySector.faceLayer.transform),
              "Hovering the already-selected center sector adds a stronger visible gold highlight without lifting or scaling")
        navigation.cancelAnimations()
        navigation.select(.power, animated: false)
        let partial = navigation.clippedRect(for: addApp)!
        check(navigation.hitTest(point: CGPoint(x: partial.midX, y: partial.midY)) == .addApp
              && navigation.hitTest(point: CGPoint(x: addApp.rect.midX, y: navigation.rightViewport.maxY + 4)) != .addApp,
              "A partial card is clickable only in the portion shown by the mask")
        let reservedFrames = right.map { navigation.nativeHitRect(for: $0) }
        for selected in right {
            navigation.select(selected.module!, animated: false)
            var coversEveryVisibleFace = true
            for delta in [CGFloat(-10_000), -180, -50, 0, 8, 20, 39, 80, 220, 10_000] {
                navigation.cancelAnimations()
                navigation.scrollPixels(-10_000, animated: false)
                _ = navigation.scroll(at: CGPoint(x: 900, y: 300), delta: delta, phase: .began)
                for entry in right {
                    let shown = entry.projectedRect.intersection(navigation.rightViewport)
                    if !shown.isNull && !shown.isEmpty {
                        coversEveryVisibleFace = coversEveryVisibleFace
                            && navigation.nativeHitRect(for: entry).insetBy(dx: -0.000001, dy: -0.000001).contains(shown)
                    }
                }
            }
            check(coversEveryVisibleFace && right.map { navigation.nativeHitRect(for: $0) } == reservedFrames,
                  "Stable native envelopes cover every selected and inactive card along its curved and elastic trajectory")
        }
        navigation.cancelAnimations()
        navigation.select(.power, animated: false)
        navigation.scrollPixels(-10_000, animated: false)
        var layouts = 0
        navigation.onLayoutChange = { layouts += 1 }
        let scrollPoint = CGPoint(x: navigation.rightViewport.midX, y: navigation.rightViewport.midY)
        let originalY = right[0].rect.minY
        let leftRects = left.map(\.rect)
        for _ in 0..<30 { _ = navigation.scroll(at: scrollPoint, delta: 0.5) }
        check(near(navigation.rightScrollOffset, 15) && near(right[0].rect.minY, originalY - 15) && layouts == 1,
              "Tiny wheel samples move by exact pixels while native relayout occurs only when arrow availability changes")
        check(left.map(\.rect) == leftRects && animationCount(navigation.layer) == 0,
              "Continuous scroll moves only right-side cards and adds no per-sample animations")
        _ = navigation.scroll(at: scrollPoint, delta: -10)
        _ = navigation.scroll(at: scrollPoint, delta: 7)
        check(near(navigation.rightScrollOffset, 12), "Reversing direction preserves exact pixel displacement instead of discarding accumulated movement")
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .began)
        _ = navigation.scroll(at: scrollPoint, delta: 2.25, phase: .changed)
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .ended)
        check(near(navigation.rightScrollOffset, 14.25) && !navigation.isScrollGestureActive && animationCount(navigation.layer) == 0,
              "Ending an in-bounds gesture preserves its fractional offset without snapping")
        let outside = CGPoint(x: 500, y: 320)
        check(navigation.scroll(at: outside, delta: 0.75, momentumPhase: .began)
              && navigation.scroll(at: outside, delta: 0.5, momentumPhase: .changed),
              "Momentum remains owned by navigation when the pointer leaves after finger-up")
        _ = navigation.scroll(at: outside, delta: 0, momentumPhase: .ended)
        check(near(navigation.rightScrollOffset, 15.5) && !navigation.isScrollGestureActive
              && !navigation.scroll(at: outside, delta: 2, momentumPhase: .changed),
              "Momentum ends at a continuous offset and releases routing ownership")
        check(!navigation.scroll(at: CGPoint(x: 500, y: 320), delta: 100)
              && !navigation.scroll(at: scrollPoint, delta: .nan)
              && !navigation.scroll(at: CGPoint(x: CGFloat.infinity, y: 0), delta: 10),
              "Scrolling outside navigation or with invalid deltas leaves other modules in control")

        navigation.scrollPixels(-10_000, animated: false)
        _ = navigation.scroll(at: scrollPoint, delta: -30, phase: .began)
        let smallPull = navigation.rightScrollOffset
        _ = navigation.scroll(at: scrollPoint, delta: -70, phase: .changed)
        check(smallPull < 0 && navigation.rightScrollOffset < smallPull && navigation.rightScrollOffset > -58,
              "Dragging beyond the top produces bounded elastic resistance instead of a hard clamp")
        check(navigation.clippedRect(for: addApp)!.height < partial.height
              && navigation.hitTest(point: CGPoint(x: addApp.rect.midX, y: navigation.rightViewport.maxY + 4)) != .addApp,
              "Elastic pulling clips more of the final card without making its hidden portion clickable")
        check(navigation.scroll(at: outside, delta: 0, phase: .ended) && near(navigation.rightScrollOffset, 0),
              "A zero-delta finger-up outside the viewport commits the legal top boundary")
        let bounce = right.compactMap { $0.layer.animation(forKey: "navigation.position") }
        check(NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
              ? bounce.isEmpty : !bounce.isEmpty && bounce.allSatisfy { $0 is CASpringAnimation && $0.duration <= 0.5 && $0.repeatCount == 0 },
              "Overscroll release uses one finite spring per moving card, respecting Reduce Motion")
        check(right.compactMap { navigation.clippedRect(for: $0) }.allSatisfy(navigation.rightViewport.contains),
              "Even a rebound's native movement envelopes remain clipped")
        let releaseFrom = (right[0].layer.animation(forKey: "navigation.position") as? CABasicAnimation)?.fromValue as? NSValue
        let reboundLayouts = layouts
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .ended)
        check(navigation.scroll(at: outside, delta: -12, momentumPhase: .began),
              "A rebound continues to own its momentum after the pointer leaves navigation")
        for _ in 0..<20 { _ = navigation.scroll(at: outside, delta: -2, momentumPhase: .changed) }
        check(near(navigation.rightScrollOffset, 0) && !navigation.isScrollGestureActive
              && layouts == reboundLayouts
              && (NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
                  || (right[0].layer.animation(forKey: "navigation.position") as? CABasicAnimation)?.fromValue as? NSValue == releaseFrom),
              "A repeated finger-end and inertial tail cannot interrupt, restart or defer the original rebound")
        _ = navigation.scroll(at: outside, delta: 0, momentumPhase: .ended)
        check(!navigation.scroll(at: outside, delta: -2, momentumPhase: .changed),
              "Finishing a consumed inertial tail releases navigation routing ownership")
        navigation.cancelAnimations()
        check(animationCount(navigation.layer) == 0 && !navigation.isScrollGestureActive && near(navigation.rightScrollOffset, 0),
              "Closing cancels rebound and commits legal geometry with no remaining motion")
        check(right.allSatisfy { $0.interactionRect == $0.projectedRect },
              "Cancelled movement envelopes shrink to the committed card bounds")

        navigation.scrollPixels(10_000, animated: false)
        _ = navigation.scroll(at: scrollPoint, delta: 50, phase: .began)
        let bottomPull = navigation.rightScrollOffset
        check(bottomPull > navigation.maxRightScrollOffset && bottomPull < navigation.maxRightScrollOffset + 58,
              "The lower edge uses the same bounded overscroll resistance")
        _ = navigation.scroll(at: scrollPoint, delta: 10, momentumPhase: .began)
        check(near(navigation.rightScrollOffset, navigation.maxRightScrollOffset) && !navigation.isScrollGestureActive,
              "Momentum reaching an elastic boundary rebounds immediately instead of waiting for its inertial tail")
        let boundaryFrom = (right[0].layer.animation(forKey: "navigation.position") as? CABasicAnimation)?.fromValue as? NSValue
        _ = navigation.scroll(at: outside, delta: 8, momentumPhase: .changed)
        check(near(navigation.rightScrollOffset, navigation.maxRightScrollOffset)
              && (NSWorkspace.shared.accessibilityDisplayShouldReduceMotion
                  || (right[0].layer.animation(forKey: "navigation.position") as? CABasicAnimation)?.fromValue as? NSValue == boundaryFrom),
              "Further boundary momentum leaves the same finite spring running")
        _ = navigation.scroll(at: scrollPoint, delta: 0, momentumPhase: .cancelled)
        check(near(navigation.rightScrollOffset, navigation.maxRightScrollOffset)
              && !navigation.isScrollGestureActive && animationCount(navigation.layer) == 0,
              "Cancelling momentum clears rebound tracks and restores the lower bound immediately")
        navigation.scrollPixels(-10_000, animated: false)
        _ = navigation.scroll(at: scrollPoint, delta: -20, phase: .began)
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .ended)
        _ = navigation.scroll(at: scrollPoint, delta: 3, phase: .began)
        check(near(navigation.rightScrollOffset, 3) && navigation.isScrollGestureActive && animationCount(navigation.layer) == 0,
              "A fresh finger gesture can immediately take control from an unfinished rebound")
        _ = navigation.scroll(at: scrollPoint, delta: .greatestFiniteMagnitude, phase: .began)
        check(navigation.rightScrollOffset.isFinite && right.allSatisfy { $0.rect.minX.isFinite && $0.rect.minY.isFinite },
              "Extremely large finite deltas cannot create NaN layer geometry")
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .cancelled)

        navigation.scrollPixels(-10_000, animated: false)
        _ = navigation.scroll(at: scrollPoint, delta: 3, phase: .began)
        _ = navigation.scroll(at: scrollPoint, delta: 0, phase: .ended)
        check(!navigation.scroll(at: outside, delta: 0, phase: .began)
              && !navigation.scroll(at: outside, delta: 2, momentumPhase: .began),
              "A new outside gesture clears old momentum ownership before another module scrolls")
        navigation.scrollPixels(-10_000, animated: false)
        navigation.scrollRows(1, animated: false)
        check(near(navigation.rightScrollOffset, min(32, navigation.maxRightScrollOffset)) && navigation.canScrollUp && !navigation.canScrollDown,
              "An accessible arrow nudges by pixels and clamps to the compact strip’s nearby lower bound")
        navigation.scrollRows(1, animated: false)
        check(near(navigation.rightScrollOffset, navigation.maxRightScrollOffset),
              "Repeated arrows stop at the continuous lower bound without snapping to a page")
        let up = navigation.scrollUpRect
        check(navigation.handleScrollClick(at: CGPoint(x: up.midX, y: up.midY)) && near(navigation.rightScrollOffset, max(0, navigation.maxRightScrollOffset - 32)),
              "The visible up-arrow performs the same scroll operation as accessibility controls")
        navigation.cancelAnimations()
        navigation.select(.addApp, animated: false)
        check(near(navigation.rightScrollOffset, navigation.maxRightScrollOffset)
              && navigation.rightViewport.contains(addApp.projectedRect) && navigation.selectedModule == .addApp,
              "Programmatic selection minimally reveals a partial module inside the viewport")
        navigation.scrollPixels(-10_000, animated: false)
        navigation.select(.addApp, animated: false)
        check(navigation.rightViewport.contains(addApp.projectedRect),
              "Selecting an already selected but partially clipped module reveals it again")
        navigation.select(.notes, animated: false)
        check(near(navigation.rightScrollOffset, 12)
              && navigation.rightViewport.contains(right[0].projectedRect),
              "Selecting the first module restores enough space for its full card")
        check(ObjectIdentifier(rightClip.mask!) == featherIdentity,
              "Scrolling and section selection retain the same feather mask")
        check(navigation.entries.map { ObjectIdentifier($0.layer) } == layerIdentities,
              "All layer identities survive scroll, reveal, selection and animation cancellation")

        navigation.select(.power, animated: false)
        let selectedFlags = navigation.entries.map(\.isSelected)
        let transforms = navigation.entries.map { $0.faceLayer.transform }
        for module in HUDModule.allCases {
            navigation.hover(module)
            let entry = navigation.entries.first { $0.module == module }!
            let first = entry.faceLayer.animation(forKey: "navigation.transform")
            navigation.hover(module)
            check(navigation.selectedModule == .power && navigation.entries.map(\.isSelected) == selectedFlags,
                  "Hover feedback never changes module selection")
            if module != .power, entry.isVisible {
                let side = module.group == .left || module.group == .right
                check(near(hypot(entry.faceLayer.transform.m11, entry.faceLayer.transform.m12), 1)
                      && near(entry.faceLayer.transform.m42, side ? -3 : 0)
                      && navigation.nativeHitRect(for: entry).contains(navigation.clippedRect(for: entry)!),
                      "Hover preserves side-card size, lifts them gently, and keeps bottom sectors fixed inside reserved hit geometry")
                if side {
                    check(NSWorkspace.shared.accessibilityDisplayShouldReduceMotion ? first == nil
                          : first is CABasicAnimation && !(first is CASpringAnimation) && first!.duration == HUDNavigation.hoverTransitionDuration
                            && entry.faceLayer.animation(forKey: "navigation.transform")?.beginTime == first!.beginTime,
                          "Hover uses one monotonic lift that repeated pointer samples do not restart")
                }
            }
        }
        navigation.hover(nil); navigation.cancelAnimations()
        check(navigation.selectedModule == .power && animationCount(navigation.layer) == 0 && animationCount(navigation.bottomLayer) == 0
              && zip(navigation.entries, transforms).allSatisfy { CATransform3DEqualToTransform($0.0.faceLayer.transform, $0.1) },
              "Closing resets hover depth and removes scale and highlight feedback without changing selection")
        navigation.hover(.notes); navigation.cancelAnimations()
        check(near(liftedNote.faceLayer.transform.m11, 1), "Hiding clears hover even without a mouse-exit event")
        navigation.hover(.notes)
        check(near(liftedNote.faceLayer.transform.m11, 1) && near(noteBacking.transform.m11, 1)
              && near(liftedNote.faceLayer.transform.m42, -3) && near(noteBacking.transform.m42, -0.8),
              "Reopening permits the same card's layered lift again without scaling")
        navigation.cancelAnimations()

        let names: [(HUDModule, String, String)] = [
            (.notes, "Notes", "便笺"), (.fileShelf, "Temporary File Shelf", "文件暂存架"),
            (.clipboard, "Clipboard Cache", "剪贴板"), (.volume, "Volume", "音量"),
            (.workMode, "Work Mode", "工作模式"), (.eventLog, "Event Log", "事件日志"), (.map, "Map", "地图"),
            (.addApp, "+ Add App", "+ 添加应用"), (.system, "System", "系统"),
            (.display, "Display", "显示"), (.hotkeys, "Hotkeys", "快捷键"), (.about, "About", "关于"),
            (.storage, "Storage", "存储"), (.activityMonitor, "Activity Monitor", "活动监视器"),
            (.power, "Power / Device Battery", "电源 / 设备电量"), (.profile, "Personal Profile", "个人名片")
        ]
        let identities = navigation.entries.map { $0.module?.rawValue }
        for language in [AppLanguage.english, .simplifiedChinese] {
            L10n.language = language
            navigation.update(dark: language == .english, accent: .systemYellow, contentsScale: 2)
            for (module, english, chinese) in names {
                check(module.navigationTitle == (language == .english ? english : chinese),
                      "Native accessibility labels retain the complete localized module name")
            }
            let bottomLabels = sectors.map { $0.faceLayer.sublayers!.compactMap { $0 as? CATextLayer }.first! }
            check(bottomLabels.allSatisfy { near($0.fontSize, 11.5) }, "Both sector labels use the same readable font size in each locale")
            check(navigation.entries.map { $0.module?.rawValue } == identities && navigation.selectedModule == .power,
                  "Localized tile repaint preserves persistent identity and selection")
        }
        // Dynamic launch tiles share the existing mechanical card implementation
        // but have identities that can never alias the Add App module.
        L10n.language = .english
        let launchers = HUDNavigation(selected: .activityMonitor)
        let moduleLayers = Dictionary(uniqueKeysWithValues: launchers.entries.map { ($0.target, ObjectIdentifier($0.layer)) })
        let factoryEntry = launchers.entries.first { $0.module == .addApp }!
        let originalScrollRange = launchers.maxRightScrollOffset
        let presentations = (0..<12).map { index in
            HUDAppShortcutPresentation(id: UUID(), name: "My App \(index)",
                                       iconPreset: AppShortcutIcon.allCases[index + 1], icon: nil)
        }
        var layoutNotifications = 0
        launchers.onLayoutChange = { layoutNotifications += 1 }
        launchers.updateAppShortcuts(presentations, animated: false)
        let rightLaunchers = launchers.entries.filter { $0.group == .right }
        check(launchers.entries.count == 28 && rightLaunchers.count == 20,
              "Saved application shortcuts add actual right-side navigation entries")
        check(rightLaunchers.suffix(13).map(\.target) == presentations.map { .appShortcut($0.id) } + [.module(.addApp)],
              "Saved applications retain insertion order directly before the final Add App tile")
        check(Set(launchers.entries.map(\.target)).count == launchers.entries.count
              && launchers.entries.filter { $0.module == .addApp }.count == 1,
              "Typed launch targets cannot collide with one another or alias Add App")
        check(launchers.maxRightScrollOffset > originalScrollRange && launchers.selectedModule == .activityMonitor,
              "Adding apps extends native scrolling without changing the selected central module")
        check(launchers.entries.filter { $0.module != nil }.allSatisfy { moduleLayers[$0.target] == ObjectIdentifier($0.layer) },
              "All sixteen module layers survive app insertion without shell reconstruction")
        check(layoutNotifications > 0, "Manifest changes notify native accessibility and hit geometry")
        func namedLayer(_ name: String, within layer: CALayer) -> CALayer? {
            if layer.name == name { return layer }
            for child in layer.sublayers ?? [] { if let found = namedLayer(name, within: child) { return found } }
            return nil
        }
        let mapEntry = navigation.entries.first { $0.module == .map }!
        let mapRaster = namedLayer("navigation.appIcon", within: mapEntry.layer)!
        let aboutEntry = navigation.entries.first { $0.module == .about }!
        let aboutRaster = namedLayer("navigation.appIcon", within: aboutEntry.layer)!
        let aboutVector = namedLayer("navigation.icon", within: aboutEntry.layer) as! CAShapeLayer
        check(!mapRaster.isHidden && mapRaster.contents != nil,
              "Map retains bundled square artwork independently of shortcut presets")
        check(aboutRaster.isHidden && !aboutVector.isHidden && aboutVector.path != nil,
              "About restores its original authored information symbol")
        let firstApp = launchers.entries.first { $0.target == .appShortcut(presentations[0].id) }!
        let firstFace = ObjectIdentifier(firstApp.faceLayer)
        let appContent = namedLayer("navigation.contents", within: firstApp.layer)!
        let appBacking = namedLayer("navigation.backingPlate", within: firstApp.layer)!
        let appBorder = namedLayer("navigation.outerBorder", within: firstApp.layer) as! CAShapeLayer
        let appGlyph = namedLayer("navigation.icon", within: firstApp.layer) as! CAShapeLayer
        let appImage = namedLayer("navigation.appIcon", within: firstApp.layer)!
        let appLabel = appContent.sublayers!.compactMap { $0 as? CATextLayer }.first!
        check(firstApp.module == nil && firstApp.group == .right && firstApp.navigationTitle == "My App 0",
              "App entries expose their custom name and an explicit non-module target")
        check(near(firstApp.rect.width, 78) && near(firstApp.rect.height, 74)
              && near(appBacking.position.x - firstApp.layer.bounds.midX, -8)
              && appBorder.path!.boundingBoxOfPath.minX < 0,
              "App tiles reuse compact right-card geometry, leftward backing and detached outline")
        check(AppShortcutArtwork.gameIcon(for: presentations[0].iconPreset) == .power
              && appGlyph.isHidden && !appImage.isHidden && appImage.contents != nil
              && appLabel.string as? String == "My App 0",
              "A saved matching preset renders its bundled game artwork and custom label directly on the navigation tile")
        let appCenter = CGPoint(x: firstApp.rect.midX, y: min(firstApp.rect.midY, launchers.rightViewport.maxY - 3))
        check(launchers.hitTarget(point: appCenter) == firstApp.target && launchers.hitTest(point: appCenter) == nil,
              "Exact input returns the app UUID and never selects the Add App configuration module")
        launchers.hoverTarget(firstApp.target)
        check(near(firstApp.faceLayer.transform.m42, -3) && near(appBacking.transform.m42, -0.8)
              && near(appContent.transform.m42, -3.8) && near(firstApp.faceLayer.transform.m11, 1),
              "App hover preserves the shared layered lift: backing least, glyphs most, without scaling")
        check(launchers.hoverHitTarget(point: appCenter) == firstApp.target && launchers.selectedModule == .activityMonitor,
              "App hover owns the typed target without changing the center content")
        check(namedLayer("navigation.iconRing", within: firstApp.layer)?.opacity == 0.55,
              "App tiles retain the yellow hover ring without blinking")
        launchers.pressShortcut(id: presentations[0].id, animated: false)
        check(firstApp.isSelected && near(firstApp.faceLayer.transform.m11, 1.045)
              && launchers.selectedModule == .activityMonitor,
              "App clicks can engage selected-depth feedback while preserving the active central module")
        launchers.select(.activityMonitor, animated: false)
        check(!firstApp.isSelected, "Reselecting the current module clears temporary app press feedback")
        launchers.cancelAnimations()
        check(animationCount(launchers.layer) == 0 && near(firstApp.faceLayer.transform.m42, 0),
              "Closing removes dynamic app hover and finite motion as well as ordinary navigation motion")

        let image = NSImage(size: CGSize(width: 32, height: 32), flipped: false) { rect in
            NSColor.systemBlue.setFill(); NSBezierPath(rect: rect).fill(); return true
        }
        var edited = presentations
        edited[0] = HUDAppShortcutPresentation(id: presentations[0].id, name: "Renamed 应用", iconPreset: .original, icon: image)
        let appLayerID = ObjectIdentifier(firstApp.layer)
        launchers.updateAppShortcuts(edited, animated: false)
        check(launchers.entries.first { $0.target == firstApp.target } === firstApp
              && ObjectIdentifier(firstApp.layer) == appLayerID && ObjectIdentifier(firstApp.faceLayer) == firstFace,
              "Editing a shortcut keeps the exact existing card and face layers")
        check(firstApp.navigationTitle == "Renamed 应用" && appLabel.string as? String == "Renamed 应用"
              && !appImage.isHidden && appImage.contents != nil && appGlyph.isHidden,
              "Rename and original-icon choices immediately update the existing navigation tile")
        launchers.update(dark: false, accent: .systemPink, contentsScale: 3)
        check(appImage.contentsScale >= 3 && appLabel.contentsScale >= 3 && firstApp.navigationTitle == "Renamed 应用",
              "Dynamic app images and bold labels receive the same display-scale and theme updates")
        edited[0] = HUDAppShortcutPresentation(id: presentations[0].id, name: "Renamed 应用", iconPreset: .camera, icon: image)
        launchers.updateAppShortcuts(edited, animated: false)
        check(appImage.isHidden && !appGlyph.isHidden && appGlyph.path != nil && appGlyph.strokeColor != nil && appGlyph.fillColor == nil,
              "Switching back to a preset immediately restores the shared vector artwork")
        let allIDs = Dictionary(uniqueKeysWithValues: launchers.entries.map { ($0.target, ObjectIdentifier($0.layer)) })
        launchers.updateAppShortcuts(Array(edited.reversed()), animated: true)
        check(launchers.entries.allSatisfy { allIDs[$0.target] == ObjectIdentifier($0.layer) },
              "Reordering app references reflows existing layers instead of recreating them")
        check(launchers.entries.filter { $0.group == .right }.last === factoryEntry,
              "The original Add App card remains last after edits and reordering")
        if !NSWorkspace.shared.accessibilityDisplayShouldReduceMotion {
            check(launchers.entries.contains { $0.layer.animation(forKey: "navigation.position") is CABasicAnimation },
                  "Dynamic navigation reflow uses the existing finite mechanical position transition")
        }
        launchers.cancelAnimations()
        launchers.select(.addApp, animated: false)
        check(launchers.rightViewport.contains(factoryEntry.projectedRect)
              && launchers.hitTarget(point: CGPoint(x: factoryEntry.rect.midX, y: factoryEntry.rect.midY)) == .module(.addApp),
              "The final Add App tile stays reachable after many saved applications extend scrolling")
        let visibleApps = launchers.visibleEntries.filter { $0.module == nil }
        check(!visibleApps.isEmpty, "The lower viewport exposes saved apps alongside the final Add App tile")
        for entry in visibleApps {
            if let clipped = launchers.clippedRect(for: entry) {
                check(launchers.nativeHitRect(for: entry).insetBy(dx: -0.001, dy: -0.001).contains(clipped),
                      "Native hit envelopes cover the complete visible portion of each dynamic app tile")
            }
        }
        launchers.hoverTarget(firstApp.target)
        launchers.updateAppShortcuts([], animated: false)
        check(launchers.entries.count == 16 && firstApp.layer.superlayer == nil
              && launchers.maxRightScrollOffset == originalScrollRange,
              "Removing all shortcuts detaches their layers and restores the original scroll range")
        check(launchers.rightScrollOffset <= launchers.maxRightScrollOffset && !launchers.canScrollDown,
              "Removal clamps a previously deep scroll offset without leaving an empty viewport")
        check(launchers.entries.allSatisfy { moduleLayers[$0.target] == ObjectIdentifier($0.layer) }
              && animationCount(firstApp.layer) == 0,
              "Removal preserves module identities and cancels detached app animations")
        launchers.updateAppShortcuts([presentations[0], presentations[0]], animated: false)
        check(launchers.entries.count == 17 && launchers.entries.filter { $0.module == nil }.count == 1,
              "A repeated presentation UUID cannot install duplicate buttons or accessibility actions")
        launchers.updateAppShortcuts(presentations, animated: true)
        check(launchers.selectedModule == .addApp && launchers.rightViewport.contains(factoryEntry.projectedRect)
              && launchers.visibleEntries.contains { $0.target == .appShortcut(presentations.last!.id) },
              "Saving while Add App is selected reveals the new app and final Add App tile in the same reflow")
        launchers.cancelAnimations()

        let themed = HUDNavigation()
        let themedSectors = themed.entries.filter { $0.group == .bottom }
        let initialSectorOutlines = themedSectors.map { ($0.faceLayer.sublayers?.first { $0.name == "navigation.plate" } as! CAShapeLayer).path! }
        let blueAccent = NSColor(srgbRed: 0.12, green: 0.42, blue: 0.96, alpha: 1)
        for dark in [true, false] {
            themed.update(dark: dark, accent: blueAccent, contentsScale: 2)
            for state in 0..<3 {
                themed.select(state == 0 ? .power : .activityMonitor, animated: false)
                themed.hover(state == 2 ? .activityMonitor : nil)
                for (index, entry) in themedSectors.enumerated() {
                    for name in ["navigation.plate", "navigation.innerBorder", "navigation.bottomStripe"] {
                        let shape = entry.faceLayer.sublayers!.first { $0.name == name } as! CAShapeLayer
                        let color = name == "navigation.innerBorder" ? shape.strokeColor : shape.fillColor
                        let rgb = NSColor(cgColor: color!)!.usingColorSpace(.sRGB)!
                        check(rgb.blueComponent > rgb.greenComponent && rgb.greenComponent > rgb.redComponent,
                              "Bottom face, border and lower band keep the chosen hue in idle, selected and hovered states")
                    }
                    let face = entry.faceLayer.sublayers!.first { $0.name == "navigation.plate" } as! CAShapeLayer
                    check(face.path == initialSectorOutlines[index] && CATransform3DIsIdentity(entry.faceLayer.transform),
                          "Recoloring preserves exact center-sector shape and fixed hover geometry")
                }
            }
            themed.select(.notes, animated: false)
            let side = themed.entries.first { $0.module == .notes }!
            func descendants(_ layer: CALayer) -> [CALayer] { [layer] + (layer.sublayers ?? []).flatMap(descendants) }
            let marker = descendants(side.layer).first { $0.name == "navigation.iconRing" } as! CAShapeLayer
            check(marker.strokeColor == blueAccent.cgColor,
                  "The selected side-button circle follows the theme while the silver face remains neutral")
        }
        themed.cancelAnimations()
        return count
    }
}
