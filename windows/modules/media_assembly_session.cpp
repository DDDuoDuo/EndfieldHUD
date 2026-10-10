#include "modules/media_assembly_session.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
MediaAssemblyView MediaAssemblySession::view()const noexcept{return {document_.get(),&adjustments_,preview_?core::Point{double(preview_->width),double(preview_->height)}:core::Point{},busy_,exportToken_.has_value(),playing_,currentTime_};}
void MediaAssemblySession::stopPlayback()noexcept{playing_=wantsPlayback_=false;}
void MediaAssemblySession::setActive(bool value){
    if(active_==value)return;active_=value;
    if(value)requestPreview();else{stopPlayback();pending_.reset();++previewGeneration_;preview_.reset();}
    ++revision_;
}
std::optional<std::uint64_t>MediaAssemblySession::beginImport(){
    if(exportToken_)return {};stopPlayback();busy_=true;error_.reset();++revision_;return ++importGeneration_;
}
bool MediaAssemblySession::finishImport(std::uint64_t generation,std::shared_ptr<const MediaAssemblyDocumentInfo>document,std::optional<std::string>error){
    if(generation!=importGeneration_||!busy_)return false;
    // Provider metadata must already satisfy the same source import limits.
    if(document&&(!std::isfinite(document->pixels.x)||!std::isfinite(document->pixels.y)||document->pixels.x<=0||document->pixels.y<=0||document->pixels.x*document->pixels.y>64000000||document->id.empty()||(document->video&&(!std::isfinite(document->duration)||document->duration<=0)))){document.reset();error="Unsupported media";}
    busy_=false;error_=std::move(error);if(document&&!error_){document_=std::move(document);adjustments_={};currentTime_=0;preview_.reset();requestPreview();}++revision_;return true;
}
bool MediaAssemblySession::close(){
    if(exportToken_)return false;++importGeneration_;++previewGeneration_;stopPlayback();pending_.reset();document_.reset();adjustments_={};preview_.reset();currentTime_=0;busy_=cropPreview_=false;error_.reset();progress_=0;++revision_;return true;
}
bool MediaAssemblySession::setCropPreview(bool value){if(cropPreview_==value)return false;cropPreview_=value;if(value)stopPlayback();requestPreview();++revision_;return true;}
double MediaAssemblySession::clampedTime(double value)const noexcept{return std::min(std::max(adjustments_.trimStart,value),std::max(adjustments_.trimStart,adjustments_.trimEnd.value_or(document_?document_->duration:0)-.001));}
bool MediaAssemblySession::update(MediaAssemblyAdjustments value,std::optional<double>time){
    if(exportToken_||!document_||!value.valid()||(time&&!std::isfinite(*time))||(document_->video&&(!value.timeRange(document_->duration)||!value.stickers.empty())))return false;
    if(value==adjustments_)return time?seek(*time):false;
    auto before=adjustments_,after=value;before.stickers.clear();after.stickers.clear();if(cropPreview_){before.crop={};after.crop={};}const bool needsRender=before!=after;
    adjustments_=std::move(value);error_.reset();if(document_->video){currentTime_=clampedTime(time.value_or(currentTime_));playing_=false;if(time)wantsPlayback_=false;}
    if(needsRender||time)requestPreview();++revision_;return true;
}
bool MediaAssemblySession::seek(double time){
    if(!document_||!document_->video||!std::isfinite(time))return false;currentTime_=clampedTime(time);if(!playing_)requestPreview();++revision_;return true;
}
bool MediaAssemblySession::reset(){return update({});}
bool MediaAssemblySession::togglePlayback(){if(!active_||!document_||!document_->video||exportToken_)return false;wantsPlayback_=!wantsPlayback_;if(!wantsPlayback_){playing_=false;requestPreview();}++revision_;return true;}
void MediaAssemblySession::playbackChanged(bool playing,double time){if(!active_||!document_||!document_->video||!std::isfinite(time))return;const double value=clampedTime(time);playing=playing&&wantsPlayback_&&!exportToken_;if(playing_==playing&&currentTime_==value)return;playing_=playing;currentTime_=value;++revision_;}
void MediaAssemblySession::playbackEnded(){if(!document_||!document_->video)return;stopPlayback();requestPreview();++revision_;}
void MediaAssemblySession::requestPreview(){if(!active_||!document_)return;auto value=adjustments_;if(cropPreview_)value.crop={};value.stickers.clear();pending_=MediaAssemblyPreviewRequest{++previewGeneration_,document_,std::move(value),currentTime_};}
std::optional<MediaAssemblyPreviewRequest>MediaAssemblySession::takePreview(){if(running_||!pending_)return {};running_=pending_->generation;auto out=std::move(pending_);pending_.reset();return out;}
bool MediaAssemblySession::completePreview(std::uint64_t generation,std::optional<Preview>frame,std::optional<std::string>error){
    if(!running_||*running_!=generation)return false;running_.reset();
    if(!active_||!document_||generation!=previewGeneration_)return false;
    if(frame&&(!frame->image||!frame->width||!frame->height||frame->width>1024||frame->height>1024)){error="Invalid preview dimensions";frame.reset();}
    error_=std::move(error);if(frame&&!error_){preview_=std::move(frame);acceptedPreview_=generation;}++revision_;return true;
}
std::optional<std::uint64_t>MediaAssemblySession::beginExport(std::string_view expected){
    if(exportToken_||busy_||!document_||expected!=document_->id)return {};stopPlayback();progress_=0;error_.reset();exportToken_=++exportGeneration_;++revision_;return exportToken_;
}
bool MediaAssemblySession::exportProgress(std::uint64_t token,double value){if(!exportToken_||*exportToken_!=token||!std::isfinite(value))return false;value=std::clamp(value,0.,1.);if(std::abs(value-progress_)<.005)return false;progress_=value;if(active_)++revision_;return true;}
bool MediaAssemblySession::finishExport(std::uint64_t token,std::optional<std::string>error){if(!exportToken_||*exportToken_!=token)return false;exportToken_.reset();error_=std::move(error);if(!error_)progress_=1;++revision_;return true;}
}
