#include "modules/media_assembly_session.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
namespace {
namespace m=endfield::modules;unsigned checks{};
void check(bool v,const char*why){++checks;if(!v)throw std::runtime_error(why);}
std::shared_ptr<const m::MediaAssemblyDocumentInfo>doc(std::string id,bool video=false){return std::make_shared<const m::MediaAssemblyDocumentInfo>(m::MediaAssemblyDocumentInfo{std::move(id),video,{1920,1080},120});}
m::MediaAssemblySession::Preview frame(){return {1024,576,std::make_shared<const int>(42)};}
void import(m::MediaAssemblySession&s,std::string id,bool video=false){const auto token=s.beginImport();check(bool(token),"Explicit import starts");check(s.finishImport(*token,doc(std::move(id),video)),"Current import delivered");}
void queues(){
    m::MediaAssemblySession s;import(s,"photo");check(!s.pendingCount()&&!s.preview(),"Hidden imports do not start image processing");s.setActive(true);check(s.pendingCount()==1,"Activation schedules one bounded preview");auto request=s.takePreview();check(request&&request->document->id=="photo"&&s.working()&&!s.takePreview(),"Only one request can run on borrowed worker");
    for(unsigned i=0;i<1000;++i){auto a=*s.view().adjustments;a.brightness=double(i%100)/100.;s.update(std::move(a));check(s.pendingCount()<=1,"Rapid adjustments replace one pending request");}
    check(!s.completePreview(request->generation,frame())&&!s.preview(),"Stale decode cannot replace current edit");auto latest=s.takePreview();check(latest&&latest->adjustments.brightness==.99,"Coalesced decode is latest adjustment");check(s.completePreview(latest->generation,frame())&&s.preview()&&s.previewRevision()==latest->generation,"Matching bounded preview published");
    auto p=*s.view().adjustments;p.stickers.push_back({"one",m::MediaAssemblyStickerKind::sticker7,.5,.5,.2,0});check(s.update(p)&&!s.pendingCount(),"Sticker insertion changes geometry without processing base image");p.stickers[0].x=.65;p.stickers[0].rotation=37;check(s.update(p)&&!s.pendingCount(),"Moving and rotating sticker reuse poster");
    s.setCropPreview(true);auto crop=s.takePreview();check(crop&&crop->adjustments.crop==m::MediaAssemblyCrop{}&&crop->adjustments.stickers.empty(),"Crop editor uses original uncropped bounded image and retained sticker overlays");check(s.completePreview(crop->generation,frame()),"Uncropped base delivered");p.crop={.1,.1,.8,.8};check(s.update(p)&&!s.pendingCount(),"Crop handle drag is geometry-only while editing");s.setCropPreview(false);check(s.pendingCount()==1,"Leaving crop processes the committed crop once");
    const auto final=s.takePreview();s.setActive(false);check(!s.preview()&&!s.pendingCount()&&!s.completePreview(final->generation,frame()),"Hide clears bounded posterior and rejects late decode");s.setActive(true);check(s.pendingCount()==1,"Reopen requests just current editor state");
}
void replacements(){
    m::MediaAssemblySession s;s.setActive(true);const auto old=s.beginImport(),next=s.beginImport();check(old&&next&&!s.finishImport(*old,doc("stale")),"Replaced import rejected");check(s.finishImport(*next,doc("current")),"Newest import accepted");const auto work=s.takePreview();check(work.has_value(),"Owned decode started");check(s.close()&&!s.view().document,"Close clears only transient source");check(!s.completePreview(work->generation,frame())&&!s.preview(),"Late source decode cannot restore closed document");
    const auto token=s.beginImport();auto invalid=std::make_shared<m::MediaAssemblyDocumentInfo>(m::MediaAssemblyDocumentInfo{"invalid",false,{64000001,1},0});check(s.finishImport(*token,invalid)&&!s.view().busy&&s.error(),"Invalid native metadata terminates import with error instead of hanging");
    import(s,"valid");auto req=s.takePreview();check(req.has_value(),"New source recovers after invalid metadata");auto bad=frame();bad.width=1025;check(s.completePreview(req->generation,bad)&&s.error()&&!s.preview(),"Native preview cannot exceed source1024 bound");
}
void video(){
    m::MediaAssemblySession s;s.setActive(true);import(s,"movie",true);auto initial=s.takePreview();check(s.completePreview(initial->generation,frame()),"Movie poster accepted");check(s.togglePlayback()&&s.wantsPlayback(),"Playback request routed by owner");s.playbackChanged(true,20);check(s.view().playing&&s.view().currentTime==20,"Broker position accepted without decoding");auto a=*s.view().adjustments;a.brightness=.2;check(s.update(a)&&s.wantsPlayback()&&!s.view().playing,"Video edit retires old player but preserves original resume intent");
    s.playbackChanged(true,21);a.trimStart=10;a.trimEnd=50;check(s.update(a,50)&&!s.wantsPlayback()&&!s.view().playing&&std::abs(s.view().currentTime-49.999)<1e-10,"Dragging end stops player and previews exact bounded endpoint");
    const auto old=s.takePreview();if(old)s.completePreview(old->generation,frame());s.togglePlayback();s.playbackChanged(true,40);s.playbackEnded();check(!s.wantsPlayback()&&!s.view().playing&&s.pendingCount()==1,"End event stops and prepares poster without polling");
    a.stickers.push_back({"not-video"});check(!s.update(a),"Video cannot acquire unsupported sticker edits");check(!s.seek(std::numeric_limits<double>::quiet_NaN()),"Invalid seek ignored");
}
void exports(){
    m::MediaAssemblySession s;s.setActive(true);import(s,"one");check(!s.beginExport("old"),"Old save-panel document cannot export replacement");const auto token=s.beginExport("one");check(token&&!s.close()&&!s.beginImport()&&!s.reset(),"Running explicit export preserves source until completion");check(!s.exportProgress(*token,.004)&&s.progress()==0,"Subthreshold progress does not redraw");check(s.exportProgress(*token,.6)&&s.progress()==.6,"Progress uses external event");const auto revision=s.revision();s.setActive(false);const auto hidden=s.revision();check(hidden>revision&&s.exportProgress(*token,.8)&&s.revision()==hidden,"Hidden export progress does not invalidate HUD");check(!s.finishExport(*token+1),"Unrelated completion ignored");check(s.finishExport(*token)&&s.progress()==1&&s.close(),"Correct completion releases export gate");
}
}
int main(){try{queues();replacements();video();exports();std::cout<<"PASS "<<checks<<" bounded Media Assembly session checks\n";return 0;}catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
