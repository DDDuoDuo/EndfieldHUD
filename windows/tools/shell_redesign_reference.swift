import AppKit
import QuartzCore

// Oracle for the Windows port of the Mac 1.3.0 shell surfaces (B2b): the five
// compact clock styles and their status row (HUDClockStyleArtwork), the
// header sanity bar's Work Mode link and recovery menu layout
// (HUDAccountGauge) and the side buttons' optical icon sizing
// (HUDSourceDesktopIconLayout). The unchanged Mac classes run detached: no
// SystemHUDView, window, timer, account, clock or user data. Text widths come
// from the Mac's own measurement so the portable Windows geometry can be
// compared exactly; Windows measures with DirectWrite at runtime.
@main enum ShellRedesignReference {
    static func child<T>(_ value: Any, _ label: String) -> T { Mirror(reflecting: value).children.first { $0.label == label }!.value as! T }
    static func rect(_ r: CGRect) -> [Double] { [r.minX, r.minY, r.width, r.height].map(Double.init) }
    static func color(_ c: CGColor?) -> Any {
        guard let c, let s = c.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil),
              let k = s.components else { return NSNull() }
        return k.map(Double.init)
    }
    static func path(_ p: CGPath?) -> Any {
        guard let p else { return NSNull() }
        var out: [[Any]] = []
        p.applyWithBlock { element in
            let e = element.pointee, pts = e.points
            func pt(_ i: Int) -> [Double] { [Double(pts[i].x), Double(pts[i].y)] }
            switch e.type {
            case .moveToPoint: out.append(["move", pt(0)])
            case .addLineToPoint: out.append(["line", pt(0)])
            case .addQuadCurveToPoint: out.append(["quad", pt(0), pt(1)])
            case .addCurveToPoint: out.append(["cubic", pt(0), pt(1), pt(2)])
            case .closeSubpath: out.append(["close"])
            @unknown default: out.append(["unknown"])
            }
        }
        return out
    }
    static func text(_ t: HUDStandardTextLayer) -> [String: Any] {
        ["frame": rect(t.frame), "fontSize": Double(t.fontSize), "string": t.string as? String ?? "",
         "alignment": t.alignmentMode.rawValue, "hidden": t.isHidden, "color": color(t.foregroundColor),
         "weight": t.weight.rawValue, "tabular": t.tabular, "truncation": t.truncationMode.rawValue]
    }
    static func shape(_ s: CAShapeLayer) -> [String: Any] {
        ["path": path(s.path), "fill": color(s.fillColor), "stroke": color(s.strokeColor),
         "lineWidth": Double(s.lineWidth), "lineCap": s.lineCap.rawValue, "hidden": s.isHidden]
    }
    static func width(_ string: String, _ size: CGFloat, _ weight: HUDFontWeight, _ tabular: Bool) -> Double {
        let layer = HUDStandardTextLayer(size: size, weight: weight, tabular: tabular); layer.string = string
        return Double(HUDClockStyleArtwork.width(of: layer))
    }

    // MARK: Clock
    static func clock() -> [String: Any] {
        let accent = NSColor(srgbRed: 250 / 255, green: 212 / 255, blue: 31 / 255, alpha: 1)
        let artwork = HUDClockStyleArtwork()
        // SystemHUDView.text(...): the host's clock and badge layers.
        let time = HUDStandardTextLayer(size: 20, weight: .semibold, tabular: true)
        time.frame = CGRect(x: 340, y: 0, width: 220, height: 27); time.alignmentMode = .right; time.truncationMode = .end; time.isWrapped = false
        let date = HUDStandardTextLayer(size: 9, weight: .regular, tabular: true)
        date.frame = CGRect(x: 340, y: 31, width: 220, height: 18); date.alignmentMode = .right; date.truncationMode = .end; date.isWrapped = false
        let work = HUDStandardTextLayer(size: 10, weight: .semibold)
        work.frame = CGRect(x: 320, y: 55, width: 240, height: 18); work.alignmentMode = .right; work.truncationMode = .end; work.isWrapped = false
        let readings: [HUDClockReading?] = [nil, HUDClockReading(time: "12:34:56", date: "MON Oct 7"),
            HUDClockReading(time: "09:05:07 AM", date: "SUN Oct 6"), HUDClockReading(time: "11:59:58 PM", date: "SAT Dec 28"),
            HUDClockReading(time: "00:00:00", date: "WED Jan 1"), HUDClockReading(time: "23:07:31", date: "THU Feb 29")]
        let workStates: [(String, String, Bool)] = [("idle", "WORK MODE", false), ("running", "WORK MODE / ACTIVE", true), ("paused", "WORK MODE / PAUSED", true)]
        let instrument: CAShapeLayer = child(artwork, "instrument"), fill: CAShapeLayer = child(artwork, "fill")
        let track: CAShapeLayer = child(artwork, "track"), hands: CAShapeLayer = child(artwork, "hands")
        let secondHand: CAShapeLayer = child(artwork, "secondHand"), seconds: HUDStandardTextLayer = child(artwork, "seconds")
        let aux: HUDStandardTextLayer = child(artwork, "aux"), rail: CAShapeLayer = child(artwork, "rail")
        let recordDot: CALayer = child(artwork, "recordDot"), recordLabel: HUDStandardTextLayer = child(artwork, "recordLabel")
        var states: [[String: Any]] = [], strings = Set<String>(), dates = Set<String>()
        var index = 0
        for (sample, reading) in readings.enumerated() { for style in HUDClockStyle.allCases {
            let (phase, label, active) = workStates[index % 3]
            let recording = index % 2 == 1; index += 1
            work.string = label
            // updateStatusPanel: the status row first, then the style.
            artwork.updateStatus(work: work, workActive: active, recording: recording, accent: accent)
            artwork.update(style: style, reading: reading, time: time, date: date, accent: accent)
            if let value = reading?.time {
                let words = value.split(separator: " ", maxSplits: 1).map(String.init)
                strings.insert(words[0]); strings.insert(words[0].split(separator: ":").prefix(2).joined(separator: ":"))
            } else { strings.insert("--:--:--"); strings.insert("--:--") }
            dates.insert(reading?.date ?? "")
            states.append(["style": style.rawValue, "sample": sample, "reading": reading.map { ["time": $0.time, "date": $0.date] } ?? NSNull() as Any,
                "workPhase": phase, "workActive": active, "recording": recording,
                "time": text(time), "date": text(date), "seconds": text(seconds), "aux": text(aux), "work": text(work),
                "instrument": shape(instrument), "fill": shape(fill), "track": shape(track), "hands": shape(hands), "secondHand": shape(secondHand),
                "selection": shape(artwork.selection), "rail": shape(rail), "recordLabel": text(recordLabel),
                "recordDot": ["frame": rect(recordDot.frame), "color": color(recordDot.backgroundColor), "opacity": Double(recordDot.opacity)]])
        }}
        // Every width the artwork can ask for: the time strings at each fit
        // size (bold tabular), the dates (medium or bold, tabular) and the label.
        var measures: [[Any]] = []
        for value in strings.sorted() { for size in [50.0, 62, 38, 40, 44] { measures.append([value, size, 700, true, width(value, CGFloat(size), .bold, true)]) } }
        for value in dates.sorted() where !value.isEmpty {
            for (size, weight) in [(13.0, HUDFontWeight.medium), (12, .medium), (13, .bold)] {
                measures.append([value, size, weight.rawValue, true, width(value, CGFloat(size), weight, true)])
            }
        }
        measures.append(["RECORDING MODE", 12.0, HUDFontWeight.semibold.rawValue, false, width("RECORDING MODE", 12, .semibold, false)])
        let probe = HUDFontResolver.nsFont(size: 100, weight: .bold, tabular: true)
        return ["states": states, "measures": measures,
                "font": ["ascender": Double(probe.ascender) / 100, "descender": Double(probe.descender) / 100,
                         "leading": Double(probe.leading) / 100, "capHeight": Double(probe.capHeight) / 100, "family": probe.familyName ?? ""]]
    }

    // MARK: Sanity bar
    static func gauge() -> [String: Any] {
        let now = Date(timeIntervalSinceReferenceDate: 781_000_000)
        var rows: [[String: Any]] = []
        let cases: [(String, Double?, Double?)] = [("countdown", 312, 85267), ("none", nil, nil), ("long", 59, 360_005), ("short", 4, 61)]
        for language in [AppLanguage.english, .simplifiedChinese, .traditionalChinese, .japanese, .korean] {
            L10n.language = language
            for (name, next, full) in cases {
                let gauge = HUDAccountGauge()
                let sanity = HypergryphSanityPresentation(game: .endfield, current: 42, maximum: 360, observedAt: now,
                    nextRecoveryAt: next.map { now.addingTimeInterval($0) }, fullRecoveryAt: full.map { now.addingTimeInterval($0) },
                    isRefreshing: false, refreshAvailable: true)
                gauge.update(value: "42 / 360", accessibilityLabel: "Sanity 42 of 360", visible: true, accent: .systemYellow, scale: 2, sanity: sanity, at: now)
                gauge.perform("toggle")
                let popover: CALayer = child(gauge, "popover")
                let cells: [CALayer] = ["nextLabel", "fullLabel", "nextValue", "fullValue"].map { child(gauge, $0) }
                let style = HUDTextStyle(size: 14.4, weight: .medium, color: NSColor(white: 0.92, alpha: 1).cgColor)
                let labels = [L10n.text("Next recovery", "下次回复"), L10n.text("Full recovery", "全部回复")]
                let colon = [AppLanguage.simplifiedChinese, .traditionalChinese, .japanese].contains(L10n.resolvedLanguage) ? "：" : ":"
                var standard: [String: Double] = [:]
                for value in labels.map({ $0 + colon }) + [gauge.nextRecoveryText, gauge.fullRecoveryText, "000:00:00"] {
                    standard[value] = Double(HUDText.width(value, style))
                }
                rows.append(["language": language.rawValue, "case": name, "next": next.map { $0 as Any } ?? NSNull(), "full": full.map { $0 as Any } ?? NSNull(),
                    "popover": rect(popover.frame), "cells": cells.map { rect($0.frame) },
                    "texts": [labels[0] + colon, labels[1] + colon, gauge.nextRecoveryText, gauge.fullRecoveryText], "standardWidths": standard,
                    "actions": gauge.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": rect($0.rect), "enabled": $0.enabled] }])
            }
        }
        L10n.language = .english
        // The Work Mode link: no sanity data, the bar shown.
        let gauge = HUDAccountGauge(); var opened = 0; gauge.onOpenWorkMode = { opened += 1 }
        gauge.update(value: "12", accessibilityLabel: "Work Mode 12 minutes", visible: true, accent: .systemYellow, scale: 2, sanity: nil, at: now)
        var link: [String: Any] = ["opensWorkMode": gauge.opensWorkMode, "canOpen": gauge.canOpen, "isInteractive": gauge.isInteractive,
            "actions": gauge.accessibleActions.map { ["id": $0.id, "label": $0.label, "rect": rect($0.rect), "enabled": $0.enabled] }]
        let inside = gauge.mouseDown(at: CGPoint(x: 90, y: 20)); let afterInside = opened
        let outside = gauge.mouseDown(at: CGPoint(x: 90, y: 50)); gauge.perform("refresh"); let afterRefresh = opened
        gauge.perform("toggle")
        link["pressInside"] = ["handled": inside, "opened": afterInside, "popover": gauge.isPopoverOpen]
        link["pressOutside"] = ["handled": outside, "opened": afterRefresh]
        link["toggleOpened"] = opened
        gauge.update(value: "12", accessibilityLabel: "", visible: false, accent: .systemYellow, scale: 2, sanity: nil, at: now)
        link["hidden"] = ["opensWorkMode": gauge.opensWorkMode, "isInteractive": gauge.isInteractive, "handled": gauge.mouseDown(at: CGPoint(x: 90, y: 20))]
        return ["menus": rows, "link": link]
    }

    // MARK: Side button icons
    static func icons() -> [String: Any] {
        let sizes: [CGSize] = [CGSize(width: 96, height: 96), CGSize(width: 96, height: 40), CGSize(width: 30, height: 96),
                               CGSize(width: 81, height: 77), CGSize(width: 1, height: 1), CGSize(width: 0, height: 10), CGSize(width: 12, height: 0)]
        return ["box": Double(HUDSourceDesktopIconLayout.box),
                "glyphs": sizes.map { s -> [String: Any] in
                    ["size": [Double(s.width), Double(s.height)], "glyph": rect(HUDSourceDesktopIconLayout.glyphFrame(s))] },
                "leftIconSide": HUDSourceWatchView.desktopLeftIconSide, "leftIconGap": HUDSourceWatchView.desktopLeftIconGap]
    }

    static func main() {
        guard CommandLine.arguments.count == 2, ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil else {
            fputs("usage: CFFIXED_USER_HOME=... shell-redesign-reference OUTPUT.json\n", stderr); exit(2)
        }
        NSApplication.shared.setActivationPolicy(.prohibited)
        var config = AppConfiguration.defaults
        config.language = .english; config.theme = .dark; config.ambientAnimation = false; config.reduceMotion = true
        HUDRuntimeAppearance.configuration = config; L10n.language = .english
        let result: [String: Any] = ["schemaVersion": 1, "clock": clock(), "gauge": gauge(), "icons": icons(),
            "safety": ["systemHUDViewConstructed": false, "windowCreated": false, "timers": 0, "userData": false]]
        do {
            let data = try JSONSerialization.data(withJSONObject: result, options: [.sortedKeys, .withoutEscapingSlashes])
            try data.write(to: URL(fileURLWithPath: CommandLine.arguments[1]), options: .atomic)
            print("Wrote shell redesign oracle: \(data.count) bytes")
        } catch { fputs("Shell redesign export failed: \(error)\n", stderr); exit(1) }
    }
}
