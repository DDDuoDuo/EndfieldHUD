#pragma once
// The Mac reads every movie coded BT.709 (both colour tags BT.709, or
// unknown as AVFoundation guesses) in Core Image's CoreMedia 709 colour
// space: NotesMediaPresentation's poster (AVAssetImageGenerator
// .copyCGImage, a kCGColorSpaceCoreMedia709 image) and its AVPlayerLayer
// playback, in Notes, Archive and Projection alike. Measured black-box by the
// Media Assembly area (tools/media_assembly_video_colour_reference.py,
// tests/fixtures/media-assembly-video-colour-source.json): a decoded code v
// reads as linear max((v/255)^1.961, v/255/16) on BT.709 (= sRGB) primaries,
// so a decoded mid grey 128 shows as 139. Windows decoders hand out the
// decoded values (Media Foundation's YCbCr to RGB, no transfer change).
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace endfield::native {
// The sRGB code a decoded code shows as, per channel (the primaries match).
const std::array<std::uint8_t, 256>& notesCoreMedia709Codes() noexcept;
// A movie read that way: its MF_MT_TRANSFER_FUNCTION and MF_MT_VIDEO_PRIMARIES
// (MFVideoTransferFunction / MFVideoPrimaries values) are each BT.709 or unknown.
bool notesCoreMedia709(std::uint32_t transferFunction, std::uint32_t primaries) noexcept;
// Applies the reading to straight RGBA8 pixels in place (alpha unchanged).
void notesApplyCoreMedia709(std::uint8_t* rgba, std::size_t pixels) noexcept;

#ifdef _WIN32
// The same reading as one in-place GPU pass over a frame server's BGRA8
// target (a renderer media target surface, after TransferVideoFrame and
// before commitMediaTexture): the target is copied to a scratch texture, a
// full-target triangle writes the reading into a second scratch and that is
// copied back. Both scratch textures are the target's size and are recreated
// only when it changes; release() drops them. The shaders are compiled once
// per process, on first use. Owner (UI) thread only; throws on failure.
class NotesCoreMedia709Pass final {
public:
    NotesCoreMedia709Pass();
    ~NotesCoreMedia709Pass();
    NotesCoreMedia709Pass(const NotesCoreMedia709Pass&) = delete;
    NotesCoreMedia709Pass& operator=(const NotesCoreMedia709Pass&) = delete;
    void apply(void* dxgiSurface);
    void release() noexcept;
    std::size_t scratchBytes() const noexcept;
    std::uint64_t passes() const noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
#endif
} // namespace endfield::native
