#include "modules/now_playing_lyrics_sources.hpp"
#include "modules/archive_model.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <charconv>
#include <cmath>
#include <stdexcept>
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
std::optional<Json>parse(std::span<const std::uint8_t>bytes){
    if(bytes.size()>nowPlayingMaximumLyricsResponseBytes)return {};
    auto value=text(bytes);
    // JSONSerialization accepts a UTF-8 byte order mark.
    if(value.size()>=3&&value.substr(0,3)=="\xef\xbb\xbf")value.remove_prefix(3);
    try{return Json::parse(value,nowPlayingMaximumLyricsResponseBytes);}catch(...){return {};}
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
        ranked.push_back({*identifier,picture?nowPlayingCatalogCoverURL(*picture):std::nullopt,*score});
    }
    std::stable_sort(ranked.begin(),ranked.end(),[](const auto&a,const auto&b){return a.score>b.score;});
    if(ranked.empty())return {};
    if(ranked.size()>1&&ranked[1].score==ranked[0].score&&ranked[1].identifier!=ranked[0].identifier)return {};
    return ranked[0];
}catch(...){return {};}
std::optional<std::string>nowPlayingCatalogCoverURL(std::string_view raw){
    if(raw.size()>2048)return {};
    const auto parts=splitURL(raw);if(!parts||(parts->scheme!="http"&&parts->scheme!="https"))return {};
    if(parts->hasUser||parts->hasPort||parts->hasFragment)return {};
    const auto host=hostLower(parts->host);
    if(host!="p1.music.126.net"&&host!="p2.music.126.net"&&host!="p3.music.126.net"&&host!="p4.music.126.net")return {};
    return "https://"+parts->host+encodedPath(parts->path)+"?param=600y600";
}
std::optional<NowPlayingLyrics>nowPlayingCatalogDecodeLyrics(std::span<const std::uint8_t>bytes)try{
    const auto root=parse(bytes);if(!root||!root->isObject())return {};
    const auto code=number((*root)["code"]);if(!code||truncated(*code)!=200)return {};
    const auto&lrc=(*root)["lrc"];if(!lrc.isObject())return {};
    const auto primary=string(lrc["lyric"]);if(!primary)return {};
    return NowPlayingLyrics::parse(*primary);
}catch(...){return {};}
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

struct NowPlayingLyricsLoader::Impl:std::enable_shared_from_this<Impl>{
    struct Entry {NowPlayingLyricsKey key;std::shared_ptr<const NowPlayingLyrics>lyrics;};
    NowPlayingFetch fetch;NowPlayingDecoder decode;std::function<void()>onChange;std::shared_ptr<const NowPlayingLyrics>lyrics;
    std::vector<Entry>cache;std::optional<NowPlayingLyricsKey>desired;std::optional<std::string>embeddedSource;
    std::function<void()>cancel;std::uint64_t generation{};
    Impl(NowPlayingFetch f,NowPlayingDecoder d):fetch(std::move(f)),decode(std::move(d)){if(!fetch||!decode)throw std::invalid_argument("Now Playing lyrics need a fetch and a decoder");cache.reserve(9);}
    bool wanted(std::uint64_t expected,const NowPlayingLyricsKey&key)const{return generation==expected&&desires(key);}
    bool desires(const NowPlayingLyricsKey&key)const{return desired&&*desired==key;}
    void stop(){auto value=std::move(cancel);cancel=nullptr;if(value)value();}
    void assign(std::shared_ptr<const NowPlayingLyrics>value){
        if(nowPlayingLyricsEqual(lyrics.get(),value.get()))return;
        lyrics=std::move(value);auto callback=onChange;if(callback)callback();
    }
    void save(std::shared_ptr<const NowPlayingLyrics>result,const NowPlayingLyricsKey&key){
        std::erase_if(cache,[&](const auto&e){return e.key==key;});cache.push_back({key,result});
        if(cache.size()>8)cache.erase(cache.begin(),cache.begin()+static_cast<std::ptrdiff_t>(cache.size()-8));
        assign(std::move(result));
    }
    void request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){
        // Players may attach or correct timed lyrics after the first event.
        if(track.timedLyrics&&(!embeddedSource||!nowPlayingCanonicalEqual(*track.timedLyrics,*embeddedSource)||!desires(key))){
            embeddedSource=track.timedLyrics;
            if(auto embedded=NowPlayingLyrics::parse(*track.timedLyrics)){++generation;stop();desired=key;save(shared(std::move(embedded)),key);return;}
        }
        if(desires(key))return;
        const auto expected=++generation;stop();desired=key;embeddedSource=track.timedLyrics;
        if(const auto found=std::find_if(cache.begin(),cache.end(),[&](const auto&e){return e.key==key;});found!=cache.end()){
            auto value=std::move(*found);cache.erase(found);cache.push_back(value);assign(value.lyrics);return;
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
            self->cancel=nullptr;if(!data){self->save(nullptr,key);return;}
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
std::size_t NowPlayingLyricsLoader::cacheSize()const noexcept{return impl_->cache.size();}

struct NowPlayingCatalog::Impl:std::enable_shared_from_this<Impl>{
    struct Entry {NowPlayingLyricsKey key;std::optional<std::string>artwork;std::shared_ptr<const NowPlayingLyrics>lyrics;bool finished{};};
    NowPlayingFetch fetch;NowPlayingDecoder decode;std::function<void()>onChange;std::vector<Entry>entries;std::optional<NowPlayingLyricsKey>desired;
    std::function<void()>cancel;std::uint64_t generation{};
    Impl(NowPlayingFetch f,NowPlayingDecoder d):fetch(std::move(f)),decode(std::move(d)){if(!fetch||!decode)throw std::invalid_argument("Now Playing catalog needs a fetch and a decoder");entries.reserve(9);}
    const Entry*current()const{if(!desired)return nullptr;for(const auto&e:entries)if(e.key==*desired)return &e;return nullptr;}
    void notify(){auto callback=onChange;if(callback)callback();}
    void stop(){auto value=std::move(cancel);cancel=nullptr;if(value)value();}
    void update(const NowPlayingLyricsKey&key,std::optional<std::string>artwork,std::shared_ptr<const NowPlayingLyrics>lyrics,bool finished){
        if(!desired||!(*desired==key))return;
        std::erase_if(entries,[&](const auto&e){return e.key==key;});entries.push_back({key,std::move(artwork),std::move(lyrics),finished});
        if(entries.size()>8)entries.erase(entries.begin(),entries.begin()+static_cast<std::ptrdiff_t>(entries.size()-8));
        notify();
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
    void request(const NowPlayingLyricsKey&key,const NowPlayingTrack&track){
        if(key.source!=NowPlayingSource::netease){clear();return;}
        if(desired&&*desired==key)return;
        const auto expected=++generation;stop();desired=key;
        if(const auto found=std::find_if(entries.begin(),entries.end(),[&](const auto&e){return e.key==key&&e.finished;});found!=entries.end()){
            auto cached=std::move(*found);entries.erase(found);entries.push_back(std::move(cached));notify();return;
        }
        update(key,{},nullptr,false);
        const auto url=nowPlayingCatalogSearchURL(track);if(!url){update(key,{},nullptr,true);return;}
        const auto weak=weak_from_this();
        get(*url,expected,[weak,key,track,expected](NowPlayingBody data){
            const auto self=weak.lock();if(!self)return;
            if(!data){self->update(key,{},nullptr,true);return;}
            auto found=std::make_shared<std::optional<NowPlayingCatalogMatch>>();
            self->decode([found,data,track]{*found=nowPlayingCatalogMatch(*data,track);},[weak,found,key,expected]{
                const auto self=weak.lock();if(!self||self->generation!=expected)return;
                const auto match=*found;
                if(!match){self->update(key,{},nullptr,true);return;}
                self->update(key,match->artwork,nullptr,false);
                const auto artwork=match->artwork;
                self->get(nowPlayingCatalogLyricsURL(match->identifier),expected,[weak,key,artwork,expected](NowPlayingBody data){
                    const auto self=weak.lock();if(!self)return;
                    if(!data){self->update(key,artwork,nullptr,true);return;}
                    auto result=std::make_shared<std::shared_ptr<const NowPlayingLyrics>>();
                    self->decode([result,data]{*result=shared(nowPlayingCatalogDecodeLyrics(*data));},[weak,result,key,artwork,expected]{
                        const auto self=weak.lock();if(!self||self->generation!=expected)return;
                        self->update(key,artwork,std::move(*result),true);
                    });
                });
            });
        });
    }
};
NowPlayingCatalog::NowPlayingCatalog(NowPlayingFetch fetch,NowPlayingDecoder decode):impl_(std::make_shared<Impl>(std::move(fetch),std::move(decode))){}
NowPlayingCatalog::~NowPlayingCatalog(){impl_->onChange=nullptr;impl_->stop();}
void NowPlayingCatalog::setOnChange(std::function<void()>value){impl_->onChange=std::move(value);}
std::optional<std::string>NowPlayingCatalog::artwork()const{const auto*e=impl_->current();return e?e->artwork:std::nullopt;}
std::shared_ptr<const NowPlayingLyrics>NowPlayingCatalog::lyrics()const{const auto*e=impl_->current();return e?e->lyrics:nullptr;}
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
    bool active{};std::optional<NowPlayingLyricsKey>key;std::optional<NowPlayingTrack>track;
    Impl(NowPlayingLyricsSources&o,NowPlayingFetch fetch,bool enabled,NowPlayingDecoder decode):owner(o),loader(fetch,decode),catalog(enabled?std::make_unique<NowPlayingCatalog>(fetch,decode):nullptr){}
    void notify(){auto callback=owner.onChange;if(callback)callback();}
    void changed(){
        if(active&&key&&track){
            if(catalog)catalog->request(*key,*track);
            if(track->timedLyrics)loader.request(*key,*track);
            else if((!catalog||!catalog->isLoading())&&(!catalog||!catalog->lyrics()))loader.request(*key,*track);
            else loader.clear();
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
void NowPlayingLyricsSources::update(std::optional<NowPlayingLyricsKey>key,const std::optional<NowPlayingTrack>&track){
    auto i=impl_;i->active=true;i->key=std::move(key);i->track=i->key?track:std::nullopt;i->changed();
}
void NowPlayingLyricsSources::hide(){auto i=impl_;i->active=false;i->key.reset();i->track.reset();i->loader.clear();if(i->catalog)i->catalog->clear();}
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
bool NowPlayingLyricsSources::catalogLoading()const{return impl_->catalog&&impl_->catalog->isLoading();}
const std::optional<NowPlayingLyricsKey>&NowPlayingLyricsSources::key()const noexcept{return impl_->key;}
}
