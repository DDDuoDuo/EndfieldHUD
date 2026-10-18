#include "core/notes_paste_import.hpp"
#include "core/notes_font_names.hpp"
#include "core/source_color.hpp"
#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

namespace endfield::core::notes {
namespace {
// One character format of the closed Notes model while parsing.
struct Style {
    std::string family;double size{12};std::optional<RGBA> color;
    bool bold{},italic{},underline{},strike{};
    bool operator==(const Style&)const=default;
};

// Dense output runs; equal neighbors merge. Bounded by maximumUnits.
class Builder {
public:
    explicit Builder(std::size_t maximum):maximum_(maximum){}
    void append(std::u16string_view value,const Style&style){
        if(value.empty()||overflow_)return;
        if(text_.size()+value.size()>maximum_){overflow_=true;return;}
        if(runs_.empty()||!(runs_.back().style==style))runs_.push_back({static_cast<std::uint32_t>(text_.size()),style});
        text_.append(value);
    }
    void append(char16_t unit,const Style&style){append(std::u16string_view(&unit,1),style);}
    bool empty()const noexcept{return text_.empty();}
    char16_t last()const noexcept{return text_.empty()?u'\0':text_.back();}
    std::optional<ImportedRichText> finish(){
        if(overflow_||text_.empty())return std::nullopt;
        // A lone surrogate (an unpaired \uN) becomes U+FFFD in place.
        for(std::size_t n=0;n<text_.size();++n){const auto c=text_[n];
            if(c>=0xD800&&c<=0xDBFF){if(n+1<text_.size()&&text_[n+1]>=0xDC00&&text_[n+1]<=0xDFFF){++n;continue;}text_[n]=0xFFFD;}
            else if(c>=0xDC00&&c<=0xDFFF)text_[n]=0xFFFD;}
        ImportedRichText out;out.text=std::move(text_);
        for(std::size_t n=0;n<runs_.size();++n){
            const auto start=runs_[n].start,end=n+1<runs_.size()?runs_[n+1].start:static_cast<std::uint32_t>(out.text.size());if(end<=start)continue;
            TextStyle s;const auto&v=runs_[n].style;
            if(!v.family.empty()&&v.family.size()<=256)s.fontName=v.family;
            // NotesRichText.capture: min(144, max(6, pointSize)).
            s.fontSize=std::isfinite(v.size)?std::clamp(v.size,6.,144.):12.;s.color=v.color;s.bold=v.bold;s.italic=v.italic;s.underline=v.underline;s.strikethrough=v.strike;
            if(!out.rich.runs.empty()&&out.rich.runs.back().style==s&&out.rich.runs.back().location+out.rich.runs.back().length==start)out.rich.runs.back().length+=end-start;
            else out.rich.runs.push_back({start,end-start,std::move(s),{}});
        }
        if(out.rich.runs.size()>RichText::maximumRuns||!out.rich.validFor(out.text))return std::nullopt;
        return out;
    }
private:
    struct Run {std::uint32_t start{};Style style;};
    std::u16string text_;std::vector<Run> runs_;std::size_t maximum_;bool overflow_{};
};

void appendScalar(std::u16string& out,char32_t c){
    if(c>0x10FFFF||(c>=0xD800&&c<=0xDFFF))c=0xFFFD;
    if(c<0x10000)out.push_back(static_cast<char16_t>(c));
    else{c-=0x10000;out.push_back(static_cast<char16_t>(0xD800+(c>>10)));out.push_back(static_cast<char16_t>(0xDC00+(c&0x3FF)));}
}
// Windows-1252 0x80...0x9F (undefined positions as their C1 controls).
constexpr std::array<char16_t,32> cp1252{0x20AC,0x81,0x201A,0x0192,0x201E,0x2026,0x2020,0x2021,0x02C6,0x2030,0x0160,0x2039,0x0152,0x8D,0x017D,0x8F,
    0x90,0x2018,0x2019,0x201C,0x201D,0x2022,0x2013,0x2014,0x02DC,0x2122,0x0161,0x203A,0x0153,0x9D,0x017E,0x0178};
std::u16string decodeBytes(std::uint32_t codePage,std::string_view bytes,const PasteImportOptions& o){
    std::u16string out;out.reserve(bytes.size());
    const bool ascii=std::all_of(bytes.begin(),bytes.end(),[](char c){return static_cast<unsigned char>(c)<0x80;});
    if(ascii){for(const char c:bytes)out.push_back(static_cast<char16_t>(static_cast<unsigned char>(c)));return out;}
    if(codePage==1252||codePage==0){for(const char c:bytes){const auto b=static_cast<unsigned char>(c);out.push_back(b>=0x80&&b<0xA0?cp1252[b-0x80]:static_cast<char16_t>(b));}return out;}
    if(o.decode)if(auto decoded=o.decode(codePage,bytes))return std::move(*decoded);
    for(const char c:bytes){const auto b=static_cast<unsigned char>(c);out.push_back(b<0x80?static_cast<char16_t>(b):u'\uFFFD');}
    return out;
}
// RTF \fcharset values to Windows code pages (0 = the document's \ansicpg).
std::uint32_t charsetCodePage(int charset,std::uint32_t ansi){
    switch(charset){case 77:return 10000;case 128:return 932;case 129:return 949;case 130:return 1361;case 134:return 936;case 136:return 950;
    case 161:return 1253;case 162:return 1254;case 163:return 1258;case 177:return 1255;case 178:return 1256;case 186:return 1257;
    case 204:return 1251;case 222:return 874;case 238:return 1250;case 254:return 437;default:return ansi;}
}
std::string lower(std::string_view s){std::string out(s);for(auto&c:out)c=static_cast<char>(std::tolower(static_cast<unsigned char>(c)));return out;}
std::string trim(std::string_view s){while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.front())))s.remove_prefix(1);while(!s.empty()&&std::isspace(static_cast<unsigned char>(s.back())))s.remove_suffix(1);return std::string(s);}
std::string utf8(std::u16string_view s){
    std::string out;out.reserve(s.size());
    for(std::size_t n=0;n<s.size();++n){char32_t c=s[n];
        if(c>=0xD800&&c<=0xDBFF&&n+1<s.size()&&s[n+1]>=0xDC00&&s[n+1]<=0xDFFF){c=0x10000+((c-0xD800)<<10)+(s[n+1]-0xDC00);++n;}
        else if(c>=0xD800&&c<=0xDFFF)c=0xFFFD;
        if(c<0x80)out.push_back(static_cast<char>(c));
        else if(c<0x800){out.push_back(static_cast<char>(0xC0|(c>>6)));out.push_back(static_cast<char>(0x80|(c&0x3F)));}
        else if(c<0x10000){out.push_back(static_cast<char>(0xE0|(c>>12)));out.push_back(static_cast<char>(0x80|((c>>6)&0x3F)));out.push_back(static_cast<char>(0x80|(c&0x3F)));}
        else{out.push_back(static_cast<char>(0xF0|(c>>18)));out.push_back(static_cast<char>(0x80|((c>>12)&0x3F)));out.push_back(static_cast<char>(0x80|((c>>6)&0x3F)));out.push_back(static_cast<char>(0x80|(c&0x3F)));}
    }
    return out;
}
// A Mac family (Helvetica, Times, Menlo...) as the Windows family playing its
// role; Windows and unknown families stay as named.
std::string windowsFamily(std::string_view name){if(const auto mapped=notesWindowsFontFamily(name))return std::string(*mapped);return std::string(name);}
// The Mac family names AppKit falls back to, as their Windows counterparts.
constexpr std::string_view macHelvetica="Arial",macTimes="Times New Roman",macCourier="Courier New";

// ---------------------------------------------------------------------------
// RTF (Cocoa's RTF reader semantics, verified on the pinned oracle)
// ---------------------------------------------------------------------------
class RTFReader {
public:
    RTFReader(std::string_view data,const PasteImportOptions& o):data_(data),options_(o),out_(o.maximumUnits){}
    std::optional<ImportedRichText> run(){
        while(i_<data_.size()){
            const char c=data_[i_];
            if(c=='{'){flush();++i_;stack_.push_back(state_);skip_=0;if(stack_.size()>4096)return std::nullopt;continue;}
            if(c=='}'){flush();++i_;if(state_.dest==Dest::fonts&&fontEntry_)closeFont();if(stack_.empty())break;state_=stack_.back();stack_.pop_back();skip_=0;continue;}
            if(c=='\\'){control();continue;}
            ++i_;if(c=='\r'||c=='\n'||c=='\0')continue;
            if(skip_>0){--skip_;continue;}
            text(static_cast<unsigned char>(c));
        }
        flush();return out_.finish();
    }
private:
    enum class Dest:std::uint8_t {text,fonts,colors,skip};
    struct State {Dest dest{Dest::text};int font{-1},color{0},uc{1};double size{12};bool bold{},italic{},underline{},strike{};};
    struct Font {std::string name;int charset{0};};
    std::string_view data_;const PasteImportOptions& options_;Builder out_;std::size_t i_{};
    State state_;std::vector<State> stack_;int skip_{};
    std::map<int,Font> fonts_;int defaultFont_{0};std::uint32_t ansi_{1252};
    std::vector<std::optional<std::array<int,3>>> colors_;std::array<int,3> color_{-1,-1,-1};
    std::optional<int> fontEntry_;std::string fontBytes_;
    std::string bytes_;std::uint32_t bytesPage_{1252};

    const Font* font(int index)const{const auto it=fonts_.find(index<0?defaultFont_:index);return it==fonts_.end()?nullptr:&it->second;}
    std::uint32_t codePage()const{const auto*f=font(state_.font);return charsetCodePage(f?f->charset:0,ansi_);}
    Style style()const{
        Style s;const auto*f=font(state_.font);s.family=f&&!f->name.empty()?f->name:std::string(macHelvetica);
        s.size=state_.size;s.bold=state_.bold;s.italic=state_.italic;s.underline=state_.underline;s.strike=state_.strike;
        // Cocoa reads color tables as Generic RGB; black is the default ink.
        if(state_.color>0&&static_cast<std::size_t>(state_.color)<colors_.size())if(const auto&c=colors_[static_cast<std::size_t>(state_.color)];c&&(*c)[0]+(*c)[1]+(*c)[2]>0){
            const auto v=source::genericRGBToSRGB({(*c)[0]/255.,(*c)[1]/255.,(*c)[2]/255.});s.color=RGBA{std::clamp(v[0],0.,1.),std::clamp(v[1],0.,1.),std::clamp(v[2],0.,1.),1};}
        return s;
    }
    void flush(){if(bytes_.empty())return;const auto value=decodeBytes(bytesPage_,bytes_,options_);bytes_.clear();out_.append(value,style());}
    void emit(std::u16string_view value){flush();if(state_.dest==Dest::text)out_.append(value,style());}
    void text(unsigned char b){
        if(state_.dest==Dest::text){const auto page=codePage();if(!bytes_.empty()&&page!=bytesPage_)flush();bytesPage_=page;bytes_.push_back(static_cast<char>(b));}
        else if(state_.dest==Dest::fonts){if(b==';'){closeFont();}else if(fontEntry_)fontBytes_.push_back(static_cast<char>(b));}
        else if(state_.dest==Dest::colors&&b==';'){colors_.push_back(color_[0]<0&&color_[1]<0&&color_[2]<0?std::nullopt:std::optional(std::array<int,3>{std::max(0,color_[0]),std::max(0,color_[1]),std::max(0,color_[2])}));color_={-1,-1,-1};}
    }
    void closeFont(){if(!fontEntry_)return;auto&f=fonts_[*fontEntry_];
        const auto decoded=decodeBytes(charsetCodePage(f.charset,ansi_),fontBytes_,options_);f.name=windowsFamily(trim(utf8(decoded)));fontBytes_.clear();fontEntry_.reset();}
    void control(){
        ++i_;if(i_>=data_.size())return;const char c=data_[i_];
        if(std::isalpha(static_cast<unsigned char>(c))){
            const auto start=i_;while(i_<data_.size()&&std::isalpha(static_cast<unsigned char>(data_[i_]))&&i_-start<32)++i_;
            const std::string word(data_.substr(start,i_-start));std::optional<int> param;
            if(i_<data_.size()&&(data_[i_]=='-'||std::isdigit(static_cast<unsigned char>(data_[i_])))){
                const auto begin=i_;if(data_[i_]=='-')++i_;while(i_<data_.size()&&std::isdigit(static_cast<unsigned char>(data_[i_]))&&i_-begin<11)++i_;
                long long v{};const auto r=std::from_chars(data_.data()+begin,data_.data()+i_,v);if(r.ec==std::errc{})param=static_cast<int>(std::clamp<long long>(v,-2147483647LL,2147483647LL));}
            if(i_<data_.size()&&data_[i_]==' ')++i_;
            word_(word,param);return;
        }
        ++i_;
        if(c=='\''){
            int v{};bool ok=i_+2<=data_.size();for(int n=0;ok&&n<2;++n){const char h=data_[i_+n];v=v*16+(h>='0'&&h<='9'?h-'0':h>='a'&&h<='f'?h-'a'+10:h>='A'&&h<='F'?h-'A'+10:(ok=false,0));}
            if(!ok)return;i_+=2;if(skip_>0){--skip_;return;}text(static_cast<unsigned char>(v));return;
        }
        if(skip_>0){--skip_;return;}
        switch(c){
        case '\\':case '{':case '}':text(static_cast<unsigned char>(c));break;
        case '~':emit(u"\u00A0");break;
        case '_':emit(u"\u2011");break;
        case '*':flush();state_.dest=Dest::skip;break;
        case '\r':case '\n':emit(u"\n");break; // "\<newline>" is \par
        default:break; // \- optional hyphen, \| \: index symbols
        }
    }
    void word_(const std::string& w,std::optional<int> p){
        flush();
        const bool on=!p||*p!=0;
        if(w=="u"){if(!p)return;const auto unit=static_cast<char16_t>(*p<0?*p+65536:*p);if(skip_>0){--skip_;}else emit(std::u16string_view(&unit,1));skip_=std::max(0,state_.uc);return;}
        if(skip_>0){--skip_;return;}
        if(w=="bin"){i_=std::min(data_.size(),i_+static_cast<std::size_t>(std::max(0,p.value_or(0))));return;}
        // Document header.
        if(w=="ansicpg"){if(p&&*p>0)ansi_=static_cast<std::uint32_t>(*p);return;}
        if(w=="mac"){ansi_=10000;return;}if(w=="pc"){ansi_=437;return;}if(w=="pca"){ansi_=850;return;}
        if(w=="deff"){if(p)defaultFont_=*p;return;}
        if(w=="uc"){state_.uc=std::clamp(p.value_or(1),0,8);return;}
        // Destinations. Cocoa skips these; \listtext and \fldrslt are read.
        static constexpr std::array<std::string_view,58> skipped{"stylesheet","info","pict","object","header","headerl","headerr","headerf","footer","footerl","footerr","footerf",
            "footnote","pntext","pntxta","pntxtb","pn","xmlnstbl","listtable","listoverridetable","rsidtbl","generator","themedata","colorschememapping","latentstyles","datastore",
            "mmathPr","pgdsctbl","revtbl","filetbl","fldinst","bkmkstart","bkmkend","nonshppict","shp","shpinst","shptxt","userprops","docvar","annotation","atnid","atnauthor",
            "atrfstart","atrfend","template","listpicture","fchars","lchars","objdata","objclass","objname","sp","sn","sv","tc","xe","txe","picprop"};
        if(w=="fonttbl"){state_.dest=Dest::fonts;return;}
        if(w=="colortbl"){state_.dest=Dest::colors;colors_.clear();color_={-1,-1,-1};return;}
        if(std::find(skipped.begin(),skipped.end(),w)!=skipped.end()){state_.dest=Dest::skip;return;}
        if(state_.dest==Dest::fonts){if(w=="f"&&p){closeFont();fontEntry_=*p;fonts_[*p];}else if(w=="fcharset"&&p&&fontEntry_)fonts_[*fontEntry_].charset=*p;return;}
        if(state_.dest==Dest::colors){if(p&&(w=="red"||w=="green"||w=="blue"))color_[w=="red"?0:w=="green"?1:2]=std::clamp(*p,0,255);return;}
        if(state_.dest!=Dest::text)return;
        // Character formatting.
        if(w=="f"){state_.font=p.value_or(defaultFont_);return;}
        if(w=="fs"){state_.size=p&&*p>0?*p/2.:12;return;}
        if(w=="b"){state_.bold=on;return;}if(w=="i"){state_.italic=on;return;}
        if(w=="strike"){state_.strike=on;return;}if(w=="striked"){state_.strike=p.value_or(1)!=0;return;}
        if(w=="ulnone"){state_.underline=false;return;}
        if(w.starts_with("ul")&&w!="ulc"){state_.underline=on;return;}
        if(w=="cf"){state_.color=p.value_or(0);return;}
        if(w=="plain"){state_.font=-1;state_.size=12;state_.bold=state_.italic=state_.underline=state_.strike=false;state_.color=0;return;}
        // Characters (Cocoa: \cell and \sect add nothing, \row ends a line).
        if(w=="par"||w=="row"){emit(u"\n");return;}
        if(w=="line"){emit(u"\u2028");return;}if(w=="tab"){emit(u"\t");return;}if(w=="page"){emit(u"\f");return;}
        static constexpr std::array<std::pair<std::string_view,char16_t>,14> symbols{{{"emdash",u'\x2014'},{"endash",u'\x2013'},{"bullet",u'\x2022'},{"lquote",u'\x2018'},{"rquote",u'\x2019'},
            {"ldblquote",u'\x201C'},{"rdblquote",u'\x201D'},{"emspace",u'\x2003'},{"enspace",u'\x2002'},{"qmspace",u'\x2005'},{"zwj",u'\x200D'},{"zwnj",u'\x200C'},{"ltrmark",u'\x200E'},{"rtlmark",u'\x200F'}}};
        for(const auto&[name,unit]:symbols)if(w==name){emit(std::u16string_view(&unit,1));return;}
    }
};

// ---------------------------------------------------------------------------
// HTML (WebKit's attributed-string conversion as AppKit pastes it)
// ---------------------------------------------------------------------------
struct NamedColor {std::string_view name;std::uint32_t rgb;};
constexpr std::array<NamedColor,148> namedColors{{
    {"aliceblue",0xF0F8FF},{"antiquewhite",0xFAEBD7},{"aqua",0x00FFFF},{"aquamarine",0x7FFFD4},{"azure",0xF0FFFF},{"beige",0xF5F5DC},{"bisque",0xFFE4C4},{"black",0x000000},
    {"blanchedalmond",0xFFEBCD},{"blue",0x0000FF},{"blueviolet",0x8A2BE2},{"brown",0xA52A2A},{"burlywood",0xDEB887},{"cadetblue",0x5F9EA0},{"chartreuse",0x7FFF00},{"chocolate",0xD2691E},
    {"coral",0xFF7F50},{"cornflowerblue",0x6495ED},{"cornsilk",0xFFF8DC},{"crimson",0xDC143C},{"cyan",0x00FFFF},{"darkblue",0x00008B},{"darkcyan",0x008B8B},{"darkgoldenrod",0xB8860B},
    {"darkgray",0xA9A9A9},{"darkgreen",0x006400},{"darkgrey",0xA9A9A9},{"darkkhaki",0xBDB76B},{"darkmagenta",0x8B008B},{"darkolivegreen",0x556B2F},{"darkorange",0xFF8C00},{"darkorchid",0x9932CC},
    {"darkred",0x8B0000},{"darksalmon",0xE9967A},{"darkseagreen",0x8FBC8F},{"darkslateblue",0x483D8B},{"darkslategray",0x2F4F4F},{"darkslategrey",0x2F4F4F},{"darkturquoise",0x00CED1},{"darkviolet",0x9400D3},
    {"deeppink",0xFF1493},{"deepskyblue",0x00BFFF},{"dimgray",0x696969},{"dimgrey",0x696969},{"dodgerblue",0x1E90FF},{"firebrick",0xB22222},{"floralwhite",0xFFFAF0},{"forestgreen",0x228B22},
    {"fuchsia",0xFF00FF},{"gainsboro",0xDCDCDC},{"ghostwhite",0xF8F8FF},{"gold",0xFFD700},{"goldenrod",0xDAA520},{"gray",0x808080},{"green",0x008000},{"greenyellow",0xADFF2F},
    {"grey",0x808080},{"honeydew",0xF0FFF0},{"hotpink",0xFF69B4},{"indianred",0xCD5C5C},{"indigo",0x4B0082},{"ivory",0xFFFFF0},{"khaki",0xF0E68C},{"lavender",0xE6E6FA},
    {"lavenderblush",0xFFF0F5},{"lawngreen",0x7CFC00},{"lemonchiffon",0xFFFACD},{"lightblue",0xADD8E6},{"lightcoral",0xF08080},{"lightcyan",0xE0FFFF},{"lightgoldenrodyellow",0xFAFAD2},{"lightgray",0xD3D3D3},
    {"lightgreen",0x90EE90},{"lightgrey",0xD3D3D3},{"lightpink",0xFFB6C1},{"lightsalmon",0xFFA07A},{"lightseagreen",0x20B2AA},{"lightskyblue",0x87CEFA},{"lightslategray",0x778899},{"lightslategrey",0x778899},
    {"lightsteelblue",0xB0C4DE},{"lightyellow",0xFFFFE0},{"lime",0x00FF00},{"limegreen",0x32CD32},{"linen",0xFAF0E6},{"magenta",0xFF00FF},{"maroon",0x800000},{"mediumaquamarine",0x66CDAA},
    {"mediumblue",0x0000CD},{"mediumorchid",0xBA55D3},{"mediumpurple",0x9370DB},{"mediumseagreen",0x3CB371},{"mediumslateblue",0x7B68EE},{"mediumspringgreen",0x00FA9A},{"mediumturquoise",0x48D1CC},{"mediumvioletred",0xC71585},
    {"midnightblue",0x191970},{"mintcream",0xF5FFFA},{"mistyrose",0xFFE4E1},{"moccasin",0xFFE4B5},{"navajowhite",0xFFDEAD},{"navy",0x000080},{"oldlace",0xFDF5E6},{"olive",0x808000},
    {"olivedrab",0x6B8E23},{"orange",0xFFA500},{"orangered",0xFF4500},{"orchid",0xDA70D6},{"palegoldenrod",0xEEE8AA},{"palegreen",0x98FB98},{"paleturquoise",0xAFEEEE},{"palevioletred",0xDB7093},
    {"papayawhip",0xFFEFD5},{"peachpuff",0xFFDAB9},{"peru",0xCD853F},{"pink",0xFFC0CB},{"plum",0xDDA0DD},{"powderblue",0xB0E0E6},{"purple",0x800080},{"rebeccapurple",0x663399},
    {"red",0xFF0000},{"rosybrown",0xBC8F8F},{"royalblue",0x4169E1},{"saddlebrown",0x8B4513},{"salmon",0xFA8072},{"sandybrown",0xF4A460},{"seagreen",0x2E8B57},{"seashell",0xFFF5EE},
    {"sienna",0xA0522D},{"silver",0xC0C0C0},{"skyblue",0x87CEEB},{"slateblue",0x6A5ACD},{"slategray",0x708090},{"slategrey",0x708090},{"snow",0xFFFAFA},{"springgreen",0x00FF7F},
    {"steelblue",0x4682B4},{"tan",0xD2B48C},{"teal",0x008080},{"thistle",0xD8BFD8},{"tomato",0xFF6347},{"turquoise",0x40E0D0},{"violet",0xEE82EE},{"wheat",0xF5DEB3},
    {"white",0xFFFFFF},{"whitesmoke",0xF5F5F5},{"yellow",0xFFFF00},{"yellowgreen",0x9ACD32}}};
struct Entity {std::string_view name;char32_t value;};
constexpr std::array<Entity,58> entities{{{"amp",'&'},{"lt",'<'},{"gt",'>'},{"quot",'"'},{"apos",'\''},{"nbsp",0xA0},{"copy",0xA9},{"reg",0xAE},{"trade",0x2122},
    {"hellip",0x2026},{"mdash",0x2014},{"ndash",0x2013},{"lsquo",0x2018},{"rsquo",0x2019},{"ldquo",0x201C},{"rdquo",0x201D},{"sbquo",0x201A},{"bdquo",0x201E},{"bull",0x2022},
    {"middot",0xB7},{"euro",0x20AC},{"yen",0xA5},{"pound",0xA3},{"cent",0xA2},{"sect",0xA7},{"para",0xB6},{"deg",0xB0},{"plusmn",0xB1},{"times",0xD7},{"divide",0xF7},
    {"laquo",0xAB},{"raquo",0xBB},{"lsaquo",0x2039},{"rsaquo",0x203A},{"shy",0xAD},{"iexcl",0xA1},{"iquest",0xBF},{"ensp",0x2002},{"emsp",0x2003},{"thinsp",0x2009},
    {"zwnj",0x200C},{"zwj",0x200D},{"lrm",0x200E},{"rlm",0x200F},{"dagger",0x2020},{"Dagger",0x2021},{"permil",0x2030},{"larr",0x2190},{"rarr",0x2192},{"uarr",0x2191},
    {"darr",0x2193},{"harr",0x2194},{"check",0x2713},{"micro",0xB5},{"frac12",0xBD},{"frac14",0xBC},{"frac34",0xBE},{"ordm",0xBA}}};

// A leading CSS/HTML number ("14", "+1", "1.5", "50"); the unit may follow.
std::optional<double> number(std::string_view s){while(!s.empty()&&s.front()==' ')s.remove_prefix(1);if(s.empty()||s.size()>64)return std::nullopt;
    const std::string copy(s);char* end{};const double v=std::strtod(copy.c_str(),&end);if(end==copy.c_str()||!std::isfinite(v))return std::nullopt;return v;}
// One UTF-8 scalar or character reference of `s` at `n` (advanced past it).
char32_t readScalar(std::string_view s,std::size_t& n){
    const auto b=static_cast<unsigned char>(s[n]);const std::size_t length=b<0x80?1:(b>>5)==6?2:(b>>4)==14?3:(b>>3)==30?4:1;
    if(length==1||n+length>s.size()){++n;return b<0x80?b:0xFFFD;}
    char32_t c=length==2?b&0x1F:length==3?b&0x0F:b&0x07;
    for(std::size_t k=1;k<length;++k){const auto x=static_cast<unsigned char>(s[n+k]);if((x>>6)!=2){++n;return 0xFFFD;}c=(c<<6)|(x&0x3F);}
    n+=length;return c>0x10FFFF||(c>=0xD800&&c<=0xDFFF)?0xFFFD:c;
}
char32_t readEntity(std::string_view s,std::size_t& n){
    const auto semicolon=s.find(';',n);if(semicolon==std::string_view::npos||semicolon-n>32){++n;return '&';}
    const auto name=s.substr(n+1,semicolon-n-1);
    if(name.starts_with("#")){const bool hex=name.size()>1&&(name[1]=='x'||name[1]=='X');std::uint32_t v{};const auto digits=name.substr(hex?2:1);
        const auto r=std::from_chars(digits.data(),digits.data()+digits.size(),v,hex?16:10);if(digits.empty()||r.ec!=std::errc{}||r.ptr!=digits.data()+digits.size()){++n;return '&';}
        n=semicolon+1;return v==0||v>0x10FFFF||(v>=0xD800&&v<=0xDFFF)?0xFFFD:v;}
    for(const auto&e:entities)if(name==e.name){n=semicolon+1;return e.value;}
    ++n;return '&';
}
std::string decodeText(std::string_view raw){std::u16string out;std::size_t n=0;while(n<raw.size())appendScalar(out,raw[n]=='&'?readEntity(raw,n):readScalar(raw,n));return utf8(out);}
// CSS colors; nullopt for unknown values. WebKit omits black (the default
// ink) and fully transparent colors; alpha keeps the 8-bit stored value.
struct ParsedColor {std::optional<RGBA> value;};
std::optional<ParsedColor> cssColor(std::string_view raw){
    const auto v=lower(trim(raw));
    const auto finish=[](double r,double g,double b,double a)->ParsedColor{
        a=std::round(std::clamp(a,0.,1.)*255)/255;r=std::clamp(r,0.,255.);g=std::clamp(g,0.,255.);b=std::clamp(b,0.,255.);
        if(a==0||(r==0&&g==0&&b==0&&a==1))return {};return {RGBA{std::round(r)/255,std::round(g)/255,std::round(b)/255,a}};};
    if(v=="transparent")return ParsedColor{};
    if(v.starts_with("#")){const auto h=v.substr(1);const auto hex=[&](std::size_t at,std::size_t n)->int{int out{};for(std::size_t k=0;k<n;++k){const char c=h[at+k];out=out*16+(c>='0'&&c<='9'?c-'0':c>='a'&&c<='f'?c-'a'+10:0);}return n==1?out*17:out;};
        if(!std::all_of(h.begin(),h.end(),[](char c){return std::isxdigit(static_cast<unsigned char>(c));}))return std::nullopt;
        if(h.size()==3||h.size()==4)return finish(hex(0,1),hex(1,1),hex(2,1),h.size()==4?hex(3,1)/255.:1);
        if(h.size()==6||h.size()==8)return finish(hex(0,2),hex(2,2),hex(4,2),h.size()==8?hex(6,2)/255.:1);return std::nullopt;}
    if(v.starts_with("rgb")){const auto open=v.find('('),close=v.rfind(')');if(open==std::string::npos||close==std::string::npos||close<open)return std::nullopt;
        std::string inner=v.substr(open+1,close-open-1);for(auto&c:inner)if(c==','||c=='/')c=' ';
        std::vector<std::string> parts;std::size_t at=0;while(at<inner.size()){while(at<inner.size()&&inner[at]==' ')++at;const auto from=at;while(at<inner.size()&&inner[at]!=' ')++at;if(at>from)parts.push_back(inner.substr(from,at-from));}
        if(parts.size()<3||parts.size()>4)return std::nullopt;std::array<double,4> c{0,0,0,1};
        for(std::size_t n=0;n<parts.size();++n){const bool percent=parts[n].ends_with("%");const auto value=number(std::string_view(parts[n]).substr(0,parts[n].size()-(percent?1:0)));if(!value)return std::nullopt;
            c[n]=n<3?(percent?*value*2.55:*value):(percent?*value/100:*value);}
        return finish(c[0],c[1],c[2],c[3]);}
    for(const auto&named:namedColors)if(v==named.name)return finish((named.rgb>>16)&0xFF,(named.rgb>>8)&0xFF,named.rgb&0xFF,1);
    return std::nullopt;
}

class HTMLReader {
public:
    HTMLReader(std::string_view html,std::size_t from,std::size_t to,const PasteImportOptions& o):html_(html),from_(from),to_(to),options_(o),out_(o.maximumUnits){
        Frame root;root.style.family=std::string(macTimes);stack_.push_back(std::move(root));}
    std::optional<ImportedRichText> run(){
        while(at_<html_.size()){
            if(html_[at_]=='<'&&tag())continue;
            const auto next=html_.find('<',at_+1);const auto end=next==std::string_view::npos?html_.size():next;
            textNode(at_,end);at_=end;
        }
        // Trailing collapsible space and a pending top-level break end the text.
        return out_.finish();
    }
private:
    struct Frame {
        std::string tag;Style style;bool block{},pre{},mediumSize{true},emitted{};
        int list{0};int counter{0};bool ordered{};
    };
    std::string_view html_;std::size_t from_,to_;const PasteImportOptions& options_;Builder out_;std::size_t at_{};
    std::vector<Frame> stack_;bool lineContent_{},pendingSpace_{};Style spaceStyle_;std::optional<Style> pendingBreak_;bool skipNewline_{};

    bool inside(std::size_t position)const{return position>=from_&&position<to_;}
    bool inBlock()const{return std::any_of(stack_.begin()+1,stack_.end(),[](const Frame&f){return f.block;});}
    void markEmitted(){for(auto&f:stack_)f.emitted=true;}
    void put(std::u16string_view value,const Style&s){out_.append(value,s);markEmitted();}
    void flushInline(std::size_t position){
        if(!inside(position))return;
        if(pendingBreak_){put(inBlock()?u"\u2028":u"\n",*pendingBreak_);pendingBreak_.reset();lineContent_=false;pendingSpace_=false;}
        if(pendingSpace_){put(u" ",spaceStyle_);pendingSpace_=false;}
    }
    void textNode(std::size_t begin,std::size_t end){
        const auto&top=stack_.back();if(top.tag=="script"||top.tag=="style"||top.tag=="title"||top.tag=="head")return;
        const auto source=html_.substr(0,end);std::u16string chunk;const auto emitChunk=[&]{if(!chunk.empty()){put(chunk,top.style);chunk.clear();}};
        std::size_t n=begin;
        while(n<end){
            const auto position=n;const char32_t c=source[n]=='&'?readEntity(source,n):readScalar(source,n);
            if(!inside(position)){skipNewline_=false;continue;}
            if(top.pre){
                // <pre> keeps its whitespace; a newline right after the start tag is dropped.
                if(skipNewline_&&(c=='\n'||c=='\r')){if(c=='\n')skipNewline_=false;continue;}skipNewline_=false;if(c=='\r')continue;
                if(pendingBreak_||pendingSpace_){emitChunk();flushInline(position);}appendScalar(chunk,c);lineContent_=c!='\n';continue;
            }
            if(c==' '||c=='\t'||c=='\n'||c=='\r'||c=='\f'){if(lineContent_&&!pendingSpace_){emitChunk();pendingSpace_=true;spaceStyle_=top.style;}continue;}
            if(pendingBreak_||pendingSpace_){emitChunk();flushInline(position);}
            appendScalar(chunk,c);lineContent_=true;
        }
        emitChunk();
    }
    // Returns false when '<' does not start markup (it is then text).
    bool tag(){
        const auto start=at_;if(start+1>=html_.size())return false;const char next=html_[start+1];
        if(html_.substr(start,4)=="<!--"){const auto close=html_.find("-->",start+4);at_=close==std::string_view::npos?html_.size():close+3;return true;}
        if(next=='!'||next=='?'){const auto close=html_.find('>',start);at_=close==std::string_view::npos?html_.size():close+1;return true;}
        const bool closing=next=='/';auto n=start+(closing?2:1);if(n>=html_.size()||!std::isalpha(static_cast<unsigned char>(html_[n])))return false;
        const auto nameStart=n;while(n<html_.size()&&(std::isalnum(static_cast<unsigned char>(html_[n]))||html_[n]=='-'||html_[n]==':'))++n;
        const auto name=lower(html_.substr(nameStart,n-nameStart));
        std::map<std::string,std::string> attributes;bool selfClosing=false;
        while(n<html_.size()&&html_[n]!='>'){
            if(std::isspace(static_cast<unsigned char>(html_[n]))){++n;continue;}
            if(html_[n]=='/'){selfClosing=true;++n;continue;}
            const auto attrStart=n;while(n<html_.size()&&!std::isspace(static_cast<unsigned char>(html_[n]))&&html_[n]!='='&&html_[n]!='>'&&html_[n]!='/')++n;
            auto attribute=lower(html_.substr(attrStart,n-attrStart));while(n<html_.size()&&std::isspace(static_cast<unsigned char>(html_[n])))++n;
            std::string value;
            if(n<html_.size()&&html_[n]=='='){++n;while(n<html_.size()&&std::isspace(static_cast<unsigned char>(html_[n])))++n;
                if(n<html_.size()&&(html_[n]=='"'||html_[n]=='\'')){const char q=html_[n++];const auto close=html_.find(q,n);const auto e=close==std::string_view::npos?html_.size():close;value=decodeAttribute(html_.substr(n,e-n));n=std::min(html_.size(),e+1);}
                else{const auto vs=n;while(n<html_.size()&&!std::isspace(static_cast<unsigned char>(html_[n]))&&html_[n]!='>')++n;value=decodeAttribute(html_.substr(vs,n-vs));}}
            if(!attribute.empty())attributes.emplace(std::move(attribute),std::move(value));
        }
        at_=std::min(html_.size(),n+1);
        if(closing)close(name,start);else open(name,attributes,selfClosing,start);
        // Raw text elements: their content is never text.
        if(!closing&&(name=="script"||name=="style"||name=="title"||name=="textarea")){const auto end=lower(html_.substr(at_)).find("</"+name);at_=end==std::string::npos?html_.size():at_+end;}
        return true;
    }
    static std::string decodeAttribute(std::string_view raw){return decodeText(raw);}
    static bool blockTag(std::string_view t){
        static constexpr std::array<std::string_view,36> blocks{"p","div","h1","h2","h3","h4","h5","h6","ul","ol","li","pre","blockquote","table","tr","td","th","tbody","thead","tfoot",
            "dl","dt","dd","section","article","header","footer","nav","aside","main","figure","figcaption","address","center","caption","details"};
        return std::find(blocks.begin(),blocks.end(),t)!=blocks.end();
    }
    void blockStart(std::size_t position,const Style& previous){
        pendingSpace_=false;
        if(!inside(position)){lineContent_=false;pendingBreak_.reset();return;}
        if(pendingBreak_){put(inBlock()?u"\u2028":u"\n",*pendingBreak_);pendingBreak_.reset();lineContent_=false;}
        if(lineContent_){put(u"\n",previous);lineContent_=false;}
    }
    void blockEnd(const Frame& frame,std::size_t position){
        pendingSpace_=false;const bool trailingBreak=pendingBreak_.has_value();pendingBreak_.reset();
        if(!inside(position)){lineContent_=false;return;}
        // A trailing <br> is the block's own line end; an empty <p> still
        // occupies one line.
        if(lineContent_||trailingBreak||(frame.tag=="p"&&!frame.emitted)){put(u"\n",frame.style);lineContent_=false;}
    }
    void open(const std::string& name,const std::map<std::string,std::string>& a,bool selfClosing,std::size_t position){
        auto& parent=stack_.back();Frame f;f.tag=name;f.style=parent.style;f.pre=parent.pre;f.mediumSize=parent.mediumSize;
        const auto attribute=[&](const char* key)->const std::string*{const auto it=a.find(key);return it==a.end()?nullptr:&it->second;};
        if(name=="br"){if(inside(position)){if(pendingBreak_)flushInline(position);if(pendingSpace_){put(u" ",spaceStyle_);pendingSpace_=false;}pendingBreak_=parent.style;}lineContent_=false;return;}
        if(name=="hr"){Style s=parent.style;s.color=RGBA{128/255.,128/255.,128/255.,1};blockStart(position,s);if(inside(position)){put(u"\n",s);lineContent_=false;}return;}
        if(name=="img"||name=="meta"||name=="link"||name=="input"||name=="wbr"||name=="col"||name=="area"||name=="base"||name=="param"||name=="source"||name=="track"||name=="embed")return;
        // Presentational defaults (WebKit's user agent style sheet, 12 pt medium).
        auto& s=f.style;
        if(name=="b"||name=="strong")s.bold=true;
        else if(name=="i"||name=="em"||name=="cite"||name=="var"||name=="dfn")s.italic=true;
        else if(name=="u"||name=="ins")s.underline=true;
        else if(name=="s"||name=="strike"||name=="del")s.strike=true;
        else if(name=="a"&&attribute("href")){s.color=RGBA{0,0,238/255.,1};s.underline=true;}
        else if(name=="code"||name=="tt"||name=="kbd"||name=="samp"||name=="pre"){family(f,"monospace");if(name=="pre"){f.pre=true;skipNewline_=true;}}
        else if(name=="small"||name=="sub"||name=="sup"){s.size/=1.2;f.mediumSize=false;}
        else if(name=="big"){s.size*=1.2;f.mediumSize=false;}
        else if(name.size()==2&&name[0]=='h'&&name[1]>='1'&&name[1]<='6'){constexpr std::array<double,6> sizes{24,18,14,12,10,9};s.size=sizes[static_cast<std::size_t>(name[1]-'1')];s.bold=true;f.mediumSize=false;}
        if(name=="font"){if(const auto*face=attribute("face"))family(f,*face);if(const auto*color=attribute("color"))if(const auto c=cssColor(*color))s.color=c->value;
            if(const auto*size=attribute("size"))if(const auto v=number(*size)){constexpr std::array<double,7> sizes{9,10,12,14,18,24,36};int index=static_cast<int>(*v);
                if(!size->empty()&&((*size)[0]=='+'||(*size)[0]=='-'))index=3+index;s.size=sizes[static_cast<std::size_t>(std::clamp(index,1,7)-1)];f.mediumSize=index==3;}}
        if(const auto*style=attribute("style"))css(f,*style,parent);
        f.block=blockTag(name);
        if(name=="ul"||name=="ol"){f.ordered=name=="ol";f.list=parent.list+1;f.counter=0;if(const auto*startAt=attribute("start"))if(const auto v=number(*startAt))f.counter=static_cast<int>(*v)-1;}
        else f.list=parent.list;
        if(f.block)blockStart(position,parent.style);
        stack_.push_back(std::move(f));
        if(name=="li"){Frame* list=nullptr;for(auto it=stack_.rbegin()+1;it!=stack_.rend();++it)if(it->tag=="ul"||it->tag=="ol"){list=&*it;break;}
            std::u16string marker=u"\t";if(list&&list->ordered){++list->counter;for(const char c:std::to_string(list->counter))marker.push_back(static_cast<char16_t>(c));}
            else{const int depth=list?list->list:1;marker.push_back(depth<=1?u'\u2022':depth==2?u'\u25E6':u'\u25AA');}
            marker.push_back(u'\t');if(inside(position)){put(marker,stack_.back().style);lineContent_=true;}}
        if(selfClosing&&!f.block)stack_.pop_back();
    }
    void close(const std::string& name,std::size_t position){
        for(std::size_t n=stack_.size();n-->1;)if(stack_[n].tag==name){
            while(stack_.size()>n){const auto frame=std::move(stack_.back());stack_.pop_back();if(frame.block)blockEnd(frame,position);}
            return;}
    }
    void family(Frame& f,std::string_view list){
        std::string_view rest=list;while(!rest.empty()){const auto comma=rest.find(',');auto item=trim(rest.substr(0,comma));rest=comma==std::string_view::npos?std::string_view{}:rest.substr(comma+1);
            if(item.size()>=2&&(item.front()=='"'||item.front()=='\'')&&item.back()==item.front())item=item.substr(1,item.size()-2);item=trim(item);if(item.empty())continue;
            const auto key=lower(item);
            if(key=="serif"){f.style.family=std::string(macTimes);return;}
            if(key=="sans-serif"){f.style.family=std::string(macHelvetica);return;}
            if(key=="monospace"){f.style.family=std::string(macCourier);if(f.mediumSize)f.style.size=13;return;}
            if(key=="cursive"||key=="fantasy"||key=="inherit"||key=="initial")continue;
            auto mapped=windowsFamily(item);if(options_.fontAvailable&&!options_.fontAvailable(mapped))continue;
            f.style.family=std::move(mapped);return;}
    }
    void css(Frame& f,std::string_view declarations,const Frame& parent){
        std::size_t at=0;while(at<declarations.size()){
            const auto semi=declarations.find(';',at);const auto decl=declarations.substr(at,semi==std::string_view::npos?std::string_view::npos:semi-at);at=semi==std::string_view::npos?declarations.size():semi+1;
            const auto colon=decl.find(':');if(colon==std::string_view::npos)continue;
            const auto property=lower(trim(decl.substr(0,colon)));auto value=trim(decl.substr(colon+1));
            if(const auto important=lower(value).find("!important");important!=std::string::npos)value=trim(std::string_view(value).substr(0,important));
            const auto v=lower(value);auto& s=f.style;
            if(property=="color"){if(const auto c=cssColor(value))s.color=c->value;}
            else if(property=="font-family")family(f,value);
            else if(property=="font-weight"){if(v=="bold"||v=="bolder")s.bold=true;else if(v=="normal"||v=="lighter")s.bold=false;else if(const auto w=number(v))s.bold=*w>=600;}
            else if(property=="font-style"){if(v=="italic"||v.starts_with("oblique"))s.italic=true;else if(v=="normal")s.italic=false;}
            else if(property=="text-decoration"||property=="text-decoration-line"){if(v.find("none")!=std::string::npos){s.underline=s.strike=false;}else{if(v.find("underline")!=std::string::npos)s.underline=true;if(v.find("line-through")!=std::string::npos)s.strike=true;}}
            else if(property=="font-size"){
                static constexpr std::array<std::pair<std::string_view,double>,8> keywords{{{"xx-small",9},{"x-small",9},{"small",10},{"medium",12},{"large",14},{"x-large",18},{"xx-large",24},{"xxx-large",36}}};
                bool done=false;for(const auto&[k,size]:keywords)if(v==k){s.size=size;f.mediumSize=k=="medium";done=true;}
                if(!done){if(v=="smaller"){s.size=parent.style.size/1.2;f.mediumSize=false;}else if(v=="larger"){s.size=parent.style.size*1.2;f.mediumSize=false;}
                else if(const auto n=number(v)){const auto unitAt=v.find_first_not_of("0123456789.+-");const auto unit=unitAt==std::string::npos?std::string{}:v.substr(unitAt);
                    const double size=unit=="px"?*n:unit=="pt"?*n*4/3:unit=="em"?*n*parent.style.size:unit=="rem"?*n*12:unit=="%"?*n/100*parent.style.size:-1;
                    if(size>0){s.size=size;f.mediumSize=false;}}}
            }
        }
    }
};
// CF_HTML header offsets (bytes into the whole clipboard data).
std::optional<long long> headerOffset(std::string_view data,std::string_view key){
    const auto at=data.find(key);if(at==std::string_view::npos)return std::nullopt;const auto begin=at+key.size();auto end=begin;
    while(end<data.size()&&(std::isdigit(static_cast<unsigned char>(data[end]))||data[end]=='-'))++end;long long v{};
    const auto r=std::from_chars(data.data()+begin,data.data()+end,v);if(r.ec!=std::errc{})return std::nullopt;return v;
}
}

std::string exportNotesRTF(std::u16string_view text,const RichText& rich,const NotesRTFExportOptions& options){
    // Dense segments: sparse source runs leave gaps in the default style.
    struct Segment {std::uint32_t begin{},end{};const TextStyle* style{};};
    static const TextStyle plain{};std::vector<Segment> segments;std::uint32_t at{};
    for(const auto&run:rich.runs){if(run.location>at)segments.push_back({at,run.location,&plain});segments.push_back({run.location,run.location+run.length,&run.style});at=run.location+run.length;}
    if(at<text.size())segments.push_back({at,static_cast<std::uint32_t>(text.size()),&plain});
    std::vector<std::string> fonts{options.defaultFamily.empty()?std::string("Segoe UI"):options.defaultFamily};std::vector<std::array<int,3>> colors;
    const auto fontIndex=[&](const TextStyle& s){const auto name=s.fontName?windowsFamily(*s.fontName):fonts.front();for(std::size_t n=0;n<fonts.size();++n)if(fonts[n]==name)return n;fonts.push_back(name);return fonts.size()-1;};
    const auto byte=[](double v){return static_cast<int>(std::lround(std::clamp(v,0.,1.)*255));};
    const auto colorIndex=[&](const TextStyle& s)->std::size_t{if(!s.color)return 0;const std::array<int,3> c{byte(s.color->red),byte(s.color->green),byte(s.color->blue)};
        for(std::size_t n=0;n<colors.size();++n)if(colors[n]==c)return n+1;colors.push_back(c);return colors.size();};
    std::string body;body.reserve(text.size()+segments.size()*32);
    for(const auto&segment:segments){
        const auto&s=*segment.style;
        body+="\\f"+std::to_string(fontIndex(s))+"\\fs"+std::to_string(static_cast<int>(std::lround(std::clamp(s.fontSize,1.,1638.)*2)));
        body+=s.bold?"\\b":"\\b0";body+=s.italic?"\\i":"\\i0";body+=s.underline?"\\ul":"\\ulnone";body+=s.strikethrough?"\\strike":"\\strike0";
        body+="\\cf"+std::to_string(colorIndex(s))+" ";
        for(auto n=segment.begin;n<segment.end&&n<text.size();++n){const auto c=text[n];
            if(c=='\\'||c=='{'||c=='}'){body+='\\';body+=static_cast<char>(c);}
            else if(c=='\n')body+="\\par\r\n";else if(c==0x2028)body+="\\line ";else if(c=='\t')body+="\\tab ";
            else if(c>=0x20&&c<0x7F)body+=static_cast<char>(c);
            else if(c<0x20)continue; // other controls are not RTF text
            else body+="\\u"+std::to_string(static_cast<int>(static_cast<std::int16_t>(c)))+"?";
        }
    }
    std::string out="{\\rtf1\\ansi\\ansicpg1252\\uc1\\deff0{\\fonttbl";
    const auto escapeName=[](const std::string& name){std::string value;const auto units=[&]{std::u16string u;for(std::size_t n=0;n<name.size();){char32_t c;const auto b=static_cast<unsigned char>(name[n]);std::size_t length=b<0x80?1:(b>>5)==6?2:(b>>4)==14?3:4;c=length==1?b:length==2?b&0x1F:length==3?b&0x0F:b&0x07;for(std::size_t k=1;k<length&&n+k<name.size();++k)c=(c<<6)|(static_cast<unsigned char>(name[n+k])&0x3F);n+=length;appendScalar(u,c);}return u;}();
        for(const auto c:units){if(c=='\\'||c=='{'||c=='}'||c==';')continue;if(c>=0x20&&c<0x7F)value+=static_cast<char>(c);else if(c>=0x80)value+="\\u"+std::to_string(static_cast<int>(static_cast<std::int16_t>(c)))+"?";}return value;};
    for(std::size_t n=0;n<fonts.size();++n)out+="{\\f"+std::to_string(n)+"\\fnil\\fcharset0 "+escapeName(fonts[n])+";}";
    out+="}{\\colortbl;";for(const auto&c:colors)out+="\\red"+std::to_string(c[0])+"\\green"+std::to_string(c[1])+"\\blue"+std::to_string(c[2])+";";
    out+="}\r\n"+body+"}";
    return out;
}
std::optional<ImportedRichText> importNotesRTF(std::string_view rtf,const PasteImportOptions& options){
    std::size_t start=0;while(start<rtf.size()&&std::isspace(static_cast<unsigned char>(rtf[start])))++start;
    if(rtf.substr(start,5)!="{\\rtf")return std::nullopt;
    return RTFReader(rtf.substr(start),options).run();
}
std::optional<ImportedRichText> importNotesHTML(std::string_view data,const PasteImportOptions& options){
    std::string_view html=data;std::size_t from=0,to=std::string_view::npos;
    if(data.starts_with("Version:")){
        const auto startHTML=headerOffset(data,"StartHTML:"),endHTML=headerOffset(data,"EndHTML:");
        const auto startFragment=headerOffset(data,"StartFragment:"),endFragment=headerOffset(data,"EndFragment:");
        long long base=0;
        if(startHTML&&*startHTML>=0&&static_cast<std::size_t>(*startHTML)<data.size()){base=*startHTML;const auto end=endHTML&&*endHTML>*startHTML&&static_cast<std::size_t>(*endHTML)<=data.size()?*endHTML:static_cast<long long>(data.size());html=data.substr(static_cast<std::size_t>(base),static_cast<std::size_t>(end-base));}
        else{const auto tag=data.find('<');if(tag==std::string_view::npos)return std::nullopt;base=static_cast<long long>(tag);html=data.substr(tag);}
        if(startFragment&&endFragment&&*startFragment>=base&&*endFragment>=*startFragment){from=static_cast<std::size_t>(*startFragment-base);to=static_cast<std::size_t>(*endFragment-base);}
    }
    if(to==std::string_view::npos){const auto begin=html.find("<!--StartFragment-->"),end=html.find("<!--EndFragment-->");
        if(begin!=std::string_view::npos&&end!=std::string_view::npos&&end>begin){from=begin+20;to=end;}else to=html.size();}
    to=std::min(to,html.size());from=std::min(from,to);
    return HTMLReader(html,from,to,options).run();
}
}
