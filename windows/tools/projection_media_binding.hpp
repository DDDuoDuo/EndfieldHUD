#pragma once
#include "tools/projection_preview.hpp"
#include "native/media_request_broker.hpp"
#ifdef _WIN32
namespace endfield::tools {
struct ProjectionImportFile {std::string path,displayName;std::shared_ptr<void>accessLease;};
struct ProjectionMediaBindingOptions {
    std::function<native::NotesImageAccess(const modules::ProjectionMediaReference&)>resolve;
    std::function<std::string(HRESULT)>errorText;
};
// One module client of the EXISTING app media broker. The root alone accepts
// notices/samples/deadlines. This adapter performs no file reads, creates no
// codec/player/thread/window, and retains <=16 source media plus retiring draws.
// Import inspection uses the same worker and is all-or-nothing, matching the
// Projection source (unlike Archive's successful-sibling import policy).
// All borrowed owners outlive this binding. On close, hide cancels provider
// activity while poster resources remain until the final panel fades/detaches.
// Detach/release the preview before final releaseResources()/destruction.
class ProjectionMediaBinding final {
public:
    ProjectionMediaBinding(ProjectionPreview&,modules::ProjectionModel&,
        native::NativeMediaRequestBroker&,native::NativeMediaRequestBroker::Client,
        native::Renderer&,ProjectionMediaBindingOptions);
    ~ProjectionMediaBinding();
    bool sync(double); // model/visibility changes; unchanged frames allocate nothing
    bool refresh(double); // after root shared accept/sample, own records only
    bool action(const ProjectionPreviewAction&,double); // play/seek only
    bool beginImport(std::uint64_t,std::span<const ProjectionImportFile>,std::optional<core::Point>,double);
    bool cancelImport();
    bool collectRetired(); // after replacement composition publication
    bool releaseResources(double);
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
