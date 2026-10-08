#include "core/localization.hpp"
#include <array>
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace endfield::core;
namespace {
unsigned checks{};void check(bool value,const char*why){++checks;if(!value)throw std::runtime_error(why);}
Language resolve(std::initializer_list<std::string_view>ids){return resolveLanguage({ids.begin(),ids.size()});}
}
int main(){try{
    check(resolve({"fr-FR","ja-JP"})==Language::japanese,"First supported preference wins");
    check(resolve({"zh_Hans_TW"})==Language::simplifiedChinese,"Explicit simplified script beats region");
    check(resolve({"ZH-hant-CN"})==Language::traditionalChinese,"Explicit traditional script beats region");
    for(auto id:{"zh-TW","zh-HK","zh-MO"})check(resolve({id})==Language::traditionalChinese,"Traditional region mapping");
    check(resolve({"ko-KR"})==Language::korean&&resolve({"en-US","ko-KR"})==Language::english,"Korean and English preferences");
    check(resolve({"fr","de"})==Language::english,"Unsupported system language falls back to English");
    for(unsigned n=0;n<6;++n)check(languageFromSetting(languageSetting(static_cast<Language>(n)))==static_cast<Language>(n),"Preserve original saved language setting");
    check(!languageFromSetting("zh-cn"),"Do not rewrite unknown saved preferences silently");
    const std::array<std::string_view,2>args{"{1} %s","名字"};
    check(renderTranslation("{1} · {0} · {1}",args)=="名字 · {1} %s · 名字","Inserted placeholders are never rescanned");
    check(!renderTranslation("{0}",args)&&!renderTranslation("{2}",args),"Reject missing and out-of-range arguments");
    check(!renderTranslation("{999999999999999999999999999}",args),"Reject overflowing placeholder index");
    check(renderTranslation("{x} {01}",args)==std::nullopt,"Missing zero index stays an error");
    check(renderTranslation("{x} {} {-1}",{})=="{x} {} {-1}","Non-numeric braces remain literal");
    const auto catalog=translationCatalog();check(catalog.size()==922,"Full pinned five-language catalog");
    for(const auto&e:catalog){
        check(translationEntry(e.english,e.simplifiedChinese)==&e,"Lookup preserves contextual source pair");
        std::size_t count{};for(std::size_t p=0;p<e.english.size();++p)if(e.english[p]=='{'&&p+1<e.english.size()&&e.english[p+1]>='0'&&e.english[p+1]<='9'){
            std::size_t n{},q=p+1;while(q<e.english.size()&&e.english[q]>='0'&&e.english[q]<='9')n=n*10+e.english[q++]-'0';if(q<e.english.size()&&e.english[q]=='}')count=std::max(count,n+1);
        }
        std::vector<std::string_view>values(count,"用户 {99} %s");
        const std::array originals{e.english,e.simplifiedChinese,e.traditionalChinese,e.japanese,e.korean};
        for(unsigned n=0;n<5;++n){const auto expected=renderTranslation(originals[n],values);check(expected.has_value(),"Original catalog has complete argument coverage");check(localized(e.english,e.simplifiedChinese,static_cast<Language>(n+1),values)==*expected,"Every original translation round-trips");}
    }
    check(localized("No music playing","No music playing",Language::japanese)=="No music playing","Keep source's fixed English music status");
    std::cout<<"Original localization: "<<checks<<" checks passed\n";return 0;
}catch(const std::exception&e){std::cerr<<"Localization failed after "<<checks<<": "<<e.what()<<'\n';return 1;}}
