import Foundation

enum HUDSourceRectClippingTests {
    static func run() -> Int {
        var count = 0
        func check(_ condition: Bool, _ message: String) {
            count += 1; if !condition { fatalError(message) }
        }
        func id(_ value: Int) -> HUDSourceID { HUDSourceID(rawValue: "CAB-clip:\(value)") }
        func mask(_ node: Int, softness: SIMD2<Double>, hg: SIMD4<Double> = .zero,
                  enabled: Bool = true) -> HUDSourceWatchComponent {
            HUDSourceWatchComponent(id: id(node + 100), type: "MonoBehaviour", script: "RectMask2D", data: [
                "m_Enabled": .bool(enabled), "m_Softness": .object(["x": .number(softness.x), "y": .number(softness.y)]),
                "m_HGSoftness": .object(["x": .number(hg.x), "y": .number(hg.y), "z": .number(hg.z), "w": .number(hg.w)])])
        }
        let components = [id(1): [mask(1, softness: SIMD2(80, 80))],
                          id(2): [mask(2, softness: SIMD2(0, 48), hg: SIMD4(1, 2, 3, 4))],
                          id(3): [mask(3, softness: SIMD2(9, 9), enabled: false)],
                          id(4): [mask(4, softness: SIMD2(100, 100))]]
        let uniforms = HUDSourceRectClipping.uniforms(graphicID: id(5), maskIDs: [id(1), id(2)], components: components)
        check(uniforms["clipRectParam"] == [0, 0, 48, 0],
              "Nested clipping uses the nearest mask's source softness, not the outer or maximum value")
        check(uniforms["uiMaskHGSoftness"] == [1, 2, 3, 4],
              "HG four-edge feathering remains independent of the native two-axis softness")
        let ownMask = HUDSourceRectClipping.uniforms(graphicID: id(4), maskIDs: [id(1), id(2), id(3), id(4)], components: components)
        check(ownMask == uniforms, "A graphic ignores its own RectMask2D and disabled ancestors when selecting softness")
        check(HUDSourceRectClipping.uniforms(graphicID: id(4), maskIDs: [id(4)], components: components).isEmpty,
              "A mask does not apply its own softness to the graphic on the same GameObject")
        // Independent evaluation of the original UIImage/TMP clip kernel:
        // (rectSize - abs(vertexMaskXY)) * .25 / (.25 * softness + pixelSize).
        // Six units inside a vertical edge yields rectSize - abs(maskY) = 12.
        let sourceSoftnessY = Double(uniforms["clipRectParam"]![2])
        let softAlpha = min(1, 12 * 0.25 / (sourceSoftnessY * 0.25 + 1))
        let hardAlpha = min(1, 12 * 0.25 / 1)
        check(abs(softAlpha - 3.0 / 13.0) < 1e-12 && hardAlpha == 1,
              "The original 48-unit vertical softness creates a visible feather where zero softness is already opaque")
        return count
    }
}
