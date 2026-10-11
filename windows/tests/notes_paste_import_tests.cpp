// Foreign RTF/HTML clipboard text as Notes rich text (core/notes_paste_import)
// against the pinned Mac oracle: the HUD's own rich text view pasting each
// document from a private pasteboard and NotesRichText.capture storing it
// (windows/tools/notes_paste_reference.sh). Portable; synthetic data only.
#include "core/notes_paste_import.hpp"
#include "core/notes_font_names.hpp"
#include "core/data/json.hpp"
#include <array>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <iostream>
#include <vector>
#include <sstream>
#include <stdexcept>
#include <string>

namespace {
namespace notes=endfield::core::notes;using Json=ehud::data::Json;
unsigned checks{};
void check(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
std::u16string utf16(std::string_view s){
    std::u16string out;for(std::size_t n=0;n<s.size();){const auto b=static_cast<unsigned char>(s[n]);std::size_t length=b<0x80?1:(b>>5)==6?2:(b>>4)==14?3:4;char32_t c=length==1?b:length==2?b&0x1F:length==3?b&0x0F:b&0x07;
        for(std::size_t k=1;k<length;++k)c=(c<<6)|(static_cast<unsigned char>(s[n+k])&0x3F);n+=length;
        if(c<0x10000)out.push_back(static_cast<char16_t>(c));else{c-=0x10000;out.push_back(static_cast<char16_t>(0xD800+(c>>10)));out.push_back(static_cast<char16_t>(0xDC00+(c&0x3FF)));}}
    return out;
}
// The Windows family a stored Mac font name plays the role of.
std::string family(const std::string& mac){if(const auto mapped=notes::notesWindowsFontFamily(mac))return std::string(*mapped);return mac;}
void compareOracle(const std::string& path){
    std::ifstream in(path,std::ios::binary);check(bool(in),"Open the paste oracle "+path);std::stringstream buffer;buffer<<in.rdbuf();
    const auto root=Json::parse(buffer.str(),8*1024*1024);const auto&cases=root["cases"].array();check(cases.size()>=40,"The oracle records every paste case");
    check(root["readablePasteboardTypes"].array().front().string()=="NeXT Rich Text Format v1.0 pasteboard type","The source rich text view reads RTF first");
    unsigned rtf{},html{};
    for(const auto&c:cases){
        const auto name=c["name"].string(),kind=c["kind"].string(),input=c["input"].string();
        const auto imported=kind=="rtf"?notes::importNotesRTF(input):notes::importNotesHTML(input);check(imported.has_value(),name+": the document imports");
        (kind=="rtf"?rtf:html)++;
        const auto expected=utf16(c["text"].string());
        check(imported->text==expected,name+": the pasted text matches the source paste");
        // Per UTF-16 unit: the Windows family of the stored Mac font, size,
        // traits and color. (Two Mac fonts with one Windows counterpart, such
        // as Times and Times New Roman, are one run on Windows.)
        const auto&runs=c["captured"]["runs"].array();std::vector<const notes::TextRun*> units(imported->text.size(),nullptr);
        for(const auto&run:imported->rich.runs)for(auto n=run.location;n<run.location+run.length&&n<units.size();++n)units[n]=&run;
        for(std::size_t r=1;r<imported->rich.runs.size();++r)check(!(imported->rich.runs[r].style==imported->rich.runs[r-1].style),name+": equal neighbouring runs are merged");
        std::size_t covered{};
        for(std::size_t n=0;n<runs.size();++n){
            const auto&want=runs[n];const auto&style=want["style"];const auto at=name+" run "+std::to_string(n);const auto location=static_cast<std::size_t>(want["location"].integer()),length=static_cast<std::size_t>(want["length"].integer());
            check(location==covered&&location+length<=units.size(),at+": dense source runs");covered+=length;
            for(auto unit=location;unit<location+length;++unit){
                check(units[unit]!=nullptr,at+": every pasted unit carries a style");const auto&got=*units[unit];
                check(got.style.fontName&&*got.style.fontName==family(style["fontName"].string()),at+": font family ("+got.style.fontName.value_or("none")+" for "+style["fontName"].string()+")");
                check(std::abs(got.style.fontSize-style["fontSize"].number())<1e-4,at+": point size");
                check(got.style.bold==style["bold"].boolean()&&got.style.italic==style["italic"].boolean()&&got.style.underline==style["underline"].boolean()&&got.style.strikethrough==style["strikethrough"].boolean(),at+": traits and lines");
                if(style["color"].isNull()){check(!got.style.color,at+": theme ink (no stored color)");continue;}
                check(got.style.color.has_value(),at+": an explicit color");
                // RTF color tables are Generic RGB (shared portable profile,
                // <1/255); HTML colors are sRGB bytes.
                const double tolerance=kind=="rtf"?1./255:1e-5;const auto&want_c=style["color"];const auto&g=*got.style.color;
                check(std::abs(g.red-want_c["red"].number())<=tolerance&&std::abs(g.green-want_c["green"].number())<=tolerance&&std::abs(g.blue-want_c["blue"].number())<=tolerance&&std::abs(g.alpha-want_c["alpha"].number())<=1e-5,at+": color");
            }
        }
        check(covered==units.size(),name+": the source runs cover the text");
        check(imported->rich.validFor(imported->text),name+": a valid Notes rich payload");
    }
    check(rtf>=20&&html>=20,"Both clipboard formats are covered");
}
// Truncated, nested and malformed documents never throw; anything imported
// is a valid Notes payload.
void robustness(){
    const std::array<std::string_view,32> pieces{"{","}","\\b","\\b0","\\u-10179?","\\u65535","\\'","\\'zz","\\'e9","\\bin3 ab","\\*\\x","\\fonttbl{\\f0 A;}","\\colortbl;\\red300;","\\f9","\\fs-4","\\par",
        "<b>","</b>","<p>","</p>","<br>","<pre>","&#xFFFFFF;","&amp","<span style=\"color: rgb(1e9,-1,x); font-size: -3em\">","<!--","<ol start=\"-5\"><li>","\xff\xfe","x"," ","\n","<td>"};
    std::uint32_t seed=12345;unsigned imported{};
    for(unsigned round=0;round<3000;++round){
        std::string doc=round%2?"{\\rtf1 ":"<html>";const auto count=1+(round%40);
        for(unsigned n=0;n<count;++n){seed=seed*1664525u+1013904223u;doc+=pieces[(seed>>16)%pieces.size()];}
        try{const auto value=round%2?notes::importNotesRTF(doc):notes::importNotesHTML(doc);
            if(value){++imported;check(value->rich.validFor(value->text)&&!value->text.empty(),"Malformed input still yields a valid payload");}}
        catch(const std::exception& e){check(false,std::string("Importers never throw: ")+e.what());}
    }
    check(imported>500,"Most malformed documents still import their text");
}
void windowsClipboard(){
    // CF_HTML: offsets into the whole data; the fragment's ancestors style it.
    const std::string body="<html><body><p style=\"color: rgb(255, 0, 0)\"><b>context</b> <!--StartFragment--><b>frag</b> tail<!--EndFragment--> after</p></body></html>";
    const auto header=[&](std::size_t startHTML){const auto startFragment=body.find("<!--StartFragment-->")+20,endFragment=body.find("<!--EndFragment-->");
        char text[200];std::snprintf(text,sizeof text,"Version:0.9\r\nStartHTML:%010zu\r\nEndHTML:%010zu\r\nStartFragment:%010zu\r\nEndFragment:%010zu\r\n",startHTML,startHTML+body.size(),startHTML+startFragment,startHTML+endFragment);return std::string(text);};
    const auto probe=header(0);const auto data=header(probe.size())+body;
    const auto clip=notes::importNotesHTML(data);check(clip&&clip->text==u"frag tail","CF_HTML pastes only its fragment");
    check(clip->rich.runs.size()==2&&clip->rich.runs[0].style.bold&&!clip->rich.runs[1].style.bold&&clip->rich.runs[0].style.color&&clip->rich.runs[0].style.color->red==1,"The fragment keeps the context paragraph's color");
    const auto markers=notes::importNotesHTML("<html><body><i><!--StartFragment-->only<!--EndFragment--></i> not</body></html>");check(markers&&markers->text==u"only"&&markers->rich.runs[0].style.italic,"Comment markers alone delimit the fragment");
    // DBCS RTF through the native decoder; without one the bytes are kept as U+FFFD.
    const std::string gbk="{\\rtf1\\ansi{\\fonttbl{\\f0\\fcharset134 SimSun;}}\\f0 \\'cb\\'ce z\\par}";
    notes::PasteImportOptions decoding;unsigned calls{};decoding.decode=[&](std::uint32_t page,std::string_view bytes)->std::optional<std::u16string>{
        // A GBK stand-in for MultiByteToWideChar: the one double-byte pair, ASCII as is.
        ++calls;if(page!=936)return std::nullopt;std::u16string out;
        for(std::size_t n=0;n<bytes.size();){if(bytes.substr(n,2)=="\xcb\xce"){out+=u"宋";n+=2;}else if(static_cast<unsigned char>(bytes[n])<0x80)out.push_back(static_cast<char16_t>(bytes[n++]));else return std::nullopt;}
        return out;};
    const auto decoded=notes::importNotesRTF(gbk,decoding);check(decoded&&decoded->text==u"宋 z\n"&&calls>=1&&decoded->rich.runs[0].style.fontName==std::optional<std::string>("SimSun"),"A GBK run decodes through the native code page");
    const auto unknown=notes::importNotesRTF(gbk);check(unknown&&unknown->text==u"\uFFFD\uFFFD z\n","Without a decoder no byte is dropped");
    // WebKit takes the first installed family of a list.
    notes::PasteImportOptions fonts;fonts.fontAvailable=[](std::string_view f){return f!="Not Installed";};
    const auto fallback=notes::importNotesHTML("<span style=\"font-family: 'Not Installed', Georgia\">g</span>",fonts);check(fallback&&fallback->rich.runs[0].style.fontName==std::optional<std::string>("Georgia"),"An uninstalled family falls through to the next");
    // Bounds and refusals.
    notes::PasteImportOptions small;small.maximumUnits=4;check(!notes::importNotesRTF("{\\rtf1 too long\\par}",small),"Content past the bound pastes as plain text");
    check(!notes::importNotesRTF("plain text")&&!notes::importNotesHTML("")&&!notes::importNotesHTML("<!-- only a comment --><span> </span>"),"Non-RTF and empty documents decline");
    // Pasted into the Notes model: one undo group with the imported runs.
    const auto red=notes::importNotesRTF("{\\rtf1\\ansi{\\fonttbl{\\f0 Arial;}}{\\colortbl;\\red255\\green0\\blue0;}\\f0\\fs28\\b big\\cf1  red\\par}");check(red&&red->text==u"big red\n","An RTF paste imports");
    notes::RichDocument document(u"x",std::nullopt,1024);document.setSelection({{1,1},endfield::core::text::ActiveEnd::end,false});
    document.setInsertionRuns(red->rich.runs,static_cast<std::uint32_t>(red->text.size()));document.replace({1,1},red->text);
    check(document.text()==u"xbig red\n"&&document.runs().size()>=2&&document.runs()[1].style.bold&&document.runs()[1].style.fontSize==14,"The document takes the imported runs");
    check(document.undo()&&document.text()==u"x","One undo removes the whole paste");
}
}
int main(int argc,char** argv){
    try{check(argc==2,"Pass the paste oracle fixture");compareOracle(argv[1]);robustness();windowsClipboard();
        std::cout<<"PASS "<<checks<<" Notes paste import contracts\n";return 0;}
    catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
