import Foundation

/// One File Shelf entry offered as a note or projection media source (the
/// centre's "Choose from Shelf" menu lists these; nothing is resolved until one
/// is picked).
struct NotesShelfMediaChoice {
    let id: UUID
    let title: String
    let detail: String
    let isSupported: Bool
    let isAvailable: Bool
}
