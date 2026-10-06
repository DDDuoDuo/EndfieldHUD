import AppKit

/// Section decoration belongs to presentation, never navigation or saved titles.
enum HUDSectionHeading {
    static func text(_ title: String) -> String {
        title.hasPrefix("//") ? title : "// " + title
    }
}
