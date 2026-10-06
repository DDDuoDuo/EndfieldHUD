import AppKit
import AVFoundation

/// A normalized top-left crop in the original, upright media; transforms follow
/// crop so changing rotation never changes which source pixels were selected.
struct MediaAssemblyCrop: Codable, Equatable {
    var x = 0.0, y = 0.0, width = 1.0, height = 1.0
    var isValid: Bool { [x, y, width, height].allSatisfy(\.isFinite) && x >= 0 && y >= 0 && width >= 0.01 && height >= 0.01 && x + width <= 1.000001 && y + height <= 1.000001 }
}
enum MediaAssemblyFilter: String, Codable, CaseIterable {
    case none
    case special1 = "sp_filter_1"
    case special2 = "sp_filter_2"
    case special3 = "sp_filter_3"
    case special4 = "sp_filter_4"
    case special5 = "sp_filter_5"
    case special6 = "sp_filter_6"
    case filter1 = "filter_1"
    case filter2 = "filter_2"
    case filter3 = "filter_3"
    case filter4 = "filter_4"
    case filter5 = "filter_5"
    case filter6 = "filter_6"
    case filter7 = "filter_7"
    case filter8 = "filter_8"
    var title: String {
        switch self {
        case .none: return L10n.text("None", "无")
        case .special1: return L10n.text("Black Gold", "黑金")
        case .special2: return L10n.text("Warm", "温暖")
        case .special3: return L10n.text("Pale", "苍白")
        case .special4: return L10n.text("Film", "胶片")
        case .special5: return L10n.text("Hope", "希望色彩")
        case .special6: return L10n.text("Blood Memory", "血忆")
        case .filter1: return L10n.text("Bright", "明亮")
        case .filter2: return L10n.text("Cool", "冷调")
        case .filter3: return L10n.text("Fresh", "清新")
        case .filter4: return L10n.text("Dim", "暗色")
        case .filter5: return L10n.text("Monochrome", "黑白")
        case .filter6: return L10n.text("Spring", "春")
        case .filter7: return L10n.text("Renaissance", "文艺")
        case .filter8: return L10n.text("Blue Orange", "蓝橙")
        }
    }
}
enum MediaAssemblyStickerKind: String, Codable, CaseIterable {
    case sticker1 = "sticker_bp_1"
    case sticker2 = "sticker_bp_2"
    case sticker3 = "sticker_bp_3"
    case sticker4 = "sticker_bp_4"
    case sticker5 = "sticker_bp_5"
    case sticker6 = "sticker_bp_6"
    case sticker7 = "sticker_1"
    case sticker8 = "sticker_2"
    case sticker9 = "sticker_3"
    case sticker10 = "sticker_4"
    case sticker11 = "sticker_5"
    case sticker12 = "sticker_6"
    case sticker13 = "sticker_7"
    case sticker14 = "sticker_8"
    case sticker15 = "sticker_9"
    case sticker16 = "sticker_10"
    case sticker17 = "sticker_11"
    case sticker18 = "sticker_activity_1"
    case sticker19 = "sticker_activity_2"
    case sticker20 = "sticker_activity_3"
    case sticker21 = "sticker_activity_4"
    case sticker22 = "sticker_channel_v1d3d6_1"
    case sticker27 = "sticker_birthday_2026"
    case sticker28 = "sticker_giftpack_1"
    var title: String {
        switch self {
        case .sticker1: return L10n.text("Memory", "记忆")
        case .sticker2: return L10n.text("Fortune", "如意方圆")
        case .sticker3: return L10n.text("Behemoth Heart", "巨兽心脏")
        case .sticker4: return L10n.text("Sword Anchor", "剑桩")
        case .sticker5: return L10n.text("Sea Sentinel", "永镇蚀海")
        case .sticker6: return L10n.text("Dreamcatcher", "捕梦")
        case .sticker7: return L10n.text("Endfield", "终末地")
        case .sticker8: return L10n.text("Union", "联盟工团")
        case .sticker9: return L10n.text("High Voltage", "高压")
        case .sticker10: return L10n.text("Explosion", "爆炸")
        case .sticker11: return L10n.text("Hongshan Academy", "宏科院")
        case .sticker12: return L10n.text("Engineering Center", "工程中心")
        case .sticker13: return L10n.text("Ellipsis", "省略")
        case .sticker14: return L10n.text("To Be Continued", "待续")
        case .sticker15: return L10n.text("Knockout", "击倒")
        case .sticker16: return L10n.text("Round One", "第一回合")
        case .sticker17: return L10n.text("The End", "剧终")
        case .sticker18: return L10n.text("Frontier Festival", "开拓节美食")
        case .sticker19: return L10n.text("Wuling Chef", "武陵特厨")
        case .sticker20: return L10n.text("Contingency Contract", "危机合约")
        case .sticker21: return L10n.text("Dawn", "启明")
        case .sticker22: return L10n.text("Safety Helmet", "工业安全帽")
        case .sticker27: return L10n.text("First Birthday", "第一年的生日")
        case .sticker28: return L10n.text("Suppressor", "抑制器")
        }
    }
    var pixelSize: CGSize {
        // Source PNG canvas sizes, including transparent margins.
        let edge = self == .sticker21 ? 324 : 320
        return CGSize(width: edge, height: edge)
    }
}
struct MediaAssemblySticker: Codable, Equatable, Identifiable {
    var id = UUID()
    var kind: MediaAssemblyStickerKind = .sticker7
    var x = 0.5, y = 0.5, size = 0.2, rotation = 0.0
    var isValid: Bool { [x, y, size, rotation].allSatisfy(\.isFinite) && (0...1).contains(x) && (0...1).contains(y) && (0.02...1).contains(size) && abs(rotation) <= 360 }
}
struct MediaAssemblyAdjustments: Codable, Equatable {
    var crop = MediaAssemblyCrop()
    var rotationQuarterTurns = 0
    var mirrored = false
    var brightness = 0.0, contrast = 1.0, saturation = 1.0
    var temperature = 6500.0, tint = 0.0
    var highlights = 1.0, shadows = 0.0, exposure = 0.0
    var curve: [Double] = [0, 0.25, 0.5, 0.75, 1]
    var levelsBlack = 0.0, levelsWhite = 1.0, levelsGamma = 1.0
    var trimStart = 0.0, trimEnd: Double? = nil
    var filter: MediaAssemblyFilter = .none
    var stickers: [MediaAssemblySticker] = []
    var isValid: Bool {
        crop.isValid && (-3...3).contains(rotationQuarterTurns) && (-1...1).contains(brightness) && (0...4).contains(contrast)
            && (0...2).contains(saturation) && (2000...12000).contains(temperature) && (-200...200).contains(tint)
            && (0...1).contains(highlights) && (0...1).contains(shadows) && (-4...4).contains(exposure)
            && curve.count == 5 && curve.allSatisfy { $0.isFinite && (0...1).contains($0) }
            && (0...0.99).contains(levelsBlack) && (0.01...1).contains(levelsWhite) && levelsWhite - levelsBlack >= 0.01
            && (0.1...4).contains(levelsGamma) && trimStart.isFinite && trimStart >= 0
            && (trimEnd.map { $0.isFinite && $0 > trimStart } ?? true) && stickers.count <= 16 && stickers.allSatisfy(\.isValid)
    }
    func timeRange(duration: Double) throws -> CMTimeRange {
        let end = min(duration, trimEnd ?? duration)
        guard isValid, duration.isFinite, duration > 0, trimStart < end, end - trimStart >= 0.05 else { throw MediaAssemblyError.invalidAdjustment }
        return CMTimeRange(start: CMTime(seconds: trimStart, preferredTimescale: 600), duration: CMTime(seconds: end - trimStart, preferredTimescale: 600))
    }
}
struct MediaAssemblyDocument {
    let id = UUID()
    let reference: NotesMediaReference
    let sourceURL: URL
    let identity: MediaAssemblyFileIdentity
    var pixelSize: CGSize { CGSize(width: reference.pixelWidth, height: reference.pixelHeight) }
    var duration: Double { reference.duration ?? 0 }
    var isVideo: Bool { reference.kind == .video }
    var isAnimated: Bool { reference.kind == .gif && reference.frameCount > 1 }
    var name: String { sourceURL.lastPathComponent }
    var suggestedFilename: String {
        sourceURL.deletingPathExtension().lastPathComponent + "-edited." + (isVideo ? "mov" : "png")
    }
}
struct MediaAssemblyFileIdentity: Equatable {
    let inode: UInt64, device: UInt64, bytes: UInt64
    let modified: Date
    static func read(_ url: URL) throws -> Self {
        let a = try FileManager.default.attributesOfItem(atPath: url.path)
        guard a[.type] as? FileAttributeType == .typeRegular,
              let inode = (a[.systemFileNumber] as? NSNumber)?.uint64Value,
              let device = (a[.systemNumber] as? NSNumber)?.uint64Value,
              let bytes = (a[.size] as? NSNumber)?.uint64Value, let date = a[.modificationDate] as? Date else { throw MediaAssemblyError.unavailable }
        return Self(inode: inode, device: device, bytes: bytes, modified: date)
    }
}
enum MediaAssemblyError: LocalizedError {
    case unsupported, invalidAdjustment, unavailable, changedOnDisk, unsupportedExport, exportFailed, cancelled, exists
    var errorDescription: String? {
        switch self {
        case .unsupported: return L10n.text("This media format is not supported for editing.", "此媒体格式不支持编辑。")
        case .invalidAdjustment: return L10n.text("Choose a valid crop, adjustment or trim range.", "请选择有效的裁剪、调整或剪辑范围。")
        case .unavailable: return L10n.text("The original media is unavailable.", "原始媒体不可用。")
        case .changedOnDisk: return L10n.text("The original changed. Open it again before exporting.", "原文件已更改，请重新打开后再导出。")
        case .unsupportedExport: return L10n.text("This Mac cannot export the selected format. Choose PNG, JPEG, TIFF, HEIC, MOV or MP4.", "此 Mac 无法导出所选格式，请选择 PNG、JPEG、TIFF、HEIC、MOV 或 MP4。")
        case .exportFailed: return L10n.text("Export failed. The original file was preserved.", "导出失败，原文件已保留。")
        case .cancelled: return L10n.text("Export cancelled.", "导出已取消。")
        case .exists: return L10n.text("The destination already exists. Confirm overwrite or choose another name.", "目标文件已存在，请确认覆盖或选择其他名称。")
        }
    }
}
/// Cancel and the final atomic rename are serialized. Once commit has completed,
/// a racing cancel cannot turn a successful write into a reported cancellation.
final class MediaAssemblyExportTicket {
    private let lock = NSLock()
    private var cancelled = false, committed = false
    private var cancelSession: (() -> Void)?
    private var progressSource: (() -> Double)?
    private var completedFraction = 0.0
    var isCancelled: Bool { lock.lock(); defer { lock.unlock() }; return cancelled }
    var progress: Double { lock.lock(); let source = progressSource, fraction = completedFraction; lock.unlock(); return min(1, max(0, source?() ?? fraction)) }
    func setProgress(_ value: Double) { lock.lock(); completedFraction = value; lock.unlock() }
    func observeProgress(_ source: @escaping () -> Double) { lock.lock(); progressSource = source; lock.unlock() }
    func cancel() { lock.lock(); if !committed { cancelled = true }; let action = cancelled ? cancelSession : nil; lock.unlock(); action?() }
    func observeCancellation(_ action: @escaping () -> Void) { lock.lock(); cancelSession = action; let run = cancelled; lock.unlock(); if run { action() } }
    func finish() { lock.lock(); cancelSession = nil; progressSource = nil; lock.unlock() }
    func commit(_ operation: () throws -> Void) throws { lock.lock(); defer { lock.unlock() }; guard !cancelled else { throw MediaAssemblyError.cancelled }; try operation(); committed = true }
}
