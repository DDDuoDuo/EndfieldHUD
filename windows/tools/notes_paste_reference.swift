// Compiled in one generated file with unchanged private source helpers
// (beginEditing's text view setup and finishEditing's capture) and the
// unchanged Sources/NotesRichText.swift. Each case pastes one foreign RTF or
// HTML document into a detached rich NSTextView configured exactly as the HUD
// configures a text note, through a private uniquely named pasteboard (never
// the general clipboard), and records the text and NotesRichText runs the
// HUD would store.
final class FixtureEditor: NSTextView {
    let fixtureUndo = UndoManager()
    override var undoManager: UndoManager? { fixtureUndo }
}

@main enum NotesPasteReference {
    struct Case { let name: String; let kind: String; let input: String }
    static let cases: [Case] = [
        Case(name: "rtf-basic", kind: "rtf", input: #"{\rtf1\ansi\ansicpg1252\deff0{\fonttbl{\f0\fswiss\fcharset0 Arial;}{\f1\froman\fcharset0 Times New Roman;}}{\colortbl ;\red255\green0\blue0;\red0\green0\blue255;}\f0\fs24 Plain \b bold\b0  \i italic\i0  \ul under\ulnone  \strike strike\strike0  \cf1 red\cf0  \f1\fs36 big serif\f0\fs24\par}"#),
        Case(name: "rtf-wordpad", kind: "rtf", input: "{\\rtf1\\ansi\\ansicpg1252\\deff0\\nouicompat\\deflang1033{\\fonttbl{\\f0\\fnil\\fcharset0 Georgia;}{\\f1\\fnil\\fcharset0 Courier New;}}\r\n{\\*\\generator Riched20 10.0.19041}\\viewkind4\\uc1 \r\n\\pard\\sa200\\sl276\\slmult1\\f0\\fs22\\lang9 First line\\par\r\n\\b Second \\i line\\b0\\i0\\par\r\n\\f1 code\\f0\\par\r\n}\r\n"),
        Case(name: "rtf-unicode", kind: "rtf", input: #"{\rtf1\ansi\ansicpg1252\uc1\deff0{\fonttbl{\f0 Arial;}}\f0\fs20 \u20013?\u25991? CJK, \u-10179?\u-8704? emoji, caf\'e9\line soft\tab tab\par}"#),
        Case(name: "rtf-nested", kind: "rtf", input: #"{\rtf1\ansi{\fonttbl{\f0 Verdana;}}\f0 a{\b b{\i c}d}e{\*\unknown skipped}f\par}"#),
        Case(name: "rtf-sizes", kind: "rtf", input: #"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0\fs17 small \fs288 huge \fs400 clamp\fs24\par}"#),
        Case(name: "rtf-colors", kind: "rtf", input: #"{\rtf1\ansi{\fonttbl{\f0 Arial;}}{\colortbl;\red0\green0\blue0;\red255\green255\blue255;\red18\green52\blue86;}\f0 \cf1 black \cf2 white \cf3 blue\cf0  auto\par}"#),
        Case(name: "rtf-lines", kind: "rtf", input: #"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 \uld dotted\ulnone  x \ulw words\ul0  y \striked1 double\striked0  z\par}"#),
        Case(name: "rtf-uc2", kind: "rtf", input: #"{\rtf1\ansi\ansicpg936\uc2{\fonttbl{\f0 Arial;}}\f0 \u20013\'d6\'d0 x\par}"#),
        Case(name: "rtf-tail", kind: "rtf", input: #"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 tail}"#),
        Case(name: "rtf-plain", kind: "rtf", input: #"{\rtf1\ansi plain default\par}"#),
        Case(name: "rtf-table", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 \trowd\cellx1000\cellx2000 a\cell b\cell\row c\par}"##),
        Case(name: "rtf-listtext", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}{\f1 Symbol;}}\f0 {\listtext\f1 \'b7\tab}item\par {\pntext x}y\par}"##),
        Case(name: "rtf-symbols", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 a\emdash b\endash c\bullet d\lquote e\rquote f\ldblquote g\rdblquote h\~i\-j\_k\page l\sect m\par}"##),
        Case(name: "rtf-field", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}{\colortbl;\red5\green99\blue193;}\f0 {\field{\*\fldinst HYPERLINK "https://example.com"}{\fldrslt {\cf1\ul link}}} after\par}"##),
        Case(name: "rtf-skipped", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}{\stylesheet{\s0 Normal;}}{\info{\title T}}{\*\generator W}\f0 body\par}"##),
        Case(name: "rtf-macfonts", kind: "rtf", input: ##"{\rtf1\ansi\deff0{\fonttbl\f0\fswiss Helvetica;\f1\froman Times;}\f1 mac\f0 style\par}"##),
        Case(name: "rtf-plainreset", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0\b\i\ul\fs32 styled\plain plain\par}"##),
        Case(name: "rtf-escapes", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 a\\b\{c\}d\par}"##),
        Case(name: "rtf-crlfpar", kind: "rtf", input: "{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}\\f0 one\\\r\ntwo\\par}"),
        Case(name: "rtf-toggles", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 \b0 \i1 it\i0 \ul0 x\ulnone\par}"##),
        Case(name: "rtf-cp1252", kind: "rtf", input: ##"{\rtf1\ansi{\fonttbl{\f0 Arial;}}\f0 caf\'e9 \'80 \'93q\'94\par}"##),
        Case(name: "rtf-emoji", kind: "rtf", input: ##"{\rtf1\ansi\uc1{\fonttbl{\f0 Arial;}}\f0 face \u-10179?\u-8704? done\par}"##),
        Case(name: "html-inline", kind: "html", input: #"<meta charset="utf-8"><span style="font-family: Arial; font-size: 16px; color: rgb(255, 0, 0); font-weight: 700;">Red bold</span><span style="font-family: Arial; font-size: 16px;"> plain</span>"#),
        Case(name: "html-tags", kind: "html", input: #"<meta charset="utf-8"><b>bold</b> <i>italic</i> <u>under</u> <s>strike</s> <strong>strong</strong> <em>em</em>"#),
        Case(name: "html-blocks", kind: "html", input: #"<meta charset="utf-8"><p>para one</p><p>para <br>two</p><div>div</div>"#),
        Case(name: "html-font", kind: "html", input: ##"<meta charset="utf-8"><font face="Courier New" color="#00ff00">mono green</font>"##),
        Case(name: "html-entities", kind: "html", input: #"<meta charset="utf-8">a &amp; b &lt; c&nbsp;d &#20013; &#x1F600;"#),
        Case(name: "html-whitespace", kind: "html", input: "<meta charset=\"utf-8\"><span>  many   spaces\n and\tnewlines </span>"),
        Case(name: "html-sizes", kind: "html", input: #"<meta charset="utf-8"><span style="font-size: 12pt">pt</span> <span style="font-size: 10px">px</span> <span style="font-size: 24px">big</span>"#),
        Case(name: "html-ul", kind: "html", input: ##"<meta charset="utf-8"><ul><li>one</li><li>two</li></ul>"##),
        Case(name: "html-ol", kind: "html", input: ##"<meta charset="utf-8"><ol><li>one</li><li>two</li></ol>"##),
        Case(name: "html-nestedlist", kind: "html", input: ##"<meta charset="utf-8"><ul><li>a<ul><li>nested</li></ul></li><li>b</li></ul>"##),
        Case(name: "html-headings", kind: "html", input: ##"<meta charset="utf-8"><h1>Title</h1><h2>Sub</h2><h3>h3</h3><h4>h4</h4><h5>h5</h5><h6>h6</h6>body"##),
        Case(name: "html-link", kind: "html", input: ##"<meta charset="utf-8">see <a href="https://example.com">link</a> here"##),
        Case(name: "html-weights", kind: "html", input: ##"<meta charset="utf-8"><b>bold <span style="font-weight: normal">normal</span> <i>bi</i></b> <span style="font-weight: 600">w600</span><span style="font-weight: 500">w500</span><span style="font-style: oblique">obl</span>"##),
        Case(name: "html-chrome", kind: "html", input: ##"<meta charset="utf-8"><span style="color: rgb(32, 33, 36); font-family: arial, sans-serif; font-size: 14px; font-style: normal; font-variant-ligatures: normal; font-weight: 400; letter-spacing: normal; text-align: left; text-decoration-line: underline; white-space: normal; background-color: rgb(255, 255, 255); display: inline !important;">Chrome copy</span>"##),
        Case(name: "html-table", kind: "html", input: ##"<meta charset="utf-8"><table><tr><td>a</td><td>b</td></tr><tr><td>c</td><td>d</td></tr></table>"##),
        Case(name: "html-colors", kind: "html", input: ##"<meta charset="utf-8"><span style="color: #abc">short hex</span> <span style="color: red">named</span> <span style="color: rgba(0,0,255,0.5)">alpha</span> <span style="color: rgb(0, 0, 0)">black</span> <span style="color: transparent">clear</span> <span style="color: rgb(255,255,255)">white</span>"##),
        Case(name: "html-families", kind: "html", input: ##"<meta charset="utf-8"><span style="font-family: 'Times New Roman', serif">quoted</span> <span style="font-family: monospace">mono</span> <span style="font-family: sans-serif">sans</span> <span style="font-family: Georgia; font-size: 18.5px">frac</span>"##),
        Case(name: "html-decorations", kind: "html", input: ##"<meta charset="utf-8"><span style="text-decoration: line-through">strike</span><del>del</del><ins>ins</ins><strike>st</strike> <span style="text-decoration: underline line-through">both</span><u style="text-decoration: none">none</u>"##),
        Case(name: "html-keywords", kind: "html", input: ##"<meta charset="utf-8"><span style="font-size: small">small</span><span style="font-size: large">large</span><span style="font-size: 1.5em">em</span><font size="5">f5</font> <code>code</code> <small>sm</small> <big>big</big> <sup>sup</sup>"##),
        Case(name: "html-pre", kind: "html", input: "<meta charset=\"utf-8\"><pre>a  b\n c</pre>after"),
        Case(name: "html-paragraphs", kind: "html", input: ##"<meta charset="utf-8"><p></p><p>after empty</p><div><p>a</p></div><div><div>b</div></div><p>end space </p><p> start space</p><div>c<br></div><div>d</div>"##),
        Case(name: "html-breaks", kind: "html", input: ##"<meta charset="utf-8">line1<br>line2<br><br>end a <span> b </span> c<br> d"##),
        Case(name: "html-rule", kind: "html", input: ##"<meta charset="utf-8"><blockquote>quote</blockquote>x<hr>y"##),
    ]
    static func main() {
        var rows: [[String: Any]] = []
        for c in cases {
            let editor = prepare()
            let name = NSPasteboard.Name("com.endfield.notes-paste-oracle." + UUID().uuidString)
            let board = NSPasteboard(name: name)
            board.clearContents()
            let type: NSPasteboard.PasteboardType = c.kind == "rtf" ? .rtf : .html
            board.setData(c.input.data(using: .utf8)!, forType: type)
            let pasted = editor.readSelection(from: board, type: type)
            board.releaseGlobally()
            precondition(pasted, "Paste failed: " + c.name)
            let captured = finish(editor)
            let encoded = try! JSONSerialization.jsonObject(with: JSONEncoder().encode(NotesRichText.capture(editor.textStorage!, scale: 1, defaultColor: .white)))
            var row: [String: Any] = ["name": c.name, "kind": c.kind, "input": c.input, "text": editor.string,
                "utf16Length": (editor.string as NSString).length, "captured": encoded]
            row["stored"] = captured.map { try! JSONSerialization.jsonObject(with: JSONEncoder().encode($0)) } ?? NSNull()
            rows.append(row)
        }
        let output = ["sourceDescription": "NSTextView.readSelection(from:type:) of a private pasteboard into the HUD text note editor; NotesRichText.capture(defaultColor: white)",
                      "readablePasteboardTypes": prepare().readablePasteboardTypes.map(\.rawValue),
                      "cases": rows] as [String: Any]
        let data = try! JSONSerialization.data(withJSONObject: output, options: [.prettyPrinted, .sortedKeys])
        FileHandle.standardOutput.write(data)
    }
}
