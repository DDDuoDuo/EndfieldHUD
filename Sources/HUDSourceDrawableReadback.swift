import Foundation

/// Output representation only; source shaders, blend factors and tone mapping
/// are unchanged. The input is a raw BGRA8_sRGB drawable blit: encoded linear
/// premultiplied RGB and linear alpha, not a premultiplied sRGB CGImage.
enum HUDSourceDrawableReadback {
    enum Failure: Error, Equatable {
        case invalidDimensions, sizeOverflow, invalidRowBytes, byteCountMismatch
        case nonFiniteComponent, invalidComponentRange, unrepresentablePixel
    }

    struct PixelSample: Codable, Equatable {
        let x: Int
        let y: Int
        let bgra: [UInt8]
    }

    struct Report: Codable, Equatable {
        let width: Int
        let height: Int
        let rowBytes: Int
        let validPixelCount: Int
        let opaquePixelCount: Int
        let fractionalAlphaPixelCount: Int
        let zeroAlphaPixelCount: Int
        let rawEncodedRGBExceedsAlphaPixelCount: Int
        /// Includes positive RGB at alpha zero. Counts nominal decoded values;
        /// the separate quantization counter identifies ambiguous UNORM edges.
        let linearRGBExceedsAlphaPixelCount: Int
        let zeroAlphaPositiveRGBPixelCount: Int
        let quantizationConsistentExcessPixelCount: Int
        let maximumLinearRGBMinusAlpha: Double
        let isStraightAlphaRepresentable: Bool
        /// Up to 20 distinct, deterministic 4-column / 5-row grid samples.
        /// y indexes the raw blit rows; no image-orientation transform is made.
        let samples: [PixelSample]
    }

    struct StraightConversion {
        /// Nil for the entire image when any stored pixel is unrepresentable.
        /// No additive color, zero-alpha emission or HDR value is clamped away.
        let encodedBGRA: Data?
        let report: Report
    }

    // Analyze millions of pixels without evaluating pow for each channel.
    // Transfer is monotonic, so one lookup of the largest RGB byte suffices.
    private static let decodedSRGB = (0..<256).map { decodeSRGB(Double($0) / 255) }
    private static let decodedLowerQuantizationBound = (0..<256).map {
        decodeSRGB(max(0, (Double($0) - 0.5) / 255))
    }

    static func analyze(rawBGRA: Data, width: Int, height: Int, rowBytes: Int) throws -> Report {
        let pixelCount = try validate(rawBGRA: rawBGRA, width: width, height: height, rowBytes: rowBytes)
        var opaque = 0, fractional = 0, zero = 0, encodedExcess = 0
        var linearExcess = 0, zeroPositive = 0, quantizationExcess = 0
        var maximumExcess = 0.0
        var samples: [PixelSample] = []
        rawBGRA.withUnsafeBytes { (storage: UnsafeRawBufferPointer) in
            let bytes = storage.bindMemory(to: UInt8.self)
            for y in 0..<height {
                let row = y * rowBytes
                for x in 0..<width {
                    let offset = row + x * 4
                    let alpha = bytes[offset + 3]
                    if alpha == 255 { opaque += 1 }
                    else if alpha == 0 { zero += 1 }
                    else { fractional += 1 }
                    let maximumRGB = max(bytes[offset], max(bytes[offset + 1], bytes[offset + 2]))
                    // D(c) <= c in this bounded sRGB range. If the encoded RGB
                    // already fits alpha, its decoded linear value also fits.
                    guard maximumRGB > alpha else { continue }
                    encodedExcess += 1
                    let a = Double(alpha) / 255
                    let excess = decodedSRGB[Int(maximumRGB)] - a
                    guard excess > 0 else { continue }
                    linearExcess += 1
                    maximumExcess = max(maximumExcess, excess)
                    if alpha == 0 { zeroPositive += 1 }
                    else if decodedLowerQuantizationBound[Int(maximumRGB)] <= (Double(alpha) + 0.5) / 255 {
                        // Report the rounding ambiguity; do not silently adjust
                        // these nominal bytes or call the conversion lossless.
                        quantizationExcess += 1
                    }
                }
            }
            var visited: Set<Int> = []
            for gridY in 0..<5 {
                let y = (height - 1) * gridY / 4
                for gridX in 0..<4 {
                    let x = (width - 1) * gridX / 3
                    let offset = y * rowBytes + x * 4
                    guard visited.insert(offset).inserted else { continue }
                    samples.append(PixelSample(x: x, y: y,
                                               bgra: [bytes[offset], bytes[offset + 1], bytes[offset + 2], bytes[offset + 3]]))
                }
            }
        }
        return Report(width: width, height: height, rowBytes: rowBytes, validPixelCount: pixelCount,
                      opaquePixelCount: opaque, fractionalAlphaPixelCount: fractional, zeroAlphaPixelCount: zero,
                      rawEncodedRGBExceedsAlphaPixelCount: encodedExcess,
                      linearRGBExceedsAlphaPixelCount: linearExcess, zeroAlphaPositiveRGBPixelCount: zeroPositive,
                      quantizationConsistentExcessPixelCount: quantizationExcess,
                      maximumLinearRGBMinusAlpha: maximumExcess, isStraightAlphaRepresentable: linearExcess == 0,
                      samples: samples)
    }

    /// An opaque black-background reference, not the original HG opaque RT or
    /// a desktop-blur capture. Retains every encoded RGB byte; only active pixel
    /// alpha becomes 255. Padding bytes are retained and never counted as pixels.
    static func blackMatteEncodedBGRA(rawBGRA: Data, width: Int, height: Int, rowBytes: Int) throws -> Data {
        _ = try validate(rawBGRA: rawBGRA, width: width, height: height, rowBytes: rowBytes)
        var result = copyBytes(rawBGRA)
        result.withUnsafeMutableBytes { (storage: UnsafeMutableRawBufferPointer) in
            let bytes = storage.bindMemory(to: UInt8.self)
            for y in 0..<height {
                for x in 0..<width { bytes[y * rowBytes + x * 4 + 3] = 255 }
            }
        }
        return result
    }

    /// Converts only images whose stored tuple can be represented as bounded
    /// straight sRGB. Alpha and row padding are unchanged. The caller must use a
    /// nonpremultiplied CGImage alpha declaration for the returned byte buffer.
    static func straightEncodedBGRA(rawBGRA: Data, width: Int, height: Int, rowBytes: Int) throws -> StraightConversion {
        let report = try analyze(rawBGRA: rawBGRA, width: width, height: height, rowBytes: rowBytes)
        guard report.isStraightAlphaRepresentable else { return StraightConversion(encodedBGRA: nil, report: report) }
        var result = copyBytes(rawBGRA)
        result.withUnsafeMutableBytes { (storage: UnsafeMutableRawBufferPointer) in
            let bytes = storage.bindMemory(to: UInt8.self)
            for y in 0..<height {
                for x in 0..<width {
                    let offset = y * rowBytes + x * 4
                    let alpha = bytes[offset + 3]
                    if alpha == 255 { continue }
                    if alpha == 0 {
                        bytes[offset] = 0; bytes[offset + 1] = 0; bytes[offset + 2] = 0
                        continue
                    }
                    let a = Double(alpha) / 255
                    for channel in 0..<3 {
                        let straightLinear = decodedSRGB[Int(bytes[offset + channel])] / a
                        let encoded = encodeSRGB(straightLinear)
                        bytes[offset + channel] = UInt8((encoded * 255).rounded(.toNearestOrEven))
                    }
                }
            }
        }
        return StraightConversion(encodedBGRA: result, report: report)
    }

    /// The scalar version used to validate the same conversion independently of
    /// UNORM storage. Rejects nonfinite values and unrepresentable additive RGB.
    static func straightEncodedRGB(linearPremultipliedRGB: SIMD3<Double>, alpha: Double) throws -> SIMD3<Double> {
        guard alpha.isFinite, linearPremultipliedRGB.x.isFinite,
              linearPremultipliedRGB.y.isFinite, linearPremultipliedRGB.z.isFinite else {
            throw Failure.nonFiniteComponent
        }
        guard (0...1).contains(alpha), linearPremultipliedRGB.x >= 0,
              linearPremultipliedRGB.y >= 0, linearPremultipliedRGB.z >= 0 else {
            throw Failure.invalidComponentRange
        }
        guard linearPremultipliedRGB.x <= alpha, linearPremultipliedRGB.y <= alpha,
              linearPremultipliedRGB.z <= alpha else { throw Failure.unrepresentablePixel }
        if alpha == 0 { return .zero }
        return SIMD3(encodeSRGB(linearPremultipliedRGB.x / alpha),
                     encodeSRGB(linearPremultipliedRGB.y / alpha),
                     encodeSRGB(linearPremultipliedRGB.z / alpha))
    }

    private static func validate(rawBGRA: Data, width: Int, height: Int, rowBytes: Int) throws -> Int {
        guard width > 0, height > 0 else { throw Failure.invalidDimensions }
        let (activeRowBytes, rowOverflow) = width.multipliedReportingOverflow(by: 4)
        let (pixelCount, pixelOverflow) = width.multipliedReportingOverflow(by: height)
        guard !rowOverflow, !pixelOverflow else { throw Failure.sizeOverflow }
        guard rowBytes >= activeRowBytes else { throw Failure.invalidRowBytes }
        let (byteCount, byteOverflow) = rowBytes.multipliedReportingOverflow(by: height)
        guard !byteOverflow else { throw Failure.sizeOverflow }
        guard rawBGRA.count == byteCount else { throw Failure.byteCountMismatch }
        return pixelCount
    }

    private static func copyBytes(_ source: Data) -> Data {
        var result = Data(count: source.count)
        source.withUnsafeBytes { (input: UnsafeRawBufferPointer) in
            result.withUnsafeMutableBytes { (output: UnsafeMutableRawBufferPointer) in
                output.copyMemory(from: input)
            }
        }
        return result
    }

    private static func decodeSRGB(_ value: Double) -> Double {
        value <= 0.04045 ? value / 12.92 : pow((value + 0.055) / 1.055, 2.4)
    }

    private static func encodeSRGB(_ value: Double) -> Double {
        value <= 0.0031308 ? value * 12.92 : 1.055 * pow(value, 1 / 2.4) - 0.055
    }
}
