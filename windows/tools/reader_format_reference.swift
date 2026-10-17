import Foundation
enum L10n {static func text(_ en:String,_ zh:String)->String{en}}
@main struct ReaderFormatsReference {
    static func main() throws {
        guard CommandLine.arguments.count==2 else{fatalError("Output directory required")}
        let out=URL(fileURLWithPath:CommandLine.arguments[1],isDirectory:true)
        let cases:[(String,Data)]=[
            ("paragraphs",Data("<html><head><title>\u{3000}Book\u{a0}</title></head><body><p>Hello&nbsp;world</p><p>中文🙂</p><img src='images/a.png'/><div>Tail<br/>line</div></body></html>".utf8)),
            ("ignored",Data("<html><body><p>before</p><script>bad<style>nested</style></script><video>bad</video><object>bad</object><iframe>bad</iframe><audio>bad</audio><p>after</p></body></html>".utf8)),
            ("CDATA",Data("<html><body>before<![CDATA[hidden <content>]]>after</body></html>".utf8)),
            ("externalDTD",Data("<!DOCTYPE html SYSTEM 'https://example.invalid/no-fetch.dtd'><html><body><p>safe</p></body></html>".utf8)),
            ("internalEntity",Data("<!DOCTYPE html [<!ENTITY foo 'bad'>]><html><body>&foo;</body></html>".utf8)),
            ("duplicateID",Data("<package><manifest><item id='x' href='a' media-type='text/html'/><item id='x' href='b' media-type='text/html'/></manifest></package>".utf8)),
            ("namespaces",Data("<o:package xmlns:o='urn:test'><o:title>Named</o:title><item id='a' href='a.xhtml' media-type='application/xhtml+xml'/><itemref idref='a'/><itemref idref='b' linear='no'/><rootfile full-path='OPS/book.opf'/></o:package>".utf8)),
            ("SVGImage",Data("<html xmlns:xlink='http://www.w3.org/1999/xlink'><body>A<image xlink:href='cover.png'/>B</body></html>".utf8)),
            ("UTF16LE",("<?xml version='1.0' encoding='UTF-16'?><html><body><p>中文🙂</p></body></html>".data(using:.utf16)!) ),
            ("NUL",Data("<html><body>bad\0</body></html>".utf8)),
            ("malformed",Data("<html><body>unclosed".utf8))]
        var facts:[[String:Any]]=[]
        for (name,data) in cases {var row:[String:Any]=["name":name,"base64":data.base64EncodedString()];do{row["result"]=try readerXMLReference(data);row["success"]=true}catch{row["success"]=false};facts.append(row)}
        let txtCases:[(String,Data)]=[
            ("UTF8",Data("UTF8 中文🙂 e\u{301}".utf8)),
            ("UTF16LE",Data([0xff,0xfe,0x2d,0x4e,0x3d,0xd8,0x42,0xde])),
            ("UTF16BE",Data([0xfe,0xff,0x4e,0x2d,0xd8,0x3d,0xde,0x42])),
            ("GB18030",Data([0xd6,0xd0,0xce,0xc4])),
            ("GB18030-private",Data([0xa8,0xbc,0xa6,0xd9])),
            ("empty",Data()),("NUL",Data([65,0])),("truncatedGB",Data([0x81])),
            ("unpairedUTF16",Data([0xff,0xfe,0x00,0xd8]))]
        let directory=FileManager.default.temporaryDirectory.appendingPathComponent("Endfield-reader-format-\(UUID().uuidString)",isDirectory:true)
        try FileManager.default.createDirectory(at:directory,withIntermediateDirectories:false);defer{try? FileManager.default.removeItem(at:directory)}
        var textFacts:[[String:Any]]=[]
        for (name,data) in txtCases {var row:[String:Any]=["name":name,"base64":data.base64EncodedString()];let url=directory.appendingPathComponent(name+".txt");try data.write(to:url);do{let document=try ReaderDocument(access:ReaderFileAccess(url:url));row["result"]=document.readerFormatTextFacts();row["success"]=true}catch{row["success"]=false};textFacts.append(row)}
        let scalars:[UInt32]=[9,10,11,12,13,0x20,0x85,0xa0,0x1680,0x180e,0x2000,0x2001,0x2002,0x2003,0x2004,0x2005,0x2006,0x2007,0x2008,0x2009,0x200a,0x200b,0x2028,0x2029,0x202f,0x205f,0x2060,0x3000,0xfeff]
        let whitespace=scalars.map{["scalar":Int($0),"trimmed":CharacterSet.whitespacesAndNewlines.contains(UnicodeScalar($0)!)] as [String:Any]}
        var provenance=try JSONSerialization.jsonObject(with:Data(contentsOf:out.appendingPathComponent("provenance.json"))) as! [String:Any]
        provenance["cases"]=facts;provenance["txtCases"]=textFacts;provenance["whitespace"]=whitespace
        let data=try JSONSerialization.data(withJSONObject:provenance,options:[.sortedKeys,.prettyPrinted,.withoutEscapingSlashes]);try data.write(to:out.appendingPathComponent("reader-format-reference.json"))
        print("Original ReaderXML reference: \(facts.count) cases, \(data.count) bytes")
    }
}
