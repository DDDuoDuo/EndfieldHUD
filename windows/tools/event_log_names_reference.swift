import Foundation
import CoreGraphics

enum L10n {static func text(_ english:String,_ chinese:String)->String{english}}
@main enum Main {
    static func main() throws {
        precondition(CommandLine.arguments.count==2)
        let control=CharacterSet.controlCharacters,white=CharacterSet.whitespaces,blank=white.union(.newlines)
        func ranges(_ set:CharacterSet)->[[UInt32]] {
            var result:[[UInt32]]=[],start:UInt32?,last:UInt32=0
            for value in UInt32(0)...0x10ffff {
                let contained=UnicodeScalar(value).map {set.contains($0)} ?? false
                if contained {if start==nil {start=value};last=value}
                else if let begin=start {result.append([begin,last]);start=nil}
            }
            if let begin=start {result.append([begin,last])};return result
        }
        let sets=["control":ranges(control),"whitespace":ranges(white),"spaceAndNewline":ranges(blank),"spaceOrControl":ranges(CharacterSet.whitespacesAndNewlines.union(.controlCharacters))]
        var values=["","  App\r\n\t Name  ","扬声器","日本語 한국어","e\u{301} / a\u{308}","👨‍👩‍👧‍👦","🇺🇸🇰🇷","🏳️‍🌈","क्‍ष","\u{0600}A","a\u{200d}b","A\u{00a0}\u{2007}\u{202f}B","A\u{feff}B","A\u{ad}B"]
        for sequence in ["A","中","e\u{301}","👨‍👩‍👧‍👦","🇺🇸","क्‍ष","🏴\u{e0067}\u{e0062}\u{e007f}","\r\n","\u{200d}","\u{0600}"] {
            for count in [1,2,39,40,52,53,79,80,159,160,161,511,512,513] {
                let value=String(repeating:sequence,count:count)
                if value.utf8.count<=4096 {values.append(value);values.append("  "+value+"  END")}
            }
        }
        for key in sets.keys.sorted() {for range in sets[key]! {for point in Set([range[0],range[1],range[0]>0 ? range[0]-1:0,min(0x10ffff,range[1]+1)]).sorted() {
            if let scalar=UnicodeScalar(point) {values.append("A"+String(scalar)+"B");values.append(String(scalar)+"C"+String(scalar))}
        }}}
        for prefix in [0,1,150,155,158,159,160,161] {for tail in ["e\u{301}","👨‍👩‍👧‍👦","🇺🇸"," \n ","\u{200d}","中","\u{00a0}\u{301}"] {values.append(String(repeating:"x",count:prefix)+tail+"END")}}
        var state:UInt64=0x123456789abcdef
        let alphabet:[String]=["x","中","e\u{301}","🇺🇸","👨‍👩‍👧‍👦","\t","\r\n","\u{200d}","\u{feff}","\u{00a0}","\u{0600}","\u{301}","क्‍ष"]
        for _ in 0..<160 {var value="";for _ in 0..<96 {state=state &* 6364136223846793005 &+ 1;value+=alphabet[Int((state>>32)%UInt64(alphabet.count))]};values.append(value)}
        var seen=Set<String>();let rows=values.filter {$0.utf8.count<=4096 && seen.insert($0).inserted}.map {["input":$0,"expected":SystemEventLog.compact($0)]}
        let object:[String:Any]=["scope":"Original SystemEventLog.compact; Foundation-only synthetic names; no log instance, file store, UI, or services","sets":sets,"cases":rows]
        let output=URL(fileURLWithPath:CommandLine.arguments[1]);try JSONSerialization.data(withJSONObject:object,options:[.sortedKeys]).write(to:output)
        print("Original Event Log name compaction: \(rows.count) synthetic cases")
    }
}
