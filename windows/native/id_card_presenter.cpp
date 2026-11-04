#include "native/id_card_presenter.hpp"
#ifdef _WIN32
#include "native/source_scene.hpp"
#include <stdexcept>
#include <utility>

namespace endfield::native {
namespace {
void need(bool v,const char*m){if(!v)throw std::invalid_argument(m);}
}
void IdCardFeed::publish(const modules::IdCardBinding*card,const std::array<std::optional<IdCardTextureView>,3>&textures){
    for(const auto&t:textures)if(t)need(t->width>0&&t->height>0&&t->revision>0&&t->rgba.size()==std::size_t(t->width)*t->height*4,"ID card textures are tight straight RGBA8 with a revision");
    card_=card;
    if(card)textures_=textures;else textures_.fill(std::nullopt);
    ++revision_;changed();
}
void IdCardFeed::withdraw(){card_=nullptr;textures_.fill(std::nullopt);++revision_;changed();}
void IdCardFeed::changed(){if(presenter_){presenter_->dirty_=true;if(presenter_->changed_)presenter_->changed_();}}
IdCardFeed::~IdCardFeed(){if(presenter_){presenter_->feed_=nullptr;presenter_->dirty_=true;if(presenter_->changed_)presenter_->changed_();}}

IdCardTextureTarget idCardTextureTarget(SourceScene&materials){
    return {[&materials](std::string_view id,unsigned w,unsigned h,std::span<const std::uint8_t>rgba,std::uint64_t revision){return materials.replaceTexture(id,w,h,rgba,revision);},
            [&materials](std::string_view id){return materials.restoreTexture(id);}};
}
IdCardPresenter::IdCardPresenter(LayerScene&labels,IdCardCaptionLeaves leaves,LayerRasterOptions options,IdCardTextureTarget target,core::source::SourceDesktopFrameSettings baseline)
    :captions_(labels,std::move(leaves),std::move(options)),target_(std::move(target)),baseline_(std::move(baseline)),applied_(baseline_){
    need(static_cast<bool>(target_.replace)&&static_cast<bool>(target_.restore),"ID card textures need a replace/restore target");
    highlight_=highlightOf(baseline_);
}
// HUDSourceDesktopNavigationLayout.profileHighlightNodeID: the hover plate
// the source binds to desktop.profile.hover (desktopImages[highlightID]).
std::optional<std::string>IdCardPresenter::highlightOf(const core::source::SourceDesktopFrameSettings&baseline){
    std::optional<std::string>out;
    for(const auto&[node,image]:baseline.images)
        if(image.texture==idCardTextureIDs[static_cast<std::size_t>(IdCardTextureSlot::hover)]){need(!out,"One profile hover plate");out=node;}
    return out;
}
void IdCardPresenter::setBaseline(core::source::SourceDesktopFrameSettings baseline){
    if(baseline==baseline_)return;
    auto highlight=highlightOf(baseline); // validated before anything changes
    baseline_=std::move(baseline);highlight_=std::move(highlight);dirty_=true;
}
IdCardPresenter::~IdCardPresenter(){disconnect();}
void IdCardPresenter::connect(IdCardFeed&feed,std::function<void()>changed){
    disconnect();
    if(feed.presenter_)feed.presenter_->disconnect();
    feed_=&feed;feed.presenter_=this;changed_=std::move(changed);dirty_=true;
}
void IdCardPresenter::disconnect()noexcept{if(feed_){feed_->presenter_=nullptr;feed_=nullptr;}changed_=nullptr;dirty_=false;}
bool IdCardPresenter::pending()const noexcept{return dirty_||(feed_&&feed_->revision()!=seen_);}
std::optional<core::source::SourceDesktopFrameSettings>IdCardPresenter::apply(){
    ++stats_.applies;
    if(!pending()){++stats_.unchanged;return std::nullopt;}
    dirty_=false;seen_=feed_?feed_->revision():0;
    // No feed (never connected, disconnected or destroyed): the packet card.
    const auto*card=feed_?feed_->card():nullptr;
    // Captions follow the binding revision; a missing card keeps the packet captions.
    if(card&&card->revision()&&card->revision()!=cardRevision_){cardRevision_=card->revision();if(captions_.update(*card))++stats_.captionRepaints;}
    for(std::size_t k=0;k<3;++k){
        const auto*view=card?feed_->texture(static_cast<IdCardTextureSlot>(k)):nullptr;
        if(view){
            if(replaced_[k]&&textureRevision_[k]==view->revision)continue;
            if(target_.replace(idCardTextureIDs[k],view->width,view->height,view->rgba,view->revision))++stats_.textureReplacements;
            replaced_[k]=true;textureRevision_[k]=view->revision;
        }else if(replaced_[k]){
            if(target_.restore(idCardTextureIDs[k]))++stats_.textureRestores;
            replaced_[k]=false;textureRevision_[k]=0;
        }
    }
    // HUDSourceWatchView.setDesktopProfile frame settings: the baseline plus
    // the card's glow/fill/tint/sprite, with each bound image sized like the
    // texture it now samples (frameBuilder.desktopImages[id].size).
    auto next=baseline_;
    if(card&&card->revision()){
        card->apply(next,highlight_);
        const auto bind=[&](const std::string&node,IdCardTextureSlot slot){
            const auto k=static_cast<std::size_t>(slot);if(!replaced_[k]||node.empty())return;
            const auto*view=feed_->texture(slot);if(!view)return;
            auto&image=next.images[node];image.texture=std::string(idCardTextureIDs[k]);
            image.size={static_cast<float>(view->width),static_cast<float>(view->height)};
        };
        bind(card->source().playerHead,IdCardTextureSlot::avatar);
        bind(card->source().background,IdCardTextureSlot::background);
        if(highlight_)bind(*highlight_,IdCardTextureSlot::hover);
    }
    if(next==applied_)return std::nullopt;
    applied_=std::move(next);++stats_.settingsChanges;return applied_;
}
void IdCardPresenter::setReveal(std::function<void(std::string_view,std::optional<double>)>reveal){reveal_=std::move(reveal);revealed_.reset();if(reveal_)reveal_(captions_.leaves().surfaceIDs[1],std::nullopt);}
bool IdCardPresenter::advance(double time,bool reduceMotion){
    const bool running=captions_.advance(time,reduceMotion);const auto edge=captions_.revealEdge();
    if(edge!=revealed_){revealed_=edge;if(reveal_)reveal_(captions_.leaves().surfaceIDs[1],edge);}
    return running;
}
}
#endif
