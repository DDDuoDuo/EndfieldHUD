import AppKit
import QuartzCore
// Detached HUDQuitConfirmationView with its DEFAULT quit content (no
// setContent), as SystemHUDView.presentQuitConfirmation shows it for the red
// desktop QuitBtn. Neutral stubs cannot consult preferences or providers.
enum L10n { static var chinese = false; static func text(_ en: String, _ zh: String) -> String { chinese ? zh : en } }
enum HUDRuntimeAppearance { static let reduceMotion = true }
enum HUDMotionMath { static func transform(normalizedPoint: CGPoint, depth: CGFloat, travel: CGFloat, reducedMotion: Bool, parallaxIntensity: CGFloat, perspectiveIntensity: CGFloat, projectionBounds: CGRect) -> CATransform3D { CATransform3DIdentity } }
@main enum QuitReference {
 static func main() throws {
  precondition(CommandLine.arguments.count == 2 && ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil)
  NSApplication.shared.setActivationPolicy(.prohibited)
  func rect(_ r: NSRect) -> [Double] { [r.minX, r.minY, r.width, r.height] }
  func color(_ c: CGColor?) -> Any { guard let c, let v = NSColor(cgColor: c)?.usingColorSpace(.sRGB) else { return NSNull() }; return [v.redComponent, v.greenComponent, v.blueComponent, v.alphaComponent] }
  func font(_ f: NSFont?) -> Any { guard let f else { return NSNull() }; return ["name": f.fontName, "size": f.pointSize] as [String: Any] }
  func path(_ p: CGPath?) -> [[String: Any]] { guard let p else { return [] }; var result: [[String: Any]] = []; p.applyWithBlock { e in let v = e.pointee; let n: Int; let op: String; switch v.type { case .moveToPoint: n = 1; op = "move"; case .addLineToPoint: n = 1; op = "line"; case .addQuadCurveToPoint: n = 2; op = "quad"; case .addCurveToPoint: n = 3; op = "cubic"; case .closeSubpath: n = 0; op = "close"; @unknown default: fatalError() }; result.append(["op": op, "points": (0..<n).map { [v.points[$0].x, v.points[$0].y] }]) }; return result }
  var cases: [[String: Any]] = []
  for chinese in [false, true] { for width in [1280.0, 240, 180] { for dark in [true, false] {
   L10n.chinese = chinese
   let view = HUDQuitConfirmationView(frame: NSRect(x: 0, y: 0, width: width, height: 800), reduceMotion: { true })
   view.configure(dark: dark, accent: NSColor(srgbRed: 250.0 / 255, green: 212.0 / 255, blue: 31.0 / 255, alpha: 1)); view.show(); view.layoutSubtreeIfNeeded(); view.layout()
   let card = view.subviews[0]; var controls: [[String: Any]] = []
   for child in card.subviews {
    if let field = child as? NSTextField { controls.append(["kind": "text", "value": field.stringValue, "frame": rect(field.frame), "font": font(field.font), "color": color(field.textColor?.cgColor)]) }
    else if let button = child as? NSButton { controls.append(["kind": "button", "value": button.title, "frame": rect(button.frame), "titleRect": rect(button.cell!.titleRect(forBounds: button.bounds)), "font": font(button.font), "accessibility": button.accessibilityLabel() ?? ""]) }
   }
   let art = (card.layer!.sublayers ?? []).prefix(4).map { l -> [String: Any] in var v: [String: Any] = ["bounds": rect(l.bounds), "background": color(l.backgroundColor)]; if let s = l as? CAShapeLayer { v["path"] = path(s.path); v["fill"] = color(s.fillColor); v["stroke"] = color(s.strokeColor); v["lineWidth"] = s.lineWidth }; return v }
   cases.append(["chinese": chinese, "width": width, "dark": dark, "card": rect(card.frame), "background": color(view.layer!.backgroundColor), "artwork": art, "controls": controls,
                 "accessibilityLabel": view.accessibilityLabel() ?? "", "accessibilityHelp": view.accessibilityHelp() ?? ""])
   view.dismiss(animated: false)
  }}}
  precondition(NSApp.windows.isEmpty)
  try JSONSerialization.data(withJSONObject: ["cases": cases, "actualSource": true, "windowCreated": false, "source": "Sources/HUDQuitConfirmationView.swift"], options: [.sortedKeys]).write(to: URL(fileURLWithPath: CommandLine.arguments[1]))
  print("Quit confirmation source: \(cases.count) detached view cases")
 }
}
