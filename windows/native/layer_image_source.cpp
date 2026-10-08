#include "native/layer_image_source.hpp"
#include "core/data/json.hpp"
#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <thread>

namespace endfield::native {
namespace detail {struct LayerImageBudget {std::atomic<std::size_t>count{},bytes{};};}
namespace {
void need(bool value,const char*message){if(!value)throw std::invalid_argument(message);}
std::uint8_t premultiply(std::uint8_t color,std::uint8_t alpha)noexcept{return static_cast<std::uint8_t>((unsigned(color)*unsigned(alpha)+127u)/255u);}
bool samePixels(const LayerMemoryImage&image,unsigned width,unsigned height,std::span<const std::uint8_t>rgba){
    if(image.width()!=width||image.height()!=height)return false;const auto pixels=image.premultipliedBGRA();
    for(std::size_t n=0;n<rgba.size();n+=4){const auto a=rgba[n+3];if(pixels[n]!=premultiply(rgba[n+2],a)||pixels[n+1]!=premultiply(rgba[n+1],a)||pixels[n+2]!=premultiply(rgba[n],a)||pixels[n+3]!=a)return false;}return true;
}
}
LayerMemoryImage::LayerMemoryImage(std::string key,std::uint64_t revision,unsigned width,unsigned height,std::vector<std::uint8_t>pixels,std::shared_ptr<detail::LayerImageBudget>budget):key_(std::move(key)),revision_(revision),width_(width),height_(height),pixels_(std::move(pixels)),budget_(std::move(budget)){
    budget_->count.fetch_add(1,std::memory_order_relaxed);budget_->bytes.fetch_add(pixels_.size(),std::memory_order_relaxed);
}
LayerMemoryImage::~LayerMemoryImage(){budget_->bytes.fetch_sub(pixels_.size(),std::memory_order_relaxed);budget_->count.fetch_sub(1,std::memory_order_relaxed);}
struct LayerImageSource::Impl {
    const std::thread::id owner{std::this_thread::get_id()};
    std::shared_ptr<detail::LayerImageBudget>budget{std::make_shared<detail::LayerImageBudget>()};
    std::vector<std::shared_ptr<const LayerMemoryImage>>cache; // oldest first
    std::vector<std::weak_ptr<const LayerMemoryImage>>live; // bounded revision conflict registry
    LayerImageSourceStats counts;
    Impl(){cache.reserve(maximumCachedImages);live.reserve(maximumLiveImages);}
    void onThread()const{need(owner==std::this_thread::get_id(),"Layer image provider requires its creating thread");}
    void remember(std::shared_ptr<const LayerMemoryImage>image){
        const auto old=std::find_if(cache.begin(),cache.end(),[&](const auto&entry){return entry->key()==image->key();});
        if(old!=cache.end())cache.erase(old);else if(cache.size()==maximumCachedImages){cache.erase(cache.begin());++counts.evictions;}
        cache.push_back(std::move(image)); // reserved bounded storage, no allocation
    }
};
LayerImageSource::LayerImageSource():impl_(std::make_unique<Impl>()){}
LayerImageSource::~LayerImageSource()=default;
std::shared_ptr<const LayerMemoryImage>LayerImageSource::publish(std::string_view key,std::uint64_t revision,unsigned width,unsigned height,std::span<const std::uint8_t>rgba){
    auto&i=*impl_;i.onThread();need(!key.empty()&&key.size()<=maximumKeyBytes&&ehud::data::Json::validUtf8(key)&&key.find('\0')==std::string_view::npos,"Invalid memory image identity");
    need(revision!=0,"Memory image revision must be nonzero");need(width>0&&height>0&&width<=maximumDimension&&height<=maximumDimension,"Memory image dimensions exceed explicit bounds");
    const auto bytes=std::size_t(width)*height*4;need(rgba.size()==bytes,"Memory image requires tight complete RGBA8 rows");
    std::shared_ptr<const LayerMemoryImage>same;
    for(const auto&entry:i.live)if(const auto image=entry.lock();image&&image->key()==key){need(revision>=image->revision(),"Stale memory image revision");if(revision==image->revision())same=image;}
    if(same){need(samePixels(*same,width,height,rgba),"Conflicting pixels for immutable memory image revision");i.remember(same);return same;}
    need(i.budget->count.load(std::memory_order_relaxed)<maximumLiveImages,"Memory image borrowers exceed live snapshot capacity");
    const auto used=i.budget->bytes.load(std::memory_order_relaxed);need(used<=maximumLiveBytes&&bytes<=maximumLiveBytes-used,"Memory image borrowers exceed live pixel budget");
    // Every throwing allocation precedes cache/LRU mutation. Snapshot lifetime
    // accounting includes a control-block allocation failure via its destructor.
    std::vector<std::uint8_t>pixels(bytes);for(std::size_t n=0;n<bytes;n+=4){const auto a=rgba[n+3];pixels[n]=premultiply(rgba[n+2],a);pixels[n+1]=premultiply(rgba[n+1],a);pixels[n+2]=premultiply(rgba[n],a);pixels[n+3]=a;}
    auto image=std::shared_ptr<const LayerMemoryImage>(new LayerMemoryImage(std::string(key),revision,width,height,std::move(pixels),i.budget));
    i.live.erase(std::remove_if(i.live.begin(),i.live.end(),[](const auto&entry){return entry.expired();}),i.live.end());
    i.live.push_back(image);i.remember(image);++i.counts.publications;return image;
}
std::shared_ptr<const LayerMemoryImage>LayerImageSource::acquire(std::string_view key,std::uint64_t revision){
    auto&i=*impl_;i.onThread();const auto found=std::find_if(i.cache.begin(),i.cache.end(),[&](const auto&entry){return entry->key()==key&&entry->revision()==revision;});
    if(found==i.cache.end()){++i.counts.cacheMisses;return {};}
    auto result=*found;std::rotate(found,std::next(found),i.cache.end());++i.counts.cacheHits;return result;
}
bool LayerImageSource::retire(std::string_view key){auto&i=*impl_;i.onThread();const auto found=std::find_if(i.cache.begin(),i.cache.end(),[&](const auto&entry){return entry->key()==key;});if(found==i.cache.end())return false;i.cache.erase(found);return true;}
void LayerImageSource::clear(){auto&i=*impl_;i.onThread();i.cache.clear();i.live.erase(std::remove_if(i.live.begin(),i.live.end(),[](const auto&entry){return entry.expired();}),i.live.end());}
LayerImageSourceStats LayerImageSource::stats()const{const auto&i=*impl_;i.onThread();auto result=i.counts;result.cachedImages=i.cache.size();for(const auto&entry:i.cache)result.cachedBytes+=entry->premultipliedBGRA().size();result.liveImages=i.budget->count.load(std::memory_order_relaxed);result.liveBytes=i.budget->bytes.load(std::memory_order_relaxed);return result;}
} // namespace endfield::native
