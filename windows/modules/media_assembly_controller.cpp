#include "modules/media_assembly_controller.hpp"
#include <cmath>
#include <stdexcept>

namespace endfield::modules {
namespace {
void need(bool v,const char*why){if(!v)throw std::invalid_argument(why);}
void checkTime(double t){need(std::isfinite(t)&&t>=0,"Media Assembly controller needs finite time");}
constexpr double editedDelay=.4,progressInterval=.25;
}
MediaAssemblyController::MediaAssemblyController(std::shared_ptr<MediaAssemblyEngine>e,MediaAssemblySubmit s,Callbacks c,MediaAssemblySubmit x):engine_(std::move(e)),submit_(std::move(s)),exportSubmit_(std::move(x)),callbacks_(std::move(c)){
    need(engine_&&submit_&&callbacks_.uuid,"Media Assembly controller requires an engine, the utility executor and an identity source");
}
MediaAssemblyController::~MediaAssemblyController(){alive_->value=false;if(ticket_)ticket_->cancel();}
void MediaAssemblyController::changed(){if(callbacks_.changed)callbacks_.changed();}
void MediaAssemblyController::flushEdited(){if(!editedDeadline_)return;editedDeadline_.reset();if(callbacks_.event)callbacks_.event("edited");}
void MediaAssemblyController::scheduleEdited(double t){editedDeadline_=t+editedDelay;}
std::shared_ptr<const MediaAssemblyPreviewImage>MediaAssemblyController::previewImage()const noexcept{
    const auto&p=session_.preview();return p?std::static_pointer_cast<const MediaAssemblyPreviewImage>(p->image):nullptr;
}
void MediaAssemblyController::setActive(bool value,double t){
    checkTime(t);lastTime_=t;if(session_.active()==value)return;
    if(!value){flushEdited();session_.setActive(false);engine_->clearCaches();}
    else{session_.setActive(true);pump();}
    changed();
}
bool MediaAssemblyController::importPath(std::string path,std::shared_ptr<void>lease,bool recordEvent,double t){
    checkTime(t);if(session_.exporting()||path.empty())return false;
    flushEdited();const auto token=session_.beginImport();if(!token)return false;
    auto engine=engine_;auto result=std::make_shared<std::optional<MediaAssemblyDocumentInfo>>();
    const std::weak_ptr<Alive>alive=alive_;const auto id=callbacks_.uuid();
    const bool accepted=submit_([engine,path,lease=std::move(lease),result]()mutable{
        struct Release {std::shared_ptr<void>&l;~Release(){l.reset();}}release{lease};
        *result=engine->open(path);
    },[this,alive,token=*token,result,recordEvent,id](std::exception_ptr error){
        const auto a=alive.lock();if(!a||!a->value)return;
        std::shared_ptr<const MediaAssemblyDocumentInfo>document;std::optional<MediaAssemblyError>failure;
        if(error)failure=mediaAssemblyErrorFrom(error,MediaAssemblyError::unsupported);
        else if(*result){auto info=std::move(**result);info.id=id;document=std::make_shared<const MediaAssemblyDocumentInfo>(std::move(info));}
        else failure=MediaAssemblyError::unsupported;
        if(!session_.finishImport(token,document,failure))return;
        if(session_.document()&&session_.document()==document&&!session_.error()){if(recordEvent){lastExport_.reset();if(callbacks_.event)callbacks_.event("imported");}}
        pump();changed();
    });
    if(!accepted){session_.finishImport(*token,nullptr,MediaAssemblyError::unavailable);}
    changed();return accepted;
}
bool MediaAssemblyController::closeDocument(double t){
    checkTime(t);if(session_.exporting())return false;
    flushEdited();if(!session_.close())return false;lastExport_.reset();engine_->clearCaches();
    // A queued old decode can start after the immediate purge. Clearing again
    // behind it releases that last bounded source without a poller.
    auto engine=engine_;submit_([engine]{engine->clearCaches();},[](std::exception_ptr){});
    changed();return true;
}
bool MediaAssemblyController::update(MediaAssemblyAdjustments value,std::optional<double>scrub,double t){
    checkTime(t);const bool before=session_.error().has_value();const auto previous=session_.adjustments();
    if(!session_.update(std::move(value),scrub)){if(session_.error()&&!before)changed();return false;}
    if(session_.adjustments()!=previous)scheduleEdited(t);pump();changed();return true;
}
bool MediaAssemblyController::reset(double t){return update({},std::nullopt,t);}
bool MediaAssemblyController::seek(double v,double t){checkTime(t);if(!session_.seek(v))return false;pump();changed();return true;}
bool MediaAssemblyController::setCropPreview(bool v,double t){checkTime(t);if(!session_.setCropPreview(v))return false;pump();changed();return true;}
bool MediaAssemblyController::togglePlayback(double t){checkTime(t);if(!session_.togglePlayback())return false;pump();changed();return true;}
void MediaAssemblyController::report(MediaAssemblyError e){session_.report(e);changed();}
void MediaAssemblyController::setDeferredPreviews(bool value,double t){
    checkTime(t);lastTime_=std::max(lastTime_,t);if(deferred_==value)return;deferred_=value;
    if(session_.refreshPreview()){pump();changed();}
}
std::vector<std::string>MediaAssemblyController::exportExtensions()const{const auto&d=session_.document();return d?mediaAssemblyExportExtensions(*d,engine_->heicEncoder()):std::vector<std::string>{};}
void MediaAssemblyController::pump(){
    auto request=session_.takePreview();if(!request)return;
    auto engine=engine_;auto image=std::make_shared<MediaAssemblyPreviewImage>();const std::weak_ptr<Alive>alive=alive_;
    const auto generation=request->generation;
    const auto taken=*request;
    const bool accepted=submit_([engine,request=*request,image,deferred=deferred_]{*image=deferred?engine->previewSource(*request.document,request.adjustments,request.time):engine->preview(*request.document,request.adjustments,request.time);},
        [this,alive,generation,image](std::exception_ptr error){
            const auto a=alive.lock();if(!a||!a->value)return;++completed_;
            std::optional<MediaAssemblySession::Preview>frame;std::optional<MediaAssemblyError>failure;
            if(error)failure=mediaAssemblyErrorFrom(error,MediaAssemblyError::unsupported);
            else frame=MediaAssemblySession::Preview{image->width,image->height,std::shared_ptr<const MediaAssemblyPreviewImage>(image)};
            const bool published=session_.completePreview(generation,std::move(frame),failure);
            pump();if(published)changed();
        });
    // A full executor keeps the request pending; the next event or
    // completion retries it (no timer).
    if(!accepted)session_.returnPreview(taken);
}
bool MediaAssemblyController::exportTo(std::string destination,bool overwrite,std::string_view expected,double t){
    checkTime(t);lastTime_=t;const auto document=session_.document();
    if(session_.exporting()||session_.busy()||!document||(!expected.empty()&&expected!=document->id)){return false;}
    flushEdited();const auto token=session_.beginExport(document->id);if(!token)return false;
    auto ticket=std::make_shared<MediaAssemblyExportTicket>();ticket_=ticket;exportToken_=token;progressDeadline_=t+progressInterval;
    MediaAssemblyExportRequest request{document,session_.adjustments(),std::move(destination),overwrite,callbacks_.uuid(),engine_->heicEncoder()};
    auto engine=engine_;auto target=std::make_shared<std::string>();const std::weak_ptr<Alive>alive=alive_;
    const auto&run=exportSubmit_?exportSubmit_:submit_;
    const bool accepted=run([engine,request,ticket,target]{
        struct Finish {MediaAssemblyExportTicket&t;~Finish(){t.finish();}}finish{*ticket};
        *target=engine->exportMedia(request,*ticket);
    },[this,alive,ticket,token=*token,target,document](std::exception_ptr error){
        const auto a=alive.lock();if(!a||!a->value||ticket_!=ticket)return;
        ticket_.reset();exportToken_.reset();progressDeadline_.reset();
        if(error){session_.finishExport(token,mediaAssemblyErrorFrom(error,MediaAssemblyError::exportFailed));changed();return;}
        session_.finishExport(token);lastExport_=*target;if(callbacks_.event)callbacks_.event("exported");
        // Atomic overwrite creates a new file identity. Reload the new source
        // instead of ever applying the edits twice, without "imported".
        if(engine_->samePath(*target,document->path))importPath(*target,nullptr,false,lastTime_);
        changed();
    });
    if(!accepted){ticket_.reset();exportToken_.reset();progressDeadline_.reset();session_.finishExport(*token,MediaAssemblyError::exportFailed);}
    changed();return accepted;
}
void MediaAssemblyController::cancelExport(){if(ticket_)ticket_->cancel();}
std::optional<double>MediaAssemblyController::nextWakeTime()const noexcept{
    std::optional<double>out=editedDeadline_;if(progressDeadline_&&(!out||*progressDeadline_<*out))out=progressDeadline_;return out;
}
bool MediaAssemblyController::deadline(double t){
    checkTime(t);lastTime_=std::max(lastTime_,t);bool did{};
    if(editedDeadline_&&t>=*editedDeadline_){flushEdited();did=true;}
    if(progressDeadline_&&t>=*progressDeadline_&&ticket_&&exportToken_){
        progressDeadline_=t+progressInterval;if(session_.exportProgress(*exportToken_,ticket_->progress())&&session_.active())changed();did=true;
    }
    pump();return did;
}
}
