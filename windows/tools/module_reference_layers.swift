import AppKit
import QuartzCore
import CryptoKit

/// Shared structural export of model layers, not a screenshot or replacement UI.
/// Geometry remains in each layer's local coordinate system. Children retain
/// source insertion order; zPosition, masks and anchor transforms are explicit.
final class ModuleReferenceLayerEncoder {
    private let output: URL
    private var assets: [String: [String: Any]] = [:]
    private(set) var unsupported: [[String: String]] = []
    var rasterAssets: [[String: Any]] { assets.keys.sorted().compactMap { assets[$0] } }

    init(output: URL) throws {
        self.output = output
        try FileManager.default.createDirectory(at: output.appendingPathComponent("raster"), withIntermediateDirectories: true)
    }
    static func rect(_ r: CGRect) -> [CGFloat] { [r.origin.x, r.origin.y, r.width, r.height] }
    static func point(_ p: CGPoint) -> [CGFloat] { [p.x, p.y] }
    static func transform(_ t: CATransform3D) -> [[CGFloat]] {
        [[t.m11,t.m12,t.m13,t.m14], [t.m21,t.m22,t.m23,t.m24],
         [t.m31,t.m32,t.m33,t.m34], [t.m41,t.m42,t.m43,t.m44]]
    }
    static func sha256(_ data: Data) -> String { SHA256.hash(data: data).map { String(format: "%02x", $0) }.joined() }
    private func flag(_ id: String, _ feature: String, category: String = "unsupported-render-effect") {
        unsupported.append(["node": id, "feature": feature, "category": category])
    }
    private func color(_ c: CGColor?) -> Any {
        guard let c else { return NSNull() }
        let converted = c.converted(to: CGColorSpace(name: CGColorSpace.sRGB)!, intent: .defaultIntent, options: nil)
        let value: [String: Any] = ["sourceColorSpace": (c.colorSpace?.name as String?) ?? "unnamed",
                "sourceColorSpaceModel": c.colorSpace?.model.rawValue ?? -1,
                "sourceComponents": c.components ?? [], "sRGB": converted?.components as Any? ?? NSNull()]
        return value
    }
    private func font(_ f: NSFont) -> [String: Any] {
        ["postScriptName": f.fontName, "familyName": f.familyName as Any? ?? NSNull(),
         "pointSize": f.pointSize, "symbolicTraits": f.fontDescriptor.symbolicTraits.rawValue,
         "ascender": f.ascender, "descender": f.descender, "leading": f.leading]
    }
    private func path(_ p: CGPath?) -> Any {
        guard let p else { return NSNull() }
        var elements: [[String: Any]] = []
        p.applyWithBlock { pointer in
            let e = pointer.pointee
            let kind: String, count: Int
            switch e.type {
            case .moveToPoint: kind = "move"; count = 1
            case .addLineToPoint: kind = "line"; count = 1
            case .addQuadCurveToPoint: kind = "quadratic"; count = 2
            case .addCurveToPoint: kind = "cubic"; count = 3
            case .closeSubpath: kind = "close"; count = 0
            @unknown default: kind = "unknown"; count = 0
            }
            elements.append(["op": kind, "points": (0..<count).map { Self.point(e.points[$0]) }])
        }
        return elements
    }
    private func text(_ t: CATextLayer, id: String) -> [String: Any] {
        var value: [String: Any] = ["fontSize": t.fontSize, "foregroundColor": color(t.foregroundColor),
            "alignment": t.alignmentMode.rawValue, "truncation": t.truncationMode.rawValue,
            "wrapped": t.isWrapped, "allowsFontSubpixelQuantization": t.allowsFontSubpixelQuantization]
        if let f = t.font as? NSFont { value["font"] = font(f) }
        else if let f = t.font as? String { value["font"] = ["postScriptName": f] }
        else if t.font != nil { flag(id, "CATextLayer font type", category: "unsupported-font") }
        if let string = t.string as? NSAttributedString {
            value["string"] = string.string
            var runs: [[String: Any]] = []
            string.enumerateAttributes(in: NSRange(location: 0, length: string.length)) { attributes, range, _ in
                var attrs: [String: Any] = [:]
                for key in attributes.keys.sorted(by: { $0.rawValue < $1.rawValue }) {
                    let item = attributes[key]!
                    switch key {
                    case .font:
                        if let f = item as? NSFont { attrs[key.rawValue] = self.font(f) }
                        else { self.flag(id, "attributed font", category: "unsupported-font") }
                    case .foregroundColor, .backgroundColor, .strokeColor:
                        if let c = item as? NSColor { attrs[key.rawValue] = self.color(c.cgColor) }
                        else { self.flag(id, "attributed color " + key.rawValue) }
                    case .kern, .baselineOffset, .ligature, .strokeWidth, .underlineStyle, .strikethroughStyle, .obliqueness, .expansion:
                        if let n = item as? NSNumber { attrs[key.rawValue] = n }
                        else { self.flag(id, "attributed number " + key.rawValue) }
                    case .paragraphStyle:
                        if let p = item as? NSParagraphStyle {
                            attrs[key.rawValue] = ["alignment": p.alignment.rawValue, "lineBreakMode": p.lineBreakMode.rawValue,
                                "lineSpacing": p.lineSpacing, "paragraphSpacing": p.paragraphSpacing,
                                "paragraphSpacingBefore": p.paragraphSpacingBefore, "firstLineHeadIndent": p.firstLineHeadIndent,
                                "headIndent": p.headIndent, "tailIndent": p.tailIndent, "minimumLineHeight": p.minimumLineHeight,
                                "maximumLineHeight": p.maximumLineHeight, "lineHeightMultiple": p.lineHeightMultiple,
                                "baseWritingDirection": p.baseWritingDirection.rawValue]
                            if !p.tabStops.isEmpty { self.flag(id, "paragraph tab stops", category: "unsupported-text-attribute") }
                        }
                    default: self.flag(id, "text attribute " + key.rawValue, category: "unsupported-text-attribute")
                    }
                }
                runs.append(["utf16Range": [range.location, range.length], "attributes": attrs])
            }
            value["runs"] = runs
        } else if let string = t.string as? String { value["string"] = string; value["runs"] = [] }
        else { value["string"] = ""; value["runs"] = []; if t.string != nil { flag(id, "text string type") } }
        return value
    }
    private func raster(_ contents: Any, id: String) throws -> Any {
        let image: CGImage?
        if let ns = contents as? NSImage { image = ns.cgImage(forProposedRect: nil, context: nil, hints: nil) }
        else if CFGetTypeID(contents as CFTypeRef) == CGImage.typeID { image = (contents as! CGImage) }
        else { image = nil }
        guard let image, let data = NSBitmapImageRep(cgImage: image).representation(using: .png, properties: [:]) else {
            flag(id, "layer contents type " + String(reflecting: type(of: contents)))
            return NSNull()
        }
        let digest = Self.sha256(data), filename = "raster/\(digest).png"
        if assets[digest] == nil {
            try data.write(to: output.appendingPathComponent(filename), options: .atomic)
            assets[digest] = ["path": filename, "sha256": digest, "width": image.width, "height": image.height,
                "sourceBitsPerComponent": image.bitsPerComponent, "sourceBitsPerPixel": image.bitsPerPixel,
                "sourceAlphaInfo": image.alphaInfo.rawValue, "sourceColorSpace": image.colorSpace?.name as String? ?? "unnamed",
                "role": "intrinsic source layer contents; not whole-module rasterization"]
        }
        return ["asset": filename, "sha256": digest]
    }
    func encode(_ layer: CALayer, id: String) throws -> [String: Any] {
        var row: [String: Any] = ["id": id, "name": layer.name as Any? ?? NSNull(),
            "class": String(NSStringFromClass(type(of: layer)).split(separator: ".").last!), "kind": "layer",
            "bounds": Self.rect(layer.bounds), "frame": Self.rect(layer.frame),
            "position": Self.point(layer.position), "anchorPoint": Self.point(layer.anchorPoint),
            "anchorPointZ": layer.anchorPointZ, "zPosition": layer.zPosition,
            "transform": Self.transform(layer.transform), "sublayerTransform": Self.transform(layer.sublayerTransform),
            "opacity": layer.opacity, "hidden": layer.isHidden, "masksToBounds": layer.masksToBounds,
            "geometryFlipped": layer.isGeometryFlipped, "contentsAreFlipped": layer.contentsAreFlipped(),
            "doubleSided": layer.isDoubleSided, "allowsGroupOpacity": layer.allowsGroupOpacity,
            "opaque": layer.isOpaque, "drawsAsynchronously": layer.drawsAsynchronously,
            "allowsEdgeAntialiasing": layer.allowsEdgeAntialiasing, "edgeAntialiasingMask": layer.edgeAntialiasingMask.rawValue,
            "contentsFormat": layer.contentsFormat.rawValue,
            "backgroundColor": color(layer.backgroundColor), "borderColor": color(layer.borderColor),
            "borderWidth": layer.borderWidth, "cornerRadius": layer.cornerRadius,
            "maskedCorners": layer.maskedCorners.rawValue, "cornerCurve": layer.cornerCurve.rawValue,
            "shadowColor": color(layer.shadowColor), "shadowOpacity": layer.shadowOpacity,
            "shadowRadius": layer.shadowRadius, "shadowOffset": [layer.shadowOffset.width, layer.shadowOffset.height],
            "shadowPath": path(layer.shadowPath), "contentsGravity": layer.contentsGravity.rawValue,
            "contentsRect": Self.rect(layer.contentsRect), "contentsCenter": Self.rect(layer.contentsCenter),
            "contentsScale": layer.contentsScale, "minificationFilter": layer.minificationFilter.rawValue,
            "magnificationFilter": layer.magnificationFilter.rawValue, "minificationFilterBias": layer.minificationFilterBias,
            "shouldRasterize": layer.shouldRasterize, "rasterizationScale": layer.rasterizationScale,
            "animationKeys": (layer.animationKeys() ?? []).sorted()]
        if let shape = layer as? CAShapeLayer {
            row["kind"] = "shape"
            row["shape"] = ["path": path(shape.path), "fillColor": color(shape.fillColor), "strokeColor": color(shape.strokeColor),
                "fillRule": shape.fillRule.rawValue, "lineWidth": shape.lineWidth, "lineCap": shape.lineCap.rawValue,
                "lineJoin": shape.lineJoin.rawValue, "miterLimit": shape.miterLimit,
                "strokeStart": shape.strokeStart, "strokeEnd": shape.strokeEnd,
                "lineDashPhase": shape.lineDashPhase, "lineDashPattern": shape.lineDashPattern as Any? ?? NSNull()]
        } else if let t = layer as? CATextLayer { row["kind"] = "text"; row["text"] = text(t, id: id) }
        else if let gradient = layer as? CAGradientLayer {
            row["kind"] = "gradient"
            row["gradient"] = ["colors": (gradient.colors ?? []).map { item -> Any in
                    if CFGetTypeID(item as CFTypeRef) == CGColor.typeID { return color((item as! CGColor)) }
                    flag(id, "gradient color type"); return NSNull()
                },
                "locations": gradient.locations as Any? ?? NSNull(), "startPoint": Self.point(gradient.startPoint),
                "endPoint": Self.point(gradient.endPoint), "type": gradient.type.rawValue]
        } else if type(of: layer) != CALayer.self { flag(id, "layer subclass " + String(NSStringFromClass(type(of: layer)).split(separator: ".").last!), category: "unverified-layer-subclass") }
        if let contents = layer.contents { row["contents"] = try raster(contents, id: id) }
        else { row["contents"] = NSNull() }
        if let mask = layer.mask { row["mask"] = try encode(mask, id: id + "/mask") }
        else { row["mask"] = NSNull() }
        row["children"] = try (layer.sublayers ?? []).enumerated().map { try encode($0.element, id: id + "/" + String($0.offset)) }
        if layer.delegate != nil { flag(id, "layer delegate (not executed)", category: "unverified-custom-drawing") }
        if !(layer.filters ?? []).isEmpty { flag(id, "filters") }
        if !(layer.backgroundFilters ?? []).isEmpty { flag(id, "backgroundFilters") }
        if layer.compositingFilter != nil { flag(id, "compositingFilter") }
        if !(layer.animationKeys() ?? []).isEmpty { flag(id, "model tree exported; presentation animations not sampled", category: "metadata-only-animation") }
        return row
    }
}
