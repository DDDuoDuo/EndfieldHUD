#include "modules/media_assembly_controller.hpp"
#include "modules/media_assembly_menu.hpp"
#include <algorithm>
#include <cmath>
#include <deque>
#include <iostream>
#include <map>
#include <stdexcept>

namespace {
namespace m=endfield::modules;namespace c=endfield::core;
unsigned checks{};
void check(bool v,const std::string&why){++checks;if(!v)throw std::runtime_error(why);}
template<class F>m::MediaAssemblyError failure(F&&f){try{f();}catch(const m::MediaAssemblyFailure&e){return e.error();}throw std::runtime_error("Expected a typed Media Assembly failure");}

void errors(){
    for(auto l:{c::Language::english,c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean})
        for(int e=0;e<=int(m::MediaAssemblyError::exists);++e)check(!m::mediaAssemblyErrorMessage(m::MediaAssemblyError(e),l).empty(),"Every source error is localized");
    check(m::mediaAssemblyErrorMessage(m::MediaAssemblyError::changedOnDisk,c::Language::english)=="The original changed. Open it again before exporting.","Original wording");
    check(m::mediaAssemblyErrorMessage(m::MediaAssemblyError::changedOnDisk,c::Language::japanese)!=m::mediaAssemblyErrorMessage(m::MediaAssemblyError::changedOnDisk,c::Language::english),"Catalog languages resolve");
    const auto windows=m::mediaAssemblyErrorMessage(m::MediaAssemblyError::unsupportedExport,c::Language::english);
    check(windows.find("Mac")==std::string::npos&&windows.find("MOV")==std::string::npos&&windows.find("MP4")!=std::string::npos,"Export capability message names this platform and its real formats");
    for(auto l:{c::Language::simplifiedChinese,c::Language::traditionalChinese,c::Language::japanese,c::Language::korean})check(m::mediaAssemblyErrorMessage(m::MediaAssemblyError::unsupportedExport,l).find("Mac")==std::string::npos,"Localized capability message is platform adapted");
}
void documents(){
    m::MediaAssemblyDocumentInfo d{"id",false,{10,10},0};d.name="Holiday.photo.JPG";check(d.suggestedFilename()=="Holiday.photo-edited.png","Image suggestion keeps the stem");
    d.name.clear();d.path="C:\\\\Users\\\\x\\\\clip.mov";d.video=true;check(d.suggestedFilename()=="clip-edited.mp4","Movie suggestion is MP4 on Windows");
    d.name=".hidden";d.video=false;check(d.suggestedFilename()==".hidden-edited.png","Leading dot is not an extension");
    d.kind=m::MediaAssemblyKind::gif;d.frameCount=1;check(!d.animated(),"Single-frame GIF is not animated");d.frameCount=12;check(d.animated(),"Animated GIF");
}
void formats(){
    check(m::mediaAssemblyExportFormat("PNG",false,false)==m::MediaAssemblyExportFormat{false,m::MediaAssemblyImageFormat::png},"PNG");
    check(m::mediaAssemblyExportFormat("jpeg",false,false)->image==m::MediaAssemblyImageFormat::jpeg&&m::mediaAssemblyExportFormat("tif",false,false)->image==m::MediaAssemblyImageFormat::tiff,"JPEG/TIFF aliases");
    check(!m::mediaAssemblyExportFormat("heic",false,false)&&m::mediaAssemblyExportFormat("heif",false,true)->image==m::MediaAssemblyImageFormat::heic,"HEIC only with an installed encoder");
    check(!m::mediaAssemblyExportFormat("gif",false,true)&&!m::mediaAssemblyExportFormat("mov",true,true)&&m::mediaAssemblyExportFormat("m4v",true,false)->video,"GIF/MOV unsupported, MP4/M4V video");
    m::MediaAssemblyDocumentInfo photo{"p",false,{1,1},0};check((m::mediaAssemblyExportExtensions(photo,false)==std::vector<std::string>{"png","jpg","tiff"}),"Source image extensions without HEIC");
    check(m::mediaAssemblyExportExtensions(photo,true).back()=="heic","HEIC offered when enumerable");photo.video=true;check((m::mediaAssemblyExportExtensions(photo,true)==std::vector<std::string>{"mp4"}),"Movie offers MP4");
    check(m::mediaAssemblyOverwriteCompatible("heif",m::MediaAssemblyImageFormat::heic)&&!m::mediaAssemblyOverwriteCompatible("jpeg",m::MediaAssemblyImageFormat::png),"Overwrite keeps container");
    check(m::mediaAssemblyTemporaryPath("C:\\\\a\\\\b.PNG","1234")=="C:\\\\a\\\\.endfield-export-1234.png","Temporary sibling name");
    bool rejected{};try{(void)m::mediaAssemblyTemporaryPath("x.png","../x");}catch(const std::exception&){rejected=true;}check(rejected,"Temporary identity cannot traverse");
}
void ticket(){
    m::MediaAssemblyExportTicket t;int cancels{};t.observeCancellation([&]{++cancels;});t.setProgress(.4);check(t.progress()==.4,"Completed fraction");
    double source=.7;t.observeProgress([&]{return source;});check(t.progress()==.7,"Progress source wins");source=3;check(t.progress()==1,"Clamped");
    t.cancel();check(t.cancelled()&&cancels==1,"Cancel calls the session cancel");check(failure([&]{t.commit([]{});})==m::MediaAssemblyError::cancelled,"Cancelled ticket cannot commit");
    m::MediaAssemblyExportTicket u;bool ran{};u.commit([&]{ran=true;});u.cancel();check(ran&&u.committed()&&!u.cancelled(),"Cancel after commit cannot turn success into cancellation");
    m::MediaAssemblyExportTicket v;v.cancel();int late{};v.observeCancellation([&]{++late;});check(late==1,"Late observer of a cancelled ticket runs immediately");
    v.finish();check(v.progress()==0,"Finish drops progress source");
}
struct Disk {
    std::map<std::string,m::MediaAssemblyFileIdentity>files;std::uint64_t serial{1};bool failMove{};std::vector<std::string>removed;
    m::MediaAssemblyFileIdentity make(){m::MediaAssemblyFileIdentity i;i.volume=7;i.file[0]=std::uint8_t(++serial);i.bytes=serial*10;i.modified=std::int64_t(serial);return i;}
    m::MediaAssemblyFileOperations ops(){return {[this](const std::string&p)->std::optional<m::MediaAssemblyFileIdentity>{auto it=files.find(p);if(it==files.end())return {};return it->second;},
        [this](const std::string&from,const std::string&to,bool replace){if(failMove||!files.contains(from)||(!replace&&files.contains(to)))return false;files[to]=make();files.erase(from);return true;},
        [this](const std::string&p){removed.push_back(p);files.erase(p);}};}
};
void commit(){
    Disk disk;disk.files["C:/m/a.png"]=disk.make();auto doc=std::make_shared<m::MediaAssemblyDocumentInfo>(m::MediaAssemblyDocumentInfo{"doc",false,{100,80},0});doc->path="C:/m/a.png";doc->container="png";doc->identity=disk.files["C:/m/a.png"];
    auto same=[](const std::string&a,const std::string&b){return a==b;};const auto ops=disk.ops();
    m::MediaAssemblyExportRequest r{doc,{},"C:/m/out.png",false,"u1",false};m::MediaAssemblyExportTicket t;
    auto plan=m::mediaAssemblyPrepareExport(r,ops,t,same);check(plan.temporary=="C:/m/.endfield-export-u1.png"&&!plan.existing&&!plan.replacesSource,"Plan for a new destination");
    disk.files[plan.temporary]=disk.make();check(m::mediaAssemblyCommitExport(plan,r,ops,t)=="C:/m/out.png"&&disk.files.contains("C:/m/out.png")&&!disk.files.contains(plan.temporary),"Atomic commit moves the temporary");
    check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,t,same);})==m::MediaAssemblyError::exists,"Existing destination requires explicit overwrite");
    r.overwrite=true;r.temporaryID="u2";plan=m::mediaAssemblyPrepareExport(r,ops,t,same);disk.files[plan.temporary]=disk.make();disk.files["C:/m/out.png"]=disk.make();
    check(failure([&]{(void)m::mediaAssemblyCommitExport(plan,r,ops,t);})==m::MediaAssemblyError::changedOnDisk&&!disk.files.contains(plan.temporary),"Destination changed during export is refused and the temporary is removed");
    r.temporaryID="u3";plan=m::mediaAssemblyPrepareExport(r,ops,t,same);disk.files[plan.temporary]=disk.make();disk.files["C:/m/a.png"]=disk.make();
    check(failure([&]{(void)m::mediaAssemblyCommitExport(plan,r,ops,t);})==m::MediaAssemblyError::changedOnDisk,"Source changed during export is refused");
    check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,t,same);})==m::MediaAssemblyError::changedOnDisk,"Changed source refuses a new export");
    doc->identity=disk.files["C:/m/a.png"];r.destination="C:/m/a.png";r.temporaryID="u4";plan=m::mediaAssemblyPrepareExport(r,ops,t,same);check(plan.replacesSource,"Overwrite original detected");
    r.destination="C:/m/a.jpg";check(!m::mediaAssemblyPrepareExport(r,ops,t,same).replacesSource,"Different name is not the original");
    disk.files["C:/m/a.jpg"]=disk.make();r.destination="C:/m/a.png";doc->container="jpeg";check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,t,same);})==m::MediaAssemblyError::unsupportedExport,"Overwrite original keeps its container type");
    doc->container="png";r.destination="C:/m/c.mov";check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,t,same);})==m::MediaAssemblyError::unsupportedExport,"Unsupported extension");
    r.destination="C:/m/d.png";r.temporaryID="u5";plan=m::mediaAssemblyPrepareExport(r,ops,t,same);disk.files[plan.temporary]=disk.make();disk.failMove=true;
    check(failure([&]{(void)m::mediaAssemblyCommitExport(plan,r,ops,t);})==m::MediaAssemblyError::exportFailed&&!disk.files.contains(plan.temporary)&&!disk.files.contains("C:/m/d.png"),"Failed rename preserves original and removes temporary");
    disk.failMove=false;m::MediaAssemblyExportTicket cancelled;cancelled.cancel();check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,cancelled,same);})==m::MediaAssemblyError::cancelled,"Cancelled before work");
    r.temporaryID="u6";plan=m::mediaAssemblyPrepareExport(r,ops,t,same);disk.files[plan.temporary]=disk.make();m::MediaAssemblyExportTicket racing;
    check(failure([&]{(void)m::mediaAssemblyCommitExport(plan,r,ops,racing,[&]{racing.cancel();});})==m::MediaAssemblyError::cancelled&&!disk.files.contains("C:/m/d.png"),"Cancel before the commit lock wins");
    auto bad=r;bad.adjustments.exposure=9;check(failure([&]{(void)m::mediaAssemblyPrepareExport(bad,ops,t,same);})==m::MediaAssemblyError::invalidAdjustment,"Invalid edits");
    disk.files.erase("C:/m/a.png");check(failure([&]{(void)m::mediaAssemblyPrepareExport(r,ops,t,same);})==m::MediaAssemblyError::unavailable,"Missing source is unavailable");
}

struct Executor {
    struct Job {std::function<void()>work;std::function<void(std::exception_ptr)>done;};
    std::deque<Job>jobs;std::size_t capacity{32};
    m::MediaAssemblySubmit submit(){return [this](std::function<void()>w,std::function<void(std::exception_ptr)>d){if(jobs.size()>=capacity)return false;jobs.push_back({std::move(w),std::move(d)});return true;};}
    bool step(){if(jobs.empty())return false;auto j=std::move(jobs.front());jobs.pop_front();std::exception_ptr e;try{j.work();}catch(...){e=std::current_exception();}j.done(e);return true;}
    void run(){while(step()){}}
};
struct Engine final:m::MediaAssemblyEngine {
    Disk&disk;int opens{},previews{},clears{},exports{};std::optional<m::MediaAssemblyError>openError,exportError;double lastTime{-1};std::string lastAdjust;
    explicit Engine(Disk&d):disk(d){}
    m::MediaAssemblyDocumentInfo open(const std::string&path)override{++opens;if(openError)throw m::MediaAssemblyFailure(*openError);
        m::MediaAssemblyDocumentInfo d{"",path.ends_with(".mp4"),{1920,1080},path.ends_with(".mp4")?60.:0.};d.path=path;d.container=path.ends_with(".mp4")?"video":"png";d.identity=disk.files.at(path);d.kind=d.video?m::MediaAssemblyKind::video:m::MediaAssemblyKind::image;return d;}
    m::MediaAssemblyPreviewImage preview(const m::MediaAssemblyDocumentInfo&,const m::MediaAssemblyAdjustments&a,double t)override{++previews;lastTime=t;lastAdjust=std::to_string(a.brightness);return {4,2,std::vector<std::uint8_t>(32,255)};}
    std::string exportMedia(const m::MediaAssemblyExportRequest&r,m::MediaAssemblyExportTicket&t)override{
        ++exports;if(exportError)throw m::MediaAssemblyFailure(*exportError);const auto ops=disk.ops();auto plan=m::mediaAssemblyPrepareExport(r,ops,t,[](const std::string&a,const std::string&b){return a==b;});
        t.setProgress(.25);disk.files[plan.temporary]=disk.make();t.setProgress(.7);return m::mediaAssemblyCommitExport(plan,r,ops,t);}
    void clearCaches()noexcept override{++clears;}
    bool heicEncoder()const override{return false;}
    bool samePath(const std::string&a,const std::string&b)const override{return a==b;}
};
void controller(){
    Disk disk;disk.files["C:/m/a.png"]=disk.make();disk.files["C:/m/b.mp4"]=disk.make();Executor q;auto engine=std::make_shared<Engine>(disk);
    std::vector<std::string>events;int changes{};int ids{};
    m::MediaAssemblyController ctl(engine,q.submit(),{[&]{++changes;},[&](std::string_view e){events.emplace_back(e);},[&]{return "doc-"+std::to_string(++ids);}});
    std::shared_ptr<int>lease=std::make_shared<int>(1);std::weak_ptr<int>watch=lease;
    check(ctl.importPath("C:/m/a.png",std::move(lease),true,1)&&ctl.session().busy(),"Import starts on the utility worker");q.run();
    check(watch.expired(),"Shelf lease is released after open");check(ctl.session().document()&&ctl.session().document()->id=="doc-1"&&events==std::vector<std::string>{"imported"},"Imported once with an owner identity");
    check(engine->previews==0,"Inactive editor never decodes");ctl.setActive(true,1);q.run();check(engine->previews==1&&ctl.previewImage()&&ctl.previewImage()->width==4,"Activation decodes one bounded preview");
    for(int i=1;i<=50;++i){auto a=ctl.session().adjustments();a.brightness=i/100.;ctl.update(a,{},1+i*.01);}
    check(q.jobs.size()==1,"Rapid edits keep one running job");q.run();check(engine->previews==3&&engine->lastAdjust==std::to_string(.5),"Then exactly one latest pending preview");
    check(ctl.nextWakeTime()&&std::abs(*ctl.nextWakeTime()-1.9)<1e-9,"Edited event debounced 0.4 s after the last edit");check(!ctl.deadline(1.8)&&events.size()==1,"Not before the deadline");
    check(ctl.deadline(1.9)&&events.back()=="edited"&&!ctl.nextWakeTime(),"Edited recorded once at the deadline");
    auto a=ctl.session().adjustments();a.contrast=1.5;ctl.update(a,{},2);ctl.setActive(false,2.1);check(events.back()=="edited"&&events.size()==3&&!ctl.previewImage()&&engine->clears>=1,"Hide flushes edited and releases bounded preview/caches");
    ctl.setActive(true,3);q.run();
    // Invalid edits report the source error and keep the previous edit.
    auto invalid=ctl.session().adjustments();invalid.levelsBlack=.5;invalid.levelsWhite=.49;check(!ctl.update(invalid,{},3)&&ctl.session().error()==m::MediaAssemblyError::invalidAdjustment,"Invalid edit is reported");
    // Export: stale panel refused, progress polled, success event.
    check(!ctl.exportTo("C:/m/x.png",false,"doc-0",4),"Old document identity cannot export");
    a=ctl.session().adjustments();a.saturation=.5;ctl.update(a,{},4);
    check(ctl.exportTo("C:/m/x.png",false,"doc-1",4)&&ctl.session().exporting()&&events.back()=="edited","Export flushes edited and starts");
    check(!ctl.closeDocument(4)&&!ctl.importPath("C:/m/b.mp4",nullptr,true,4),"Close/open refused while exporting");
    check(ctl.nextWakeTime()&&std::abs(*ctl.nextWakeTime()-4.25)<1e-9,"Progress is polled at 0.25 s only while exporting");
    q.run();check(!ctl.session().exporting()&&ctl.session().progress()==1&&events.back()=="exported"&&ctl.lastExport()=="C:/m/x.png"&&!ctl.nextWakeTime(),"Exported");
    // Overwrite the original: re-import without a second "imported".
    const auto importsBefore=std::count(events.begin(),events.end(),"imported");
    check(ctl.exportTo("C:/m/a.png",true,"doc-1",5),"Overwrite original starts");q.run();
    const auto reloaded=ctl.session().document()->id;
    check(std::count(events.begin(),events.end(),"imported")==importsBefore&&reloaded!="doc-1"&&ctl.session().document()->identity==disk.files["C:/m/a.png"],"Overwrite reloads the new file identity silently");
    // Failure keeps the original and reports the error.
    engine->exportError=m::MediaAssemblyError::exportFailed;check(ctl.exportTo("C:/m/y.png",false,reloaded,6),"Export");q.run();
    check(ctl.session().error()==m::MediaAssemblyError::exportFailed&&ctl.session().progress()==0,"Failure reported, progress cleared");engine->exportError.reset();
    // Cancel while running.
    check(ctl.exportTo("C:/m/z.png",false,reloaded,7),"Export again");ctl.cancelExport();q.run();check(ctl.session().error()==m::MediaAssemblyError::cancelled&&!disk.files.contains("C:/m/z.png"),"Cancelled export leaves no output");
    // Close clears caches immediately and again behind queued work.
    const auto clears=engine->clears;check(ctl.closeDocument(8)&&engine->clears==clears+1&&q.jobs.size()==1,"Close purges now and queues one trailing purge");q.run();check(engine->clears==clears+2&&!ctl.session().document(),"Trailing purge ran");
    // Unsupported open reports the source error.
    engine->openError=m::MediaAssemblyError::unsupported;ctl.importPath("C:/m/a.png",nullptr,true,9);q.run();check(ctl.session().error()==m::MediaAssemblyError::unsupported&&!ctl.session().document(),"Unsupported open");engine->openError.reset();
    // Video: preview time follows seek/trim; executor saturation retries.
    ctl.importPath("C:/m/b.mp4",nullptr,true,10);q.run();check(ctl.session().document()->video,"Movie opened");
    // A separate export executor keeps long exports off the shared queue.
    {Executor exports;m::MediaAssemblyController second(engine,q.submit(),{{},{},[&]{return "x-"+std::to_string(++ids);}},exports.submit());second.importPath("C:/m/a.png",nullptr,false,20);q.run();
        check(second.exportTo("C:/m/e2.png",false,second.session().document()->id,20)&&exports.jobs.size()==1&&q.jobs.empty(),"Export uses the dedicated executor");exports.run();check(disk.files.contains("C:/m/e2.png"),"Dedicated export committed");}
    q.capacity=0;check(ctl.seek(12,10)&&ctl.session().pendingCount()==1,"Full executor keeps the latest request pending");q.capacity=32;ctl.deadline(10.5);q.run();check(engine->lastTime==12,"Pending preview retried on the next owner event");
    a=ctl.session().adjustments();a.trimStart=5;a.trimEnd=20;check(ctl.update(a,20,11)&&std::abs(ctl.session().currentTime()-19.999)<1e-9,"Trim scrubs to the bounded end");q.run();check(std::abs(engine->lastTime-19.999)<1e-9,"Preview decodes the scrubbed frame");
}

// Host GPU previews: deferred sources instead of CPU pixels, same queue.
void deferred(){
    struct Deferring final:m::MediaAssemblyEngine {
        int previews{},sources{};std::shared_ptr<const m::MediaAssemblyBitmap>bitmap=std::make_shared<m::MediaAssemblyBitmap>(m::MediaAssemblyBitmap{8,6,std::vector<std::uint8_t>(8*6*4,90)});
        m::MediaAssemblyDocumentInfo open(const std::string&path)override{m::MediaAssemblyDocumentInfo d{"",false,{8,6},0};d.path=path;d.container="png";d.identity={1,{2,3},4,5};return d;}
        m::MediaAssemblyPreviewImage preview(const m::MediaAssemblyDocumentInfo&,const m::MediaAssemblyAdjustments&,double)override{++previews;return {8,6,std::vector<std::uint8_t>(8*6*4,255)};}
        m::MediaAssemblyPreviewImage previewSource(const m::MediaAssemblyDocumentInfo&,const m::MediaAssemblyAdjustments&a,double)override{++sources;m::MediaAssemblyPreviewImage out{6,8,{}};out.source=bitmap;out.edits=a;return out;}
        std::string exportMedia(const m::MediaAssemblyExportRequest&,m::MediaAssemblyExportTicket&)override{return {};}
        void clearCaches()noexcept override{}bool heicEncoder()const override{return false;}bool samePath(const std::string&a,const std::string&b)const override{return a==b;}
    };
    Executor q;auto engine=std::make_shared<Deferring>();int ids{};
    m::MediaAssemblyController ctl(engine,q.submit(),{{},{},[&]{return "d-"+std::to_string(++ids);}});
    check(!ctl.deferredPreviews(),"CPU previews by default");ctl.setDeferredPreviews(true,1);check(ctl.deferredPreviews()&&q.jobs.empty(),"Mode change without a document requests nothing");
    ctl.importPath("C:/m/a.png",nullptr,true,1);ctl.setActive(true,1);q.run();
    check(engine->sources==1&&engine->previews==0&&ctl.previewImage()&&ctl.previewImage()->deferred()&&ctl.previewImage()->source==engine->bitmap,"Deferred preview carries the shared bounded source");
    auto a=ctl.session().adjustments();a.rotationQuarterTurns=1;a.stickers.push_back({"s",m::MediaAssemblyStickerKind::sticker1,.5,.5,.2,0});ctl.update(a,{},2);q.run();
    check(engine->sources==2&&ctl.previewImage()->edits&&ctl.previewImage()->edits->rotationQuarterTurns==1&&ctl.previewImage()->edits->stickers.empty(),"Each edit requests one deferred preview with the edits, stickers excluded");
    ctl.setDeferredPreviews(false,3);check(q.jobs.size()==1,"Leaving GPU previews re-requests the shown preview");q.run();
    check(engine->previews==1&&!ctl.previewImage()->deferred()&&!ctl.previewImage()->straightRGBA.empty(),"CPU preview replaces the deferred one");
    ctl.setDeferredPreviews(false,4);check(q.jobs.empty(),"Same mode is a no-op");
    m::MediaAssemblyPreviewImage none;check(!none.deferred(),"Empty images are not deferred");
}
void keys(){
    m::MediaAssemblyPresentation p;m::MediaAssemblyAdjustments a;m::MediaAssemblyDocumentInfo d{"k",false,{1600,900},0};m::MediaAssemblyView v{&d,&a,{},false,false,false,0};p.synchronize(v);
    auto apply=[&](m::MediaAssemblyRequest r){if(r.adjustments)a=*r.adjustments;v={&d,&a,{},false,false,false,v.currentTime};};
    check(!p.key(m::MediaAssemblyKey::space,{},v).consumed,"Space ignored for pictures");check(!p.key(m::MediaAssemblyKey::escape,{},v).consumed&&!p.escapeUnwinds(v),"Escape with nothing to unwind reaches the HUD");
    apply(p.perform("stickers",v));apply(p.perform("sticker:sticker_1",v,"s1"));check(a.stickers.size()==1&&p.selectedSticker()=="s1","Sticker selected");
    apply(p.key(m::MediaAssemblyKey::right,{},v));check(std::abs(a.stickers[0].x-.51)<1e-12,"Arrow nudges 0.01");
    apply(p.key(m::MediaAssemblyKey::up,{.shift=true},v));check(std::abs(a.stickers[0].y-.45)<1e-12,"Shift arrow nudges 0.05");
    apply(p.key(m::MediaAssemblyKey::left,{.alt=true},v));check(a.stickers[0].rotation==-5,"Alt+Left rotates -5");
    apply(p.key(m::MediaAssemblyKey::up,{.alt=true},v));check(std::abs(a.stickers[0].size-.21)<1e-12,"Alt+Up grows");
    check(p.escapeUnwinds(v),"Selection unwinds before the HUD");auto r=p.key(m::MediaAssemblyKey::escape,{},v);check(r.consumed&&p.selectedSticker().empty(),"Escape deselects");
    apply(p.pointerDown({220,198},v,c::Language::english,"unused"));p.pointerUp(v);
    const auto zoom=p.viewport().zoom();check(p.key(m::MediaAssemblyKey::plus,{},v).consumed&&p.viewport().zoom()>zoom,"Plus zooms in");check(p.key(m::MediaAssemblyKey::minus,{},v).consumed&&p.viewport().zoom()==zoom,"Minus zooms out");
    apply(p.perform("tools",v));apply(p.perform("adjust",v));check(p.tool().has_value(),"Tool opened");r=p.key(m::MediaAssemblyKey::escape,{},v);check(r.consumed&&!p.tool()&&p.drawer()==m::MediaAssemblyDrawer::tools,"Escape backs out of the tool");
    // Sticker selected: Delete removes it.
    apply(p.perform("stickers",v));apply(p.perform("sticker:sticker_2",v,"s2"));apply(p.key(m::MediaAssemblyKey::deleteBackward,{},v));check(a.stickers.size()==1&&p.selectedSticker().empty(),"Backspace deletes the selected sticker");
    m::MediaAssemblyDocumentInfo movie{"mv",true,{1920,1080},60};m::MediaAssemblyAdjustments ma;m::MediaAssemblyView mv{&movie,&ma,{},false,false,false,20};p.synchronize(mv);
    r=p.key(m::MediaAssemblyKey::space,{},mv);check(r.consumed&&r.command==m::MediaAssemblyCommand::play,"Space toggles video playback");
    check(!p.key(m::MediaAssemblyKey::space,{.control=true},mv).consumed,"Modified space is not playback");
    r=p.key(m::MediaAssemblyKey::left,{},mv);check(r.command==m::MediaAssemblyCommand::seek&&r.time==15,"Left seeks back 5 s");r=p.key(m::MediaAssemblyKey::right,{},mv);check(r.time==25,"Right seeks forward 5 s");
    check(!p.key(m::MediaAssemblyKey::right,{.shift=true},mv).consumed,"Shift arrow without a sticker is not a seek");
}
void accessibility(){
    m::MediaAssemblyPresentation p;m::MediaAssemblyAdjustments a;m::MediaAssemblyDocumentInfo d{"ax",true,{1920,1080},60};m::MediaAssemblyView v{&d,&a,{},false,false,false,12};p.synchronize(v);
    auto apply=[&](m::MediaAssemblyRequest r){if(r.adjustments)a=*r.adjustments;};
    auto find=[&](std::string_view id){const auto all=p.accessibility(v,c::Language::english,false);for(const auto&e:all)if(e.id==id)return e;throw std::runtime_error("Missing accessible element");};
    check(find("closeMedia").name=="Close"&&find("open").role==m::MediaAssemblyAccessibleRole::button,"Projected buttons, Close for x");
    const auto seek=find("seek");check(seek.name=="Playback position"&&seek.step==5&&seek.maximum==59.999&&seek.value==12,"Playback position slider");
    auto r=p.setAccessibleValue("seek",100,v);check(r.command==m::MediaAssemblyCommand::seek&&r.time==59.999,"Seek value clamps to the range");
    check(p.accessibility(v,c::Language::english,true).empty(),"Menus hide the projected controls");
    apply(p.perform("tools",v));apply(p.perform("trim",v));const auto start=find("trimStart");check(start.maximum==59.95&&find("trimEnd").minimum==.05,"Trim sliders");
    r=p.setAccessibleValue("trimStart",10,v);check(r.command==m::MediaAssemblyCommand::trim&&r.adjustments&&r.adjustments->trimStart==10,"Trim start via AX");
    m::MediaAssemblyDocumentInfo photo{"ph",false,{1600,900},0};m::MediaAssemblyAdjustments pa;m::MediaAssemblyView pv{&photo,&pa,{},false,false,false,0};p.synchronize(pv);
    auto papply=[&](m::MediaAssemblyRequest q){if(q.adjustments)pa=*q.adjustments;};
    papply(p.perform("stickers",pv));papply(p.perform("sticker:sticker_1",pv,"s1"));
    const auto all=p.accessibility(pv,c::Language::simplifiedChinese,false);check(std::count_if(all.begin(),all.end(),[](const auto&e){return e.id.starts_with("sticker")&&e.role==m::MediaAssemblyAccessibleRole::slider;})==4,"Four sticker sliders");
    papply(p.setAccessibleValue("stickerRotation",45,pv));check(pa.stickers[0].rotation==45,"Sticker rotation via AX");
    papply(p.perform("tools",pv));papply(p.perform("crop",pv));papply(p.setAccessibleValue("cropEdge2",.6,pv));check(std::abs(pa.crop.width-.6)<1e-12,"Crop X2 edge");
    papply(p.perform("toolBack",pv));papply(p.perform("adjust",pv));const auto bright=p.accessibility(pv,c::Language::english,false);
    check(std::any_of(bright.begin(),bright.end(),[](const auto&e){return e.id=="brightness"&&e.name=="Brightness";}),"Inline parameter sliders");papply(p.setAccessibleValue("brightness",.333,pv));check(std::abs(pa.brightness-.33)<1e-12,"Parameter value rounds to its step");
}
void menus(){
    m::MediaAssemblyDocumentInfo png{"p",false,{10,10},0};png.kind=m::MediaAssemblyKind::image;
    auto e=m::mediaAssemblyExportMenu(png,c::Language::english);check(e.heading=="Export"&&e.items.size()==3&&e.size.x==370&&e.size.y==104,"Export menu: close, Save As, Overwrite");
    check(e.items[0].id=="close"&&e.items[0].rect==c::Rect{339,8,23,23}&&e.items[2].rect==c::Rect{8,69,354,26},"Source item geometry");
    m::MediaAssemblyDocumentInfo gif{"g",false,{10,10},0};gif.kind=m::MediaAssemblyKind::gif;gif.frameCount=8;
    e=m::mediaAssemblyExportMenu(gif,c::Language::simplifiedChinese);check(e.heading=="GIF · 导出首帧"&&e.items.size()==2&&e.size.y==75,"Animated GIF exports first frame, no overwrite");
    gif.frameCount=1;check(m::mediaAssemblyExportMenu(gif,c::Language::english).heading=="Export"&&m::mediaAssemblyExportMenu(gif,c::Language::english).items.size()==2,"Still GIF has no overwrite either");
    const auto o=m::mediaAssemblyOverwriteMenu(c::Language::english);check(o.heading=="Replace the original file?"&&o.items[1].id=="cancel"&&o.items[2].id=="confirm","Overwrite confirmation");
    check(o.actionAt({20,50})=="cancel"&&o.actionAt({350,15})=="close"&&!o.actionAt({5,5}),"Menu hit testing");
    // Placement under the trigger, clamped; flips above near the bottom.
    check(m::mediaAssemblyMenuOrigin({350,7,78,28},{208,75})==c::Point{224,41},"Open chooser under its trigger");
    check(m::mediaAssemblyMenuOrigin({350,371,78,28},{370,104})==c::Point{62,259},"Export menu flips above its trigger");
    check(m::mediaAssemblyMenuOrigin({2,7,78,28},{370,75})==c::Point{8,41},"Left clamp");
    check(m::mediaAssemblyMenuOrigin({350,7,78,28},{340,260})==c::Point{92,41},"Shelf picker under Open");
    std::vector<m::MediaAssemblyAction>none;check(m::mediaAssemblyMenuAnchor(none,"export")==c::Rect{12,331,66,28},"Source fallback anchor");
    check(m::MediaAssemblyMenuMotion::opacity(true,.08,false)==float(.5)&&m::MediaAssemblyMenuMotion::opacity(false,.16,false)==0&&m::MediaAssemblyMenuMotion::opacity(true,0,true)==1,"0.16 s fade");
    const auto art=m::mediaAssemblyMenuArtwork(o,{});check(art["children"].array().size()==2+3*4+1,"Retained menu artwork leaves");
}
}
int main(){
    try{errors();documents();formats();ticket();commit();controller();deferred();keys();accessibility();menus();std::cout<<"PASS "<<checks<<" Media Assembly controller checks\n";return 0;}
    catch(const std::exception&e){std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}
}
