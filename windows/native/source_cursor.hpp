#pragma once
#include <cstdint>
#include <filesystem>
#include <span>

namespace endfield::native {
// Owns an application-local cursor handle. Creation never installs, hides or
// changes the system cursor. OverlayHost borrows handle() while this is alive.
class SourceCursor final {
public:
    SourceCursor()=default;
    ~SourceCursor();
    SourceCursor(SourceCursor&&)noexcept;
    SourceCursor& operator=(SourceCursor&&)noexcept;
    SourceCursor(const SourceCursor&)=delete;
    SourceCursor& operator=(const SourceCursor&)=delete;
    // Exact current Mac artwork and hotspot: 58 backing pixels, (0,0).
    // PNG bytes are hash-checked before WIC decoding; requires caller COM.
    static SourceCursor fromOriginalPNG(const std::filesystem::path&);
    // Premultiplied, top-left BGRA. Used by isolated synthetic native tests.
    static SourceCursor fromPixels(unsigned width,unsigned height,
        unsigned hotspotX,unsigned hotspotY,std::span<const std::uint8_t> bgra);
    void* handle()const noexcept{return handle_;}
private:
    explicit SourceCursor(void* handle):handle_(handle){}
    void* handle_{};
};
} // namespace endfield::native
