#pragma once
#include "core/localization.hpp"
#include "core/scene.hpp"
#include <array>
#include <functional>
#include <string>
#include <string_view>
namespace endfield::core::source {
struct DesktopTextSize {double width{},height{};bool operator==(const DesktopTextSize&)const=default;};
struct DesktopCaptionRequest {
 std::string_view target,title; // target is the original module ID, or an opaque custom action ID.
 DesktopTextSize authoredSize;Language language{Language::english};
 bool module{true},rightSlot{},selected{},dark{true};
 std::array<double,3>accentSRGB{250./255,212./255,31./255};
};
struct DesktopCaptionPlan {
 std::string text;DesktopTextSize size;double fontSize{},ascender{},descender{},leading{};
 bool bold{},wrapped{},ellipsis{},expandFileShelfCaption{};std::array<double,4>color{};
};
// Content-event only. The caller supplies the same resolved font measurement
// used by painting. No approximate average-glyph-width or font-size guess.
using DesktopCaptionMeasure=std::function<DesktopTextSize(std::string_view,double,bool,double,bool)>;
DesktopCaptionPlan sourceDesktopCaption(const DesktopCaptionRequest&,const DesktopCaptionMeasure&);
// The source uses NSColor's GenericRGB blend, not arithmetic sRGB darkening.
std::array<double,3> sourceSelectedCaptionColor(std::array<double,3>accentSRGB);
}
