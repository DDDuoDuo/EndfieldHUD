#include "core/localization.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <stdexcept>
#include <vector>

namespace endfield::core {
namespace {
constexpr TranslationEntry entries[]{
#include "core/localization_catalog.inc"
};
constexpr std::array<std::string_view,6> settings{"system","english","simplifiedChinese","traditionalChinese","japanese","korean"};
std::string_view selected(const TranslationEntry& e,Language l){switch(l){
case Language::simplifiedChinese:return e.simplifiedChinese;case Language::traditionalChinese:return e.traditionalChinese;
case Language::japanese:return e.japanese;case Language::korean:return e.korean;default:return e.english;}}
}
std::optional<Language>languageFromSetting(std::string_view value)noexcept{for(std::size_t i=0;i<settings.size();++i)if(settings[i]==value)return static_cast<Language>(i);return{};}
std::string_view languageSetting(Language value)noexcept{const auto i=static_cast<std::size_t>(value);return i<settings.size()?settings[i]:settings[0];}
Language resolveLanguage(std::span<const std::string_view> preferred){
    for(auto id:preferred){std::string normalized;normalized.reserve(id.size());for(unsigned char c:id)normalized.push_back(c=='_'?'-':c>='A'&&c<='Z'?char(c-'A'+'a'):char(c));
        const std::string_view n=normalized;const auto first=n.substr(0,n.find('-'));
        if(first=="en")return Language::english;if(first=="ja")return Language::japanese;if(first=="ko")return Language::korean;if(first!="zh")continue;
        bool hans{},hant{},traditional{};std::size_t at{};while(at<n.size()){const auto end=n.find('-',at);const auto part=n.substr(at,end==n.npos?n.size()-at:end-at);hans|=part=="hans";hant|=part=="hant";traditional|=part=="tw"||part=="hk"||part=="mo";if(end==n.npos)break;at=end+1;}
        if(hant)return Language::traditionalChinese;if(hans)return Language::simplifiedChinese;return traditional?Language::traditionalChinese:Language::simplifiedChinese;
    }return Language::english;
}
bool isChinese(Language l)noexcept{return l==Language::simplifiedChinese||l==Language::traditionalChinese;}
bool isCJK(Language l)noexcept{return l!=Language::system&&l!=Language::english;}
std::span<const TranslationEntry> translationCatalog()noexcept{return entries;}
const TranslationEntry*translationEntry(std::string_view english,std::string_view simplified)noexcept{
    const auto found=std::lower_bound(std::begin(entries),std::end(entries),std::pair{english,simplified},[](const auto&e,const auto&key){return e.english<key.first||(e.english==key.first&&e.simplifiedChinese<key.second);});
    return found!=std::end(entries)&&found->english==english&&found->simplifiedChinese==simplified?found:nullptr;
}
std::optional<std::string>renderTranslation(std::string_view value,std::span<const std::string_view>arguments){
    std::vector<bool>used(arguments.size());std::string result;result.reserve(value.size());std::size_t at{};
    while(at<value.size()){
        if(value[at]=='{'&&at+1<value.size()&&value[at+1]>='0'&&value[at+1]<='9'){
            std::size_t end=at+1;while(end<value.size()&&value[end]>='0'&&value[end]<='9')++end;
            if(end<value.size()&&value[end]=='}'){
                std::size_t index{};const auto parsed=std::from_chars(value.data()+at+1,value.data()+end,index);
                if(parsed.ec!=std::errc{}||index>=arguments.size())return{};
                result+=arguments[index];used[index]=true;at=end+1;continue;
            }
        }result+=value[at++];
    }
    if(std::find(used.begin(),used.end(),false)!=used.end())return{};return result;
}
std::string localized(std::string_view english,std::string_view simplified,Language language,std::span<const std::string_view>arguments){
    const auto*entry=translationEntry(english,simplified);
    const auto value=entry?selected(*entry,language):isChinese(language)?simplified:english;
    if(auto rendered=renderTranslation(value,arguments))return std::move(*rendered);
    if(auto fallback=renderTranslation(english,arguments))return std::move(*fallback);
    throw std::invalid_argument("Localization arguments do not match source placeholders");
}
}
