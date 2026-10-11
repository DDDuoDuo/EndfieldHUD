#pragma once
#include "modules/reader_state.hpp"
#include "modules/reader_text.hpp"
#include "native/font_resources.hpp"
namespace endfield::native {
struct NativeReaderInfo {
    std::string title;std::size_t sections{};
    std::string requestedFont,selectedFont;
    // Windows.Data.Pdf exposes pages/password state, but no document title:
    // modules/reader_pdf_title reads the trailer /Info /Title itself, as the
    // Mac ReaderDocument takes PDFKit's title attribute. True when this PDF's
    // title came from that metadata, false for the file-name fallback. Only
    // new imports read it; an already saved library title never changes.
    bool pdfMetadataTitleAvailable{};
};
// pdfDevices: owned Direct3D devices created for PDF pages (one per document
// lifetime between release() calls, plus a recreation after a lost device);
// pdfDeviceReady: one is held now (none after release()).
struct NativeReaderStats {std::uint64_t textLayouts{},pagesRendered{},cacheHits{},pdfRenders{},imageDecodes{},imageDecodeCacheHits{},detailMetadataCacheHits{},pdfDevices{};std::size_t cachedPages{},cachedImageBytes{};bool pdfDeviceReady{};};
// Existing borrowed FIFO UtilityExecutor only, used serially by its jobs on
// whichever worker runs them (the executor may replace an idle worker); the
// document keeps the process MTA alive while open. No worker/window/service,
// timer, renderer, font loader or referenced-file writes. One read-only opened
// handle and at most one 2048×2048 decoded EPUB image are retained for a document; Windows references only, explicit relink
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
    NativeReaderInfo info()const; // executor job only; title is source prefix200 graphemes
    NativeReaderStats stats()const; // executor job only; no filesystem/raster operation
    std::vector<modules::ReaderPage>render(std::optional<modules::ReaderLocation>,std::optional<double>,
        const modules::ReaderPreferences&,core::Point,bool,const modules::ReaderCancel&)override;
    modules::ReaderPage detail(modules::ReaderLocation,const modules::ReaderPreferences&,
        core::Point,bool,modules::ReaderImageView,const modules::ReaderCancel&)override;
    void release()override;
private:struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
