#include "core/notes_font_names.hpp"
#include <array>
#include <string>
#include <utility>

namespace endfield::core::notes {
namespace {
// Keys: the PostScript family (text before the first '-') or family name,
// lowercase, without spaces, with a trailing "psmt"/"mt"/"ps" removed.
constexpr std::array<std::pair<std::string_view,std::string_view>,52> table{{
    {"helvetica","Arial"},{"helveticaneue","Arial"},{"arial","Arial"},{"arialrounded","Arial"},
    {"timesnewroman","Times New Roman"},{"times","Times New Roman"},{"timesroman","Times New Roman"},
    {"georgia","Georgia"},{"verdana","Verdana"},{"tahoma","Tahoma"},{"trebuchetms","Trebuchet MS"},
    {"impact","Impact"},{"comicsansms","Comic Sans MS"},{"palatino","Palatino Linotype"},
    {"couriernew","Courier New"},{"courier","Courier New"},{"menlo","Consolas"},{"monaco","Consolas"},
    {"sfmono","Consolas"},{"andalemono","Consolas"},
    {"avenir","Segoe UI"},{"avenirnext","Segoe UI"},{"futura","Segoe UI"},{"gillsans","Segoe UI"},
    {"optima","Segoe UI"},{"lucidagrande","Segoe UI"},{"sfpro","Segoe UI"},{"sfprotext","Segoe UI"},
    {"sfprodisplay","Segoe UI"},{"sfprorounded","Segoe UI"},{"sfcompact","Segoe UI"},
    {"pingfangtc","Microsoft JhengHei"},{"pingfanghk","Microsoft JhengHei"},
    {"stheiti","Microsoft YaHei"},{"heitisc","Microsoft YaHei"},{"heititc","Microsoft JhengHei"},
    {"stsongti","SimSun"},{"songtisc","SimSun"},{"stsong","SimSun"},{"songti","SimSun"},
    {"stkaiti","KaiTi"},{"kaitisc","KaiTi"},{"kaiti","KaiTi"},{"stfangsong","FangSong"},
    {"hiraginosans","Yu Gothic"},{"hirakakupron","Yu Gothic"},{"hirakakustdn","Yu Gothic"},{"hiraginokakugothicpron","Yu Gothic"},
    {"hiraminpron","Yu Mincho"},{"hiraginomincho","Yu Mincho"},
    {"applesdgothicneo","Malgun Gothic"},{"applecoloremoji","Segoe UI Emoji"}}};
std::string key(std::string_view name){
    if(const auto dash=name.find('-');dash!=std::string_view::npos)name=name.substr(0,dash);
    std::string out;out.reserve(name.size());for(const char c:name){if(c==' ')continue;out+=c>='A'&&c<='Z'?char(c-'A'+'a'):c;}
    for(const std::string_view suffix:{"psmt","mt","ps"})if(out.size()>suffix.size()&&out.ends_with(suffix)){out.resize(out.size()-suffix.size());break;}
    return out;
}
}
std::optional<std::string_view> notesWindowsFontFamily(std::string_view name)noexcept{
    if(name.empty()||name.size()>256||name.starts_with("."))return std::nullopt;
    try{const auto k=key(name);for(const auto&[from,to]:table)if(k==from)return to;}catch(...){}
    return std::nullopt;
}
}
