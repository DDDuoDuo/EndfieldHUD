#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/archive_model.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <icu.h>
#else
#include <unicode/uchar.h>
#include <unicode/unorm2.h>
#include <unicode/ustring.h>
#include <unicode/utf16.h>
#endif

namespace endfield::modules {
namespace {
using Json=ehud::data::Json;

char asciiLower(char c)noexcept{return c>='A'&&c<='Z'?static_cast<char>(c-'A'+'a'):c;}
bool asciiEqualFold(std::string_view a,std::string_view b)noexcept{
    if(a.size()!=b.size())return false;
    for(std::size_t n=0;n<a.size();++n)if(asciiLower(a[n])!=asciiLower(b[n]))return false;
    return true;
}
bool asciiStartsFold(std::string_view value,std::string_view prefix)noexcept{return value.size()>=prefix.size()&&asciiEqualFold(value.substr(0,prefix.size()),prefix);}

const UNormalizer2*normalizer(int kind){
    UErrorCode error=U_ZERO_ERROR;
    const UNormalizer2*value=kind==0?unorm2_getNFCInstance(&error):kind==1?unorm2_getNFDInstance(&error):unorm2_getNFKDInstance(&error);
    if(U_FAILURE(error)||!value)throw std::runtime_error("Cannot load Now Playing Unicode normalizer");
    return value;
}
std::u16string normalize(std::u16string_view text,int kind){
    if(text.empty())return {};
    const auto*n=normalizer(kind);UErrorCode error=U_ZERO_ERROR;
    const auto length=unorm2_normalize(n,reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),nullptr,0,&error);
    if(error!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(error))throw std::runtime_error("Cannot normalize Now Playing text");
    std::u16string out(static_cast<std::size_t>(length),u'\0');error=U_ZERO_ERROR;
    unorm2_normalize(n,reinterpret_cast<const UChar*>(text.data()),static_cast<int32_t>(text.size()),reinterpret_cast<UChar*>(out.data()),length,&error);
    if(U_FAILURE(error))throw std::runtime_error("Cannot normalize Now Playing text");
    return out;
}
std::vector<UChar32>codePoints(std::u16string_view text){
    std::vector<UChar32>out;out.reserve(text.size());
    for(int32_t at=0;at<static_cast<int32_t>(text.size());){UChar32 c;U16_NEXT(text.data(),at,static_cast<int32_t>(text.size()),c);out.push_back(c);}
    return out;
}
void appendUTF16(std::u16string&out,UChar32 c){
    if(c<0x10000)out.push_back(static_cast<char16_t>(c));
    else{c-=0x10000;out.push_back(static_cast<char16_t>(0xd800+(c>>10)));out.push_back(static_cast<char16_t>(0xdc00+(c&1023)));}
}
std::u16string utf16(std::span<const UChar32>values){std::u16string out;out.reserve(values.size());for(auto c:values)appendUTF16(out,c);return out;}
std::vector<UChar32>decomposition(const UNormalizer2*n,UChar32 c){
    UChar buffer[32];UErrorCode error=U_ZERO_ERROR;
    const auto length=unorm2_getDecomposition(n,c,buffer,32,&error);
    if(U_FAILURE(error)||length<0)return {};
    return codePoints(std::u16string_view(reinterpret_cast<const char16_t*>(buffer),static_cast<std::size_t>(length)));
}
// CFStringFold applies diacritic stripping to Latin, Greek and Cyrillic bases
// (source oracle: kana, Han, Hangul, Armenian, Arabic and Indic keep theirs).
bool foldsDiacritics(UChar32 c)noexcept{return c<0x0510||(c>=0x1e00&&c<0x2000);}
bool kana(UChar32 c)noexcept{return (c>=0x3040&&c<=0x30ff)||(c>=0x31f0&&c<=0x31ff)||(c>=0xff66&&c<=0xff9d);}
// Separate combining marks removed after an unchanged/Latin cluster base:
// generic diacritic blocks always; kana voicing marks only off a kana base
// (source oracle keeps a non-composing kana + U+3099 but strips Latin + U+3099).
bool strippableMark(UChar32 c,UChar32 base)noexcept{
    if((c>=0x0300&&c<=0x036f)||(c>=0x1ab0&&c<=0x1aff)||(c>=0x1dc0&&c<=0x1dff)||(c>=0x20d0&&c<=0x20ff)||(c>=0xfe20&&c<=0xfe2f))return true;
    return (c==0x3099||c==0x309a||c==0xff9e||c==0xff9f)&&!kana(base);
}
bool extendsCluster(UChar32 c)noexcept{
    const auto value=u_getIntPropertyValue(c,UCHAR_GRAPHEME_CLUSTER_BREAK);
    return value==U_GCB_EXTEND||value==U_GCB_SPACING_MARK||value==U_GCB_ZWJ;
}
bool alphanumeric(UChar32 c)noexcept{return (U_GET_GC_MASK(c)&(U_GC_L_MASK|U_GC_M_MASK|U_GC_N_MASK))!=0;}
// One cluster base: width, diacritic and case folding in CFStringFold order.
std::vector<UChar32>foldBase(UChar32 base,bool&changed){
    std::vector<UChar32>value{base};changed=false;
    const auto type=u_getIntPropertyValue(base,UCHAR_DECOMPOSITION_TYPE);
    if(type==U_DT_WIDE||type==U_DT_NARROW){auto wide=decomposition(normalizer(2),base);if(!wide.empty()){value=std::move(wide);changed=true;}}
    if(!value.empty()){const auto parts=decomposition(normalizer(1),value.front());if(parts.size()>1&&foldsDiacritics(parts.front())){value.front()=parts.front();changed=true;}}
    const auto source=utf16(value);UErrorCode error=U_ZERO_ERROR;
    const auto length=u_strFoldCase(nullptr,0,reinterpret_cast<const UChar*>(source.data()),static_cast<int32_t>(source.size()),U_FOLD_CASE_DEFAULT,&error);
    if(error!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(error))throw std::runtime_error("Cannot fold Now Playing text");
    std::u16string folded(static_cast<std::size_t>(length),u'\0');error=U_ZERO_ERROR;
    u_strFoldCase(reinterpret_cast<UChar*>(folded.data()),length,reinterpret_cast<const UChar*>(source.data()),static_cast<int32_t>(source.size()),U_FOLD_CASE_DEFAULT,&error);
    if(U_FAILURE(error))throw std::runtime_error("Cannot fold Now Playing text");
    auto next=codePoints(folded);
    // Unicode folds Cherokee to its uppercase; Foundation folds to lowercase.
    for(auto&c:next){if(c>=0x13a0&&c<=0x13ef)c=c-0x13a0+0xab70;else if(c>=0x13f0&&c<=0x13f5)c=c-0x13f0+0x13f8;}
    if(next!=value){value=std::move(next);changed=true;}
    if(value.size()!=1||value.front()!=base)changed=true;
    return value;
}
std::string utf8(std::u16string_view value){return archiveUTF8(value);}
std::vector<std::string>split(std::string_view value){
    // CharacterSet(charactersIn: "/,、;&") scalar separators.
    std::vector<std::string>out;std::size_t start{};
    for(std::size_t at=0;at<=value.size();){
        std::size_t width=0;
        if(at<value.size()){const char c=value[at];if(c=='/'||c==','||c==';'||c=='&')width=1;else if(value.substr(at,3)=="\xe3\x80\x81")width=3;}
        if(at==value.size()||width){out.emplace_back(value.substr(start,at-start));if(at==value.size())break;at+=width;start=at;}else ++at;
    }
    return out;
}
std::string firstArtist(std::string_view artist){return split(artist).front();}
std::string_view text(std::span<const std::uint8_t>bytes){return {reinterpret_cast<const char*>(bytes.data()),bytes.size()};}
std::optional<Json>parse(std::span<const std::uint8_t>bytes,std::size_t limit=nowPlayingMaximumLyricsResponseBytes){
    if(bytes.size()>limit)return {};
    auto value=text(bytes);
    // JSONSerialization accepts a UTF-8 byte order mark.
    if(value.size()>=3&&value.substr(0,3)=="\xef\xbb\xbf")value.remove_prefix(3);
    try{return Json::parse(value,limit);}catch(...){return {};}
}
// NSNumber bridging of a JSON number or boolean.
struct Number {std::optional<std::int64_t>exact;double value{};};
std::optional<Number>number(const Json&value){
    if(value.isBool())return Number{value.boolean()?1:0,value.boolean()?1.:0.};
    if(!value.isNumber())return {};
    Number out;try{out.value=value.number();}catch(...){return {};}
    try{const auto whole=value.integer();out.exact=whole;}catch(...){}
    return out;
}
std::optional<std::int64_t>truncated(const Number&n){
    if(n.exact)return n.exact;
    if(!std::isfinite(n.value)||n.value>=9.2233720368547758e18||n.value<=-9.2233720368547758e18)return {};
    return static_cast<std::int64_t>(std::trunc(n.value));
}
std::optional<std::string>string(const Json&value){if(!value.isString())return {};return value.string();}
bool isObjectArray(const Json&value){if(!value.isArray())return false;for(const auto&row:value.array())if(!row.isObject())return false;return true;}
bool isStringArray(const Json&value){if(!value.isArray())return false;for(const auto&row:value.array())if(!row.isString())return false;return true;}
std::shared_ptr<const NowPlayingLyrics>shared(std::optional<NowPlayingLyrics>value){return value?std::make_shared<const NowPlayingLyrics>(std::move(*value)):nullptr;}
std::string hostLower(std::string_view host){std::string out(host);for(auto&c:out)c=asciiLower(c);return out;}
bool hex(char c)noexcept{return (c>='0'&&c<='9')||(c>='a'&&c<='f')||(c>='A'&&c<='F');}
int hexValue(char c)noexcept{return c<='9'?c-'0':(c|0x20)-'a'+10;}
// A minimal RFC 3986 split: scheme ":" "//" authority path ["?" query] ["#" fragment].
struct URLParts {std::string scheme,user,host,port,path;bool hasUser{},hasPort{},portDelimiter{},hasQuery{},hasFragment{};};
std::optional<URLParts>splitURL(std::string_view raw){
    const auto colon=raw.find(':');if(colon==std::string_view::npos||colon==0)return {};
    URLParts out;out.scheme=std::string(raw.substr(0,colon));
    for(char c:out.scheme)if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='+'||c=='-'||c=='.'))return {};
    auto rest=raw.substr(colon+1);if(rest.substr(0,2)!="//")return {};rest.remove_prefix(2);
    const auto hash=rest.find('#');if(hash!=std::string_view::npos){out.hasFragment=true;rest=rest.substr(0,hash);}
    const auto question=rest.find('?');if(question!=std::string_view::npos){out.hasQuery=true;rest=rest.substr(0,question);}
    const auto slash=rest.find('/');auto authority=rest.substr(0,slash);out.path=slash==std::string_view::npos?std::string():std::string(rest.substr(slash));
    if(const auto at=authority.rfind('@');at!=std::string_view::npos){out.hasUser=true;out.user=std::string(authority.substr(0,at));authority.remove_prefix(at+1);}
    if(const auto port=authority.rfind(':');port!=std::string_view::npos&&authority.find(']')==std::string_view::npos){out.portDelimiter=true;out.hasPort=port+1<authority.size();out.port=std::string(authority.substr(port+1));authority=authority.substr(0,port);}
    out.host=std::string(authority);
    return out;
}
std::string percentDecoded(std::string_view value){
    std::string out;out.reserve(value.size());
    for(std::size_t n=0;n<value.size();++n){if(value[n]=='%'&&n+2<value.size()&&hex(value[n+1])&&hex(value[n+2])){out.push_back(static_cast<char>(hexValue(value[n+1])*16+hexValue(value[n+2])));n+=2;}else out.push_back(value[n]);}
    return out;
}
// URLComponents(string:) (macOS 14+) re-encodes characters invalid in a path.
// NetEase writes some timestamps as [mm:ss:cc] (or [mm:ss:ccc]): a colon
// where LRC has the fraction point. Read literally they are hours:minutes:
// seconds (60 times too late); NetEase's own player reads them as
// [mm:ss.cc]. Only exactly that shape is rewritten.
std::string neteaseStamps(std::string value){
    if(value.size()>512*1024)return value; // normalizingNetEaseStamps' input bound (Mac 1.3.0)
    const auto digit=[&](std::size_t k){return k<value.size()&&value[k]>='0'&&value[k]<='9';};
    for(auto at=value.find('[');at!=std::string::npos;at=value.find('[',at+1)){
        if(!digit(at+1)||!digit(at+2)||at+3>=value.size()||value[at+3]!=':'||!digit(at+4)||!digit(at+5)||at+6>=value.size()||value[at+6]!=':'||!digit(at+7)||!digit(at+8))continue;
        auto end=at+9;if(digit(end))++end;
        if(end<value.size()&&value[end]==']')value[at+6]='.';
    }
    return value;
}
// The elements of the top-level "list" array of NetEase's queue file, each
// handed over as its own JSON text, without building the whole document (a
// long queue holds more values than one document may, and its full tree would
// be many times the file's size). false: no well-formed "list" member.
bool space(char c)noexcept{return c==' '||c=='\t'||c=='\n'||c=='\r';}
std::size_t skipSpace(std::string_view t,std::size_t at)noexcept{while(at<t.size()&&space(t[at]))++at;return at;}
// The position after the JSON value at `at`, or npos (structure only; each
// element is parsed strictly afterwards).
std::size_t skipValue(std::string_view t,std::size_t at)noexcept{
    if(at>=t.size())return std::string_view::npos;
    if(t[at]=='"'){for(++at;at<t.size();++at){if(t[at]=='\\'){++at;continue;}if(t[at]=='"')return at+1;}return std::string_view::npos;}
    if(t[at]=='{'||t[at]=='['){
        unsigned depth{};
        for(;at<t.size();++at){
            const char c=t[at];
            if(c=='"'){for(++at;at<t.size()&&t[at]!='"';++at)if(t[at]=='\\')++at;if(at>=t.size())return std::string_view::npos;continue;}
            if(c=='{'||c=='['){if(++depth>64)return std::string_view::npos;}
            else if(c=='}'||c==']'){if(!depth)return std::string_view::npos;if(!--depth)return at+1;}
        }
        return std::string_view::npos;
    }
    const auto start=at;while(at<t.size()&&t[at]!=','&&t[at]!='}'&&t[at]!=']'&&!space(t[at]))++at;
    return at>start?at:std::string_view::npos;
}
bool queueElements(std::string_view t,const std::function<bool(std::string_view)>&visit){
    if(t.size()>=3&&t.substr(0,3)=="\xef\xbb\xbf")t.remove_prefix(3);
    auto at=skipSpace(t,0);if(at>=t.size()||t[at]!='{')return false;
    bool found{};at=skipSpace(t,at+1);
    if(at<t.size()&&t[at]=='}')return false;
    for(;;){
        if(at>=t.size()||t[at]!='"')return false;
        const auto end=skipValue(t,at);if(end==std::string_view::npos)return false;
        const auto key=t.substr(at+1,end-at-2);
        at=skipSpace(t,end);if(at>=t.size()||t[at]!=':')return false;at=skipSpace(t,at+1);
        if(key=="list"&&!found){
            if(at>=t.size()||t[at]!='[')return false;
            found=true;at=skipSpace(t,at+1);
            if(at<t.size()&&t[at]==']')++at;
            else for(;;){
                const auto element=skipValue(t,at);if(element==std::string_view::npos)return false;
                // The visitor has enough rows: the rest of the file is not needed.
                if(!visit(t.substr(at,element-at)))return true;
                at=skipSpace(t,element);
                if(at<t.size()&&t[at]==','){at=skipSpace(t,at+1);continue;}
                if(at<t.size()&&t[at]==']'){++at;break;}
                return false;
            }
        }else{const auto value=skipValue(t,at);if(value==std::string_view::npos)return false;at=value;}
        at=skipSpace(t,at);
        if(at<t.size()&&t[at]==','){at=skipSpace(t,at+1);continue;}
        if(at<t.size()&&t[at]=='}')return found;
        return false;
    }
}
// The JSON "code" of a NetEase answer is 200.
bool answered(const Json&root){if(!root.isObject())return false;const auto code=number(root["code"]);return code&&truncated(*code)==200;}
std::string encodedPath(std::string_view path){
    static constexpr char digits[]="0123456789ABCDEF";std::string out;out.reserve(path.size());
    for(std::size_t n=0;n<path.size();++n){
        const auto c=static_cast<unsigned char>(path[n]);
        const bool allowed=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("-._~!$&'()*+,;=:@/").find(static_cast<char>(c))!=std::string_view::npos;
        if(c=='%'&&n+2<path.size()&&hex(path[n+1])&&hex(path[n+2])){out.append(path.substr(n,3));n+=2;}
        else if(allowed)out.push_back(static_cast<char>(c));
        else{out.push_back('%');out.push_back(digits[c>>4]);out.push_back(digits[c&15]);}
    }
    return out;
}
}

std::string_view nowPlayingSourceKey(NowPlayingSource source)noexcept{
    switch(source){case NowPlayingSource::music:return "music";case NowPlayingSource::spotify:return "spotify";case NowPlayingSource::netease:return "netease";
    case NowPlayingSource::qqMusic:return "qqMusic";case NowPlayingSource::kugou:return "kugou";case NowPlayingSource::system:return "system";}
    return "system";
}
std::map<std::string,std::string,std::less<>>nowPlayingPlaybackMetadata(const NowPlayingPlaybackEvent&event){
    return {{"action",std::string(event.action)},{"source",std::string(nowPlayingSourceKey(event.source))}};
}
NowPlayingSource nowPlayingSourceForAppUserModelID(std::string_view id)noexcept{
    if(asciiStartsFold(id,"SpotifyAB.SpotifyMusic_"))return NowPlayingSource::spotify;
    if(asciiStartsFold(id,"AppleInc.AppleMusicWin_"))return NowPlayingSource::music;
    // NetEase Cloud Music (Microsoft Store package family).
    if(asciiStartsFold(id,"1F8B0F94.122165AE053F_"))return NowPlayingSource::netease;
    if(const auto slash=id.find_last_of("\\/");slash!=std::string_view::npos)id.remove_prefix(slash+1);
    if(asciiEqualFold(id,"Spotify.exe"))return NowPlayingSource::spotify;
    if(asciiEqualFold(id,"cloudmusic.exe"))return NowPlayingSource::netease;
    if(asciiEqualFold(id,"QQMusic.exe"))return NowPlayingSource::qqMusic;
    if(asciiEqualFold(id,"KuGou.exe"))return NowPlayingSource::kugou;
    return NowPlayingSource::system;
}

std::string NowPlayingTrackMatcher::normalized(std::string_view value){
    if(value.empty()||!Json::validUtf8(value))return {};
    const auto points=codePoints(normalize(archiveUTF16(value),0));
    std::vector<UChar32>out;out.reserve(points.size());
    for(std::size_t at=0;at<points.size();){
        std::size_t end=at+1;while(end<points.size()&&extendsCluster(points[end]))++end;
        bool changed{};const auto base=foldBase(points[at],changed);
        // A base folded out of Latin/Greek/Cyrillic keeps its marks verbatim.
        const bool verbatim=changed&&!base.empty()&&!foldsDiacritics(base.front());
        for(const auto c:base)if(alphanumeric(c))out.push_back(c);
        for(std::size_t k=at+1;k<end;++k)if((verbatim||!strippableMark(points[k],base.empty()?points[at]:base.front()))&&alphanumeric(points[k]))out.push_back(points[k]);
        at=end;
    }
    return utf8(normalize(utf16(out),0));
}
std::vector<std::string>NowPlayingTrackMatcher::artists(std::string_view value){
    std::vector<std::string>out;
    for(const auto&part:split(value)){auto key=normalized(part);if(!key.empty())out.push_back(std::move(key));}
    std::sort(out.begin(),out.end());out.erase(std::unique(out.begin(),out.end()),out.end());return out;
}
std::optional<int>NowPlayingTrackMatcher::score(std::string_view title,std::span<const std::string>aliases,std::span<const std::string>names,
    std::string_view album,std::optional<double>duration,const NowPlayingTrack&target){
    const auto titleKey=normalized(target.title);if(titleKey.empty())return {};
    bool titled=normalized(title)==titleKey;for(const auto&alias:aliases)titled=titled||normalized(alias)==titleKey;if(!titled)return {};
    std::vector<std::string>source;for(const auto&name:names)for(auto&key:artists(name))source.push_back(std::move(key));
    std::sort(source.begin(),source.end());source.erase(std::unique(source.begin(),source.end()),source.end());
    const auto wanted=artists(target.artist);if(source.empty()||wanted.empty())return {};
    bool shared{};for(const auto&key:source)shared=shared||std::binary_search(wanted.begin(),wanted.end(),key);if(!shared)return {};
    int result=150+(source==wanted?15:0);
    if(target.duration&&duration){
        const auto expected=*target.duration,actual=*duration;
        if(!std::isfinite(actual)||actual<=0||std::abs(expected-actual)>std::max(3.,std::min(8.,expected*0.01)))return {};
        result+=std::abs(expected-actual)<=1?35:20;
    }
    if(!target.album.empty()&&normalized(album)==normalized(target.album))result+=20;
    return result;
}

bool nowPlayingCanonicalEqual(std::string_view a,std::string_view b){
    if(a==b)return true;
    if(!Json::validUtf8(a)||!Json::validUtf8(b))return false;
    return normalize(archiveUTF16(a),0)==normalize(archiveUTF16(b),0);
}
bool nowPlayingLyricsEqual(const NowPlayingLyrics*a,const NowPlayingLyrics*b){
    if(a==b)return true;if(!a||!b)return false;
    const auto&x=a->lines(),&y=b->lines();if(x.size()!=y.size())return false;
    for(std::size_t n=0;n<x.size();++n)if(x[n].time!=y[n].time||!nowPlayingCanonicalEqual(x[n].text,y[n].text))return false;
    return true;
}

std::string nowPlayingQueryValue(std::string_view value){
    static constexpr char digits[]="0123456789ABCDEF";std::string out;out.reserve(value.size()*3);
    for(const char raw:value){
        const auto c=static_cast<unsigned char>(raw);
        const bool allowed=(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||std::string_view("!$'()*+,-./:;?@_~").find(raw)!=std::string_view::npos;
        if(allowed)out.push_back(raw);else{out.push_back('%');out.push_back(digits[c>>4]);out.push_back(digits[c&15]);}
    }
    return out;
}
std::optional<std::string>nowPlayingLyricsRequestURL(const NowPlayingTrack&track){
    if(track.title.empty()||track.artist.empty())return {};
    std::string url="https://lrclib.net/api/get?track_name="+nowPlayingQueryValue(track.title)+"&artist_name="+nowPlayingQueryValue(track.artist);
    if(!track.album.empty())url+="&album_name="+nowPlayingQueryValue(track.album);
    if(track.duration)url+="&duration="+std::to_string(static_cast<long long>(std::round(*track.duration)));
    return url;
}
std::optional<std::string>nowPlayingLyricsSearchURL(const NowPlayingTrack&track){
    if(track.title.empty()||track.artist.empty())return {};
    return "https://lrclib.net/api/search?track_name="+nowPlayingQueryValue(track.title)+"&artist_name="+nowPlayingQueryValue(firstArtist(track.artist));
}
std::optional<NowPlayingLyrics>nowPlayingDecodeLyricsResponse(std::span<const std::uint8_t>bytes)try{
    const auto root=parse(bytes);if(!root||!root->isObject())return {};
    const auto lrc=string((*root)["syncedLyrics"]);if(!lrc)return {};
    return NowPlayingLyrics::parse(*lrc);
}catch(...){return {};}
std::optional<NowPlayingLyrics>nowPlayingDecodeLyricsSearch(std::span<const std::uint8_t>bytes,const NowPlayingTrack&track)try{
    const auto root=parse(bytes);if(!root||!isObjectArray(*root))return {};
    struct Candidate {int score{};std::shared_ptr<const NowPlayingLyrics>lyrics;};std::vector<Candidate>candidates;
    const auto&rows=root->array();
    for(std::size_t n=0;n<std::min<std::size_t>(100,rows.size());++n){
        const auto&row=rows[n];const auto lrc=string(row["syncedLyrics"]);if(!lrc)continue;
        // Both conditions are pure; scoring first only avoids parsing the
        // timed lyrics of rows that can never be candidates (owner thread).
        const std::array<std::string,1>names{string(row["artistName"]).value_or("")};
        const auto duration=number(row["duration"]);
        const auto score=NowPlayingTrackMatcher::score(string(row["trackName"]).value_or(""),{},names,string(row["albumName"]).value_or(""),
            duration?std::optional(duration->value):std::nullopt,track);
        if(!score)continue;
        auto lyrics=NowPlayingLyrics::parse(*lrc);if(!lyrics)continue;
        candidates.push_back({*score,shared(std::move(lyrics))});
    }
    std::stable_sort(candidates.begin(),candidates.end(),[](const auto&a,const auto&b){return a.score>b.score;});
    if(candidates.empty())return {};
    if(candidates.size()>1&&candidates[1].score==candidates[0].score&&!nowPlayingLyricsEqual(candidates[1].lyrics.get(),candidates[0].lyrics.get()))return {};
    return *candidates[0].lyrics;
}catch(...){return {};}
std::optional<std::string>nowPlayingCatalogSearchURL(const NowPlayingTrack&track){
    if(track.title.empty()||track.artist.empty())return {};
    return "https://music.163.com/api/cloudsearch/pc?s="+nowPlayingQueryValue(track.title+" "+firstArtist(track.artist))+"&type=1&limit=10&offset=0";
}
std::string nowPlayingCatalogLyricsURL(std::int64_t identifier){return "https://music.163.com/api/song/lyric?id="+std::to_string(identifier)+"&lv=-1&kv=-1&tv=-1";}
std::optional<NowPlayingCatalogMatch>nowPlayingCatalogMatch(std::span<const std::uint8_t>bytes,const NowPlayingTrack&track)try{
    const auto root=parse(bytes);if(!root||!root->isObject())return {};
    const auto code=number((*root)["code"]);if(!code||truncated(*code)!=200)return {};
    const auto&result=(*root)["result"];if(!result.isObject())return {};const auto&songs=result["songs"];if(!isObjectArray(songs))return {};
    std::vector<NowPlayingCatalogMatch>ranked;
    for(std::size_t n=0;n<std::min<std::size_t>(20,songs.array().size());++n){
        const auto&song=songs.array()[n];
        const auto id=number(song["id"]);const auto identifier=id?truncated(*id):std::nullopt;if(!identifier||*identifier<=0)continue;
        const auto title=string(song["name"]);if(!title)continue;
        const auto&people=isObjectArray(song["ar"])?song["ar"]:song["artists"];std::vector<std::string>names;
        if(isObjectArray(people))for(std::size_t k=0;k<std::min<std::size_t>(32,people.array().size());++k)if(auto name=string(people.array()[k]["name"]))names.push_back(std::move(*name));
        static const Json empty=Json::Object{};
        const auto&album=song["al"].isObject()?song["al"]:song["album"].isObject()?song["album"]:empty;
        const auto length=number(song["dt"]).has_value()?number(song["dt"]):number(song["duration"]);
        const auto&aliasValue=isStringArray(song["alia"])?song["alia"]:song["alias"];std::vector<std::string>aliases;
        if(isStringArray(aliasValue))for(std::size_t k=0;k<std::min<std::size_t>(8,aliasValue.array().size());++k)aliases.push_back(aliasValue.array()[k].string());
        const auto score=NowPlayingTrackMatcher::score(*title,aliases,names,string(album["name"]).value_or(""),
            length?std::optional(length->value/1000):std::nullopt,track);
        if(!score)continue;
        const auto picture=string(album["picUrl"]);
        std::optional<double>seconds;
        if(length&&std::isfinite(length->value)&&length->value>0)seconds=std::min(length->value/1000,nowPlayingMaximumSeconds);
        ranked.push_back({*identifier,picture?nowPlayingCatalogCoverURL(*picture):std::nullopt,*score,seconds});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto&a,const auto&b){return a.score>b.score;});
    if(ranked.empty())return {};
    if(ranked.size()>1&&ranked[1].score==ranked[0].score&&ranked[1].identifier!=ranked[0].identifier)return {};
    return ranked[0];
}catch(...){return {};}
std::shared_ptr<const NowPlayingNetEaseQueue>nowPlayingNetEaseQueue(std::span<const std::uint8_t>bytes)try{
    if(bytes.size()>nowPlayingMaximumPlayingListBytes)return nullptr;
    auto queue=std::make_shared<NowPlayingNetEaseQueue>();queue->rows.reserve(256);
    std::size_t elements{};
    const auto row=[&](std::string_view element)->bool{
        if(++elements>NowPlayingNetEaseQueue::maximumRows)return false;
        // One queue entry; a damaged one is skipped.
        Json entry;try{entry=Json::parse(element,nowPlayingMaximumLyricsResponseBytes);}catch(...){return true;}
        if(!entry.isObject())return true;
        const auto&song=entry["track"];if(!song.isObject())return true;
        auto title=string(song["name"]);if(!title)return true;
        // A title without letters or digits can never match (the matcher's rule).
        auto titleKey=NowPlayingTrackMatcher::normalized(*title);if(titleKey.empty())return true;
        // The song id is the numeric track id, else the entry id (which the
        // client stores either as a number or as a digit string).
        const auto numericId=[](const Json&value)->std::optional<std::int64_t>{
            if(value.isNumber()){if(const auto n=number(value))return truncated(*n);return std::nullopt;}
            const auto text=string(value);if(!text||text->empty()||text->size()>19)return std::nullopt;
            std::int64_t out{};const auto end=text->data()+text->size();
            const auto result=std::from_chars(text->data(),end,out);
            return result.ec==std::errc{}&&result.ptr==end&&out>0?std::optional(out):std::nullopt;
        };
        std::optional<std::int64_t>identifier=numericId(song["id"]);
        if(!identifier||*identifier<=0)identifier=numericId(entry["id"]);
        if(!identifier||*identifier<=0)return true;
        NowPlayingNetEaseQueue::Row out;out.titleKey=std::move(titleKey);out.title=std::move(*title);out.identifier=*identifier;
        const auto&people=song["artists"];
        if(isObjectArray(people))for(std::size_t k=0;k<std::min<std::size_t>(32,people.array().size());++k)if(auto name=string(people.array()[k]["name"]))out.artists.push_back(std::move(*name));
        static const Json empty=Json::Object{};
        const auto&album=song["album"].isObject()?song["album"]:empty;
        out.album=string(album["name"]).value_or("");
        if(const auto length=number(song["duration"]))out.length=length->value/1000;
        // The queue stores the cover under "picUrl" (older builds) or "cover".
        auto cover=string(album["picUrl"]);if(!cover)cover=string(album["cover"]);
        if(cover)out.artwork=nowPlayingCatalogCoverURL(*cover);
        queue->rows.push_back(std::move(out));
        return true;
    };
    if(!queueElements(text(bytes),row))return nullptr;
    queue->rows.shrink_to_fit();
    return queue;
}catch(...){return nullptr;}
std::optional<NowPlayingNetEaseEntry>nowPlayingNetEaseQueueEntry(const NowPlayingNetEaseQueue&queue,const NowPlayingTrack&track)try{
    const auto wanted=NowPlayingTrackMatcher::normalized(track.title);if(wanted.empty())return {};
    // The same conservative identity as the catalog match: a wrong queue entry
    // (another track of the same artist, or an ambiguous tie) is never taken.
    // Rows of another title can never score, so only equal titles are scored.
    struct Ranked {int score{};NowPlayingNetEaseEntry entry;};
    std::vector<Ranked>ranked;
    for(const auto&row:queue.rows){
        if(row.titleKey!=wanted)continue;
        const auto score=NowPlayingTrackMatcher::score(row.title,{},row.artists,row.album,row.length,track);
        if(!score)continue;
        NowPlayingNetEaseEntry entry;entry.identifier=row.identifier;
        if(row.length&&std::isfinite(*row.length)&&*row.length>0)entry.duration=std::min(*row.length,nowPlayingMaximumSeconds);
        entry.artwork=row.artwork;
        ranked.push_back({*score,std::move(entry)});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto&a,const auto&b){return a.score>b.score;});
    if(ranked.empty())return {};
    if(ranked.size()>1&&ranked[1].score==ranked[0].score&&ranked[1].entry.identifier!=ranked[0].entry.identifier)return {};
    return ranked[0].entry;
}catch(...){return {};}
std::optional<NowPlayingNetEaseEntry>nowPlayingNetEasePlayingListEntry(std::span<const std::uint8_t>bytes,const NowPlayingTrack&track){
    if(NowPlayingTrackMatcher::normalized(track.title).empty())return {};
    const auto queue=nowPlayingNetEaseQueue(bytes);
    return queue?nowPlayingNetEaseQueueEntry(*queue,track):std::nullopt;
}
std::optional<std::string>nowPlayingCatalogCoverURL(std::string_view raw,unsigned pixels){
    if(raw.size()>2048)return {};
    const auto parts=splitURL(raw);if(!parts||(parts->scheme!="http"&&parts->scheme!="https"))return {};
    if(parts->hasUser||parts->hasPort||parts->hasFragment)return {};
    const auto host=hostLower(parts->host);
    if(host!="p1.music.126.net"&&host!="p2.music.126.net"&&host!="p3.music.126.net"&&host!="p4.music.126.net")return {};
    const auto size=std::to_string(std::clamp(pixels,nowPlayingMinimumCoverPixels,nowPlayingMaximumCoverPixels));
    return "https://"+parts->host+encodedPath(parts->path)+"?param="+size+"y"+size;
}
unsigned nowPlayingCatalogCoverPixels(unsigned drawn)noexcept{return std::max(nowPlayingCatalogDefaultCoverPixels,std::min(nowPlayingMaximumCoverPixels,drawn));}
std::optional<NowPlayingLyrics>nowPlayingCatalogDecodeLyrics(std::span<const std::uint8_t>bytes)try{
    const auto root=parse(bytes);if(!root||!answered(*root))return {};
    const auto&lrc=(*root)["lrc"];if(!lrc.isObject())return {};
    const auto primary=string(lrc["lyric"]);if(!primary)return {};
    return NowPlayingLyrics::parse(neteaseStamps(*primary));
}catch(...){return {};}
bool nowPlayingCatalogAnswered(std::span<const std::uint8_t>bytes)try{
    const auto root=parse(bytes);return root&&answered(*root);
}catch(...){return false;}
bool nowPlayingLyricsBodyCacheable(std::string_view url,std::span<const std::uint8_t>bytes)try{
    const auto parts=splitURL(url);if(!parts)return false;
    auto path=percentDecoded(parts->path);while(path.size()>1&&path.back()=='/')path.pop_back();
    const auto host=hostLower(parts->host);
    const auto root=parse(bytes);if(!root)return false;
    if(host=="lrclib.net"){
        // A found record (a 404 never reaches the cache) or a non-empty search.
        if(path=="/api/get")return root->isObject()&&(number((*root)["id"]).has_value()||string((*root)["syncedLyrics"]).has_value());
        if(path=="/api/search")return root->isArray()&&!root->array().empty();
        return false;
    }
    if(host!="music.163.com"||!answered(*root))return false;
    if(path=="/api/cloudsearch/pc"){const auto&result=(*root)["result"];return result.isObject()&&result["songs"].isArray()&&!result["songs"].array().empty();}
    if(path=="/api/song/lyric"){const auto&lrc=(*root)["lrc"];if(!lrc.isObject())return false;const auto lyric=string(lrc["lyric"]);return lyric&&!lyric->empty();}
    return false;
}catch(...){return false;}
bool nowPlayingLyricsURLAllowed(std::string_view url){
    if(url.size()>8192)return false;
    const auto parts=splitURL(url);if(!parts||parts->scheme!="https"||parts->hasUser||parts->portDelimiter||parts->hasFragment)return false;
    auto path=percentDecoded(parts->path);while(path.size()>1&&path.back()=='/')path.pop_back();
    if(parts->host=="lrclib.net")return path=="/api/get"||path=="/api/search";
    return parts->host=="music.163.com"&&(path=="/api/cloudsearch/pc"||path=="/api/song/lyric");
}
bool nowPlayingArtworkURLAllowed(std::string_view url){
    if(url.size()>2048)return false;
    const auto parts=splitURL(url);if(!parts||hostLower(parts->scheme)!="https"||parts->hasUser||parts->hasFragment)return false;
    if(!parts->port.empty()&&parts->port!="443")return false;
    const auto host=hostLower(parts->host);if(host.empty()||host.back()=='.')return false;
    return host=="i.scdn.co"||host=="mosaic.scdn.co"||host=="image-cdn.spotifycdn.com"||host=="p1.music.126.net"||host=="p2.music.126.net"||host=="p3.music.126.net"||host=="p4.music.126.net";
}

NowPlayingLyricsKey NowPlayingLyricsKey::make(std::string_view application,const NowPlayingTrack&track){
    return {std::string(application),nowPlayingSourceForAppUserModelID(application),track.identifier,track.title,track.artist,track.album,track.duration};
}
bool NowPlayingLyricsKey::operator==(const NowPlayingLyricsKey&other)const{
    if(source!=other.source||duration!=other.duration||identifier.has_value()!=other.identifier.has_value())return false;
    if(identifier&&!nowPlayingCanonicalEqual(*identifier,*other.identifier))return false;
    return nowPlayingCanonicalEqual(application,other.application)&&nowPlayingCanonicalEqual(title,other.title)
        &&nowPlayingCanonicalEqual(artist,other.artist)&&nowPlayingCanonicalEqual(album,other.album);
}
NowPlayingFetch nowPlayingOfflineFetch(){return [](const std::string&,std::function<void(NowPlayingBody)>completion){completion(nullptr);return std::function<void()>([]{});};}
NowPlayingDecoder nowPlayingInlineDecoder(){return [](std::function<void()>work,std::function<void()>done){work();done();};}
double nowPlayingRetryDelay(unsigned failures)noexcept{
    double delay=5;for(unsigned k=1;k<failures&&delay<300;++k)delay*=3;
    return std::min(delay,300.);
}
namespace {
// Consecutive failed lookups per track (bounded like the caches): the step of
// its backoff. A definitive answer forgets it.
struct Failures {
    std::vector<std::pair<NowPlayingLyricsKey,unsigned>>rows;
    unsigned next(const NowPlayingLyricsKey&key){
        for(auto&row:rows)if(row.first==key)return ++row.second;
        rows.emplace_back(key,1u);if(rows.size()>8)rows.erase(rows.begin());return 1;
    }
    void forget(const NowPlayingLyricsKey&key){std::erase_if(rows,[&](const auto&row){return row.first==key;});}
};
}

struct NowPlayingLyricsLoader::Impl:std::enable_shared_from_this<Impl>{
    // retryAt: a search that got no answer (Windows, with a clock only).
    struct Entry {NowPlayingLyricsKey key;std::shared_ptr<const NowPlayingLyrics>lyrics;std::optional<double>retryAt;};
    NowPlayingFetch fetch;NowPlayingDecoder decode;std::function<void()>onChange;std::shared_ptr<const NowPlayingLyrics>lyrics;
    std::vector<Entry>cache;std::optional<NowPlayingLyricsKey>desired;std::optional<std::string>embeddedSource;
    std::function<void()>cancel;std::uint64_t generation{};NowPlayingClock clock;Failures failures;
    Impl(NowPlayingFetch f,NowPlayingDecoder d):fetch(std::move(f)),decode(std::move(d)){if(!fetch||!decode)throw std::invalid_argument("Now Playing lyrics need a fetch and a decoder");cache.reserve(9);}
    bool wanted(std::uint64_t expected,const NowPlayingLyricsKey&key)const{return generation==expected&&desires(key);}
    bool desires(const NowPlayingLyricsKey&key)const{return desired&&*desired==key;}
    bool due(const Entry&e)const{return e.retryAt&&clock&&clock()>=*e.retryAt;}
    std::vector<Entry>::iterator find(const NowPlayingLyricsKey&key){return std::find_if(cache.begin(),cache.end(),[&](const auto&e){return e.key==key;});}
    void stop(){auto value=std::move(cancel);cancel=nullptr;if(value)value();}
    void assign(std::shared_ptr<const NowPlayingLyrics>value){
        if(nowPlayingLyricsEqual(lyrics.get(),value.get()))return;
        lyrics=std::move(value);auto callback=onChange;if(callback)callback();
    }
    void save(std::shared_ptr<const NowPlayingLyrics>result,const NowPlayingLyricsKey&key,bool failed=false){
        // A failed lookup is retried after its backoff (with a clock); any
        // answer resets the backoff.
        std::optional<double>retryAt;
        if(failed&&clock)retryAt=clock()+nowPlayingRetryDelay(failures.next(key));else failures.forget(key);
        std::erase_if(cache,[&](const auto&e){return e.key==key;});cache.push_back({key,result,retryAt});
        if(cache.size()>8)cache.erase(cache.begin(),cache.begin()+static_cast<std::ptrdiff_t>(cache.size()-8));
        assign(std::move(result));
    }
    void request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){
        // Players may attach or correct timed lyrics after the first event.
        if(track.timedLyrics&&(!embeddedSource||!nowPlayingCanonicalEqual(*track.timedLyrics,*embeddedSource)||!desires(key))){
            embeddedSource=track.timedLyrics;
            // A NetEase player's own timed lyrics are normalised like its catalog's (MODS-359).
            if(auto embedded=NowPlayingLyrics::parse(key.source==NowPlayingSource::netease?neteaseStamps(*track.timedLyrics):*track.timedLyrics)){++generation;stop();desired=key;save(shared(std::move(embedded)),key);return;}
        }
        if(desires(key)){
            // The wanted track's failed lookup starts over once its wait passed.
            const auto found=find(key);if(found==cache.end()||!due(*found))return;
            cache.erase(found);
        }
        const auto expected=++generation;stop();desired=key;embeddedSource=track.timedLyrics;
        if(const auto found=find(key);found!=cache.end()){
            if(!due(*found)){auto value=std::move(*found);cache.erase(found);cache.push_back(value);assign(value.lyrics);return;}
            cache.erase(found);
        }
        assign(nullptr);
        const auto url=nowPlayingLyricsRequestURL(track);if(!url){save(nullptr,key);return;}
        auto completed=std::make_shared<bool>(false);const auto weak=weak_from_this();
        auto cancellation=fetch(*url,[weak,completed,expected,key,track](NowPlayingBody data){
            *completed=true;const auto self=weak.lock();if(!self||!self->wanted(expected,key))return;
            self->cancel=nullptr;if(!data){self->searchFallback(track,key,expected);return;} // nothing to decode
            auto result=std::make_shared<std::shared_ptr<const NowPlayingLyrics>>();
            self->decode([result,data]{*result=shared(nowPlayingDecodeLyricsResponse(*data));},[weak,result,expected,key,track]{
                const auto self=weak.lock();if(!self||!self->wanted(expected,key))return;
                if(*result)self->save(std::move(*result),key);else self->searchFallback(track,key,expected);
            });
        });
        if(!*completed&&generation==expected)cancel=std::move(cancellation);
    }
    void searchFallback(const NowPlayingTrack&track,const NowPlayingLyricsKey&key,std::uint64_t expected){
        const auto url=nowPlayingLyricsSearchURL(track);if(!url){save(nullptr,key);return;}
        auto completed=std::make_shared<bool>(false);const auto weak=weak_from_this();
        auto cancellation=fetch(*url,[weak,completed,expected,key,track](NowPlayingBody data){
            *completed=true;const auto self=weak.lock();if(!self||!self->wanted(expected,key))return;
            // No answer to the search (no body, a refusal, a timeout): a failure, not "no lyrics".
            self->cancel=nullptr;if(!data){self->save(nullptr,key,true);return;}
            auto result=std::make_shared<std::shared_ptr<const NowPlayingLyrics>>();
            self->decode([result,data,track]{*result=shared(nowPlayingDecodeLyricsSearch(*data,track));},[weak,result,expected,key]{
                const auto self=weak.lock();if(!self||!self->wanted(expected,key))return;
                self->save(std::move(*result),key);
            });
        });
        if(!*completed&&generation==expected)cancel=std::move(cancellation);
    }
    void clear(){++generation;stop();desired.reset();embeddedSource.reset();assign(nullptr);}
};
NowPlayingLyricsLoader::NowPlayingLyricsLoader(NowPlayingFetch fetch,NowPlayingDecoder decode):impl_(std::make_shared<Impl>(std::move(fetch),std::move(decode))){}
NowPlayingLyricsLoader::~NowPlayingLyricsLoader(){impl_->onChange=nullptr;impl_->stop();}
void NowPlayingLyricsLoader::setOnChange(std::function<void()>value){impl_->onChange=std::move(value);}
const std::shared_ptr<const NowPlayingLyrics>&NowPlayingLyricsLoader::lyrics()const noexcept{return impl_->lyrics;}
void NowPlayingLyricsLoader::request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){auto i=impl_;i->request(key,track);}
void NowPlayingLyricsLoader::invalidateMissing(const NowPlayingLyricsKey&key){
    auto i=impl_;
    if(std::any_of(i->cache.begin(),i->cache.end(),[&](const auto&e){return e.key==key&&!e.lyrics;})){
        std::erase_if(i->cache,[&](const auto&e){return e.key==key;});if(i->desires(key))i->clear();
    }
}
void NowPlayingLyricsLoader::clear(){auto i=impl_;i->clear();}
void NowPlayingLyricsLoader::setClock(NowPlayingClock value){impl_->clock=std::move(value);}
bool NowPlayingLyricsLoader::holds(const NowPlayingLyricsKey&key)const{return impl_->desires(key)&&impl_->lyrics;}
std::optional<double>NowPlayingLyricsLoader::retryTime()const{
    const auto&i=*impl_;if(!i.desired||!i.clock)return {};
    for(const auto&e:i.cache)if(e.key==*i.desired)return e.retryAt;
    return {};
}
void NowPlayingLyricsLoader::forgetFailures(){
    auto i=impl_;i->failures.rows.clear();
    const bool wanted=i->desired&&std::any_of(i->cache.begin(),i->cache.end(),[&](const auto&e){return e.retryAt&&e.key==*i->desired;});
    std::erase_if(i->cache,[](const auto&e){return e.retryAt.has_value();});
    if(wanted)i->clear();
}
std::size_t NowPlayingLyricsLoader::cacheSize()const noexcept{return impl_->cache.size();}

struct NowPlayingCatalog::Impl:std::enable_shared_from_this<Impl>{
    // retryAt: a finished lookup that got no answer (Windows, with a clock only).
    struct Entry {NowPlayingLyricsKey key;std::optional<std::string>artwork;std::shared_ptr<const NowPlayingLyrics>lyrics;bool finished{};std::optional<double>duration;std::optional<double>retryAt;};
    NowPlayingFetch fetch;NowPlayingDecoder decode;NowPlayingPlayingListReader playingList;std::function<void()>onChange;std::vector<Entry>entries;std::optional<NowPlayingLyricsKey>desired;
    std::function<void()>cancel;std::uint64_t generation{};NowPlayingClock clock;Failures failures;
    // The last parsed queue file and its bytes (held weakly: the reader owns them).
    std::weak_ptr<const std::vector<std::uint8_t>>queueBytes;std::shared_ptr<const NowPlayingNetEaseQueue>queueIndex;std::uint64_t parses{};
    Impl(NowPlayingFetch f,NowPlayingDecoder d):fetch(std::move(f)),decode(std::move(d)){if(!fetch||!decode)throw std::invalid_argument("Now Playing catalog needs a fetch and a decoder");entries.reserve(9);}
    const Entry*current()const{if(!desired)return nullptr;for(const auto&e:entries)if(e.key==*desired)return &e;return nullptr;}
    bool due(const Entry&e)const{return e.retryAt&&clock&&clock()>=*e.retryAt;}
    void notify(){auto callback=onChange;if(callback)callback();}
    void stop(){auto value=std::move(cancel);cancel=nullptr;if(value)value();}
    void update(const NowPlayingLyricsKey&key,std::optional<std::string>artwork,std::shared_ptr<const NowPlayingLyrics>lyrics,bool finished,std::optional<double>duration={},std::optional<double>retryAt={}){
        if(!desired||!(*desired==key))return;
        if(finished&&!retryAt)failures.forget(key);
        std::erase_if(entries,[&](const auto&e){return e.key==key;});entries.push_back({key,std::move(artwork),std::move(lyrics),finished,duration,retryAt});
        if(entries.size()>8)entries.erase(entries.begin(),entries.begin()+static_cast<std::ptrdiff_t>(entries.size()-8));
        notify();
    }
    // No answer (no body, a timeout, a refusal or a damaged answer): finished
    // without lyrics, so LRCLIB may answer meanwhile. With a clock it is
    // retried after its backoff and never kept as "no lyrics"; what the
    // lookup already knew (cover, length) stays.
    void fail(const NowPlayingLyricsKey&key,std::optional<std::string>artwork,std::optional<double>duration){
        if(!desired||!(*desired==key))return;
        if(const auto*e=current()){if(!artwork)artwork=e->artwork;if(!duration)duration=e->duration;}
        std::optional<double>retryAt;if(clock)retryAt=clock()+nowPlayingRetryDelay(failures.next(key));
        update(key,std::move(artwork),nullptr,true,duration,retryAt);
    }
    void get(const std::string&url,std::uint64_t expected,std::function<void(NowPlayingBody)>completion){
        auto completed=std::make_shared<bool>(false);const auto weak=weak_from_this();
        auto cancellation=fetch(url,[weak,completed,expected,completion=std::move(completion)](NowPlayingBody data){
            *completed=true;const auto self=weak.lock();if(!self||self->generation!=expected)return;
            self->cancel=nullptr;completion(std::move(data));
        });
        if(!*completed&&generation==expected)cancel=std::move(cancellation);
    }
    void clear(){
        if(!desired&&!cancel)return;
        ++generation;stop();desired.reset();
        // Incomplete work resumes on a later visible request; finished
        // positive and negative entries stay in the bounded cache.
        std::erase_if(entries,[](const auto&e){return !e.finished;});
    }
    // Reads the song's own lyric document by its catalog id, then finishes the
    // entry with the given cover and length (shared by the queue-file and the
    // fuzzy-search paths).
    void lyricsById(std::int64_t identifier,const NowPlayingLyricsKey&key,std::optional<std::string>artwork,std::optional<double>length,std::uint64_t expected){
        update(key,artwork,nullptr,false,length);
        const auto weak=weak_from_this();
        get(nowPlayingCatalogLyricsURL(identifier),expected,[weak,key,artwork,length,expected](NowPlayingBody data){
            const auto self=weak.lock();if(!self)return;
            if(!data){self->fail(key,artwork,length);return;}
            auto result=std::make_shared<std::shared_ptr<const NowPlayingLyrics>>();auto ok=std::make_shared<bool>(false);
            self->decode([result,ok,data]{*ok=nowPlayingCatalogAnswered(*data);*result=shared(nowPlayingCatalogDecodeLyrics(*data));},[weak,result,ok,key,artwork,length,expected]{
                const auto self=weak.lock();if(!self||self->generation!=expected)return;
                // An answer without lyrics (nolyric) is final; a refusal is not.
                if(!*ok){self->fail(key,artwork,length);return;}
                self->update(key,artwork,std::move(*result),true,length);
            });
        });
    }
    // The public cloudsearch (title + first artist), then the matched song's
    // own lyrics; the fallback when the queue file gives no confident match.
    void searchFuzzy(const NowPlayingLyricsKey&key,const NowPlayingTrack&track,std::uint64_t expected){
        const auto url=nowPlayingCatalogSearchURL(track);if(!url){update(key,{},nullptr,true);return;}
        const auto weak=weak_from_this();
        get(*url,expected,[weak,key,track,expected](NowPlayingBody data){
            const auto self=weak.lock();if(!self)return;
            if(!data){self->fail(key,{},{});return;}
            auto found=std::make_shared<std::optional<NowPlayingCatalogMatch>>();auto ok=std::make_shared<bool>(false);
            self->decode([found,ok,data,track]{*ok=nowPlayingCatalogAnswered(*data);*found=nowPlayingCatalogMatch(*data,track);},[weak,found,ok,key,expected]{
                const auto self=weak.lock();if(!self||self->generation!=expected)return;
                const auto match=*found;
                // No confident match in an answer is final; a refusal is not.
                if(!match){if(*ok)self->update(key,{},nullptr,true);else self->fail(key,{},{});return;}
                self->lyricsById(match->identifier,key,match->artwork,match->duration,expected);
            });
        });
    }
    void request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){
        if(key.source!=NowPlayingSource::netease){clear();return;}
        if(desired&&*desired==key){
            // The wanted track's failed lookup starts over once its wait passed.
            const auto*e=current();if(!e||!e->finished||!due(*e))return;
        }
        const auto expected=++generation;stop();desired=key;
        std::optional<std::string>knownArtwork;std::optional<double>knownDuration;
        if(const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto&e){return e.key==key&&e.finished;});found!=entries.end()){
            if(!due(*found)){auto cached=std::move(*found);entries.erase(found);entries.push_back(std::move(cached));notify();return;}
            // A retry keeps the cover and length already known while it runs.
            knownArtwork=found->artwork;knownDuration=found->duration;
        }
        update(key,std::move(knownArtwork),nullptr,false,knownDuration);
        // NetEase's own queue file resolves the exact song id and length; only
        // its absence or an uncertain entry falls back to the title search.
        NowPlayingBody queue;if(playingList)try{queue=playingList();}catch(...){queue.reset();}
        if(!queue){searchFuzzy(key,track,expected);return;}
        // The file is parsed once per contents (the reader hands back the same
        // bytes while the file is unchanged); every later track only matches.
        const auto held=queueBytes.lock();
        auto parsed=std::make_shared<std::shared_ptr<const NowPlayingNetEaseQueue>>(held&&held==queue?queueIndex:nullptr);
        const auto weak=weak_from_this();
        auto found=std::make_shared<std::optional<NowPlayingNetEaseEntry>>();
        decode([found,parsed,queue,track]{if(!*parsed)*parsed=nowPlayingNetEaseQueue(*queue);if(*parsed)*found=nowPlayingNetEaseQueueEntry(**parsed,track);},
            [weak,found,parsed,queue,key,track,expected]{
            const auto self=weak.lock();if(!self)return;
            if(*parsed&&self->queueIndex!=*parsed){self->queueBytes=queue;self->queueIndex=*parsed;++self->parses;}
            if(self->generation!=expected)return;
            if(const auto&entry=*found)self->lyricsById(entry->identifier,key,entry->artwork,entry->duration,expected);
            else self->searchFuzzy(key,track,expected);
        });
    }
};
NowPlayingCatalog::NowPlayingCatalog(NowPlayingFetch fetch,NowPlayingDecoder decode):impl_(std::make_shared<Impl>(std::move(fetch),std::move(decode))){}
NowPlayingCatalog::~NowPlayingCatalog(){impl_->onChange=nullptr;impl_->stop();}
void NowPlayingCatalog::setOnChange(std::function<void()>value){impl_->onChange=std::move(value);}
void NowPlayingCatalog::setPlayingList(NowPlayingPlayingListReader value){impl_->playingList=std::move(value);}
void NowPlayingCatalog::setClock(NowPlayingClock value){impl_->clock=std::move(value);}
std::uint64_t NowPlayingCatalog::queueParses()const noexcept{return impl_->parses;}
std::optional<double>NowPlayingCatalog::retryTime()const{const auto*e=impl_->current();return e&&e->finished&&impl_->clock?e->retryAt:std::nullopt;}
void NowPlayingCatalog::forgetFailures(){
    auto i=impl_;i->failures.rows.clear();
    const bool wanted=i->current()&&i->current()->retryAt;
    std::erase_if(i->entries,[](const auto&e){return e.retryAt.has_value();});
    if(wanted)i->clear();
}
std::optional<std::string>NowPlayingCatalog::artwork()const{const auto*e=impl_->current();return e?e->artwork:std::nullopt;}
std::shared_ptr<const NowPlayingLyrics>NowPlayingCatalog::lyrics()const{const auto*e=impl_->current();return e?e->lyrics:nullptr;}
std::optional<double>NowPlayingCatalog::duration()const{const auto*e=impl_->current();return e?e->duration:std::nullopt;}
bool NowPlayingCatalog::isLoading()const{const auto*e=impl_->current();return impl_->desired.has_value()&&!(e&&e->finished);}
void NowPlayingCatalog::request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){auto i=impl_;i->request(key,track);}
void NowPlayingCatalog::clear(){auto i=impl_;i->clear();}
void NowPlayingCatalog::invalidateMissing(const NowPlayingLyricsKey&key){
    auto i=impl_;
    if(std::any_of(i->entries.begin(),i->entries.end(),[&](const auto&e){return e.key==key&&(!e.artwork||!e.lyrics);})){
        std::erase_if(i->entries,[&](const auto&e){return e.key==key;});if(i->desired&&*i->desired==key)i->clear();
    }
}
std::size_t NowPlayingCatalog::cacheSize()const noexcept{return impl_->entries.size();}

struct NowPlayingLyricsSources::Impl {
    NowPlayingLyricsSources&owner;NowPlayingLyricsLoader loader;std::unique_ptr<NowPlayingCatalog>catalog;
    bool active{},retries{};std::optional<NowPlayingLyricsKey>key;std::optional<NowPlayingTrack>track;
    Impl(NowPlayingLyricsSources&o,NowPlayingFetch fetch,bool enabled,NowPlayingDecoder decode):owner(o),loader(fetch,decode),catalog(enabled?std::make_unique<NowPlayingCatalog>(fetch,decode):nullptr){}
    void notify(){auto callback=owner.onChange;if(callback)callback();}
    void changed(){
        if(active&&key&&track){
            if(catalog)catalog->request(*key,*track);
            if(track->timedLyrics)loader.request(*key,*track);
            else if((!catalog||!catalog->isLoading())&&(!catalog||!catalog->lyrics()))loader.request(*key,*track);
            // A retried catalog lookup keeps LRCLIB's lyrics of the same track
            // on screen until it has an answer of its own.
            else if(!(retries&&catalog&&catalog->isLoading()&&loader.holds(*key)))loader.clear();
        }else{loader.clear();if(catalog)catalog->clear();}
        notify();
    }
    void catalogChanged(){
        if(!active||!key||!track||!catalog)return;
        if(const auto url=catalog->artwork()){auto callback=owner.onArtworkURL;if(callback)callback(*url,*key);}
        if(track->timedLyrics)loader.request(*key,*track);
        else if(!catalog->isLoading()&&!catalog->lyrics())loader.request(*key,*track);
        else if(catalog->lyrics())loader.clear();
        notify();
    }
};
NowPlayingLyricsSources::NowPlayingLyricsSources(NowPlayingFetch fetch,bool catalog,NowPlayingDecoder decode):impl_(std::make_shared<Impl>(*this,std::move(fetch),catalog,std::move(decode))){
    Impl*i=impl_.get();i->loader.setOnChange([i]{i->notify();});if(i->catalog)i->catalog->setOnChange([i]{i->catalogChanged();});
}
NowPlayingLyricsSources::~NowPlayingLyricsSources(){onChange=nullptr;onArtworkURL=nullptr;impl_->loader.setOnChange(nullptr);if(impl_->catalog)impl_->catalog->setOnChange(nullptr);}
void NowPlayingLyricsSources::setPlayingList(NowPlayingPlayingListReader value){if(impl_->catalog)impl_->catalog->setPlayingList(std::move(value));}
void NowPlayingLyricsSources::update(std::optional<NowPlayingLyricsKey>key,const std::optional<NowPlayingTrack>&track){
    auto i=impl_;i->active=true;i->key=std::move(key);i->track=i->key?track:std::nullopt;i->changed();
}
void NowPlayingLyricsSources::hide(){
    auto i=impl_;i->active=false;i->key.reset();i->track.reset();i->loader.clear();if(i->catalog)i->catalog->clear();
    // Failures are not remembered across presentations: the next opening asks again.
    i->loader.forgetFailures();if(i->catalog)i->catalog->forgetFailures();
}
void NowPlayingLyricsSources::setClock(NowPlayingClock value){
    auto i=impl_;i->retries=bool(value);i->loader.setClock(value);if(i->catalog)i->catalog->setClock(std::move(value));
}
std::optional<double>NowPlayingLyricsSources::retryTime()const{
    const auto&i=*impl_;if(!i.active||!i.key||!i.track)return {};
    std::optional<double>out=i.loader.retryTime();
    if(i.catalog)if(const auto value=i.catalog->retryTime())out=out?std::min(*out,*value):*value;
    return out;
}
void NowPlayingLyricsSources::invalidateMissing(){
    auto i=impl_;if(!i->key||!i->track)return;
    i->loader.invalidateMissing(*i->key);if(i->catalog)i->catalog->invalidateMissing(*i->key);
}
std::shared_ptr<const NowPlayingLyrics>NowPlayingLyricsSources::lyrics()const{
    const auto&i=*impl_;
    if(i.track&&i.track->timedLyrics&&i.loader.lyrics())return i.loader.lyrics();
    if(i.catalog)if(auto value=i.catalog->lyrics())return value;
    return i.loader.lyrics();
}
std::optional<std::string>NowPlayingLyricsSources::catalogArtwork()const{return impl_->catalog?impl_->catalog->artwork():std::nullopt;}
std::optional<double>NowPlayingLyricsSources::catalogDuration()const{return impl_->catalog?impl_->catalog->duration():std::nullopt;}
bool NowPlayingLyricsSources::catalogLoading()const{return impl_->catalog&&impl_->catalog->isLoading();}
const std::optional<NowPlayingLyricsKey>&NowPlayingLyricsSources::key()const noexcept{return impl_->key;}
}
