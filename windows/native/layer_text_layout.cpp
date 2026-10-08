#include "native/layer_text_layout.hpp"
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <dwrite.h>
#include <wrl/client.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <iterator>
#include <stdexcept>
#include <vector>
using Microsoft::WRL::ComPtr;
namespace endfield::native {
namespace {
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}
void checked(HRESULT v,const char*why){if(FAILED(v))throw std::runtime_error(why);}
bool finite(core::Rect r){return std::isfinite(r.x)&&std::isfinite(r.y)&&std::isfinite(r.width)&&std::isfinite(r.height)&&std::isfinite(r.x+r.width)&&std::isfinite(r.y+r.height);}
}
struct PaintedTextLayout::Impl {
    ComPtr<IDWriteTextLayout>layout;std::u16string text;std::uint64_t revision{};core::Rect viewport;bool document{};double height{};std::size_t textBytes{};core::Point initialOffset;DocumentTextLines lines;ehud::data::Json plainStyle;DWORD thread{GetCurrentThreadId()};
    Impl(IDWriteTextLayout*l,std::u16string t,std::uint64_t r,core::Rect v,bool d,const ehud::data::Json& style,core::Point offset):layout(l),text(std::move(t)),revision(r),viewport(v),document(d),height(v.height),initialOffset(offset),lines(d?DocumentTextLines(*l,style):DocumentTextLines{}){
        textBytes=text.size()*sizeof(char16_t);if(d){plainStyle=style;textBytes+=style["string"].string().size();DWRITE_TEXT_METRICS metrics{};checked(l->GetMetrics(&metrics),"Read complete painted document extent");need(std::isfinite(metrics.top)&&std::isfinite(metrics.height)&&metrics.height>=0,"Invalid painted document extent");height=std::max(v.height,std::ceil(double(metrics.top)+metrics.height+lines.extraHeight())+1);}
    }
};
PaintedTextLayout::PaintedTextLayout(IDWriteTextLayout*layout,std::u16string text,std::uint64_t revision,core::Rect viewport,bool document,const ehud::data::Json& style,core::Point offset):impl_(std::make_unique<Impl>(layout,std::move(text),revision,viewport,document,style,offset)){
    need(layout&&impl_->text.size()<=65536&&core::text::Buffer::validUTF16(impl_->text)&&finite(viewport)&&viewport.width>0&&viewport.height>0,"Invalid painted text layout handle");
}
PaintedTextLayout::~PaintedTextLayout()=default;
std::u16string_view PaintedTextLayout::text()const noexcept{return impl_->text;}
std::uint64_t PaintedTextLayout::sourceRevision()const noexcept{return impl_->revision;}
core::Point PaintedTextLayout::drawingOrigin()const noexcept{return {impl_->viewport.x,impl_->viewport.y};}
core::Rect PaintedTextLayout::viewport()const noexcept{return impl_->viewport;}
bool PaintedTextLayout::isDocumentLayout()const noexcept{return impl_->document;}
double PaintedTextLayout::documentHeight()const noexcept{return impl_->height;}
core::Point PaintedTextLayout::initialPaintOffset()const noexcept{return impl_->initialOffset;}
std::size_t PaintedTextLayout::metadataBytes()const noexcept{return impl_->textBytes;}
IDWriteTextLayout*PaintedTextLayout::nativeLayout()const noexcept{return impl_->layout.Get();}
const DocumentTextLines&PaintedTextLayout::lines()const noexcept{return impl_->lines;}
bool PaintedTextLayout::matchesPlainStyle(const ehud::data::Json& style,core::Rect viewport)const{
    if(GetCurrentThreadId()!=impl_->thread||!impl_->document||impl_->viewport!=viewport||!style.isObject())return false;
    return impl_->plainStyle==style;
}
std::uintptr_t PaintedTextLayout::layoutIdentity()const noexcept{return reinterpret_cast<std::uintptr_t>(impl_->layout.Get());}
struct LayerTextLayout::Impl {
    DWORD thread{GetCurrentThreadId()};std::shared_ptr<const PaintedTextLayout>painted;std::uint64_t documentRevision{};
    mutable std::vector<DWRITE_HIT_TEST_METRICS>metrics;mutable std::vector<core::Rect>rectangles;mutable std::optional<core::text::Range>cachedRange;mutable bool clipped{};
    std::vector<std::uint32_t>clusters;
    void onThread()const{if(GetCurrentThreadId()!=thread)throw std::logic_error("Painted text used outside its owning UI thread");}
    void query(core::text::Range r)const{
        onThread();if(cachedRange==r)return;need(r.start<=r.end&&r.end<=painted->text().size(),"Painted text ACP range exceeds its document");cachedRange.reset();rectangles.clear();clipped=false;
        auto*layout=painted->impl_->layout.Get();const auto viewport=painted->viewport();const core::Rect clip{0,0,viewport.width,viewport.height};
        auto append=[&](core::Rect rect,bool trimmed){need(finite(rect)&&rect.width>=0&&rect.height>=0,"DirectWrite returned invalid range geometry");
            if(painted->isDocumentLayout()){clipped|=trimmed;if(rect.width>0&&rect.height>0)rectangles.push_back(rect);return;}
            const double l=std::max(0.0,rect.x),t=std::max(0.0,rect.y),right=std::min(clip.width,rect.x+rect.width),bottom=std::min(clip.height,rect.y+rect.height);
            clipped|=trimmed||l!=rect.x||t!=rect.y||right!=rect.x+rect.width||bottom!=rect.y+rect.height;
            if(right>l&&bottom>t)rectangles.push_back({l,t,right-l,bottom-t});
        };
        if(r.start==r.end){FLOAT x{},y{};DWRITE_HIT_TEST_METRICS m{};checked(layout->HitTestTextPosition(r.start,FALSE,&x,&y,&m),"Read caret from painted layout");append({x,y+painted->impl_->lines.offsetAtACP(r.start),1,std::max(1.0,double(m.height))},m.isTrimmed!=FALSE);}
        else{
            UINT32 count{};if(metrics.empty())metrics.resize(16);
            auto result=layout->HitTestTextRange(r.start,r.end-r.start,0,0,metrics.data(),static_cast<UINT32>(metrics.size()),&count);
            if(result==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER)){need(count<=painted->text().size()+2,"Painted range geometry exceeds its ACP bound");metrics.resize(count);result=layout->HitTestTextRange(r.start,r.end-r.start,0,0,metrics.data(),static_cast<UINT32>(metrics.size()),&count);}
            checked(result,"Read selection from painted layout");need(count<=metrics.size(),"DirectWrite range count exceeds owned buffer");rectangles.reserve(count);
            for(UINT32 i=0;i<count;++i){const auto&m=metrics[i];append({m.left,m.top+painted->impl_->lines.offsetAtACP(m.textPosition),m.width,m.height},m.isTrimmed!=FALSE);}
        }
        cachedRange=r;
    }
};
LayerTextLayout::LayerTextLayout(std::shared_ptr<const PaintedTextLayout>layout,const core::text::Document&doc):impl_(std::make_unique<Impl>()){bind(std::move(layout),doc);}
LayerTextLayout::~LayerTextLayout()=default;
bool LayerTextLayout::bind(std::shared_ptr<const PaintedTextLayout>layout,const core::text::Document&doc){
    auto&i=*impl_;i.onThread();need(layout&&layout->impl_->thread==i.thread&&layout->text()==doc.text(),"Painted layout UTF-16 text differs from editable document");
    if(i.painted==layout&&i.documentRevision==doc.revision())return false;
    if(i.painted!=layout){
        UINT32 count{};auto result=layout->impl_->layout->GetClusterMetrics(nullptr,0,&count);
        need(SUCCEEDED(result)||result==HRESULT_FROM_WIN32(ERROR_INSUFFICIENT_BUFFER),"Cannot measure painted clusters");
        need(count<=layout->text().size(),"Painted cluster count exceeds its text");
        std::vector<DWRITE_CLUSTER_METRICS>metrics(count);std::vector<std::uint32_t>boundaries;boundaries.reserve(std::size_t(count)+1);boundaries.push_back(0);
        if(count){checked(layout->impl_->layout->GetClusterMetrics(metrics.data(),count,&count),"Read painted glyph clusters");need(count==metrics.size(),"Painted cluster count changed unexpectedly");}
        std::uint32_t at{};
        for(const auto&cluster:metrics){need(cluster.length>0&&cluster.length<=layout->text().size()-at,"Invalid painted cluster length");at+=cluster.length;
            const auto value=layout->text();need(at==value.size()||!(value[at]>=0xdc00&&value[at]<=0xdfff),"Painted cluster splits a UTF-16 pair");boundaries.push_back(at);}
        need(at==layout->text().size(),"Painted clusters do not cover the document");i.clusters=std::move(boundaries);
    }
    i.painted=std::move(layout);i.documentRevision=doc.revision();i.cachedRange.reset();i.rectangles.clear();return true;
}
std::uint64_t LayerTextLayout::textRevision()const noexcept{return impl_->documentRevision;}
std::span<const core::Rect>LayerTextLayout::selectionRectangles(core::text::Range range)const{impl_->query(range);return impl_->rectangles;}
std::optional<core::text::RangeBounds>LayerTextLayout::bounds(core::text::Range range)const{
    auto&i=*impl_;i.query(range);if(i.rectangles.empty())return core::text::RangeBounds{{},true};auto result=i.rectangles.front();
    for(const auto&r:i.rectangles){const double l=std::min(result.x,r.x),t=std::min(result.y,r.y),right=std::max(result.x+result.width,r.x+r.width),bottom=std::max(result.y+result.height,r.y+r.height);result={l,t,right-l,bottom-t};}
    return core::text::RangeBounds{result,i.clipped};
}
std::optional<std::uint32_t>LayerTextLayout::hit(core::Point point,bool nearest,bool roundNearest)const{
    auto&i=*impl_;i.onThread();if(!std::isfinite(point.x)||!std::isfinite(point.y)||std::abs(point.x)>std::numeric_limits<FLOAT>::max()||std::abs(point.y)>std::numeric_limits<FLOAT>::max())return {};
    BOOL trailing{},inside{};DWRITE_HIT_TEST_METRICS metric{};const auto result=i.painted->impl_->layout->HitTestPoint(static_cast<FLOAT>(point.x),static_cast<FLOAT>(i.painted->impl_->lines.nativeY(point.y)),&trailing,&inside,&metric);if(FAILED(result)||(!nearest&&!inside))return {};
    const auto position=std::uint64_t(metric.textPosition)+(roundNearest&&trailing?metric.length:0u);return position<=i.painted->text().size()?std::optional(std::uint32_t(position)):std::nullopt;
}
std::shared_ptr<const PaintedTextLayout>LayerTextLayout::painted()const noexcept{return impl_->painted;}
std::span<const std::uint32_t>LayerTextLayout::clusterBoundaries()const{impl_->onThread();return impl_->clusters;}
std::uint32_t LayerTextLayout::previousCluster(std::uint32_t acp)const{const auto&i=*impl_;i.onThread();need(acp<=i.painted->text().size(),"Cluster ACP exceeds document");const auto at=std::lower_bound(i.clusters.begin(),i.clusters.end(),acp);return at==i.clusters.begin()?0:*std::prev(at);}
std::uint32_t LayerTextLayout::nextCluster(std::uint32_t acp)const{const auto&i=*impl_;i.onThread();need(acp<=i.painted->text().size(),"Cluster ACP exceeds document");const auto at=std::upper_bound(i.clusters.begin(),i.clusters.end(),acp);return at==i.clusters.end()?i.clusters.back():*at;}

} // namespace endfield::native
#endif
