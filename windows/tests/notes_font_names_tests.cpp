#include "core/notes_font_names.hpp"
#include <iostream>
#include <stdexcept>
#include <string>

// Portable contracts for the Mac font-name table used by imported Notes and
// Archive runs. Pure string mapping: no font enumeration or system state.
namespace {
unsigned checks{};
void check(bool value,const std::string& why){++checks;if(!value)throw std::runtime_error(why);}
void maps(std::string_view mac,std::string_view windows){const auto v=endfield::core::notes::notesWindowsFontFamily(mac);check(v&&*v==windows,std::string(mac)+" maps to "+std::string(windows));}
void keeps(std::string_view mac){check(!endfield::core::notes::notesWindowsFontFamily(mac),std::string(mac)+" keeps the caller's fallback");}
}
int main(){
    try{
        maps("Helvetica","Arial");maps("Helvetica-Bold","Arial");maps("HelveticaNeue-Medium","Arial");maps("ArialMT","Arial");maps("Arial-BoldMT","Arial");
        maps("TimesNewRomanPSMT","Times New Roman");maps("TimesNewRomanPS-BoldItalicMT","Times New Roman");maps("Times-Roman","Times New Roman");
        maps("Georgia-Bold","Georgia");maps("Verdana","Verdana");maps("TrebuchetMS-Bold","Trebuchet MS");maps("ComicSansMS","Comic Sans MS");
        maps("CourierNewPSMT","Courier New");maps("Courier-Oblique","Courier New");maps("Menlo-Regular","Consolas");maps("SFMono-Semibold","Consolas");
        maps("AvenirNext-DemiBold","Segoe UI");maps("Avenir-Book","Segoe UI");maps("LucidaGrande","Segoe UI");
        maps("PingFangTC-Regular","Microsoft JhengHei");maps("STSongti-SC-Regular","SimSun");maps("STKaiti","KaiTi");maps("HiraginoSans-W6","Yu Gothic");
        maps("HiraKakuProN-W3","Yu Gothic");maps("HiraMinProN-W6","Yu Mincho");maps("AppleSDGothicNeo-Bold","Malgun Gothic");maps("AppleColorEmoji","Segoe UI Emoji");
        maps("Times New Roman","Times New Roman");maps("Comic Sans MS","Comic Sans MS");maps("Arial","Arial");
        keeps("PingFangSC-Regular");keeps(".SFNS-Regular");keeps(".AppleSystemUIFont");keeps("Segoe UI");keeps("Noto Sans SC");keeps("");keeps(std::string(300,'a'));keeps("SomeDesignerFont-Bold");
        std::cout<<"PASS "<<checks<<" Notes font name contracts\n";return 0;
    }catch(const std::exception& e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
