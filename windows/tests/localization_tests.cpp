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
    // Mac 1.3.0: 830 core + 453 redesign rows (LocalizationCatalog.swift + LocalizationCatalogRedesign.swift), plus
    // the 46 Windows-kept v1.2.0 pairs (tools/localization_windows_kept.json) the Mac pruned while Windows still
    // calls them. Dropping a kept row changes this count.
    const auto catalog=translationCatalog();check(catalog.size()==1283+46,"Full pinned five-language catalog");
    using Row=std::array<std::string_view,5>;
    const auto five=[&](const Row&r,const char*why){const auto*e=translationEntry(r[0],r[1]);check(e&&e->traditionalChinese==r[2]&&e->japanese==r[3]&&e->korean==r[4],why);};
    for(const auto&r:std::array<Row,6>{{
        {"Recording mode","录制模式","錄製模式","録画モード","녹화 모드"},
        {"Don't display","不显示","不顯示","表示しない","표시 안 함"},
        {"Are you sure you want to disable the Shipwide Defense System?","是否关闭全舰防御系统？","是否關閉全艦防禦系統？","全艦防衛システムを停止しますか？","전함 방어 시스템을 종료하시겠습니까?"},
        {"Remove {0}?","删除{0}？","刪除{0}？","{0}を削除しますか？","{0}을(를) 삭제할까요?"},
        {"Remove {0}?","移除 {0}？","移除 {0}？","{0}を削除しますか？","{0}을(를) 제거할까요?"},
        {"Delete this document?","删除此文档？","刪除此文件？","この文書を削除しますか？","이 문서를 삭제할까요?"}}})five(r,"Redesign pair in all five languages");
    const std::array<std::string_view,1>item{"A"};
    check(localized("Shelf cleared, {0} items","暂存架已清空，共 {0} 项",Language::japanese,item)=="シェルフを消去（A件）"&&
        localized("Clock style {0}","时钟样式 {0}",Language::korean,item)=="시계 스타일 A","Redesign placeholders render once");
    // Same English as a core pair, different Simplified Chinese (LocalizationCatalogRedesign.swift): both rows stay
    // and each resolves by its own pair, never by English alone.
    for(const auto&[english,redesign,redesignHant,core,coreHant]:std::array<Row,13>{{
        {"System","系统设置","系統設定","系统","系統"},{"Check","检查","檢查","勾选","勾選"},{"Up to date","已是最新","已是最新","已是最新版本","已是最新版本"},
        {"Battery","电池状态","電池狀態","电池","電池"},{"Temperature","温度","溫度","色温","色溫"},{"Copied","已复制到剪贴板","已複製到剪貼簿","已复制","已複製"},
        {"Power connected","电源已连接","電源已連接","已连接电源","已連接電源"},{"Battery unavailable","电池不可用","電池無法使用","电量不可用","電量不可用"},
        {"Levels","等级","等級","色阶","色階"},{"Reading","阅读","閱讀","显示数据","顯示資料"},{"Projection","投影已打开","投影已開啟","投影","投影"},
        {"Clear all","全部清除","全部清除","清空","清空"},{"Storage","存储空间","儲存空間","存储","儲存"}}}){
        const auto*a=translationEntry(english,redesign);const auto*b=translationEntry(english,core);
        check(a&&b&&a!=b&&a->traditionalChinese==redesignHant&&b->traditionalChinese==coreHant,"Shared English keeps both contextual pairs");
    }
    // Windows-kept v1.2.0 pairs beside the Mac 1.3.0 pair that replaced them on the Mac.
    five({"Score","得分","得分","スコア","점수"},"Kept minigame pair");five({"Score","计分","計分","スコア","점수"},"Redesign minigame pair");
    five({"Reading","阅读设置","閱讀設定","読書設定","읽기 설정"},"Kept Reader pair");five({"Delete this document?","删除此档案？","刪除此檔案？","この文書を削除しますか？","이 문서를 삭제할까요?"},"Kept Archive pair");
    // The two translations Mac 1.3.0 changed in place (Media Assembly tabs); the singular keeps ステッカー.
    check(localized("Filters","滤镜",Language::japanese)=="フィルタ"&&localized("Stickers","贴纸",Language::japanese)=="スタンプ"&&
        localized("Sticker","贴纸",Language::japanese)=="ステッカー","Mac 1.3.0 Japanese tab titles");
    check(!translationEntry("Add frame","添加边框")&&!translationEntry("Document created","已新建档案"),"Pruned pairs without a Windows caller are gone");
    check(localized("Add frame","添加边框",Language::traditionalChinese)=="添加边框"&&localized("Add frame","添加边框",Language::korean)=="Add frame",
        "Uncatalogued pair: Simplified source for both Chinese, English otherwise");
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
