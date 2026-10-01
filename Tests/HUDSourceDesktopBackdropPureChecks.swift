// Defines only pure checks. The caller may invoke runPureChecks() during typecheck/test CI.
// This file never invokes screen discovery, capture, or either permission function.
import CoreGraphics

enum HUDSourceDesktopBackdropPureChecks {
    enum Failure: Error {
        case unexpectedResult(String)
        case expectedCaptureError(String)
    }

    private static func require(_ condition: Bool, _ message: String) throws {
        guard condition else { throw Failure.unexpectedResult(message) }
    }

    private static func expectError(_ name: String, matching: (HUDSourceDesktopBackdrop.CaptureError) -> Bool,
                                    body: () throws -> Void) throws {
        do { try body() }
        catch let error as HUDSourceDesktopBackdrop.CaptureError {
            try require(matching(error), name)
            return
        }
        throw Failure.expectedCaptureError(name)
    }

    static func runPureChecks() throws {
        typealias Provider = HUDSourceDesktopBackdrop
        typealias Display = Provider.DisplayGeometry
        let primary = Display(displayID: 1, appKitGlobalFrame: CGRect(x: 0, y: 0, width: 1440, height: 900),
                              cgGlobalBounds: CGRect(x: 0, y: 0, width: 1440, height: 900))
        let left = Display(displayID: 2, appKitGlobalFrame: CGRect(x: -1920, y: 0, width: 1920, height: 1080),
                           cgGlobalBounds: CGRect(x: -1920, y: -180, width: 1920, height: 1080))
        let above = Display(displayID: 3, appKitGlobalFrame: CGRect(x: 100, y: 900, width: 1280, height: 720),
                            cgGlobalBounds: CGRect(x: 100, y: -720, width: 1280, height: 720))
        let below = Display(displayID: 4, appKitGlobalFrame: CGRect(x: 300, y: -800, width: 1280, height: 800),
                            cgGlobalBounds: CGRect(x: 300, y: 900, width: 1280, height: 800))
        let cases: [(Display, CGRect, CGRect)] = [
            (primary, CGRect(x: 100, y: 200, width: 300, height: 240), CGRect(x: 100, y: 460, width: 300, height: 240)),
            (left, CGRect(x: -1500, y: 700, width: 320, height: 240), CGRect(x: -1500, y: -40, width: 320, height: 240)),
            (above, CGRect(x: 150, y: 1100, width: 300, height: 200), CGRect(x: 150, y: -400, width: 300, height: 200)),
            (below, CGRect(x: 350, y: -750, width: 300, height: 200), CGRect(x: 350, y: 1450, width: 300, height: 200))
        ]
        for (display, appKit, cg) in cases {
            try require(try display.cgRectangle(for: appKit) == cg, "AppKit to CG conversion on display \(display.displayID)")
            try require(try display.appKitRectangle(for: cg) == appKit, "Inverse conversion on display \(display.displayID)")
        }
        let split = try Provider.plan(appKitGlobalRect: CGRect(x: -200, y: 600, width: 500, height: 400),
                                      displays: [primary, left])
        try require(split.count == 2 && split[0].requestedCGGlobalRect == CGRect(x: 0, y: 0, width: 300, height: 300)
                    && split[1].requestedCGGlobalRect == CGRect(x: -200, y: -100, width: 200, height: 400),
                    "Mixed-scale/negative-origin tiles retain separate logical extents")

        let retina = try Provider.pixelRegion(displayLocalRect: CGRect(x: 100, y: 460, width: 300, height: 240),
                                              displaySize: primary.cgGlobalBounds.size, pointPixelScale: 2, displayID: 1)
        try require(retina.sourceRect == CGRect(x: 100, y: 460, width: 300, height: 240)
                    && retina.pixelWidth == 600 && retina.pixelHeight == 480,
                    "Retina factor applies once to the output raster, never to global bounds")
        let fractional = try Provider.pixelRegion(displayLocalRect: CGRect(x: 0.25, y: 10.1, width: 100.1, height: 20.3),
                                                  displaySize: primary.cgGlobalBounds.size, pointPixelScale: 2, displayID: 1)
        try require(fractional.sourceRect == CGRect(x: 0, y: 10, width: 100.5, height: 20.5)
                    && fractional.pixelWidth == 201 && fractional.pixelHeight == 41,
                    "Fractional crop expands to native pixels instead of resampling")

        let prefix = try Provider.exclusionPrefix(orderedWindowIDs: [11, 12, 13, 14], hudWindowID: 13)
        try require(prefix == [11, 12, 13], "Exclusion includes the HUD and every window above it")
        try Provider.validateWindowMappings(excludedWindowIDs: prefix, shareableWindowIDs: [14, 12, 11, 13])
        try expectError("An unmapped system window cannot be silently ignored", matching: {
            if case .missingWindowMappings(let ids) = $0 { return ids == [12] }; return false
        }) { try Provider.validateWindowMappings(excludedWindowIDs: prefix, shareableWindowIDs: [11, 13, 14]) }
        try expectError("A missing HUD has no below-window ordering", matching: {
            if case .hudWindowNotOnScreen = $0 { return true }; return false
        }) { _ = try Provider.exclusionPrefix(orderedWindowIDs: [11, 12], hudWindowID: 13) }
        try expectError("Duplicate shareable mappings are ambiguous", matching: {
            if case .duplicateWindowID(let id) = $0 { return id == 12 }; return false
        }) { try Provider.validateWindowMappings(excludedWindowIDs: prefix, shareableWindowIDs: [11, 12, 12, 13]) }
        try expectError("Duplicate ordered IDs are rejected before constructing the filter", matching: {
            if case .duplicateWindowID(let id) = $0 { return id == 11 }; return false
        }) { _ = try Provider.exclusionPrefix(orderedWindowIDs: [11, 11, 13], hudWindowID: 13) }
        try expectError("NaN pixel scale cannot become output dimensions", matching: {
            if case .invalidNativePixelScale(let id) = $0 { return id == 1 }; return false
        }) { _ = try Provider.pixelRegion(displayLocalRect: CGRect(x: 0, y: 0, width: 10, height: 10),
                                          displaySize: CGSize(width: 1440, height: 900), pointPixelScale: .nan, displayID: 1) }
        try expectError("A rectangle entirely in a display gap has no invented fill tile", matching: {
            if case .noDisplayIntersection = $0 { return true }; return false
        }) { _ = try Provider.plan(appKitGlobalRect: CGRect(x: 1450, y: 0, width: 20, height: 20), displays: [primary, left]) }
    }
}
