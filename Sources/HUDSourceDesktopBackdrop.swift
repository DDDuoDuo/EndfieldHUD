import AppKit
import CoreGraphics
import Foundation
#if canImport(ScreenCaptureKit)
import ScreenCaptureKit
#endif

/// Captures desktop pixels below an already visible HUD. Capture never requests permission.
/// The caller supplies a rectangle in AppKit global points; each display remains a separate tile.
public final class HUDSourceDesktopBackdrop {
    public enum PermissionStatus: Equatable {
        case granted
        case notGranted
        // Apple DTS documents Catalina installations lacking the otherwise 10.15-declared symbols.
        case safePreflightUnavailableOnCatalina
    }

    public enum CaptureError: Error, CustomStringConvertible {
        case unsupportedOlderSystem
        case frameworkUnavailable
        case permissionNotGranted
        case invalidRectangle
        case noDisplayIntersection
        case invalidDisplayGeometry(CGDirectDisplayID)
        case duplicateDisplayID(CGDirectDisplayID)
        case invalidHUDWindowID
        case hudWindowNotOnScreen
        case invalidWindowList
        case duplicateWindowID(CGWindowID)
        case missingWindowMappings([CGWindowID])
        case windowOrDisplayStateChanged
        case missingDisplayMapping(CGDirectDisplayID)
        case displayCoordinateMismatch(CGDirectDisplayID)
        case invalidNativePixelScale(CGDirectDisplayID)
        case unexpectedImageSize(CGDirectDisplayID, expectedWidth: Int, expectedHeight: Int,
                                 actualWidth: Int, actualHeight: Int)

        public var description: String {
            switch self {
            case .unsupportedOlderSystem: return "Desktop pixel capture requires macOS 14 or later."
            case .frameworkUnavailable: return "This build does not include ScreenCaptureKit."
            case .permissionNotGranted: return "Screen capture access has not been granted."
            case .invalidRectangle: return "The desktop capture rectangle must have finite, positive dimensions."
            case .noDisplayIntersection: return "The desktop capture rectangle does not intersect a display."
            case .invalidDisplayGeometry(let id): return "Invalid desktop geometry for display \(id)."
            case .duplicateDisplayID(let id): return "Display \(id) appears more than once."
            case .invalidHUDWindowID: return "The HUD does not have a valid WindowServer window number."
            case .hudWindowNotOnScreen: return "The HUD must be visible and present in the on-screen window list."
            case .invalidWindowList: return "The ordered WindowServer window list is unavailable or malformed."
            case .duplicateWindowID(let id): return "Window \(id) appears more than once in the capture inventory."
            case .missingWindowMappings(let ids): return "Cannot exclude all windows at or above the HUD: \(ids)."
            case .windowOrDisplayStateChanged: return "HUD window order, geometry, or display configuration changed during capture."
            case .missingDisplayMapping(let id): return "ScreenCaptureKit did not provide exactly one entry for display \(id)."
            case .displayCoordinateMismatch(let id): return "ScreenCaptureKit and CoreGraphics disagree about display \(id) coordinates."
            case .invalidNativePixelScale(let id): return "Display \(id) has no valid native pixel mapping."
            case let .unexpectedImageSize(id, width, height, actualWidth, actualHeight):
                return "Display \(id) returned \(actualWidth)×\(actualHeight) pixels; expected \(width)×\(height)."
            }
        }
    }

    public struct DisplayGeometry: Equatable {
        public let displayID: CGDirectDisplayID
        public let appKitGlobalFrame: CGRect
        public let cgGlobalBounds: CGRect

        public init(displayID: CGDirectDisplayID, appKitGlobalFrame: CGRect, cgGlobalBounds: CGRect) {
            self.displayID = displayID
            self.appKitGlobalFrame = appKitGlobalFrame
            self.cgGlobalBounds = cgGlobalBounds
        }

        /// Converts logical display coordinates, including negative display origins. Retina scale
        /// is applied only when planning raster pixels, never again to global screen coordinates.
        public func cgRectangle(for appKitRectangle: CGRect) throws -> CGRect {
            try validate()
            guard HUDSourceDesktopBackdrop.isValidRectangle(appKitRectangle) else {
                throw CaptureError.invalidRectangle
            }
            let sx = cgGlobalBounds.width / appKitGlobalFrame.width
            let sy = cgGlobalBounds.height / appKitGlobalFrame.height
            return CGRect(x: cgGlobalBounds.minX + (appKitRectangle.minX - appKitGlobalFrame.minX) * sx,
                          y: cgGlobalBounds.minY + (appKitGlobalFrame.maxY - appKitRectangle.maxY) * sy,
                          width: appKitRectangle.width * sx, height: appKitRectangle.height * sy)
        }

        public func appKitRectangle(for cgRectangle: CGRect) throws -> CGRect {
            try validate()
            guard HUDSourceDesktopBackdrop.isValidRectangle(cgRectangle) else {
                throw CaptureError.invalidRectangle
            }
            let sx = appKitGlobalFrame.width / cgGlobalBounds.width
            let sy = appKitGlobalFrame.height / cgGlobalBounds.height
            return CGRect(x: appKitGlobalFrame.minX + (cgRectangle.minX - cgGlobalBounds.minX) * sx,
                          y: appKitGlobalFrame.maxY - (cgRectangle.maxY - cgGlobalBounds.minY) * sy,
                          width: cgRectangle.width * sx, height: cgRectangle.height * sy)
        }

        fileprivate func validate() throws {
            guard displayID != 0, HUDSourceDesktopBackdrop.isValidRectangle(appKitGlobalFrame),
                  HUDSourceDesktopBackdrop.isValidRectangle(cgGlobalBounds) else {
                throw CaptureError.invalidDisplayGeometry(displayID)
            }
        }
    }

    public struct PlannedTile {
        public let display: DisplayGeometry
        public let requestedAppKitRect: CGRect
        public let requestedCGGlobalRect: CGRect
    }

    public struct PixelRegion {
        /// Pixel-aligned source crop in display-local, upper-left logical coordinates.
        public let sourceRect: CGRect
        public let pixelWidth: Int
        public let pixelHeight: Int
        public let pointPixelScale: CGFloat
    }

    public struct Tile {
        public let displayID: CGDirectDisplayID
        public let image: CGImage
        public var colorSpace: CGColorSpace? { image.colorSpace }
        public var nativePixelSize: CGSize { CGSize(width: CGFloat(image.width), height: CGFloat(image.height)) }
        public let requestedAppKitRect: CGRect
        public let requestedCGGlobalRect: CGRect
        /// Fractional requests include at most a native-pixel fringe; the image is never resized.
        public let capturedAppKitRect: CGRect
        public let capturedCGGlobalRect: CGRect
        public let sourceRect: CGRect
        public let pointPixelScale: CGFloat
    }

    public struct Frame {
        public let requestedRect: CGRect
        public let hudWindowID: CGWindowID
        public let excludedWindowIDs: [CGWindowID]
        public let tiles: [Tile]
        public let captureStartUptime: TimeInterval
        public let captureEndUptime: TimeInterval
    }

    public init() {}

    /// Read-only permission check. A false result does not distinguish denial from no prior request.
    public static func preflightPermission() -> PermissionStatus {
        if #available(macOS 11.0, *) {
            return CGPreflightScreenCaptureAccess() ? .granted : .notGranted
        }
        return .safePreflightUnavailableOnCatalina
    }

    /// Only the caller's explicit user action may invoke this. No capture path calls it.
    public static func requestPermissionFromUserAction() -> PermissionStatus {
        // AppKit/Carbon user actions arrive on the main thread. Keep that
        // invariant without requiring legacy NSObject callback callers to
        // adopt global Swift actor isolation.
        precondition(Thread.isMainThread, "Screen capture permission requires the main user-action thread")
        if #available(macOS 11.0, *) {
            return CGRequestScreenCaptureAccess() ? .granted : .notGranted
        }
        return .safePreflightUnavailableOnCatalina
    }

    /// Pure tile planning; no WindowServer, permissions, or screen capture APIs are called.
    public static func plan(appKitGlobalRect: CGRect, displays: [DisplayGeometry]) throws -> [PlannedTile] {
        guard Self.isValidRectangle(appKitGlobalRect) else { throw CaptureError.invalidRectangle }
        var ids = Set<CGDirectDisplayID>()
        var tiles: [PlannedTile] = []
        for display in displays {
            try display.validate()
            guard ids.insert(display.displayID).inserted else { throw CaptureError.duplicateDisplayID(display.displayID) }
            let part = appKitGlobalRect.intersection(display.appKitGlobalFrame)
            if !part.isNull && part.width > 0 && part.height > 0 {
                tiles.append(PlannedTile(display: display, requestedAppKitRect: part,
                                         requestedCGGlobalRect: try display.cgRectangle(for: part)))
            }
        }
        guard !tiles.isEmpty else { throw CaptureError.noDisplayIntersection }
        return tiles
    }

    /// Pure native-pixel crop planning. Output dimensions come from the filter's pixel mapping,
    /// rather than ScreenCaptureKit's default 1920×1080 output or NSScreen backing scale guesses.
    public static func pixelRegion(displayLocalRect: CGRect, displaySize: CGSize,
                                   pointPixelScale: CGFloat, displayID: CGDirectDisplayID) throws -> PixelRegion {
        let displayRect = CGRect(origin: .zero, size: displaySize)
        guard isValidRectangle(displayRect), isValidRectangle(displayLocalRect),
              displayRect.contains(displayLocalRect), pointPixelScale.isFinite, pointPixelScale > 0 else {
            throw CaptureError.invalidNativePixelScale(displayID)
        }
        let fullWidth = displaySize.width * pointPixelScale
        let fullHeight = displaySize.height * pointPixelScale
        // A physical display extent must map to integral pixels. A mismatch is rejected, not fitted.
        guard fullWidth.isFinite, fullHeight.isFinite,
              abs(fullWidth - fullWidth.rounded()) <= 0.00001,
              abs(fullHeight - fullHeight.rounded()) <= 0.00001,
              let displayPixelWidth = Int(exactly: fullWidth.rounded()), displayPixelWidth > 0,
              let displayPixelHeight = Int(exactly: fullHeight.rounded()), displayPixelHeight > 0 else {
            throw CaptureError.invalidNativePixelScale(displayID)
        }
        let minX = (displayLocalRect.minX * pointPixelScale).rounded(.down)
        let minY = (displayLocalRect.minY * pointPixelScale).rounded(.down)
        let maxX = (displayLocalRect.maxX * pointPixelScale).rounded(.up)
        let maxY = (displayLocalRect.maxY * pointPixelScale).rounded(.up)
        guard minX >= 0, minY >= 0, maxX <= CGFloat(displayPixelWidth), maxY <= CGFloat(displayPixelHeight),
              let width = Int(exactly: maxX - minX), width > 0,
              let height = Int(exactly: maxY - minY), height > 0 else {
            throw CaptureError.invalidNativePixelScale(displayID)
        }
        return PixelRegion(sourceRect: CGRect(x: minX / pointPixelScale, y: minY / pointPixelScale,
                                              width: CGFloat(width) / pointPixelScale,
                                              height: CGFloat(height) / pointPixelScale),
                           pixelWidth: width, pixelHeight: height, pointPixelScale: pointPixelScale)
    }

    /// Pure exclusion planning. Every on-screen window at or above the HUD must be excluded,
    /// including desktop/system windows; unknown windows are never assumed to have no pixels.
    public static func exclusionPrefix(orderedWindowIDs: [CGWindowID], hudWindowID: CGWindowID) throws -> [CGWindowID] {
        guard hudWindowID != 0 else { throw CaptureError.invalidHUDWindowID }
        var seen = Set<CGWindowID>()
        for id in orderedWindowIDs {
            guard id != 0 else { throw CaptureError.invalidWindowList }
            guard seen.insert(id).inserted else { throw CaptureError.duplicateWindowID(id) }
        }
        guard let index = orderedWindowIDs.firstIndex(of: hudWindowID) else { throw CaptureError.hudWindowNotOnScreen }
        return Array(orderedWindowIDs[...index])
    }

    /// Pure mapping validation. An incomplete ScreenCaptureKit inventory is a capture error.
    public static func validateWindowMappings(excludedWindowIDs: [CGWindowID],
                                              shareableWindowIDs: [CGWindowID]) throws {
        var seen = Set<CGWindowID>()
        for id in shareableWindowIDs {
            guard id != 0 else { throw CaptureError.invalidWindowList }
            guard seen.insert(id).inserted else { throw CaptureError.duplicateWindowID(id) }
        }
        let missing = excludedWindowIDs.filter { !seen.contains($0) }
        guard missing.isEmpty else { throw CaptureError.missingWindowMappings(missing) }
    }

    /// Capture requires the HUD already to be in WindowServer's on-screen list. This prevents a
    /// caller from claiming a below-HUD capture before the window has an observable z-order.
    /// Public capture APIs do not offer an atomic z-order snapshot: stable pre/post checks reject
    /// observed changes, but cannot rule out a transient reorder between those checks.
    @MainActor
    public func captureBelowHUD(window: NSWindow, appKitGlobalRect: CGRect) async throws -> Frame {
        guard Self.isValidRectangle(appKitGlobalRect) else { throw CaptureError.invalidRectangle }
        guard #available(macOS 14.0, *) else { throw CaptureError.unsupportedOlderSystem }
        guard Self.preflightPermission() == .granted else { throw CaptureError.permissionNotGranted }
        #if canImport(ScreenCaptureKit)
        return try await captureModern(window: window, appKitGlobalRect: appKitGlobalRect)
        #else
        throw CaptureError.frameworkUnavailable
        #endif
    }

    private static func isValidRectangle(_ rect: CGRect) -> Bool {
        !rect.isNull && !rect.isInfinite && rect.origin.x.isFinite && rect.origin.y.isFinite &&
            rect.width.isFinite && rect.height.isFinite && rect.width > 0 && rect.height > 0 &&
            rect.maxX.isFinite && rect.maxY.isFinite
    }

    private struct WindowSnapshot: Equatable {
        let hudWindowID: CGWindowID
        let hudAppKitFrame: CGRect
        let hudCGFrame: CGRect
        let excludedWindowIDs: [CGWindowID]
    }

    @MainActor
    private static func windowSnapshot(_ window: NSWindow) throws -> WindowSnapshot {
        guard window.isVisible && !window.isMiniaturized else { throw CaptureError.hudWindowNotOnScreen }
        guard let id = CGWindowID(exactly: window.windowNumber), id != 0 else { throw CaptureError.invalidHUDWindowID }
        guard let rows = CGWindowListCopyWindowInfo(.optionOnScreenOnly, kCGNullWindowID) as? [[String: Any]] else {
            throw CaptureError.invalidWindowList
        }
        var ids: [CGWindowID] = []
        var hudFrame: CGRect?
        for row in rows {
            guard let number = row[kCGWindowNumber as String] as? NSNumber,
                  number.int64Value > 0, let rowID = CGWindowID(exactly: number.int64Value) else {
                throw CaptureError.invalidWindowList
            }
            ids.append(rowID)
            if rowID == id {
                guard let bounds = row[kCGWindowBounds as String] as? [String: Any],
                      let x = bounds["X"] as? NSNumber, let y = bounds["Y"] as? NSNumber,
                      let width = bounds["Width"] as? NSNumber, let height = bounds["Height"] as? NSNumber else {
                    throw CaptureError.invalidWindowList
                }
                let frame = CGRect(x: x.doubleValue, y: y.doubleValue, width: width.doubleValue, height: height.doubleValue)
                guard isValidRectangle(frame) else { throw CaptureError.invalidWindowList }
                hudFrame = frame
            }
        }
        let prefix = try exclusionPrefix(orderedWindowIDs: ids, hudWindowID: id)
        guard let frame = hudFrame else { throw CaptureError.hudWindowNotOnScreen }
        return WindowSnapshot(hudWindowID: id, hudAppKitFrame: window.frame, hudCGFrame: frame, excludedWindowIDs: prefix)
    }

    @MainActor
    private static func displayGeometry() throws -> [DisplayGeometry] {
        var result: [DisplayGeometry] = []
        var seen = Set<CGDirectDisplayID>()
        for screen in NSScreen.screens {
            guard let number = screen.deviceDescription[NSDeviceDescriptionKey("NSScreenNumber")] as? NSNumber,
                  number.int64Value > 0, let id = CGDirectDisplayID(exactly: number.int64Value) else {
                throw CaptureError.invalidDisplayGeometry(0)
            }
            guard seen.insert(id).inserted else { throw CaptureError.duplicateDisplayID(id) }
            let geometry = DisplayGeometry(displayID: id, appKitGlobalFrame: screen.frame, cgGlobalBounds: CGDisplayBounds(id))
            try geometry.validate()
            result.append(geometry)
        }
        return result.sorted { $0.displayID < $1.displayID }
    }

    #if canImport(ScreenCaptureKit)
    @available(macOS 14.0, *)
    @MainActor
    private func captureModern(window: NSWindow, appKitGlobalRect: CGRect) async throws -> Frame {
        let start = ProcessInfo.processInfo.systemUptime
        let initialWindow = try Self.windowSnapshot(window)
        let displays = try Self.displayGeometry()
        let plan = try Self.plan(appKitGlobalRect: appKitGlobalRect, displays: displays)
        let content = try await SCShareableContent.excludingDesktopWindows(false, onScreenWindowsOnly: true)
        try Self.validateWindowMappings(excludedWindowIDs: initialWindow.excludedWindowIDs,
                                         shareableWindowIDs: content.windows.map { $0.windowID })
        var windowMap: [CGWindowID: SCWindow] = [:]
        for item in content.windows { windowMap[item.windowID] = item }
        let excludedWindows = try initialWindow.excludedWindowIDs.map { id -> SCWindow in
            guard let item = windowMap[id] else { throw CaptureError.missingWindowMappings([id]) }
            return item
        }
        func ensureUnchanged() throws {
            guard try Self.windowSnapshot(window) == initialWindow,
                  try Self.displayGeometry() == displays else { throw CaptureError.windowOrDisplayStateChanged }
        }
        try ensureUnchanged()
        var tiles: [Tile] = []
        for item in plan {
            try Task.checkCancellation()
            try ensureUnchanged()
            let matches = content.displays.filter { $0.displayID == item.display.displayID }
            guard matches.count == 1, let display = matches.first else {
                throw CaptureError.missingDisplayMapping(item.display.displayID)
            }
            let expected = item.display.cgGlobalBounds
            let actual = display.frame
            guard abs(expected.minX - actual.minX) <= 0.001, abs(expected.minY - actual.minY) <= 0.001,
                  abs(expected.width - actual.width) <= 0.001, abs(expected.height - actual.height) <= 0.001 else {
                throw CaptureError.displayCoordinateMismatch(item.display.displayID)
            }
            let filter = SCContentFilter(display: display, excludingWindows: excludedWindows)
            let local = item.requestedCGGlobalRect.offsetBy(dx: -expected.minX, dy: -expected.minY)
            let pixels = try Self.pixelRegion(displayLocalRect: local, displaySize: expected.size,
                                              pointPixelScale: CGFloat(filter.pointPixelScale), displayID: item.display.displayID)
            let configuration = SCStreamConfiguration()
            configuration.sourceRect = pixels.sourceRect
            configuration.width = pixels.pixelWidth
            configuration.height = pixels.pixelHeight
            configuration.showsCursor = false
            // No colorSpaceName override: retain the display's actual capture profile. No resizing
            // or HDR claim is made; record the returned CGImage's bit depth/format at the caller.
            let image = try await SCScreenshotManager.captureImage(contentFilter: filter, configuration: configuration)
            try ensureUnchanged()
            guard image.width == pixels.pixelWidth && image.height == pixels.pixelHeight else {
                throw CaptureError.unexpectedImageSize(item.display.displayID, expectedWidth: pixels.pixelWidth,
                                                       expectedHeight: pixels.pixelHeight,
                                                       actualWidth: image.width, actualHeight: image.height)
            }
            let capturedCG = pixels.sourceRect.offsetBy(dx: expected.minX, dy: expected.minY)
            tiles.append(Tile(displayID: item.display.displayID, image: image,
                              requestedAppKitRect: item.requestedAppKitRect,
                              requestedCGGlobalRect: item.requestedCGGlobalRect,
                              capturedAppKitRect: try item.display.appKitRectangle(for: capturedCG),
                              capturedCGGlobalRect: capturedCG, sourceRect: pixels.sourceRect,
                              pointPixelScale: pixels.pointPixelScale))
        }
        try ensureUnchanged()
        return Frame(requestedRect: appKitGlobalRect, hudWindowID: initialWindow.hudWindowID,
                     excludedWindowIDs: initialWindow.excludedWindowIDs, tiles: tiles,
                     captureStartUptime: start, captureEndUptime: ProcessInfo.processInfo.systemUptime)
    }
    #endif
}
