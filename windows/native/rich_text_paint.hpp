#pragma once
#include "core/data/json.hpp"
#include <cstdint>
#include <span>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d2d1.h>
#include <dwrite.h>
namespace endfield::native {
// One immutable native line map per painted document. Extra source paragraph
// spacing changes only Y placement; original DWrite shaping and ACPs survive.
struct DocumentTextLine {
    std::uint32_t start{},length{};
    double nativeTop{},height{},baseline{},offset{},spacing{};
};
class DocumentTextLines final {
public:
    DocumentTextLines()=default;
    DocumentTextLines(IDWriteTextLayout&,const ehud::data::Json& text);
    std::span<const DocumentTextLine> lines()const noexcept{return lines_;}
    double offsetAtACP(std::uint32_t)const noexcept;
    double offsetAtBaseline(double)const noexcept;
    double nativeY(double documentY)const noexcept;
    double extraHeight()const noexcept;
private:
    std::vector<DocumentTextLine> lines_;
};
// Effects retain just straight color, never an ID2D brush tied to a discarded
// WIC target. This permits safe immutable-layout repaint after scrolling.
HRESULT setDocumentTextColor(IDWriteTextLayout&,DWRITE_TEXT_RANGE,D2D1_COLOR_F)noexcept;
HRESULT drawDocumentText(ID2D1RenderTarget&,IDWriteTextLayout&,const DocumentTextLines&,
    D2D1_POINT_2F origin,D2D1_COLOR_F defaultColor)noexcept;
} // namespace endfield::native
#endif
