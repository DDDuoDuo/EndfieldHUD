#!/usr/bin/env python3
"""Guard the stable desktop/data contract while replacing its visual shell.

This reads repository files only. --behavioral additionally runs the existing
core suite, whose persistence cases use temporary stores and defaults suites.
It never starts the application or reads the user's Application Support folder.
"""
import argparse
import hashlib
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parent.parent
MANIFEST = ROOT / "Tests/Fixtures/stable-integration-contract.json"

# The approved 1.2.0 release changes version metadata, not the stable bundle,
# preferences, permission or signed-update identity recorded in the baseline.
RELEASE_METADATA = {"CFBundleShortVersionString": "1.2.0", "CFBundleVersion": "17"}


# The selected iOS-on-Mac app uses a wrapped flat bundle. Reverse only these
# exact metadata-reader repairs, then require the original store byte hash.
# No archive, bookmark, storage-path, identity or writer exemption is allowed;
# each repair remains required even if someone substitutes a manifest hash.
ALLOWED_METADATA_REPAIRS = {
    "Sources/AppShortcutStore.swift": {
        "baselineSha256": "88e02291fafbd36629ec37b4ccf39f86e66f01c0a053da2f1a63e6084c333506",
        "reverse": [
            ('            let bundleURL = try metadataBundleURL(at: url)\n'
             '            let hasContents = FileManager.default.fileExists(atPath: bundleURL.appendingPathComponent("Contents").path)\n'
             '            let infoURL = bundleURL.appendingPathComponent(hasContents ? "Contents/Info.plist" : "Info.plist")\n',
             '            let infoURL = url.appendingPathComponent("Contents/Info.plist")\n'),
            ('            let executableDirectory = hasContents ? bundleURL.appendingPathComponent("Contents/MacOS") : bundleURL\n'
             '            let executableURL = executableDirectory.appendingPathComponent(executable)\n',
             '            let executableURL = url.appendingPathComponent("Contents/MacOS").appendingPathComponent(executable)\n'),
            ('            let localized = Bundle(url: bundleURL)?.localizedInfoDictionary\n',
             '            let localized = Bundle(url: url)?.localizedInfoDictionary\n'),
            ('    /// App Store iOS apps on Mac keep their flat bundle behind WrappedBundle.\n'
             '    /// Read metadata from that bundle, but retain the selected outer .app for\n'
             '    /// bookmarks, icons and LaunchServices. Never search for another installed\n'
             "    /// app or follow a wrapper link outside this application's own Wrapper.\n"
             '    private static func metadataBundleURL(at url: URL) throws -> URL {\n'
             '        let url = canonical(url)\n'
             '        let fm = FileManager.default\n'
             '        if fm.fileExists(atPath: url.appendingPathComponent("Contents").path)\n'
             '            || fm.fileExists(atPath: url.appendingPathComponent("Info.plist").path) { return url }\n'
             '        let wrapper = url.appendingPathComponent("Wrapper", isDirectory: true)\n'
             '        let wrapped = canonical(url.appendingPathComponent("WrappedBundle"))\n'
             '        guard canonical(wrapper) == wrapper, wrapped.deletingLastPathComponent() == wrapper,\n'
             '              wrapped.pathExtension.lowercased() == "app" else { throw AppShortcutStoreError.invalidApplication }\n'
             '        let values = try wrapped.resourceValues(forKeys: [.isDirectoryKey, .isReadableKey])\n'
             '        guard values.isDirectory == true, values.isReadable == true else { throw AppShortcutStoreError.invalidApplication }\n'
             '        return wrapped\n'
             '    }\n\n', ''),
        ],
    },
}


# Exact user-requested copy changes; every replacement remains required.
ALLOWED_TRANSLATION_UPDATES = {
    'Entry(". Drag to pan. Scroll or pinch to zoom. Right-click to place a pin. Arrow keys move the map.", "。拖动平移，滚动或捏合缩放，右键放置标记，方向键移动地图。", "。拖動平移，滾動或捏合縮放，右鍵放置標記，方向鍵移動地圖。", "。ドラッグで移動、スクロールまたはピンチで拡大縮小、右クリックでピンを配置、矢印キーでマップを移動します。")':
        'Entry(". Drag to pan. Scroll or pinch to zoom. Right-click to place or remove a pin. Click a pin to cycle yellow, green and player markers. Click terrain to recenter without changing zoom. Arrow keys move the map.", "。拖动平移，滚动或捏合缩放，右键放置或移除标记，点击标记切换黄色、绿色和玩家样式，点击地形居中并保持缩放，方向键移动地图。", "。拖動平移，滾動或捏合縮放，右鍵放置或移除標記，點擊標記切換黃色、綠色和玩家樣式，點擊地形置中並保持縮放，方向鍵移動地圖。", "。ドラッグで移動、スクロールまたはピンチで拡大縮小、右クリックでピンを配置または削除します。ピンをクリックすると黄色、緑色、プレイヤーの順に切り替わります。地形をクリックすると倍率を変えずに中央へ移動します。矢印キーでも移動できます。")',
    'Entry("Memory ", "内存 ", "記憶體 ", "メモリ ")':
        'Entry("RAM ", "RAM ", "RAM ", "RAM ")',
    'Entry("Memory", "内存", "記憶體", "メモリ")':
        'Entry("RAM", "RAM", "RAM", "RAM")',
}


# These are additive schema changes, not exemptions from the old contract.
# Reverse ONLY these exact reviewed additions, then require the result to match
# the original baseline hash. Even a newly approved whole-file hash cannot
# silently authorize changed old defaults, keys, paths or persistence behavior.
# Models uses the same existing Korean-normalization as its baseline hash.
ALLOWED_SCHEMA_EXTENSIONS = {
    "Sources/Models.swift": {
        "fields": ["clockStyle", "centerLogo", "centerLogoRevision", "alertMetric"],
        "tests": ["Tests/HUDBatchTwoConfigurationTests.swift"],
        "reverse": [
            ("    var clockStyle: HUDClockStyle = .digital\n"
             "    var centerLogo: HUDCenterLogo = .endfield\n"
             "    var centerLogoRevision: String? = nil\n"
             "    var alertMetric: HUDChargeMetric = .battery\n", ""),
            ("        result.centerLogoRevision = centerLogoRevision.flatMap { UUID(uuidString: $0)?.uuidString }\n", ""),
            ('            clockStyle: defaults.string(forKey: "clockStyle").flatMap(HUDClockStyle.init(rawValue:)) ?? .digital,\n'
             '            centerLogo: defaults.string(forKey: "centerLogo").flatMap(HUDCenterLogo.init(rawValue:)) ?? .endfield,\n'
             '            centerLogoRevision: defaults.string(forKey: "centerLogoRevision"),\n'
             '            alertMetric: defaults.string(forKey: "alertMetric").flatMap(HUDChargeMetric.init(rawValue:)) ?? .battery,\n', ""),
            ('        defaults.set(next.clockStyle.rawValue, forKey: "clockStyle")\n'
             '        defaults.set(next.centerLogo.rawValue, forKey: "centerLogo")\n'
             '        defaults.set(next.centerLogoRevision, forKey: "centerLogoRevision")\n'
             '        defaults.set(next.alertMetric.rawValue, forKey: "alertMetric")\n', ""),
        ],
    },
    "Sources/UserProfileStore.swift": {
        "fields": ["backgroundZoom", "thumbnailZoom"],
        "tests": ["Tests/UserProfileStoreTests.swift"],
        "reverse": [
            ("    var backgroundZoom: Double = 1\n", ""),
            ("    var thumbnailZoom: Double = 1\n", ""),
            ("        case backgroundWidth, backgroundZoom, backgroundOffsetX, backgroundOffsetY, thumbnailZoom, thumbnailOffsetX, thumbnailOffsetY\n",
             "        case backgroundWidth, backgroundOffsetX, backgroundOffsetY, thumbnailOffsetX, thumbnailOffsetY\n"),
            ("        backgroundZoom = try values.decodeIfPresent(Double.self, forKey: .backgroundZoom) ?? 1\n", ""),
            ("        thumbnailZoom = try values.decodeIfPresent(Double.self, forKey: .thumbnailZoom) ?? 1\n", ""),
            ("        value.backgroundZoom = backgroundZoom.isFinite ? min(20, max(1, backgroundZoom)) : 1\n", ""),
            ("        value.thumbnailZoom = thumbnailZoom.isFinite ? min(20, max(1, thumbnailZoom)) : 1\n", ""),
            ("                $0.backgroundZoom = 1\n", ""),
            ("                $0.thumbnailZoom = 1\n", ""),
            ("              (1...20).contains(profile.backgroundZoom), (1...20).contains(profile.thumbnailZoom),\n", ""),
        ],
    },
}


# New event kinds extend the version-1 vocabulary; every old enum, metadata
# rule, bounded storage limit and writer operation still reconstructs exactly.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"] = {
    "fields": ["displaySettingsChanged", "profileCropChanged"],
    "tests": ["Tests/SystemEventLogTests.swift", "Tests/SystemEventRecorderTests.swift"],
    "reverse": [
        ('    case displaySettingsChanged, profileCropChanged\n', ""),
        ('        case .displaySettingsChanged, .profileCropChanged: return .display\n', ""),
        ('        case .displaySettingsChanged: return "Display setting changed"\n        case .profileCropChanged: return "Profile crop changed"\n', ""),
        ('/// Closed metadata vocabulary: custom artwork identifiers, file paths and\n/// editable profile text never enter settings events.\nprivate enum SystemEventSettingsMetadata {\n    static let titles = ["clockStyle": "Clock style", "centerLogo": "Center logo", "alertMetric": "Charge metric"]\n    static let values = [\n        "clockStyle": ["digital": "Digital", "split": "Split", "dial": "Dial", "rail": "Rail", "stacked": "Stacked"],\n        "centerLogo": ["endfield": "Endfield", "rhodesIsland": "Rhodes Island", "babel": "Babel", "rhineLab": "Rhine Lab",\n                       "custom": "Custom", "customImported": "Custom artwork imported"],\n        "alertMetric": ["battery": "Battery", "ram": "RAM", "cpu": "CPU", "network": "Network", "disk": "Disk"],\n    ]\n    static let cropTargets = ["background": "Background", "thumbnail": "Thumbnail", "both": "Background and thumbnail"]\n}\n\n', ""),
        ('        if kind == .displaySettingsChanged, let field = metadata["field"], let value = metadata["value"],\n           let title = SystemEventSettingsMetadata.titles[field], let choice = SystemEventSettingsMetadata.values[field]?[value] {\n            fields.append(title); fields.append(choice)\n        }\n        if kind == .profileCropChanged, let target = metadata["target"].flatMap({ SystemEventSettingsMetadata.cropTargets[$0] }) {\n            fields.append(target)\n        }\n', ""),
        ('        case .displaySettingsChanged:\n            if let field = raw["field"], let value = raw["value"], SystemEventSettingsMetadata.values[field]?[value] != nil {\n                result = ["field": field, "value": value]\n            }\n        case .profileCropChanged: oneOf("target", Set(SystemEventSettingsMetadata.cropTargets.keys))\n', ""),
    ],
}


# Explicit map actions add no location or pin-identity metadata.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["mapPinStyleChanged", "mapRecentered"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"] += [
    ('    case mapPinStyleChanged, mapRecentered\n', ''),
    ('        case .mapPinStyleChanged, .mapRecentered: return .navigation\n', ''),
    ('        case .mapPinStyleChanged: return "Map pin style changed"\n        case .mapRecentered: return "Map recentered"\n', ''),
    ('        if kind == .mapPinStyleChanged, let style = metadata["style"],\n           let title = ["yellow": "Yellow", "green": "Green", "player": "Player"][style] {\n            fields.append(title)\n        }\n', ''),
    ('        case .mapPinStyleChanged: oneOf("style", ["yellow", "green", "player"])\n        case .mapRecentered: break\n', ''),
]

# Notes and playback add successful action names only. No note bodies, media
# paths, track titles, positions, credentials or app-observation data are allowed.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["noteAction", "playbackAction"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"] += [
    ('    case noteAction, playbackAction\n', ''),
    ('        case .noteAction: return .files\n        case .playbackAction: return .audio\n', ''),
    ('        case .noteAction: return "Note changed"\n        case .playbackAction: return "Playback changed"\n', ''),
    ('        if let action = metadata["action"], let title = [\n'
     '            "createdText": "Text added", "createdTODO": "Checklist added", "createdMedia": "Media added", "createdDrawing": "Drawing added",\n'
     '            "editedText": "Text edited", "formattedText": "Text formatted", "editedTODO": "Checklist edited", "drawingEdited": "Drawing edited",\n'
     '            "deletedNote": "Note deleted", "mediaPlayback": "Media playback", "playPause": "Play / pause", "previous": "Previous track",\n'
     '            "next": "Next track", "seek": "Seek"][action] { fields.append(title) }\n'
     '        if let source = metadata["source"], let name = ["music": "Music", "spotify": "Spotify", "netease": "NetEase Music", "qqMusic": "QQ Music", "kugou": "Kugou", "system": "Now Playing"][source] { fields.append(name) }\n', ''),
    ('        case .noteAction:\n'
     '            oneOf("action", ["createdText", "createdTODO", "createdMedia", "createdDrawing", "editedText", "formattedText", "editedTODO", "drawingEdited", "deletedNote", "mediaPlayback"])\n'
     '        case .playbackAction:\n'
     '            oneOf("action", ["playPause", "previous", "next", "seek"])\n'
     '            oneOf("source", ["music", "spotify", "netease", "qqMusic", "kugou", "system"])\n', ''),
]


# Projection adds session-action vocabulary only; remove before older action reversals.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["projectionAction"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"][0:0] = [('    case projectionAction\n', ''), ('        case .projectionAction: return .display\n', ''), ('        case .projectionAction: return "Projection changed"\n', ''), ('        if kind == .projectionAction, let action = metadata["action"], let title = [\n            "drawingEdited": "Drawing edited", "erased": "Drawing erased", "brushChanged": "Brush changed",\n            "backgroundChanged": "Background changed", "mediaAdded": "Media added", "mediaRemoved": "Media removed"][action] {\n            fields.append(title)\n        }\n', ''), ('        if kind != .projectionAction, let action = metadata["action"], let title = [\n', '        if let action = metadata["action"], let title = [\n'), ('        case .projectionAction:\n            oneOf("action", ["drawingEdited", "erased", "brushChanged", "backgroundChanged", "mediaAdded", "mediaRemoved"])\n', '')]


# Optional style on existing pins; remove these exact additions to recover every
# old map archive rule, path, migration and coordinate constraint unchanged.
ALLOWED_SCHEMA_EXTENSIONS["Sources/WorldMapStore.swift"] = {
    "fields": ["MapPin.style", "MapPinStyle"],
    "tests": ["Tests/WorldMapStoreTests.swift"],
    "reverse": [
        ('enum MapPinStyle: String, Codable, CaseIterable {\n    case yellow, green, player\n    var next: Self {\n        switch self {\n        case .yellow: return .green\n        case .green: return .player\n        case .player: return .yellow\n        }\n    }\n}\n\n', ''),
        ('    let style: MapPinStyle\n\n    init(id: UUID, x: Double, y: Double, createdAt: Date, style: MapPinStyle = .yellow) {\n        self.id = id; self.x = x; self.y = y; self.createdAt = createdAt; self.style = style\n    }\n    private enum CodingKeys: String, CodingKey { case id, x, y, createdAt, style }\n    init(from decoder: Decoder) throws {\n        let values = try decoder.container(keyedBy: CodingKeys.self)\n        id = try values.decode(UUID.self, forKey: .id)\n        x = try values.decode(Double.self, forKey: .x); y = try values.decode(Double.self, forKey: .y)\n        createdAt = try values.decode(Date.self, forKey: .createdAt)\n        // The additive field leaves all earlier version-1...4 pins readable.\n        style = try values.decodeIfPresent(MapPinStyle.self, forKey: .style) ?? .yellow\n    }\n', ''),
        ('    /// A style change is one explicit metadata edit. Preserve placement and\n    /// identity, and publish it only after the same atomic archive write succeeds.\n    @discardableResult\n    func cyclePinStyle(id: UUID) throws -> MapPin? {\n        guard let index = pins.firstIndex(where: { $0.id == id }) else { return nil }\n        let original = pins[index]\n        let updated = MapPin(id: original.id, x: original.x, y: original.y,\n                             createdAt: original.createdAt, style: original.style.next)\n        var next = pins; next[index] = updated\n        try commit(pins: next, viewport: viewport)\n        return updated\n    }\n\n', ''),
    ],
}



# Notes v2 adds only nullable validated payloads; reverse every exact migration,
# bind and read addition to recover the complete v1 store, including transactions,
# managed-image ownership, old kinds, data paths and corrupt/future protection.
ALLOWED_SCHEMA_EXTENSIONS["Sources/NotesStore.swift"] = {'fields': ['CanvasNote.richText',
            'CanvasNote.media',
            'CanvasNote.drawing',
            'NoteKind.drawing',
            'SQLite.user_version=2'],
 'tests': ['Tests/NotesStoreTests.swift',
           'Tests/NotesExtendedStoreTests.swift',
           'Tests/NotesRichTextTests.swift',
           'Tests/NotesDrawingTests.swift',
           'Tests/NotesMediaTests.swift'],
 'dependencies': ['Sources/NotesRichText.swift', 'Sources/NotesDrawing.swift', 'Sources/NotesMedia.swift'],
 'reverse': [('    case text, todo, image, drawing\n', '    case text, todo, image\n'),
             ('    var richText: NotesRichText?\n'
              '    var media: NotesMediaReference?\n'
              '    var drawing: NotesDrawing?\n',
              ''),
             ('         height: Double = 110, zIndex: Int = 0, createdAt: Date = Date(), isPinned: Bool = false,\n'
              '         richText: NotesRichText? = nil, media: NotesMediaReference? = nil, drawing: NotesDrawing? '
              '= nil) {\n',
              '         height: Double = 110, zIndex: Int = 0, createdAt: Date = Date(), isPinned: Bool = false) '
              '{\n'),
             ('        self.richText = richText; self.media = media; self.drawing = drawing\n', ''),
             ('            guard version <= 2 else { throw NotesStoreError.newerDatabase }\n',
              '            guard version <= 1 else { throw NotesStoreError.newerDatabase }\n'),
             ('                if version < 2 {\n'
              '                    // Verify every original row before any migration commits.\n'
              '                    _ = try readNotes(includePayloads: false)\n'
              '                    try execute("ALTER TABLE notes ADD COLUMN rich_text TEXT")\n'
              '                    try execute("ALTER TABLE notes ADD COLUMN media TEXT")\n'
              '                    try execute("ALTER TABLE notes ADD COLUMN drawing TEXT")\n'
              '                    try execute("PRAGMA user_version = 2")\n'
              '                }\n',
              '                if version == 0 { try execute("PRAGMA user_version = 1") }\n'),
             ('              Set(note.items.map(\\.id)).count == note.items.count,\n'
              '              note.richText.map({ note.kind == .text && $0.isValid(for: note.text) }) ?? true,\n'
              '              note.media.map({ note.kind == .image && $0.isValid }) ?? true,\n'
              '              note.drawing.map({ note.kind == .drawing && $0.isValid }) ?? (note.kind != '
              '.drawing)\n'
              '        else { throw NotesStoreError.invalidRecord }\n',
              '              Set(note.items.map(\\.id)).count == note.items.count else { throw '
              'NotesStoreError.invalidRecord }\n'),
             ('        guard note.kind != .image || note.imageName != nil || note.media != nil else { throw '
              'NotesStoreError.invalidImageName }\n',
              '        guard note.kind != .image || note.imageName != nil else { throw '
              'NotesStoreError.invalidImageName }\n'),
             ('        func payload<T: Encodable>(_ value: T?) throws -> String? {\n'
              '            try value.map { String(decoding: try JSONEncoder().encode($0), as: UTF8.self) }\n'
              '        }\n'
              '        let richText = try payload(note.richText), media = try payload(note.media), drawing = try '
              'payload(note.drawing)\n',
              ''),
             ('                INSERT INTO notes '
              '(id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned,rich_text,media,drawing)\n'
              '                VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)\n',
              '                INSERT INTO notes '
              '(id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned)\n'
              '                VALUES (?,?,?,?,?,?,?,?,?,?,?,?)\n'),
             ('                    '
              'z_index=excluded.z_index,created_at=excluded.created_at,is_pinned=excluded.is_pinned,\n'
              '                    rich_text=excluded.rich_text,media=excluded.media,drawing=excluded.drawing\n',
              '                    '
              'z_index=excluded.z_index,created_at=excluded.created_at,is_pinned=excluded.is_pinned\n'),
             ('                // Existing kind identifiers/check constraint remain byte-for-byte.\n'
              '                // Drawing is identified by its versioned payload on the text base.\n'
              '                try bind(note.kind == .drawing ? "text" : note.kind.rawValue, at: 2, to: '
              'statement)\n',
              '                try bind(note.kind.rawValue, at: 2, to: statement)\n'),
             ('                try bind(richText, at: 13, to: statement)\n'
              '                try bind(media, at: 14, to: statement)\n'
              '                try bind(drawing, at: 15, to: statement)\n',
              ''),
             ('    }\n'
              '\n'
              '    /// Descriptor preparation happens on the import worker; this commits only\n'
              '    /// the small validated reference and never copies the source media file.\n'
              '    func importMedia(reference: NotesMediaReference, at point: CGPoint, bounds: CGRect? = nil) '
              'throws -> CanvasNote {\n'
              '        guard reference.isValid else { throw NotesStoreError.invalidRecord }\n'
              '        let ratio = Double(reference.pixelHeight) / Double(max(1, reference.pixelWidth))\n'
              '        let width = min(300.0, 210 / max(0.2, ratio))\n'
              '        let nextZ = notes.map(\\.zIndex).max() ?? -1\n'
              '        let note = NotesGeometry.constrained(CanvasNote(kind: .image, x: Double(point.x), y: '
              'Double(point.y),\n'
              '            width: max(162, width), height: max(110, width * ratio + 50),\n'
              '            zIndex: nextZ < Int.max ? nextZ + 1 : nextZ, media: reference), in: bounds)\n'
              '        try upsert(note)\n'
              '        return note\n',
              ''),
             ('    private func readNotes(includePayloads: Bool = true) throws -> [CanvasNote] {\n',
              '    private func readNotes() throws -> [CanvasNote] {\n'),
             ('        let extras = includePayloads ? ",rich_text,media,drawing" : ""\n'
              '        try withStatement("SELECT '
              'id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned\\(extras) FROM '
              'notes ORDER BY z_index,created_at,id") { statement in\n',
              '        try withStatement("SELECT '
              'id,kind,text,checklist,image_name,x,y,width,height,z_index,created_at,is_pinned FROM notes ORDER '
              'BY z_index,created_at,id") { statement in\n'),
             ('                func payload<T: Decodable>(_ type: T.Type, at index: Int32) throws -> T? {\n'
              '                    guard includePayloads, sqlite3_column_type(statement, index) != SQLITE_NULL '
              'else { return nil }\n'
              '                    guard let raw = string(statement, index), let data = raw.data(using: .utf8),\n'
              '                          let value = try? JSONDecoder().decode(type, from: data) else { throw '
              'NotesStoreError.invalidRecord }\n'
              '                    return value\n'
              '                }\n'
              '                let rich = try payload(NotesRichText.self, at: 12)\n'
              '                let media = try payload(NotesMediaReference.self, at: 13)\n'
              '                let drawing = try payload(NotesDrawing.self, at: 14)\n'
              '                guard rich.map({ kind == .text && $0.isValid(for: text) }) ?? true,\n'
              '                      media.map({ kind == .image && $0.isValid }) ?? true,\n'
              '                      drawing.map({ kind == .text && rich == nil && $0.isValid }) ?? true,\n'
              '                      kind != .image || imageName != nil || media != nil,\n',
              '                guard kind != .image || imageName != nil,\n'),
             ('                result.append(NotesGeometry.constrained(CanvasNote(id: id, kind: drawing == nil ? '
              'kind : .drawing, text: text, items: items,\n',
              '                result.append(NotesGeometry.constrained(CanvasNote(id: id, kind: kind, text: text, '
              'items: items,\n'),
             ('                    isPinned: sqlite3_column_int(statement, 11) == 1, richText: rich, media: '
              'media, drawing: drawing)))\n',
              '                    isPinned: sqlite3_column_int(statement, 11) == 1)))\n')]}


# A new module adds a raw value without renaming or regrouping old identities.
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"] = {'fields': ['HUDModule.nowPlaying', 'HUDModule.projection'],
 'tests': ['Tests/HUDModuleTests.swift', 'Tests/NowPlayingTests.swift'],
 'reverse': [('    case projection\n', ''),
             ('        case .projection: return L10n.text("Projection", "投影")\n', ''),
             ('        case .projection: return "Projection"\n', ''),
             ('    case nowPlaying\n', ''),
             ('        case .nowPlaying: return L10n.text("Now Playing", "当前播放")\n', ''),
             ('        case .nowPlaying: return "Now Playing"\n', ''),
             ('        if self == .nowPlaying { return CGRect(x: 280, y: 100, width: 440, height: 440) }\n'
              '        return self == .workMode || self == .map ? CGRect(x: 280, y: 100, width: 440, height: 440)\n',
              '        self == .workMode || self == .map ? CGRect(x: 280, y: 100, width: 440, height: 440)\n'),
             ('        case .notes, .fileShelf, .clipboard, .volume, .workMode, .eventLog, .map, '
              '.nowPlaying, .projection, .addApp: return .right\n',
              '        case .notes, .fileShelf, .clipboard, .volume, .workMode, .eventLog, .map, .addApp: '
              'return .right\n')]}


# Batch 6: separate document stores, two additive navigation identities and closed action metadata.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["archiveAction", "readerAction", "projectionAction.cleared"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"][0:0] = [('    case archiveAction, readerAction\n', ''), ('        case .archiveAction, .readerAction: return .files\n', ''), ('        case .archiveAction: return "Archive changed"\n        case .readerAction: return "Reader changed"\n', ''), ('        if kind == .archiveAction || kind == .readerAction, let action = metadata["action"], let title = [\n            "created": "Document created", "edited": "Document edited", "deleted": "Document deleted",\n            "mediaAdded": "Media added", "mediaRemoved": "Media removed", "imported": "Document imported",\n            "bookmarked": "Bookmark changed", "progress": "Reading progress changed", "settings": "Reading settings changed"][action] {\n            fields.append(title)\n        }\n', ''), ('        case .archiveAction:\n            oneOf("action", ["created", "edited", "deleted", "mediaAdded", "mediaRemoved"])\n        case .readerAction:\n            oneOf("action", ["imported", "deleted", "bookmarked", "progress", "settings"])\n', ''), ('"mediaRemoved": "Media removed", "cleared": "Content cleared"', '"mediaRemoved": "Media removed"'), ('"backgroundChanged", "mediaAdded", "mediaRemoved", "cleared"', '"backgroundChanged", "mediaAdded", "mediaRemoved"')]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["fields"] += ["HUDModule.reader", "HUDModule.archive"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["reverse"][0:0] = [('    case projection, reader, archive\n', '    case projection\n'), ('        case .reader: return L10n.text("E-Reader", "阅读器")\n        case .archive: return L10n.text("Archive", "档案库")\n', ''), ('        case .reader: return "E-Reader"\n        case .archive: return "Archive"\n', ''), ('        if self == .reader || self == .archive { return CGRect(x: 300, y: 100, width: 400, height: 440) }\n', ''), ('.nowPlaying, .projection, .reader, .archive, .addApp: return .right', '.nowPlaying, .projection, .addApp: return .right')]

# Archive category actions expose only two closed action names, never user labels.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["archiveAction.categoryCreated", "archiveAction.categoryChanged"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"][0:0] = [
    ('"settings": "Reading settings changed",\n            "categoryCreated": "Category created", "categoryChanged": "Category changed"', '"settings": "Reading settings changed"'),
    ('"created", "edited", "deleted", "mediaAdded", "mediaRemoved", "categoryCreated", "categoryChanged"', '"created", "edited", "deleted", "mediaAdded", "mediaRemoved"')
]


# Batches 7/8 are additive tools with separate local/session stores.
ALLOWED_SCHEMA_EXTENSIONS['Sources/HUDModule.swift']["fields"] += ['HUDModule.mediaAssembly', 'HUDModule.calendar']
ALLOWED_SCHEMA_EXTENSIONS['Sources/HUDModule.swift']["reverse"][0:0] = [('case projection, reader, archive, mediaAssembly, calendar', 'case projection, reader, archive'), ('        case .mediaAssembly: return L10n.text("Media Assembly", "影像加工")\n        case .calendar: return L10n.text("Calendar", "日历")\n', ''), ('        case .mediaAssembly: return "Media Assembly"\n        case .calendar: return "Calendar"\n', ''), ('self == .reader || self == .archive || self == .calendar', 'self == .reader || self == .archive'), ('if self == .nowPlaying || self == .mediaAssembly {', 'if self == .nowPlaying {'), ('.projection, .reader, .archive, .mediaAssembly, .calendar, .addApp:', '.projection, .reader, .archive, .addApp:')]
ALLOWED_SCHEMA_EXTENSIONS['Sources/SystemEventLog.swift']["fields"] += ['mediaAssemblyAction', 'calendarAction', 'archiveAction.categoryDeleted']
ALLOWED_SCHEMA_EXTENSIONS['Sources/SystemEventLog.swift']["reverse"][0:0] = [('    case mediaAssemblyAction, calendarAction\n', ''), ('        case .mediaAssemblyAction: return .files\n        case .calendarAction: return .work\n', ''), ('        case .mediaAssemblyAction: return "Media changed"\n        case .calendarAction: return "Calendar changed"\n', ''), ('"categoryChanged": "Category changed", "categoryDeleted": "Category deleted"', '"categoryChanged": "Category changed"'), ('"categoryCreated", "categoryChanged", "categoryDeleted"]', '"categoryCreated", "categoryChanged"]'), ('        if kind == .mediaAssemblyAction, let action = metadata["action"], let title = [\n            "imported": "Media imported", "edited": "Media changed", "exported": "Media exported"][action] { fields.append(title) }\n        if kind == .calendarAction, let action = metadata["action"], let title = [\n            "created": "Event created", "edited": "Event edited", "deleted": "Event deleted"][action] { fields.append(title) }\n', ''), ('        case .mediaAssemblyAction: oneOf("action", ["imported", "edited", "exported"])\n        case .calendarAction: oneOf("action", ["created", "edited", "deleted"])\n', '')]

# Minigame adds a single navigation identity and three closed session actions.
# Reverse these before the earlier Media/Calendar additions so both previously
# reviewed whole-file contracts and the original baseline remain recoverable.
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["fields"] += ["HUDModule.minigame"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["reverse"][0:0] = [
    ('    case projection, reader, archive, mediaAssembly, calendar, minigame\n',
     '    case projection, reader, archive, mediaAssembly, calendar\n'),
    ('        case .minigame: return L10n.text("Closure\'s Minigame", "可露希尔的小游戏")\n', ''),
    ('        case .minigame: return "Closure\'s Minigame"\n', ''),
    ('        if self == .nowPlaying || self == .mediaAssembly || self == .minigame { return CGRect(x: 280, y: 100, width: 440, height: 440) }\n',
     '        if self == .nowPlaying || self == .mediaAssembly { return CGRect(x: 280, y: 100, width: 440, height: 440) }\n'),
    ('.projection, .reader, .archive, .mediaAssembly, .calendar, .minigame, .addApp: return .right',
     '.projection, .reader, .archive, .mediaAssembly, .calendar, .addApp: return .right'),
]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["minigameAction"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"][0:0] = [
    ('    case mediaAssemblyAction, calendarAction, minigameAction\n',
     '    case mediaAssemblyAction, calendarAction\n'),
    ('        case .minigameAction: return .work\n', ''),
    ('        case .minigameAction: return "Minigame"\n', ''),
    ('        if kind == .minigameAction, let action = metadata["action"], let title = ["started":"Game started", "restarted":"Game restarted", "finished":"Game finished"][action] { fields.append(title) }\n', ''),
    ('        case .minigameAction: oneOf("action", ["started", "restarted", "finished"])\n', ''),
]

# Opt-in public Apple Events permission and JIT for the bundled offline game.
# No other entitlement or usage-description addition is authorized.
ALLOWED_PERMISSION_ADDITIONS = {'infoPlist': {'NSAppleEventsUsageDescription': 'EndfieldHUD reads track information and controls playback '
                                                'in Music or Spotify when you connect that player.'},
 'localized': {'Resources/en.lproj/InfoPlist.strings': {'addition': '"NSAppleEventsUsageDescription" = '
                                                                    '"EndfieldHUD reads track information '
                                                                    'and controls playback in Music or '
                                                                    'Spotify when you connect that '
                                                                    'player.";\n',
                                                        'baselineSha256': 'd3aaa7a86d709411107130ef8800253d60ede165b815c5a9c0e4c93f02ac52a0'},
               'Resources/zh-Hans.lproj/InfoPlist.strings': {'addition': '"NSAppleEventsUsageDescription" = '
                                                                         '"当您连接 Music 或 Spotify '
                                                                         '时，EndfieldHUD '
                                                                         '会读取该播放器的曲目信息并控制播放。";\n',
                                                             'baselineSha256': '5a04b4cdc9b8df673df63f49dd1493c5d96b13b223f32edb611bc9eafe57f99b'},
               'Resources/zh-Hant.lproj/InfoPlist.strings': {'addition': '"NSAppleEventsUsageDescription" = '
                                                                         '"當您連線至 Music 或 Spotify '
                                                                         '時，EndfieldHUD '
                                                                         '會讀取該播放器的曲目資訊並控制播放。";\n',
                                                             'baselineSha256': '3002f2eebc30063733f12116ca3843dc7265150f7aee01aaacd441668bddda70'},
               'Resources/ja.lproj/InfoPlist.strings': {'addition': '"NSAppleEventsUsageDescription" = '
                                                                    '"MusicまたはSpotifyに接続すると、EndfieldHUDがそのプレーヤーの曲情報を読み取り、再生を操作します。";\n',
                                                        'baselineSha256': 'eb85071d1762a0d7f9948661afee0c3c198b80c746d9b1f41bf3445abffac7fd'},
               'Resources/ko.lproj/InfoPlist.strings': {'addition': '"NSAppleEventsUsageDescription" = '
                                                                    '"Music 또는 Spotify에 연결하면 EndfieldHUD가 해당 '
                                                                    '플레이어의 곡 정보를 읽고 재생을 제어합니다.";\n',
                                                        'baselineSha256': '322f67a99daccaa21c48ddc1669a2c053cca628b5725a8763cc1778bd9da0d7b'}},
 'entitlements': {'path': 'Resources/EndfieldHUD.entitlements',
                  'values': {'com.apple.security.automation.apple-events': True,
                             'com.apple.security.cs.allow-jit': True}}}

# Account linking extends local display identity without replacing the stable UID.
ALLOWED_SCHEMA_EXTENSIONS["Sources/UserProfileStore.swift"]["fields"] += ["gamePlayerID", "playerIDOverride", "hasManualAwakeningDate"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/UserProfileStore.swift"]["tests"] += ["Tests/HypergryphAccountControllerTests.swift"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/UserProfileStore.swift"]["reverse"][0:0] = [('    // The original local UID remains stable; game identity and explicit edits are additive.\n    var gamePlayerID: String?\n    var playerIDOverride: String?\n    var hasManualAwakeningDate = false\n    var displayedUID: String { playerIDOverride ?? gamePlayerID ?? uid }\n', ''), ('        case gamePlayerID, playerIDOverride, hasManualAwakeningDate\n', ''), ('        gamePlayerID = try values.decodeIfPresent(String.self, forKey: .gamePlayerID)\n        playerIDOverride = try values.decodeIfPresent(String.self, forKey: .playerIDOverride)\n        hasManualAwakeningDate = try values.decodeIfPresent(Bool.self, forKey: .hasManualAwakeningDate) ?? false\n', ''), ('        value.playerIDOverride = playerIDOverride.flatMap { raw in\n            let clean = String(raw.filter { !$0.isNewline }.trimmingCharacters(in: .whitespaces).prefix(64))\n            return clean.isEmpty ? nil : clean\n        }\n', '')]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["fields"] += ["HUDModule.account"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["tests"] += ["Tests/HUDAccountCanvasTests.swift", "Tests/HypergryphAccountControllerTests.swift"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/HUDModule.swift"]["reverse"][0:0] = [('    case nowPlaying, account\n', '    case nowPlaying\n'), ('        case .account: return L10n.text("Account Linking", "账户绑定")\n', ''), ('        case .account: return "Account Linking"\n', ''), ('.calendar, .minigame, .account, .addApp: return .right', '.calendar, .minigame, .addApp: return .right')]

# Account actions deliberately accept no identifiers, credentials or server strings.
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["fields"] += ["accountAction"]
ALLOWED_SCHEMA_EXTENSIONS["Sources/SystemEventLog.swift"]["reverse"][0:0] = [('    case accountAction\n', ''), ('        case .accountAction: return .display\n', ''), ('        case .accountAction: return "Account changed"\n', ''), ('        if kind == .accountAction, let action = metadata["action"], let title = ["linked":"Account linked", "unlinked":"Account unlinked", "synced":"Profile refreshed", "settings":"Account settings changed"][action] { fields.append(title) }\n', ''), ('        case .accountAction: oneOf("action", ["linked", "unlinked", "synced", "settings"])\n', '')]

def contract_data(name, data):
    if name == "Sources/Models.swift":
        # Add one language without exempting earlier language values.
        return data.replace(b"    case japanese\n    case korean\n", b"    case japanese\n", 1)
    return data


def entries(source):
    return {line.strip().removesuffix(",") for line in source.splitlines()
            if line.strip().startswith("Entry(")}


def check(root, baseline):
    failures = []
    updates = baseline.get("reviewedBehaviorUpdates", {})
    extensions = baseline.get("reviewedSchemaExtensions", {})
    permissions = baseline.get("reviewedPermissionAdditions", {})
    for name, repair in ALLOWED_METADATA_REPAIRS.items():
        if baseline["files"].get(name) != repair["baselineSha256"]:
            failures.append(f"Original metadata-repair baseline changed: {name}")
    # Persistence files cannot be exempted by these explicit behavior changes.
    allowed_updates = {"Sources/WorkModeFocusController.swift", "Sources/WorldMapGeometry.swift",
                       "Sources/AppActivityMonitor.swift", "Sources/SystemActivityMonitor.swift",
                       "Sources/Localization.swift", "Sources/HUDSettingsController.swift",
                       "Sources/LoginItemManager.swift", "Sources/BatteryMonitor.swift",
                       "Sources/SystemEventRecorder.swift", "Sources/DisplayPolicy.swift"}
    for name, update in updates.items():
        if name not in allowed_updates or update.get("baselineSha256") != baseline["files"].get(name) or not update.get("reason"):
            failures.append(f"Invalid reviewed behavior update: {name}")
    for name, extension in extensions.items():
        policy = ALLOWED_SCHEMA_EXTENSIONS.get(name)
        required_keys = {"baselineSha256", "sha256", "reason", "fields", "tests"}
        if policy and policy.get("dependencies"):
            required_keys.add("dependencies")
        if (policy is None or name not in baseline["files"]
                or extension.get("baselineSha256") != baseline["files"].get(name)
                or not extension.get("reason")
                or extension.get("fields") != policy["fields"]
                or extension.get("tests") != policy["tests"]
                or set(extension) != required_keys
                or set(extension.get("dependencies", {})) != set((policy or {}).get("dependencies", []))):
            failures.append(f"Invalid reviewed schema extension: {name}")
        for dependency, digest in extension.get("dependencies", {}).items():
            source = root / dependency
            if not source.is_file() or hashlib.sha256(source.read_bytes()).hexdigest() != digest:
                failures.append(f"Reviewed schema validation dependency changed: {name}: {dependency}")
    for name, expected in baseline["files"].items():
        if name in updates and name in allowed_updates:
            expected = updates[name]["sha256"]
        if name in extensions and name in ALLOWED_SCHEMA_EXTENSIONS:
            expected = extensions[name].get("sha256")
        if name in ALLOWED_PERMISSION_ADDITIONS["localized"]:
            expected = permissions.get("localized", {}).get(name, {}).get("sha256", expected)
        source = root / name
        if not source.is_file():
            failures.append(f"Missing stable functional file: {name}")
        else:
            data = contract_data(name, source.read_bytes())
            if name in ALLOWED_METADATA_REPAIRS:
                for replacement, previous in ALLOWED_METADATA_REPAIRS[name]["reverse"]:
                    replacement, previous = replacement.encode(), previous.encode()
                    if data.count(replacement) != 1:
                        failures.append(f"Reviewed metadata repair missing or duplicated: {name}")
                    data = data.replace(replacement, previous, 1)
            if hashlib.sha256(data).hexdigest() != expected:
                failures.append(f"Stable behavior/data contract changed: {name}")
            if name in extensions and name in ALLOWED_SCHEMA_EXTENSIONS:
                original = data
                for addition, previous in ALLOWED_SCHEMA_EXTENSIONS[name]["reverse"]:
                    addition, previous = addition.encode(), previous.encode()
                    if original.count(addition) != 1:
                        failures.append(f"Reviewed schema addition missing or duplicated: {name}")
                    original = original.replace(addition, previous, 1)
                if hashlib.sha256(original).hexdigest() != baseline["files"][name]:
                    failures.append(f"Original persistence contract changed under schema extension: {name}")
    try:
        info = plistlib.loads((root / "Resources/Info.plist").read_bytes())
        for key, expected in baseline["infoPlist"].items():
            if key in RELEASE_METADATA:
                expected = RELEASE_METADATA[key]
            if key == "CFBundleLocalizations":
                expected = expected + ["ko"]
            if info.get(key) != expected:
                failures.append(f"Stable application/update identity changed: {key}")
        if info.get("HUDReleaseTag") != "v" + RELEASE_METADATA["CFBundleShortVersionString"]:
            failures.append("Release tag does not match the approved version: HUDReleaseTag")
        for key, expected in ALLOWED_PERMISSION_ADDITIONS["infoPlist"].items():
            if info.get(key) != expected:
                failures.append(f"Approved automation usage description changed: {key}")
        allowed_usage = set(baseline["infoPlist"]) | set(ALLOWED_PERMISSION_ADDITIONS["infoPlist"])
        if any(key.startswith("NS") and key.endswith("UsageDescription") and key not in allowed_usage for key in info):
            failures.append("Unreviewed application permission usage description")
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        failures.append(f"Cannot inspect Info.plist: {error}")
    if (set(permissions) != {"reason", "tests", "infoPlist", "localized", "entitlements"}
            or not permissions.get("reason") or permissions.get("tests") != ["Tests/NowPlayingTests.swift"]
            or permissions.get("infoPlist") != ALLOWED_PERMISSION_ADDITIONS["infoPlist"]
            or set(permissions.get("localized", {})) != set(ALLOWED_PERMISSION_ADDITIONS["localized"])):
        failures.append("Invalid reviewed permission addition")
    for name, policy in ALLOWED_PERMISSION_ADDITIONS["localized"].items():
        entry = permissions.get("localized", {}).get(name, {})
        if (set(entry) != {"addition", "baselineSha256", "sha256"}
                or entry.get("addition") != policy["addition"]
                or entry.get("baselineSha256") != policy["baselineSha256"]):
            failures.append(f"Invalid reviewed localized permission addition: {name}")
        try:
            data = (root / name).read_bytes()
            addition = policy["addition"].encode()
            if (hashlib.sha256(data).hexdigest() != entry.get("sha256") or data.count(addition) != 1
                    or hashlib.sha256(data.replace(addition, b"", 1)).hexdigest() != policy["baselineSha256"]):
                failures.append(f"Original localized permission contract changed: {name}")
        except OSError as error:
            failures.append(f"Cannot inspect localized permission: {name}: {error}")
    entitlement_policy = ALLOWED_PERMISSION_ADDITIONS["entitlements"]
    entitlement = permissions.get("entitlements", {})
    if (set(entitlement) != {"path", "values", "sha256"} or entitlement.get("path") != entitlement_policy["path"]
            or entitlement.get("values") != entitlement_policy["values"]):
        failures.append("Invalid reviewed hardened-runtime entitlement")
    try:
        data = (root / entitlement_policy["path"]).read_bytes()
        if (hashlib.sha256(data).hexdigest() != entitlement.get("sha256")
                or plistlib.loads(data) != entitlement_policy["values"]):
            failures.append("Approved hardened-runtime entitlement changed")
    except (OSError, ValueError, plistlib.InvalidFileException) as error:
        failures.append(f"Cannot inspect hardened-runtime entitlement: {error}")
    try:
        current = entries((root / "Sources/LocalizationCatalog.swift").read_text())
        translation_updates = baseline.get("reviewedTranslationUpdates", {})
        for original, update in translation_updates.items():
            if (original not in baseline["translations"]
                    or original not in ALLOWED_TRANSLATION_UPDATES
                    or update.get("entry") != ALLOWED_TRANSLATION_UPDATES[original]
                    or not update.get("reason")):
                failures.append(f"Invalid reviewed translation update: {original[:100]}")
        for entry in baseline["translations"]:
            replacement = translation_updates.get(entry, {}).get("entry", entry)
            if replacement not in current:
                failures.append(f"Stable translated text changed or removed: {entry[:100]}")
    except OSError as error:
        failures.append(f"Cannot inspect localization catalog: {error}")
    return failures


def self_test(baseline):
    # Mutations stay entirely inside this owned temporary checkout fixture.
    schema_checks = 0
    metadata_checks = 0
    with tempfile.TemporaryDirectory(prefix="EndfieldHUD-Compatibility-") as temporary:
        root = Path(temporary)
        names = list(baseline["files"]) + ["Resources/Info.plist", "Sources/LocalizationCatalog.swift"]
        names += [dependency for extension in baseline.get("reviewedSchemaExtensions", {}).values()
                  for dependency in extension.get("dependencies", {})]
        names += list(ALLOWED_PERMISSION_ADDITIONS["localized"]) + [ALLOWED_PERMISSION_ADDITIONS["entitlements"]["path"]]
        for name in names:
            destination = root / name
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, destination)
        assert not check(root, baseline), "Current repository must pass before mutation checks"
        for name, repair in ALLOWED_METADATA_REPAIRS.items():
            source = root / name
            original = source.read_bytes()
            for replacement, previous in repair["reverse"]:
                replacement, previous = replacement.encode(), previous.encode()
                for altered in (original.replace(replacement, previous, 1),
                                original.replace(replacement, replacement + replacement, 1)):
                    source.write_bytes(altered)
                    assert any("metadata repair missing or duplicated" in failure for failure in check(root, baseline)), "Every exact metadata repair remains required once"
                    metadata_checks += 1
                    forged = json.loads(json.dumps(baseline))
                    forged["files"][name] = hashlib.sha256(altered).hexdigest()
                    assert any("metadata-repair baseline changed" in failure for failure in check(root, forged)), "A forged file hash cannot hide a removed or duplicated repair"
                    metadata_checks += 1
                    source.write_bytes(original)
            for old, new in ((b"EndfieldCharge/AppShortcuts", b"EndfieldHUD/AppShortcuts"),
                             (b"Archive(version: 1, items: next)", b"Archive(version: 2, items: next)"),
                             (b"try data.write(to: fileURL, options: .atomic)", b"try data.write(to: fileURL, options: [])")):
                assert original.count(old) == 1, "Metadata repair mutation must touch its intended persistence contract"
                source.write_bytes(original.replace(old, new, 1))
                assert any(f"Stable behavior/data contract changed: {name}" in failure for failure in check(root, baseline)), "Metadata repair cannot authorize changed storage paths, archive versions or atomic writes"
                metadata_checks += 1
                forged = json.loads(json.dumps(baseline))
                forged["reviewedBehaviorUpdates"][name] = {
                    "baselineSha256": baseline["files"][name], "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                    "reason": "Attempted whole-store exemption"}
                assert any(f"Invalid reviewed behavior update: {name}" in failure for failure in check(root, forged)), "The repaired shortcut store remains ineligible for a whole-file behavior exemption"
                metadata_checks += 1
                source.write_bytes(original)
            forged = json.loads(json.dumps(baseline))
            forged["files"].pop(name)
            assert any("metadata-repair baseline changed" in failure for failure in check(root, forged)), "The original shortcut store cannot be removed from the manifest"
            metadata_checks += 1
        store = root / "Sources/UserProfileStore.swift"
        original = store.read_bytes()
        store.write_bytes(original.replace(b"EndfieldCharge/Profile", b"EndfieldHUD/Profile"))
        assert any("UserProfileStore" in failure for failure in check(root, baseline)), "A changed data path must fail"
        store.write_bytes(original)
        plist = root / "Resources/Info.plist"
        original = plist.read_bytes()
        values = plistlib.loads(original)
        values["CFBundleIdentifier"] = "example.incompatible"
        plist.write_bytes(plistlib.dumps(values))
        assert any("CFBundleIdentifier" in failure for failure in check(root, baseline)), "A changed defaults/permission identity must fail"
        plist.write_bytes(original)
        for key, invalid in (("CFBundleVersion", "999"), ("HUDReleaseTag", "v0.0.0"),
                             ("SUFeedURL", "https://example.invalid/appcast.xml")):
            values = plistlib.loads(original)
            values[key] = invalid
            plist.write_bytes(plistlib.dumps(values))
            assert any(key in failure for failure in check(root, baseline)), "Release metadata and update identity remain guarded"
            plist.write_bytes(original)
        values = plistlib.loads(original)
        values["CFBundleLocalizations"].remove("en")
        plist.write_bytes(plistlib.dumps(values))
        assert any("CFBundleLocalizations" in failure for failure in check(root, baseline)), "Adding Korean cannot remove an existing bundle language"
        plist.write_bytes(original)
        models = root / "Sources/Models.swift"
        original_models = models.read_bytes()
        models.write_bytes(original_models.replace(b'case english', b'case renamedEnglish', 1))
        assert any("Models.swift" in failure for failure in check(root, baseline)), "Existing stored language values remain guarded"
        models.write_bytes(original_models)
        catalog = root / "Sources/LocalizationCatalog.swift"
        original = catalog.read_text()
        catalog.write_text(original.replace(baseline["translations"][0], "", 1))
        assert any("translated text" in failure for failure in check(root, baseline)), "Removed stable copy must fail"
        catalog.write_text(original + '\nEntry("Additional text", "新增", "新增", "追加"),\n')
        assert not check(root, baseline), "Additional integration strings may coexist with all stable translations"
        for name in baseline.get("reviewedBehaviorUpdates", {}):
            source = root / name
            original = source.read_bytes()
            source.write_bytes(original + b"\n// unreviewed mutation\n")
            assert any(name in failure for failure in check(root, baseline)), "Reviewed behavior changes remain hash guarded"
            source.write_bytes(original)
        catalog_original = catalog.read_text()
        for update in baseline.get("reviewedTranslationUpdates", {}).values():
            catalog.write_text(catalog_original.replace(update["entry"], "", 1))
            assert any("translated text" in failure for failure in check(root, baseline)), "Each reviewed replacement remains required"
            catalog.write_text(catalog_original)
        unauthorized = json.loads(json.dumps(baseline))
        protected = "Sources/UserProfileStore.swift"
        unauthorized["reviewedBehaviorUpdates"][protected] = {
            "baselineSha256": baseline["files"][protected], "sha256": baseline["files"][protected], "reason": "Test exemption"}
        assert any("Invalid reviewed behavior update" in failure for failure in check(root, unauthorized)), "Persistence files cannot be exempted"
        unauthorized = json.loads(json.dumps(baseline))
        original = next(iter(unauthorized["reviewedTranslationUpdates"]))
        unauthorized["reviewedTranslationUpdates"][original]["entry"] = 'Entry("Arbitrary", "任意", "任意", "任意")'
        assert any("Invalid reviewed translation update" in failure for failure in check(root, unauthorized)), "Unreviewed copy replacements cannot be exempted"
        for name in baseline.get("reviewedSchemaExtensions", {}):
            source = root / name
            original = source.read_bytes()
            source.write_bytes(original + b"\n// unreviewed schema mutation\n")
            assert any(name in failure for failure in check(root, baseline)), "Reviewed extensions remain whole-file hash guarded"
            schema_checks += 1
            source.write_bytes(original)
            for key, value in (("sha256", "0" * 64), ("baselineSha256", "0" * 64),
                               ("tests", []), ("fields", ["unreviewedField"])):
                unauthorized = json.loads(json.dumps(baseline))
                unauthorized["reviewedSchemaExtensions"][name][key] = value
                assert any(name in failure for failure in check(root, unauthorized)), "Schema hash, baseline provenance, fields and regressions are required"
                schema_checks += 1
            # A substituted whole-file hash cannot hide removal of a promised
            # additive field or a rewrite to an existing persistence contract.
            addition = ALLOWED_SCHEMA_EXTENSIONS[name]["reverse"][0][0].encode()
            source.write_bytes(original.replace(addition, b"", 1))
            unauthorized = json.loads(json.dumps(baseline))
            unauthorized["reviewedSchemaExtensions"][name]["sha256"] = hashlib.sha256(contract_data(name, source.read_bytes())).hexdigest()
            assert any("schema addition missing" in failure for failure in check(root, unauthorized)), "A changed hash cannot hide a missing reviewed extension"
            schema_checks += 1
            source.write_bytes(original)
        for name, old, new in (("Sources/Models.swift", b"case english", b"case renamedEnglish"),
                               ("Sources/UserProfileStore.swift", b"EndfieldCharge/Profile", b"EndfieldHUD/Profile"),
                               ("Sources/NotesStore.swift", b"EndfieldCharge/Notes", b"EndfieldHUD/Notes"),
                               ("Sources/HUDModule.swift", b"CGRect(x: 300, y: 152, width: 400, height: 334)", b"CGRect(x: 300, y: 152, width: 400, height: 335)"),
                               ("Sources/SystemEventLog.swift", b"case overlayOpened, moduleOpened", b"case renamedOverlayOpened, moduleOpened")):
            source = root / name
            original = source.read_bytes()
            source.write_bytes(original.replace(old, new, 1))
            unauthorized = json.loads(json.dumps(baseline))
            unauthorized["reviewedSchemaExtensions"][name]["sha256"] = hashlib.sha256(contract_data(name, source.read_bytes())).hexdigest()
            assert any("Original persistence contract changed" in failure for failure in check(root, unauthorized)), "Replacing an extension hash cannot authorize changed old keys or data paths"
            schema_checks += 1
            source.write_bytes(original)
        for name, extension in baseline.get("reviewedSchemaExtensions", {}).items():
            for dependency in extension.get("dependencies", {}):
                source = root / dependency
                original = source.read_bytes()
                source.write_bytes(original + b"\n// unreviewed payload validation mutation\n")
                assert any("validation dependency changed" in failure for failure in check(root, baseline)), "New schema validators remain individually hash guarded"
                source.write_bytes(original)
                unauthorized = json.loads(json.dumps(baseline))
                unauthorized["reviewedSchemaExtensions"][name]["dependencies"][dependency] = "0" * 64
                assert any("validation dependency changed" in failure for failure in check(root, unauthorized)), "Forged validator hashes fail"
                schema_checks += 2
        unauthorized = json.loads(json.dumps(baseline))
        protected = "Sources/FileShelfStore.swift"
        unauthorized["reviewedSchemaExtensions"][protected] = {
            "baselineSha256": baseline["files"][protected], "sha256": baseline["files"][protected],
            "reason": "Test exemption", "fields": ["anything"], "tests": ["anything"]}
        assert any("Invalid reviewed schema extension" in failure for failure in check(root, unauthorized)), "Schema approval is restricted to the explicitly reviewed files"
        schema_checks += 1
        for name, policy in ALLOWED_PERMISSION_ADDITIONS["localized"].items():
            source = root / name
            original = source.read_bytes()
            source.write_bytes(original.replace(policy["addition"].encode(), b"", 1))
            assert any("localized permission" in failure for failure in check(root, baseline)), "Every approved localized usage description remains required"
            source.write_bytes(original.replace(b"NSAudioCaptureUsageDescription", b"ChangedAudioPermission", 1))
            forged = json.loads(json.dumps(baseline))
            forged["reviewedPermissionAdditions"]["localized"][name]["sha256"] = hashlib.sha256(source.read_bytes()).hexdigest()
            assert any("Original localized permission contract changed" in failure for failure in check(root, forged)), "An approved new key cannot authorize changed old audio permission text"
            source.write_bytes(original)
            schema_checks += 2
        entitlement = root / ALLOWED_PERMISSION_ADDITIONS["entitlements"]["path"]
        original = entitlement.read_bytes()
        altered = plistlib.loads(original); altered["com.apple.security.get-task-allow"] = True
        entitlement.write_bytes(plistlib.dumps(altered))
        forged = json.loads(json.dumps(baseline))
        forged["reviewedPermissionAdditions"]["entitlements"]["sha256"] = hashlib.sha256(entitlement.read_bytes()).hexdigest()
        assert any("entitlement changed" in failure for failure in check(root, forged)), "An extra entitlement cannot hide behind an updated source hash"
        entitlement.write_bytes(original)
        for key in ALLOWED_PERMISSION_ADDITIONS["entitlements"]["values"]:
            for removed in (False, True):
                altered = plistlib.loads(original)
                if removed:
                    altered.pop(key)
                else:
                    altered[key] = False
                entitlement.write_bytes(plistlib.dumps(altered))
                forged = json.loads(json.dumps(baseline))
                forged["reviewedPermissionAdditions"]["entitlements"]["sha256"] = hashlib.sha256(entitlement.read_bytes()).hexdigest()
                assert any("entitlement changed" in failure for failure in check(root, forged)), "Approved Apple Events and JIT entitlements cannot be removed or disabled by forging the file hash"
                entitlement.write_bytes(original)
                schema_checks += 1
        plist = root / "Resources/Info.plist"
        original = plist.read_bytes(); values = plistlib.loads(original)
        values["NSCameraUsageDescription"] = "Unreviewed camera access"
        plist.write_bytes(plistlib.dumps(values))
        assert any("Unreviewed application permission" in failure for failure in check(root, baseline)), "Only the approved additional usage permission is allowed"
        values.pop("NSCameraUsageDescription"); values.pop("NSAppleEventsUsageDescription")
        plist.write_bytes(plistlib.dumps(values))
        assert any("automation usage description" in failure for failure in check(root, baseline)), "The public Apple Events usage purpose is required"
        plist.write_bytes(original)
        schema_checks += 3
    count = 11 + len(baseline.get("reviewedBehaviorUpdates", {})) + len(baseline.get("reviewedTranslationUpdates", {}))
    print(f"Passed {count + schema_checks + metadata_checks} isolated compatibility-guard mutation checks.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--self-test", action="store_true", help="Verify guard failures in temporary copies")
    parser.add_argument("--behavioral", action="store_true", help="Also compile/run the existing isolated core tests")
    args = parser.parse_args()
    baseline = json.loads(MANIFEST.read_text())
    failures = check(ROOT, baseline)
    if failures:
        for failure in failures:
            print(f"FAIL: {failure}")
        print("Review against the stable baseline; do not regenerate hashes to bypass a regression.")
        return 1
    print(f"Stable {baseline['baselineCommit'][:7]} contract preserved: "
          f"{len(baseline['files'])} guarded functional/localization files "
          f"({len(baseline.get('reviewedBehaviorUpdates', {}))} explicitly reviewed behavior updates, "
          f"{len(baseline.get('reviewedSchemaExtensions', {}))} additive schema extensions and "
          f"{len(ALLOWED_METADATA_REPAIRS)} exact metadata repairs with original-contract reconstruction), "
          f"{len(baseline['translations'])} translated entries "
          f"({len(baseline.get('reviewedTranslationUpdates', {}))} exact reviewed replacements), and bundle/update identity.")
    if args.self_test:
        self_test(baseline)
    if args.behavioral:
        return subprocess.call(["bash", str(ROOT / "scripts/test.sh")], cwd=ROOT)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
