import Foundation

enum HUDSourceDrawableReadbackTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String, file: StaticString = #file, line: UInt = #line) {
            count += 1; if !condition { fatalError(message, file: file, line: line) }
        }
        func fails(_ expected: HUDSourceDrawableReadback.Failure, _ operation: () throws -> Void) {
            do { try operation(); check(false, "Expected readback validation failure: \(expected)") }
            catch let actual as HUDSourceDrawableReadback.Failure { check(actual == expected, "Readback rejects the specific invalid input") }
            catch { check(false, "Unexpected readback error: \(error)") }
        }
        do {
            // Independently calculated IEC sRGB values, not the helper's LUT:
            // D(0.5)=0.21404114048223255, so half-alpha premul is half that.
            let scalar = try HUDSourceDrawableReadback.straightEncodedRGB(
                linearPremultipliedRGB: SIMD3(repeating: 0.10702057024111627), alpha: 0.5)
            check(abs(scalar.x - 0.5) < 1e-12 && scalar.x == scalar.y && scalar.y == scalar.z,
                  "Unpremultiplication precedes the transfer function for half-gray / half-alpha")
            let gray = Data([92, 92, 92, 128]) // Quantized E(0.10702057), alpha0.5.
            let grayConversion = try HUDSourceDrawableReadback.straightEncodedBGRA(rawBGRA: gray, width: 1, height: 1, rowBytes: 4)
            guard let grayBytes = grayConversion.encodedBGRA else { fatalError("Ordinary gray is representable") }
            let rgb = [UInt8](grayBytes)
            let wronglyUnpremultiplied = Double(92) * 255 / 128
            check((127...128).contains(Int(rgb[0])) && rgb[0] == rgb[1] && rgb[1] == rgb[2] && rgb[3] == 128,
                  "Quantized half-gray becomes approximately128, retaining alpha")
            check(abs(wronglyUnpremultiplied - 183.28125) < 1e-12 && wronglyUnpremultiplied - Double(rgb[0]) > 55,
                  "The previous encoded-byte unpremultiplication brightens gray to approximately184")
            check(grayConversion.report.isStraightAlphaRepresentable && grayConversion.report.fractionalAlphaPixelCount == 1,
                  "Ordinary fractional alpha is distinguished from additive excess")
            let grayMatte = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(rawBGRA: gray, width: 1, height: 1, rowBytes: 4)
            check([UInt8](grayMatte) == [92, 92, 92, 255], "Black reference preserves raw encoded color and only removes transparency")
            check([UInt8](gray) == [92, 92, 92, 128], "Neither conversion mutates its caller's raw evidence")
            let sourceGray = try HUDSourceDrawableReadback.straightEncodedBGRA(
                rawBGRA: Data([0, 216, 0, 220]), width: 1, height: 1, rowBytes: 4)
            check(sourceGray.encodedBGRA.map { [UInt8]($0) } == [0, 231, 0, 220],
                  "Source button's quantized231/255 color returns from its216/255 stored premul color without channel mixing")
            let low = try HUDSourceDrawableReadback.straightEncodedRGB(
                linearPremultipliedRGB: SIMD3(0.001, 0.0005, 0), alpha: 0.5)
            check(abs(low.x - 0.02584) < 1e-12 && abs(low.y - 0.01292) < 1e-12 && low.z == 0,
                  "The independent linear sRGB segment applies12.92 after unpremultiplication")

            let opaque = Data([12, 34, 240, 255])
            let opaqueConversion = try HUDSourceDrawableReadback.straightEncodedBGRA(rawBGRA: opaque, width: 1, height: 1, rowBytes: 4)
            check(opaqueConversion.encodedBGRA == opaque && opaqueConversion.report.opaquePixelCount == 1,
                  "Opaque output already has identical straight and premultiplied encoding")
            let clear = Data([0, 0, 0, 0])
            let clearConversion = try HUDSourceDrawableReadback.straightEncodedBGRA(rawBGRA: clear, width: 1, height: 1, rowBytes: 4)
            check(clearConversion.encodedBGRA == clear && clearConversion.report.zeroAlphaPixelCount == 1,
                  "Transparent black remains representable without division by zero")
            check(try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: .zero, alpha: 0) == .zero,
                  "Normalized transparent black also has a well-defined output")

            let additive = Data([0, 0, 255, 128, 12, 0, 0, 0])
            let additiveConversion = try HUDSourceDrawableReadback.straightEncodedBGRA(rawBGRA: additive, width: 2, height: 1, rowBytes: 8)
            check(additiveConversion.encodedBGRA == nil && !additiveConversion.report.isStraightAlphaRepresentable,
                  "An image with additive or zero-alpha color cannot masquerade as lossless straight PNG")
            check(additiveConversion.report.linearRGBExceedsAlphaPixelCount == 2
                  && additiveConversion.report.zeroAlphaPositiveRGBPixelCount == 1
                  && additiveConversion.report.rawEncodedRGBExceedsAlphaPixelCount == 2,
                  "Both additive alpha and zero-alpha emission retain explicit counters")
            check(abs(additiveConversion.report.maximumLinearRGBMinusAlpha - 127.0 / 255) < 1e-12,
                  "Known red1 / alpha128 exposes the independently calculated excess")
            let additiveMatte = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(rawBGRA: additive, width: 2, height: 1, rowBytes: 8)
            check([UInt8](additiveMatte) == [0, 0, 255, 255, 12, 0, 0, 255],
                  "The black reference retains additive RGB even at original alpha zero")
            let roundedWhite = try HUDSourceDrawableReadback.analyze(rawBGRA: Data([188, 188, 188, 128]), width: 1, height: 1, rowBytes: 4)
            check(roundedWhite.linearRGBExceedsAlphaPixelCount == 1 && roundedWhite.quantizationConsistentExcessPixelCount == 1,
                  "Half-alpha white can exceed nominal alpha only through 8-bit quantization; that ambiguity is reported")
            check(!roundedWhite.isStraightAlphaRepresentable,
                  "Quantization-consistent excess is not silently clamped or called lossless")

            // Padding deliberately contains impossible transparent-white tuples.
            let padded = Data([92, 92, 92, 128, 255, 255, 255, 0,
                               12, 34, 240, 255, 255, 255, 255, 0])
            let paddedReport = try HUDSourceDrawableReadback.analyze(rawBGRA: padded, width: 1, height: 2, rowBytes: 8)
            check(paddedReport.validPixelCount == 2 && paddedReport.opaquePixelCount == 1
                  && paddedReport.fractionalAlphaPixelCount == 1 && paddedReport.zeroAlphaPixelCount == 0
                  && paddedReport.linearRGBExceedsAlphaPixelCount == 0,
                  "Row padding never participates in pixel counts or representability")
            check(paddedReport.samples.count == 2 && paddedReport.samples.map { $0.y } == [0, 1]
                  && paddedReport.samples[1].bgra == [12, 34, 240, 255],
                  "Raw samples address the active row pitch and avoid duplicate or padding points")
            let paddedMatte = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(rawBGRA: padded, width: 1, height: 2, rowBytes: 8)
            check([UInt8](paddedMatte) == [92, 92, 92, 255, 255, 255, 255, 0,
                                         12, 34, 240, 255, 255, 255, 255, 0],
                  "Black-reference alpha writes stop at the active width and preserve both padding regions")
            let paddedStraight = try HUDSourceDrawableReadback.straightEncodedBGRA(rawBGRA: padded, width: 1, height: 2, rowBytes: 8)
            guard let paddedBytes = paddedStraight.encodedBGRA else { fatalError("Padded ordinary image is representable") }
            check([UInt8](paddedBytes)[4..<8].elementsEqual([255, 255, 255, 0])
                  && [UInt8](paddedBytes)[12..<16].elementsEqual([255, 255, 255, 0]),
                  "Straight conversion also leaves row padding unmodified")
            let containing = Data([99, 92, 92, 92, 128, 99])
            let slicedMatte = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(
                rawBGRA: containing[1..<5], width: 1, height: 1, rowBytes: 4)
            check([UInt8](slicedMatte) == [92, 92, 92, 255],
                  "A Data slice's collection index does not shift its raw byte-buffer addressing")

            let grid = Data(repeating: 0, count: 8 * 6 * 4)
            let gridReport = try HUDSourceDrawableReadback.analyze(rawBGRA: grid, width: 8, height: 6, rowBytes: 32)
            check(gridReport.samples.count == 20 && gridReport.samples.first?.x == 0
                  && gridReport.samples.last?.x == 7 && gridReport.samples.last?.y == 5,
                  "Twenty bounded raw samples cover both image edges without scanning padding")
            let reportJSON = try JSONEncoder().encode(gridReport)
            check(try JSONDecoder().decode(HUDSourceDrawableReadback.Report.self, from: reportJSON) == gridReport,
                  "Fixture diagnostics round-trip their coordinates, raw bytes and representability statistics")

            fails(.invalidDimensions) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: clear, width: 0, height: 1, rowBytes: 4) }
            fails(.invalidDimensions) { _ = try HUDSourceDrawableReadback.blackMatteEncodedBGRA(rawBGRA: clear, width: 1, height: -1, rowBytes: 4) }
            fails(.invalidRowBytes) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: clear, width: 2, height: 1, rowBytes: 4) }
            fails(.byteCountMismatch) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: clear, width: 1, height: 2, rowBytes: 4) }
            fails(.byteCountMismatch) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: padded, width: 1, height: 1, rowBytes: 4) }
            fails(.sizeOverflow) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: clear, width: Int.max, height: 1, rowBytes: Int.max) }
            fails(.sizeOverflow) { _ = try HUDSourceDrawableReadback.analyze(rawBGRA: clear, width: 1, height: 2, rowBytes: Int.max) }
            fails(.nonFiniteComponent) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: SIMD3(.nan, 0, 0), alpha: 1) }
            fails(.nonFiniteComponent) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: .zero, alpha: .infinity) }
            fails(.invalidComponentRange) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: SIMD3(-0.1, 0, 0), alpha: 1) }
            fails(.invalidComponentRange) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: .zero, alpha: 1.01) }
            fails(.unrepresentablePixel) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: SIMD3(0.5, 0, 0), alpha: 0.25) }
            fails(.unrepresentablePixel) { _ = try HUDSourceDrawableReadback.straightEncodedRGB(linearPremultipliedRGB: SIMD3(0.1, 0, 0), alpha: 0) }
        } catch { fatalError("Source drawable readback checks failed: \(error)") }
        return count
    }
}
