#include "core/localization.hpp"
#include <algorithm>
#include <array>
#include <charconv>
#include <mutex>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <icu.h>
#define EHUD_LOCALIZATION_ICU 1
#elif defined(EHUD_LOCALIZATION_ICU)
#include <unicode/ustring.h>
#include <unicode/utrans.h>
#endif

namespace endfield::core {
namespace {
constexpr TranslationEntry entries[]{
#include "core/localization_catalog.inc"
};
constexpr std::array<std::string_view,6> settings{"system","english","simplifiedChinese","traditionalChinese","japanese","korean"};
std::string_view selected(const TranslationEntry& e,Language l){switch(l){
case Language::simplifiedChinese:return e.simplifiedChinese;case Language::traditionalChinese:return e.traditionalChinese;
case Language::japanese:return e.japanese;case Language::korean:return e.korean;default:return e.english;}}
#ifdef EHUD_LOCALIZATION_ICU
// One Hans-Hant transliterator for the process (opened on first use, never
// closed) and its bounded cache of results, both under one lock: workers
// localize too. Windows binds icu.dll from System32 at run time, so Setup
// (which links this library) keeps no load-time ICU import and still starts,
// and refuses, on a Windows without it.
class HansHant {
public:
    std::optional<std::string> operator()(std::string_view text){
        if(text.size()>maximumBytes)return{};std::scoped_lock lock(mutex_);if(!value_)return{};
        if(const auto found=cache_.find(std::string(text));found!=cache_.end())return found->second;
        auto result=transliterate(text);if(!result)return{};
        if(cache_.size()>=maximumEntries)cache_.clear();cache_.emplace(std::string(text),*result);return result;
    }
    static HansHant& shared(){static HansHant value;return value;}
private:
    static constexpr std::size_t maximumBytes=1<<16,maximumEntries=512;
    decltype(&utrans_openU) open_{};decltype(&utrans_transUChars) transUChars_{};decltype(&u_strFromUTF8) fromUTF8_{};decltype(&u_strToUTF8) toUTF8_{};
    UTransliterator*value_{};std::mutex mutex_;std::unordered_map<std::string,std::string> cache_;
    template<class F> static F bind(void*f){return reinterpret_cast<F>(f);}
    HansHant(){
#ifdef _WIN32
        const auto icu=LoadLibraryExW(L"icu.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!icu)return;
        const auto symbol=[&](const char*name){return reinterpret_cast<void*>(GetProcAddress(icu,name));};
        open_=bind<decltype(open_)>(symbol("utrans_openU"));transUChars_=bind<decltype(transUChars_)>(symbol("utrans_transUChars"));
        fromUTF8_=bind<decltype(fromUTF8_)>(symbol("u_strFromUTF8"));toUTF8_=bind<decltype(toUTF8_)>(symbol("u_strToUTF8"));
#else
        open_=&utrans_openU;transUChars_=&utrans_transUChars;fromUTF8_=&u_strFromUTF8;toUTF8_=&u_strToUTF8;
#endif
        if(!open_||!transUChars_||!fromUTF8_||!toUTF8_)return;
        static constexpr UChar id[]{'H','a','n','s','-','H','a','n','t',0};UErrorCode status=U_ZERO_ERROR;
        value_=open_(id,-1,UTRANS_FORWARD,nullptr,0,nullptr,&status);if(U_FAILURE(status))value_=nullptr;
    }
    // UTF-8 -> UTF-16 (room for the rare longer Traditional form) -> transliterated in place -> UTF-8.
    std::optional<std::string> transliterate(std::string_view text)const{
        UErrorCode status=U_ZERO_ERROR;std::int32_t units{};fromUTF8_(nullptr,0,&units,text.data(),static_cast<std::int32_t>(text.size()),&status);
        if(status!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(status))return{};
        std::vector<UChar> buffer(static_cast<std::size_t>(units)*2+16);status=U_ZERO_ERROR;
        fromUTF8_(buffer.data(),static_cast<std::int32_t>(buffer.size()),&units,text.data(),static_cast<std::int32_t>(text.size()),&status);if(U_FAILURE(status))return{};
        std::int32_t length=units,limit=units;transUChars_(value_,buffer.data(),&length,static_cast<std::int32_t>(buffer.size()),0,&limit,&status);
        if(U_FAILURE(status)||length<0||length>static_cast<std::int32_t>(buffer.size()))return{};
        std::int32_t bytes{};toUTF8_(nullptr,0,&bytes,buffer.data(),length,&status);if(status!=U_BUFFER_OVERFLOW_ERROR&&U_FAILURE(status))return{};
        std::string result(static_cast<std::size_t>(bytes),'\0');status=U_ZERO_ERROR;toUTF8_(result.data(),bytes,&bytes,buffer.data(),length,&status);
        if(U_FAILURE(status))return{};return result;
    }
};
#endif
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
std::string_view regionTag(TextRegion r)noexcept{return r==TextRegion::traditionalChinese?"zh-Hant":r==TextRegion::japanese?"ja":r==TextRegion::korean?"ko":"zh-Hans";}
std::optional<TextRegion>regionFromTag(std::string_view tag)noexcept{for(const auto r:textRegions)if(regionTag(r)==tag)return r;return{};}
std::optional<TextRegion>regionNaming(Language l)noexcept{switch(l){
    case Language::simplifiedChinese:return TextRegion::simplifiedChinese;case Language::traditionalChinese:return TextRegion::traditionalChinese;
    case Language::japanese:return TextRegion::japanese;case Language::korean:return TextRegion::korean;
    case Language::english:case Language::system:break;}return{};}
TextRegion firstRegion(std::span<const std::string_view> preferred){
    for(const auto id:preferred)if(const auto r=regionNaming(resolveLanguage(std::span(&id,1))))return *r;
    return TextRegion::simplifiedChinese;}
TextRegion regionFor(Language l,TextRegion systemRegion)noexcept{return regionNaming(l).value_or(systemRegion);}
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
std::optional<std::string>hansToHant([[maybe_unused]]std::string_view simplified){
#ifdef EHUD_LOCALIZATION_ICU
    return HansHant::shared()(simplified);
#else
    return{};
#endif
}
std::string localized(std::string_view english,std::string_view simplified,Language language,std::span<const std::string_view>arguments){
    const auto*entry=translationEntry(english,simplified);
    const auto value=entry?selected(*entry,language):isChinese(language)?simplified:english;
    if(auto rendered=renderTranslation(value,arguments)){
        // L10n.text: an uncatalogued pair in zh-Hant is the rendered Simplified text through Hans-Hant.
        if(!entry&&language==Language::traditionalChinese)if(auto hant=hansToHant(*rendered))return std::move(*hant);
        return std::move(*rendered);}
    if(auto fallback=renderTranslation(english,arguments))return std::move(*fallback);
    throw std::invalid_argument("Localization arguments do not match source placeholders");
}
}
