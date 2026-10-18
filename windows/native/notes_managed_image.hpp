#pragma once
// Source NotesStore managed images: a pasted image becomes a "<UUID>.png" copy
// under <app root>/Notes/Images, exactly as NotesStore.importImage(data:) does
// (thumbnail at most 1600 pixels on its longest side, EXIF orientation
// applied, PNG encoded, written before the note is committed and removed
// again when that commit fails or the last note using it is deleted).
#include <cstdint>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace endfield::native {
struct NotesManagedImage {std::vector<std::uint8_t> png;unsigned pixelWidth{},pixelHeight{};};
inline constexpr std::size_t notesManagedImageMaximumBytes=128*1024*1024; // source imageTooLarge
inline constexpr unsigned notesManagedImageMaximumPixelSize=1600;           // kCGImageSourceThumbnailMaxPixelSize
#ifdef _WIN32
// WIC decode of image container bytes (PNG, BMP, TIFF, JPEG, ... with the
// installed codecs), color managed to sRGB, oriented, scaled down (never up)
// and PNG encoded. Throws NotesMediaFailure: imageTooLarge above 128 MB or
// 64 MP, decodingFailed for anything WIC cannot read as one image.
NotesManagedImage notesManagedImageFromData(std::span<const std::uint8_t> container);
#endif
// Writes a new "<UUID>.png" atomically (temporary file, flush, rename) under
// <appRoot>/Notes/Images, creating that directory; returns the name. Refuses
// an Images path that is a link. Throws std::runtime_error on failure.
std::string writeNotesManagedImage(const std::filesystem::path& appRoot,std::span<const std::uint8_t> png);
// Deletes one managed image by its validated "<UUID>.png" name; false when the
// name is invalid or the file could not be removed (a missing file is true).
bool removeNotesManagedImage(const std::filesystem::path& appRoot,std::string_view name)noexcept;
} // namespace endfield::native
