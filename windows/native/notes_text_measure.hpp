#pragma once
#include "native/layer_raster.hpp"
#include "modules/notes_presentation.hpp"
#include <memory>
#include <string_view>
#include <utility>

namespace endfield::native {
struct NativeNotesTextMeasurement {
    modules::NotesMeasuredText measured;
    LayerPlainTextMetrics font;
    // Binary-search source semantics: end>minimum, origin<maximum. Returns
    // indices, without allocating, scanning text or constructing a bitmap.
    std::pair<std::size_t,std::size_t> visibleLines(double minimum,double maximum)const;
    std::shared_ptr<const modules::NotesMeasuredText> presentationText(
        const std::shared_ptr<const NativeNotesTextMeasurement>&owner)const;
};
struct NativeNotesTextMeasureStats {
    std::size_t entries{},retainedBytes{};
    std::uint64_t measurements{},cacheHits{},analysisLayoutsCreated{},evictions{};
};
// Lazy plain-text line index only; no HWND, renderer, timer, editor or data I/O.
// Shares LayerRasterizer's existing DirectWrite factory/font resolver. Borrowed
// rasterizer outlives this cache and calls occur on its creating thread.
// The caller must increment textRevision whenever text changes. Equal source
// key/revision/width/size/fallback returns the identical shared result immediately.
// A transient whole-paragraph DWrite analysis layout is released once indexed;
// visible line leaves are subsequently reconstructed using the SAME selected
// font/metrics, NOT this COM object. Shared-object IME/long-editor parity is a
// separate viewport adapter and is not established by this measurement API.
class NativeNotesTextMeasurer final {
public:
    static constexpr std::size_t maximumEntries=64,maximumCacheBytes=16*1024*1024;
    // Matches the existing native NotesStore payload bound, NOT a macOS source
    // limit. Imported larger payloads require a separate explicit migration.
    static constexpr std::size_t maximumTextBytes=16*1024*1024;
    // Pathologically narrow large text can create millions of lines. Explicit
    // index-memory rejection preserves caller text and never silently truncates.
    static constexpr std::size_t maximumIndexBytes=64*1024*1024;
    explicit NativeNotesTextMeasurer(LayerRasterizer&);
    ~NativeNotesTextMeasurer();
    NativeNotesTextMeasurer(const NativeNotesTextMeasurer&)=delete;
    NativeNotesTextMeasurer&operator=(const NativeNotesTextMeasurer&)=delete;
    std::shared_ptr<const NativeNotesTextMeasurement> measure(std::string_view sourceID,std::uint64_t textRevision,
        std::string_view text,double width,double fontSize=12,const LayerRasterOptions& options={});
    bool remove(std::string_view sourceID);
    void clear();
    NativeNotesTextMeasureStats stats()const;
private:
    struct Impl;std::unique_ptr<Impl>impl_;
};
} // namespace endfield::native
