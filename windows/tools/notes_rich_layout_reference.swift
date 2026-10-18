// Compiled in the same generated file as unchanged private source helpers.
// This exporter serializes native facts; it does not implement a layout oracle.
final class FixtureEditor: NSTextView {
    let fixtureUndo = UndoManager(); var richEnabled = false
    override var undoManager: UndoManager? { fixtureUndo }
}

@main enum NotesRichLayoutReference {
    static func require(_ value: @autoclosure () -> Bool, _ message: String) throws {
        if !value() { throw NSError(domain: "NotesRichLayoutReference", code: 1, userInfo: [NSLocalizedDescriptionKey: message]) }
    }
    static func encoded<T: Encodable>(_ value: T) throws -> Any { try JSONSerialization.jsonObject(with: JSONEncoder().encode(value)) }
    static func rect(_ r: CGRect) -> [CGFloat] { [r.minX,r.minY,r.width,r.height] }
    static func range(_ r: NSRange) -> [Int] { [r.location,r.length] }
    static func paragraph(_ p: NSParagraphStyle?) -> Any {
        guard let p else { return NSNull() }
        return ["minimumLineHeight":p.minimumLineHeight,"maximumLineHeight":p.maximumLineHeight,"lineSpacing":p.lineSpacing,
            "paragraphSpacing":p.paragraphSpacing,"paragraphSpacingBefore":p.paragraphSpacingBefore,
            "firstLineHeadIndent":p.firstLineHeadIndent,"headIndent":p.headIndent,"tailIndent":p.tailIndent,
            "alignment":p.alignment.rawValue,"lineBreakMode":p.lineBreakMode.rawValue] as [String:Any]
    }
    static func font(_ f: NSFont) -> [String:Any] {
        ["fontName":f.fontName,"familyName":f.familyName as Any? ?? NSNull(),"pointSize":f.pointSize,
         "traits":NSFontManager.shared.traits(of:f).rawValue,"ascender":f.ascender,"descender":f.descender,"leading":f.leading,
         "isPrivateSystemName":f.fontName.hasPrefix(".")]
    }
    static func attributes(_ text: NSAttributedString) -> [[String:Any]] {
        var rows:[[String:Any]]=[]
        text.enumerateAttributes(in:NSRange(location:0,length:text.length)) { a,r,_ in
            rows.append(["utf16Range":range(r),"font":(a[.font] as? NSFont).map(font) as Any? ?? NSNull(),
                "foreground":(a[.foregroundColor] as? NSColor).flatMap(NotesRGBA.init).map { [$0.red,$0.green,$0.blue,$0.alpha] } as Any? ?? NSNull(),
                "paragraph":paragraph(a[.paragraphStyle] as? NSParagraphStyle),"underline":(a[.underlineStyle] as? NSNumber)?.intValue ?? 0,
                "strikethrough":(a[.strikethroughStyle] as? NSNumber)?.intValue ?? 0])
        };return rows
    }
    static func settled(_ text: String, width: CGFloat, rich: NotesRichText?) -> [[String:Any]] {
        let wrapped=NotesWrappedText(text:text,width:width,fontSize:12,richText:rich)
        return wrapped.ranges.indices.map { n in
            let line=wrapped.attributedLine(at:n,defaultColor:.white,strikethrough:false)
            let native=CTLineCreateWithAttributedString(line),runs=CTLineGetGlyphRuns(native) as! [CTRun]
            let fonts=runs.map { run -> [String:Any] in
                let a=CTRunGetAttributes(run) as NSDictionary
                let actual=a[kCTFontAttributeName] as! CTFont
                let r=CTRunGetStringRange(run)
                return ["utf16RangeInVisibleLine":[r.location,r.length],"actualPostScriptName":CTFontCopyPostScriptName(actual) as String,
                    "actualFamilyName":CTFontCopyFamilyName(actual) as String,"pointSize":CTFontGetSize(actual),
                    "ascent":CTFontGetAscent(actual),"descent":CTFontGetDescent(actual),"leading":CTFontGetLeading(actual)]
            }
            return ["utf16Range":range(wrapped.ranges[n]),"visibleText":wrapped.string(at:n),"y":wrapped.origins[n],"height":wrapped.heights[n],
                "attributedRuns":attributes(line),"coreTextGlyphFonts":fonts]
        }
    }
    static func textKit(_ editor: FixtureEditor) throws -> [String:Any] {
        guard let manager=editor.layoutManager,let container=editor.textContainer else { throw NSError(domain:"NotesRichLayoutReference",code:2) }
        manager.ensureLayout(for:container)
        var lines:[[String:Any]]=[]
        manager.enumerateLineFragments(forGlyphRange:NSRange(location:0,length:manager.numberOfGlyphs)) { fragment,used,_,glyphRange,_ in
            let chars=manager.characterRange(forGlyphRange:glyphRange,actualGlyphRange:nil)
            let count=manager.getLineFragmentInsertionPoints(forCharacterAt:chars.location,alternatePositions:false,inDisplayOrder:false,positions:nil,characterIndexes:nil)
            precondition(count<=1025)
            var positions=[CGFloat](repeating:0,count:count),indexes=[Int](repeating:0,count:count)
            let written=manager.getLineFragmentInsertionPoints(forCharacterAt:chars.location,alternatePositions:false,inDisplayOrder:false,positions:&positions,characterIndexes:&indexes)
            precondition(written==count)
            let points=(0..<count).map { ["x":positions[$0],"characterIndex":indexes[$0]] as [String:Any] }
            lines.append(["glyphRange":range(glyphRange),"utf16Range":range(chars),"fragmentRect":rect(fragment),"usedRect":rect(used),"insertionPoints":points])
        }
        return ["glyphCount":manager.numberOfGlyphs,"usedRect":rect(manager.usedRect(for:container)),"lines":lines,
            "extraLineFragmentRect":rect(manager.extraLineFragmentRect),"extraLineFragmentUsedRect":rect(manager.extraLineFragmentUsedRect),
            "caretFacts":"native insertion-point x/index plus line rectangles; actual drawn insertion-point rect/blink and screen candidate rect require a window and are not claimed"]
    }
    static func request(_ text:String,width:CGFloat=240,rich:NotesRichText?=nil) -> NotesEditRequest {
        NotesEditRequest(noteID:UUID(uuidString:"00000000-0000-4000-8000-000000000001")!,itemID:nil,text:text,
            rect:CGRect(x:0,y:0,width:width,height:100),fontSize:12,multiline:true,richText:rich)
    }
    static func apply(_ change:NotesFormatChange,to e:FixtureEditor) {
        e.fixtureUndo.beginUndoGrouping();FixtureSource.apply(change,to:e);e.fixtureUndo.endUndoGrouping()
    }
    static func checkHiddenHelpers() throws {
        // Creating/selecting a detached NSTextView itself initializes a TUI
        // helper on this Mac. It is not an authored/visible editor window.
        // Keep original native behavior and record the helper honestly.
        try require(NSApp.activationPolicy() == .prohibited && !NSApp.isActive,"Reference must remain inactive")
        try require(NSApp.windows.count<=1 && NSApp.windows.allSatisfy {
            String(describing:type(of:$0)) == "TUINSWindow" && !$0.isVisible && !$0.isKeyWindow && !$0.isMainWindow
        },"Unexpected or visible native window in detached reference")
    }
    static func run() throws {
        let args=CommandLine.arguments
        try require(args.count==4 && args[1]=="--ui-test" && args[2]=="--output","Use notes_rich_layout_reference.sh")
        try require(ProcessInfo.processInfo.environment["CFFIXED_USER_HOME"] != nil && Thread.isMainThread,"Isolated main-thread fixture required")
        NSApplication.shared.setActivationPolicy(.prohibited)
        var cases:[[String:Any]]=[]
        func emit(_ name:String,_ next:NotesEditRequest,_ e:FixtureEditor,dark:Bool=true) throws {
            try require(e.window==nil && e.string.utf16.count<=1024,"Detached bounded native text only")
            try checkHiddenHelpers()
            FixtureSource.resize(e,logicalRect:next.rect)
            let ink:NSColor=dark ? .white:.black
            let captured=NotesRichText.capture(e.textStorage!,defaultColor:ink),saved=FixtureSource.finish(next,editor:e,dark:dark)
            cases.append(["name":name,"text":e.string,"utf16Length":e.string.utf16.count,"width":next.rect.width,"viewportHeight":next.rect.height,
                "editorFrame":rect(e.frame),"selectedRange":range(e.selectedRange()),"captured":try encoded(captured),
                "savedRich":try saved.map(encoded) as Any? ?? NSNull(),"typingStyle":try encoded(NotesFormattingEditor.style(in:e,defaultColor:ink)),
                "defaultParagraph":paragraph(e.defaultParagraphStyle),"attributedRuns":attributes(e.textStorage!),
                "textKit":try textKit(e),"settledLines":settled(e.string,width:next.rect.width,rich:saved),
                "richLayoutEnabled":e.richEnabled,"canUndo":e.fixtureUndo.canUndo,"canRedo":e.fixtureUndo.canRedo,"dark":dark])
        }
        let text="Latin 123 日本語 😀\nSecond line 한글 e\u{301}",plain=request(text),base=FixtureSource.prepare(plain,dark:true)
        base.fixtureUndo.groupsByEvent=false;base.setSelectedRange((text as NSString).range(of:"123 日本語"))
        try emit("before-first-format",plain,base)
        apply(.size(48),to:base);try emit("after-first-format",plain,base)
        apply(.bold,to:base);apply(.italic,to:base);apply(.underline,to:base);apply(.strikethrough,to:base);try emit("partial-traits",plain,base)
        base.setSelectedRange(NSRange(location:0,length:(text as NSString).length));try emit("mixed-traits",plain,base)
        apply(.bold,to:base);try emit("mixed-bold-enabled",plain,base)

        let undo=FixtureSource.prepare(plain,dark:true);undo.fixtureUndo.groupsByEvent=false
        undo.setSelectedRange(NSRange(location:1,length:5));try emit("undo-before",plain,undo)
        apply(.size(48),to:undo);try emit("undo-formatted",plain,undo)
        try require(undo.fixtureUndo.canUndo,"Actual source formatting must register native undo")
        undo.fixtureUndo.undo();try emit("undo-restored",plain,undo)
        undo.fixtureUndo.redo();try emit("redo-restored",plain,undo)

        let collapsed=FixtureSource.prepare(plain,dark:true);collapsed.fixtureUndo.groupsByEvent=false
        collapsed.setSelectedRange(NSRange(location:3,length:0));try emit("collapsed-before",plain,collapsed)
        apply(.size(48),to:collapsed);apply(.bold,to:collapsed);try emit("collapsed-after",plain,collapsed)
        collapsed.fixtureUndo.beginUndoGrouping();collapsed.insertText("!",replacementRange:collapsed.selectedRange());collapsed.fixtureUndo.endUndoGrouping()
        try emit("collapsed-typed",plain,collapsed)

        for dark in [true,false] {
            let next=request("Ink"),e=FixtureSource.prepare(next,dark:dark);e.fixtureUndo.groupsByEvent=false
            e.setSelectedRange(NSRange(location:0,length:3));try emit(dark ? "theme-white":"theme-black",next,e,dark:dark)
            apply(.color(NSColor(srgbRed:dark ? 1:0,green:dark ? 1:0,blue:dark ? 1:0,alpha:1)),to:e)
            try emit(dark ? "explicit-white":"explicit-black",next,e,dark:dark)
        }
        var imported=NotesRichText();var style=NotesTextStyle();style.fontName=".SFNS-Bold";style.fontSize=48;style.italic=true
        imported.runs=[NotesTextRun(location:0,length:5,style:style)]
        let privateRequest=request("Latin 日本語 😀",rich:imported),privateEditor=FixtureSource.prepare(privateRequest,dark:true)
        try emit("private-font-import",privateRequest,privateEditor)
        let family="Menlo",publicEditor=FixtureSource.prepare(plain,dark:true);publicEditor.fixtureUndo.groupsByEvent=false
        try require(NSFontManager.shared.availableFontFamilies.contains(family),"Required explicit public-font fixture family unavailable")
        publicEditor.setSelectedRange(NSRange(location:0,length:5));apply(.italic,to:publicEditor);apply(.font(family),to:publicEditor)
        try emit("public-font-family",plain,publicEditor)
        for (name,value,width) in [("empty","",CGFloat(240)),("trailing-newline","Latin\n",240),("crlf","Latin\r\n日本語\r\n",240),("narrow-width","A😀日本語",1)] {
            let next=request(value,width:width),e=FixtureSource.prepare(next,dark:true);try emit(name,next,e)
        }
        try checkHiddenHelpers()
        let colorIdentity:[String:Any]=["whiteEqualsExplicitSRGB":NSColor.white.isEqual(NSColor(srgbRed:1,green:1,blue:1,alpha:1)),
            "blackEqualsExplicitSRGB":NSColor.black.isEqual(NSColor(srgbRed:0,green:0,blue:0,alpha:1))]
        let result:[String:Any]=["schemaVersion":1,"scope":"unchanged source rich-text/formatting/wrapping helpers plus original private editor setup/normalization statements in detached TextKit",
            "cases":cases,"colorIdentity":colorIdentity,"fontEnvironment":["operatingSystem":ProcessInfo.processInfo.operatingSystemVersionString,
                "publicFamilyRequested":family,"system12":font(.systemFont(ofSize:12)),"fallbackEvidence":"per-settled-line actual CoreText glyph fonts; TextKit fallback glyph fonts are not independently enumerated"],
            "isolation":["visibleWindowCreated":false,"editorWindowCreated":false,"applicationActivated":false,"realStoresRead":false,"clipboardAccessed":false,"inputContextActivationRequested":false,
                "internalHelperWindows":NSApp.windows.map { ["class":String(describing:type(of:$0)),"visible":$0.isVisible,"key":$0.isKeyWindow,"main":$0.isMainWindow] as [String:Any] },
                "screenshotsCaptured":false,"projectedEditorInstantiated":false,"fixtureHomeInjected":true],
            "notVerified":["full HUDNotesInteraction window/focus lifecycle","projected artwork and native painted caret/blink","screen-coordinate IME candidates",
                "Windows font substitutions/layout/pixels","large-document memory/performance","public family availability on other Macs"]]
        let data=try JSONSerialization.data(withJSONObject:result,options:[.prettyPrinted,.sortedKeys,.withoutEscapingSlashes])
        try require(data.count<2*1024*1024,"Reference output exceeds bounded fixture size")
        try data.write(to:URL(fileURLWithPath:args[3],isDirectory:true).appendingPathComponent("rich-layout.json"),options:.withoutOverwriting)
        print("Exported \(cases.count) actual source rich-text and native layout states without visible windows or capture")
    }
    static func main() { do { try run() } catch { fputs("Notes rich layout reference: \(error)\n",stderr);exit(1) } }
}
