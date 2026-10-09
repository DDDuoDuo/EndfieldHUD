#pragma once
#include "modules/reader_state.hpp"
#include "modules/reader_text.hpp"
#include "native/font_resources.hpp"
namespace endfield::native {
struct NativeReaderInfo {
    std::string title;std::size_t sections{};
    std::string requestedFont,selectedFont;
    // Windows.Data.Pdf exposes pages/password state, but no document title.
    // Filename title is explicit until a verified stream-based metadata reader
    // is available; this does not affect an already saved library title.
    bool pdfMetadataTitleAvailable{};
};
struct NativeReaderStats {std::uint64_t textLayouts{},pagesRendered{},cacheHits{},pdfRenders{},imageDecodes{};std::size_t cachedPages{};};
// Existing borrowed FIFO/MTA UtilityExecutor only. No worker/window/service,
// timer, renderer, font loader or referenced-file writes. One read-only opened
// handle is retained for a document; Windows references only, explicit relink
// required for opaque Mac bookmarks. Accepted jobs must drain before destruction.
class NativeReaderDocument final:public modules::ReaderDocumentSource {
public:
    explicit NativeReaderDocument(LayerFontResources);
    ~NativeReaderDocument();
    NativeReaderDocument(const NativeReaderDocument&)=delete;
    NativeReaderDocument&operator=(const NativeReaderDocument&)=delete;
    // Configuration-event snapshot publication is thread-safe. Worker observes
    // it at the next request, without querying the creating-thread rasterizer.
    void setFontResources(LayerFontResources);
    void open(const modules::ReaderBook&,const modules::ReaderCancel&)override;
    NativeReaderInfo info()const; // worker only; title is source prefix200 graphemes
    NativeReaderStats stats()const; // worker only; no filesystem/raster operation
    std::vector<modules::ReaderPage>render(std::optional<modules::ReaderLocation>,std::optional<double>,
        const modules::ReaderPreferences&,core::Point,bool,const modules::ReaderCancel&)override;
    modules::ReaderPage detail(modules::ReaderLocation,const modules::ReaderPreferences&,
        core::Point,bool,modules::ReaderImageView,const modules::ReaderCancel&)override;
    void release()override;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
