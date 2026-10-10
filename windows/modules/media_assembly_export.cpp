#include "modules/media_assembly_export.hpp"
#include <algorithm>
#include <cctype>
#include <cmath>

namespace endfield::modules {
namespace {
[[noreturn]]void fail(MediaAssemblyError e){throw MediaAssemblyFailure(e);}
std::string lower(std::string_view s){std::string out(s);for(auto&c:out)c=char(std::tolower(static_cast<unsigned char>(c)));return out;}
std::size_t separator(std::string_view p){return p.find_last_of("/\\");}
}
MediaAssemblyError mediaAssemblyErrorFrom(std::exception_ptr e,MediaAssemblyError fallback)noexcept{
    if(!e)return fallback;
    try{std::rethrow_exception(e);}
    catch(const MediaAssemblyFailure&f){return f.error();}
    catch(...){return fallback;}
}

bool MediaAssemblyExportTicket::cancelled()const{std::lock_guard g(lock_);return cancelled_;}
bool MediaAssemblyExportTicket::committed()const{std::lock_guard g(lock_);return committed_;}
double MediaAssemblyExportTicket::progress()const{
    std::function<double()>source;double fraction{};{std::lock_guard g(lock_);source=progressSource_;fraction=completed_;}
    const double value=source?source():fraction;return std::isfinite(value)?std::clamp(value,0.,1.):0.;
}
void MediaAssemblyExportTicket::setProgress(double v){std::lock_guard g(lock_);completed_=v;}
void MediaAssemblyExportTicket::observeProgress(std::function<double()>f){std::lock_guard g(lock_);progressSource_=std::move(f);}
void MediaAssemblyExportTicket::cancel(){
    std::function<void()>action;{std::lock_guard g(lock_);if(!committed_)cancelled_=true;if(cancelled_)action=cancelSession_;}
    if(action)action();
}
void MediaAssemblyExportTicket::observeCancellation(std::function<void()>f){
    bool run{};{std::lock_guard g(lock_);cancelSession_=f;run=cancelled_;}if(run&&f)f();
}
void MediaAssemblyExportTicket::finish(){std::lock_guard g(lock_);cancelSession_=nullptr;progressSource_=nullptr;}
void MediaAssemblyExportTicket::commit(const std::function<void()>&operation){
    std::lock_guard g(lock_);if(cancelled_)fail(MediaAssemblyError::cancelled);operation();committed_=true;
}

std::string mediaAssemblyExtension(std::string_view path){
    const auto slash=separator(path);const auto name=slash==std::string_view::npos?path:path.substr(slash+1);
    const auto dot=name.rfind('.');return dot==std::string_view::npos||dot==0?std::string{}:lower(name.substr(dot+1));
}
std::string mediaAssemblyDirectory(std::string_view path){const auto slash=separator(path);return slash==std::string_view::npos?std::string{}:std::string(path.substr(0,slash+1));}
std::string mediaAssemblyTemporaryPath(std::string_view target,std::string_view uuid){
    if(uuid.empty()||uuid.find_first_of("/\\.")!=std::string_view::npos)throw std::invalid_argument("Invalid Media Assembly temporary identity");
    const auto ext=mediaAssemblyExtension(target);return mediaAssemblyDirectory(target)+".endfield-export-"+std::string(uuid)+"."+ext;
}
std::optional<MediaAssemblyExportFormat>mediaAssemblyExportFormat(std::string_view extension,bool video,bool heic){
    const auto e=lower(extension);
    if(video){if(e=="mp4"||e=="m4v")return MediaAssemblyExportFormat{true};return {};}
    if(e=="png")return MediaAssemblyExportFormat{false,MediaAssemblyImageFormat::png};
    if(e=="jpg"||e=="jpeg")return MediaAssemblyExportFormat{false,MediaAssemblyImageFormat::jpeg};
    if(e=="tif"||e=="tiff")return MediaAssemblyExportFormat{false,MediaAssemblyImageFormat::tiff};
    if((e=="heic"||e=="heif")&&heic)return MediaAssemblyExportFormat{false,MediaAssemblyImageFormat::heic};
    return {};
}
std::vector<std::string>mediaAssemblyExportExtensions(const MediaAssemblyDocumentInfo&d,bool heic){
    if(d.video)return {"mp4"};std::vector<std::string>out{"png","jpg","tiff"};if(heic)out.push_back("heic");return out;
}
bool mediaAssemblyOverwriteCompatible(std::string_view container,MediaAssemblyImageFormat f){
    const auto c=lower(container);
    switch(f){case MediaAssemblyImageFormat::png:return c=="png";case MediaAssemblyImageFormat::jpeg:return c=="jpeg";
    case MediaAssemblyImageFormat::tiff:return c=="tiff";case MediaAssemblyImageFormat::heic:return c=="heic"||c=="heif";}
    return false;
}
MediaAssemblyExportPlan mediaAssemblyPrepareExport(const MediaAssemblyExportRequest&r,const MediaAssemblyFileOperations&ops,const MediaAssemblyExportTicket&ticket,const std::function<bool(const std::string&,const std::string&)>&same){
    if(ticket.cancelled())fail(MediaAssemblyError::cancelled);
    if(!r.document||!r.adjustments.valid()||r.destination.empty())fail(MediaAssemblyError::invalidAdjustment);
    const auto&d=*r.document;
    const auto source=ops.identity(d.path);if(!source)fail(MediaAssemblyError::unavailable);
    if(*source!=d.identity)fail(MediaAssemblyError::changedOnDisk);
    MediaAssemblyExportPlan plan;plan.target=r.destination;plan.existing=ops.identity(plan.target);
    if(!r.overwrite&&plan.existing)fail(MediaAssemblyError::exists);
    plan.temporary=mediaAssemblyTemporaryPath(plan.target,r.temporaryID);
    const auto format=mediaAssemblyExportFormat(mediaAssemblyExtension(plan.target),d.video,r.heicEncoder);if(!format)fail(MediaAssemblyError::unsupportedExport);
    plan.format=*format;plan.replacesSource=same&&same(plan.target,d.path);
    if(!d.video&&plan.replacesSource&&!mediaAssemblyOverwriteCompatible(d.container,format->image))fail(MediaAssemblyError::unsupportedExport);
    return plan;
}
std::string mediaAssemblyCommitExport(const MediaAssemblyExportPlan&plan,const MediaAssemblyExportRequest&r,const MediaAssemblyFileOperations&ops,MediaAssemblyExportTicket&ticket,const std::function<void()>&beforeCommit){
    struct Cleanup {const MediaAssemblyFileOperations&ops;const std::string&path;~Cleanup(){try{ops.remove(path);}catch(...){}}}cleanup{ops,plan.temporary};
    if(beforeCommit)beforeCommit();
    ticket.commit([&]{
        const auto source=ops.identity(r.document->path);if(!source||*source!=r.document->identity)fail(MediaAssemblyError::changedOnDisk);
        if(ops.identity(plan.target)!=plan.existing)fail(MediaAssemblyError::changedOnDisk);
        if(!ops.move(plan.temporary,plan.target,r.overwrite))fail(MediaAssemblyError::exportFailed);
    });
    return plan.target;
}
}
