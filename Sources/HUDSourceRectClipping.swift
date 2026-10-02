import Foundation

/// The mask stack already follows active hierarchy and sorting boundaries.
/// Native MaskUtilities excludes a mask on the graphic's own GameObject;
/// RectMask2D forwards its nearest child's softness without a scale conversion.
enum HUDSourceRectClipping {
    static func uniforms(graphicID: HUDSourceID, maskIDs: [HUDSourceID],
                         components: [HUDSourceID: [HUDSourceWatchComponent]]) -> [String: [Float]] {
        for id in maskIDs.reversed() where id != graphicID {
            guard let mask = components[id]?.first(where: { $0.kind == "RectMask2D" && $0.enabled }) else { continue }
            let softness = mask["m_Softness"], hg = mask["m_HGSoftness"]
            // Original UIImage/TMP vertex stages read clipRectParam.y/z.
            // x/w are unused in these translated stages and remain zero.
            return ["clipRectParam": [0, Float(softness["x"].float()), Float(softness["y"].float()), 0],
                    "uiMaskHGSoftness": ["x", "y", "z", "w"].map { Float(hg[$0].float()) }]
        }
        return [:]
    }
}
