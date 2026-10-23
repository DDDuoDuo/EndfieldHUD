#include "native/rich_text_paint.hpp"
#ifdef _WIN32
#include <dwrite_3.h>
#include <wrl/client.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <utility>
namespace endfield::native {
namespace {
using Microsoft::WRL::ComPtr;
constexpr GUID colorID{0xaf7a2c51,0xa879,0x4ce5,{0xa4,0x3a,0x3d,0xeb,0xe6,0x10,0x77,0x19}};
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}
class Color final:public IUnknown {
    std::atomic<ULONG>refs_{1};
public:
    D2D1_COLOR_F value;
    explicit Color(D2D1_COLOR_F c):value(c){}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=__uuidof(IUnknown)&&id!=colorID)return E_NOINTERFACE;*p=static_cast<IUnknown*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
};
// Draws a layout's glyph runs: document runs carry Color effects and move by
// their line's paragraph spacing; other layouts carry brush effects.
class Painter final:public IDWriteTextRenderer {
    std::atomic<ULONG>refs_{1};ID2D1RenderTarget&target_;const DocumentTextLines&lines_;D2D1_COLOR_F fallback_;ComPtr<ID2D1SolidColorBrush>brush_;ID2D1Brush*default_{};const ObliqueShear*oblique_{};
    HRESULT ink(IUnknown*effect,ID2D1Brush*&out){out=default_?default_:brush_.Get();if(!effect){if(!default_)brush_->SetColor(fallback_);return S_OK;}
        ComPtr<IUnknown>color;if(SUCCEEDED(effect->QueryInterface(colorID,&color))){brush_->SetColor(static_cast<Color*>(color.Get())->value);out=brush_.Get();return S_OK;}
        ComPtr<ID2D1Brush>b;const auto hr=effect->QueryInterface(IID_PPV_ARGS(b.GetAddressOf()));if(FAILED(hr))return hr;out=b.Get();return S_OK;} // the layout keeps the brush alive
    // The upright face of a shared-font run DirectWrite would slant.
    ComPtr<IDWriteFontFace3> upright(IDWriteFontFace*face)const{
        if(!oblique_||!(face->GetSimulations()&DWRITE_FONT_SIMULATIONS_OBLIQUE))return{};ComPtr<IDWriteFontFace3>f3;if(FAILED(face->QueryInterface(IID_PPV_ARGS(f3.GetAddressOf()))))return{};
        ComPtr<IDWriteLocalizedStrings>names;if(FAILED(f3->GetFamilyNames(names.GetAddressOf())))return{};UINT32 index{},length{};BOOL exists{};names->FindLocaleName(L"en-us",&index,&exists);if(!exists)index=0;
        if(FAILED(names->GetStringLength(index,&length)))return{};std::wstring family(std::size_t(length)+1,L'\0');if(FAILED(names->GetString(index,family.data(),length+1)))return{};family.resize(length);
        if(std::find(oblique_->families.begin(),oblique_->families.end(),family)==oblique_->families.end())return{};
        ComPtr<IDWriteFontFaceReference>reference;ComPtr<IDWriteFontFace3>result;
        if(FAILED(f3->GetFontFaceReference(reference.GetAddressOf()))||FAILED(reference->CreateFontFaceWithSimulations(face->GetSimulations()&~DWRITE_FONT_SIMULATIONS_OBLIQUE,result.GetAddressOf())))return{};
        return result;}
public:
    Painter(ID2D1RenderTarget&t,const DocumentTextLines&l,D2D1_COLOR_F c,ID2D1Brush*b,const ObliqueShear*o):target_(t),lines_(l),fallback_(c),default_(b),oblique_(o){}
    HRESULT prepare(){return target_.CreateSolidColorBrush(fallback_,brush_.GetAddressOf());}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id,void**p)override{if(!p)return E_POINTER;*p=nullptr;if(id!=__uuidof(IUnknown)&&id!=__uuidof(IDWritePixelSnapping)&&id!=__uuidof(IDWriteTextRenderer))return E_NOINTERFACE;*p=static_cast<IDWriteTextRenderer*>(this);AddRef();return S_OK;}
    ULONG STDMETHODCALLTYPE AddRef()override{return ++refs_;}
    ULONG STDMETHODCALLTYPE Release()override{const auto n=--refs_;if(!n)delete this;return n;}
    HRESULT STDMETHODCALLTYPE IsPixelSnappingDisabled(void*,BOOL*out)override{if(!out)return E_POINTER;*out=FALSE;return S_OK;}
    HRESULT STDMETHODCALLTYPE GetCurrentTransform(void*,DWRITE_MATRIX*out)override{if(!out)return E_POINTER;D2D1_MATRIX_3X2_F m;target_.GetTransform(&m);*out={m._11,m._12,m._21,m._22,m._31,m._32};return S_OK;}
    HRESULT STDMETHODCALLTYPE GetPixelsPerDip(void*,FLOAT*out)override{if(!out)return E_POINTER;FLOAT x{},y{};target_.GetDpi(&x,&y);*out=x/96;return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawGlyphRun(void*context,FLOAT x,FLOAT y,DWRITE_MEASURING_MODE mode,const DWRITE_GLYPH_RUN*run,const DWRITE_GLYPH_RUN_DESCRIPTION*description,IUnknown*effect)override{
        if(!run)return E_INVALIDARG;ID2D1Brush*b{};const auto hr=ink(effect,b);if(FAILED(hr))return hr;const auto origin=*static_cast<const D2D1_POINT_2F*>(context);
        const auto offset=description?lines_.offsetAtACP(description->textPosition):lines_.offsetAtBaseline(y-origin.y);const auto baseline=static_cast<FLOAT>(y+offset);
        if(const auto face=upright(run->fontFace)){
            // x' = x - shear (y - baseline): the run leans right above its baseline.
            auto upright=*run;upright.fontFace=face.Get();D2D1_MATRIX_3X2_F old;target_.GetTransform(&old);const auto s=oblique_->shear;
            target_.SetTransform(D2D1::Matrix3x2F(1,0,-s,1,s*baseline,0)*D2D1::Matrix3x2F(old._11,old._12,old._21,old._22,old._31,old._32));
            target_.DrawGlyphRun({x,baseline},&upright,b,mode);target_.SetTransform(old);return S_OK;}
        target_.DrawGlyphRun({x,baseline},run,b,mode);return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawUnderline(void*context,FLOAT x,FLOAT y,const DWRITE_UNDERLINE*u,IUnknown*effect)override{
        if(!u)return E_INVALIDARG;ID2D1Brush*b{};const auto hr=ink(effect,b);if(FAILED(hr))return hr;const auto origin=*static_cast<const D2D1_POINT_2F*>(context);const auto top=static_cast<FLOAT>(y+lines_.offsetAtBaseline(y-origin.y)+u->offset);
        target_.FillRectangle({x,top,x+u->width,top+u->thickness},b);return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawStrikethrough(void*context,FLOAT x,FLOAT y,const DWRITE_STRIKETHROUGH*u,IUnknown*effect)override{
        if(!u)return E_INVALIDARG;ID2D1Brush*b{};const auto hr=ink(effect,b);if(FAILED(hr))return hr;const auto origin=*static_cast<const D2D1_POINT_2F*>(context);const auto top=static_cast<FLOAT>(y+lines_.offsetAtBaseline(y-origin.y)+u->offset);
        target_.FillRectangle({x,top,x+u->width,top+u->thickness},b);return S_OK;}
    HRESULT STDMETHODCALLTYPE DrawInlineObject(void*context,FLOAT x,FLOAT y,IDWriteInlineObject*inline_,BOOL sideways,BOOL rtl,IUnknown*effect)override{
        // The ellipsis trimming sign of a truncated caption draws itself.
        if(!inline_||lines_.lines().size())return E_NOTIMPL;return inline_->Draw(context,this,x,y,sideways,rtl,effect);}
};
}
DocumentTextLines::DocumentTextLines(IDWriteTextLayout&layout,const ehud::data::Json&text){
    const auto&ranges=text["paragraphLineSpacing"];if(ranges.isNull())return;
    need(ranges.isArray(),"Invalid document paragraph spacing");if(ranges.array().empty())return;
    need(ranges.isArray()&&ranges.array().size()<=65537,"Invalid document paragraph spacing");
    struct Spacing{std::uint32_t start{},end{};double amount{};};std::vector<Spacing>spacing;spacing.reserve(ranges.array().size());std::uint32_t previous{};
    for(const auto&r:ranges.array()){need(r.isObject()&&r["start"].isNumber()&&r["end"].isNumber()&&r["points"].isNumber(),"Invalid document spacing range");const auto a=r["start"].integer(),b=r["end"].integer();const auto value=r["points"].number();
        need(a>=previous&&b>=a&&b<=65536&&std::isfinite(value)&&value>=0&&value<=1,"Document spacing range exceeds source bounds");spacing.push_back({static_cast<std::uint32_t>(a),static_cast<std::uint32_t>(b),value});previous=static_cast<std::uint32_t>(b);}
    UINT32 count{};const auto probe=layout.GetLineMetrics(nullptr,0,&count);need((SUCCEEDED(probe)||probe==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER))&&count<=65537,"Cannot index bounded document lines");
    std::vector<DWRITE_LINE_METRICS>metrics(count);if(count)need(SUCCEEDED(layout.GetLineMetrics(metrics.data(),count,&count)),"Cannot read document lines");
    lines_.reserve(count);std::uint32_t at{};double top{},offset{};std::size_t r{};
    for(const auto&m:metrics){while(r<spacing.size()&&spacing[r].end<=at&&!(m.length==0&&spacing[r].end==at))++r;double extra{};if(r<spacing.size()&&spacing[r].start<=at&&at<=spacing[r].end)extra=spacing[r].amount;
        need(m.length<=65536-at&&std::isfinite(m.height)&&m.height>0&&std::isfinite(m.baseline),"Invalid native line metrics");lines_.push_back({at,m.length,top,m.height,m.baseline,offset,extra});at+=m.length;top+=m.height;offset+=extra;}
}
double DocumentTextLines::offsetAtACP(std::uint32_t acp)const noexcept{if(lines_.empty())return 0;auto i=std::upper_bound(lines_.begin(),lines_.end(),acp,[](auto p,const auto&l){return p<l.start;});return i==lines_.begin()?0:std::prev(i)->offset;}
double DocumentTextLines::offsetAtBaseline(double y)const noexcept{if(lines_.empty())return 0;auto i=std::lower_bound(lines_.begin(),lines_.end(),y,[](const auto&l,double p){return l.nativeTop+l.baseline<p;});if(i==lines_.begin())return i->offset;if(i==lines_.end())return lines_.back().offset;const auto prior=std::prev(i);return std::abs(y-(prior->nativeTop+prior->baseline))<=std::abs(y-(i->nativeTop+i->baseline))?prior->offset:i->offset;}
double DocumentTextLines::nativeY(double y)const noexcept{if(lines_.empty())return y;auto i=std::upper_bound(lines_.begin(),lines_.end(),y,[](double p,const auto&l){return p<l.nativeTop+l.offset;});if(i==lines_.begin())return y;--i;return std::min(y-i->offset,i->nativeTop+i->height);}
double DocumentTextLines::extraHeight()const noexcept{return lines_.empty()?0:lines_.back().offset+lines_.back().spacing;}
HRESULT setDocumentTextColor(IDWriteTextLayout&layout,DWRITE_TEXT_RANGE range,D2D1_COLOR_F c)noexcept{try{ComPtr<Color>effect;effect.Attach(new Color(c));return layout.SetDrawingEffect(effect.Get(),range);}catch(...){return E_OUTOFMEMORY;}}
HRESULT drawDocumentText(ID2D1RenderTarget&target,IDWriteTextLayout&layout,const DocumentTextLines&lines,D2D1_POINT_2F origin,D2D1_COLOR_F color,const ObliqueShear*oblique)noexcept{
    try{ComPtr<Painter>paint;paint.Attach(new Painter(target,lines,color,nullptr,oblique));auto hr=paint->prepare();return FAILED(hr)?hr:layout.Draw(&origin,paint.Get(),origin.x,origin.y);}catch(...){return E_OUTOFMEMORY;}}
HRESULT drawShearedText(ID2D1RenderTarget&target,IDWriteTextLayout&layout,D2D1_POINT_2F origin,ID2D1Brush&brush,const ObliqueShear&oblique,bool clip)noexcept{
    try{static const DocumentTextLines plain;ComPtr<Painter>paint;paint.Attach(new Painter(target,plain,D2D1::ColorF(0,0,0,0),&brush,&oblique));auto hr=paint->prepare();if(FAILED(hr))return hr;
        if(clip)target.PushAxisAlignedClip(D2D1::RectF(origin.x,origin.y,origin.x+layout.GetMaxWidth(),origin.y+layout.GetMaxHeight()),D2D1_ANTIALIAS_MODE_ALIASED);
        hr=layout.Draw(&origin,paint.Get(),origin.x,origin.y);if(clip)target.PopAxisAlignedClip();return hr;}catch(...){return E_OUTOFMEMORY;}}
} // namespace endfield::native
#endif
