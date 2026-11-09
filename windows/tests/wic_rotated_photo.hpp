#pragma once
// A synthetic phone photo for the image decoders' orientation checks: 3000x2000
// pixels, the left half red and the right half blue, stored sideways with EXIF
// orientation 6, so upright it is 2000x3000 with red on top and blue below.
// JPEG carries the orientation in its APP1 Exif block, PNG in an eXIf chunk.
// Written by the system encoder; tests only.
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <cstdint>
#include <stdexcept>
#include <vector>

namespace endfield::tests {
inline constexpr unsigned rotatedPhotoWidth = 3000, rotatedPhotoHeight = 2000;
inline std::vector<std::uint8_t> rotatedPhoto(bool png = false) {
    using Microsoft::WRL::ComPtr;
    const auto ok = [](HRESULT value, const char* why) { if (FAILED(value)) throw std::runtime_error(why); };
    constexpr UINT w = rotatedPhotoWidth, h = rotatedPhotoHeight;
    ComPtr<IWICImagingFactory> factory;
    ok(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "WIC factory for the rotated photo");
    ComPtr<IStream> stream;
    ok(CreateStreamOnHGlobal(nullptr, TRUE, &stream), "Rotated photo stream");
    ComPtr<IWICBitmapEncoder> encoder;
    ok(factory->CreateEncoder(png ? GUID_ContainerFormatPng : GUID_ContainerFormatJpeg, nullptr, &encoder), "Rotated photo encoder");
    ok(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "Rotated photo encoder stream");
    ComPtr<IWICBitmapFrameEncode> frame;
    ok(encoder->CreateNewFrame(&frame, nullptr), "Rotated photo frame");
    ok(frame->Initialize(nullptr), "Rotated photo frame options");
    ok(frame->SetSize(w, h), "Rotated photo size");
    WICPixelFormatGUID format = GUID_WICPixelFormat24bppBGR;
    ok(frame->SetPixelFormat(&format), "Rotated photo pixel format");
    if (!png) {
        ComPtr<IWICMetadataQueryWriter> metadata;
        ok(frame->GetMetadataQueryWriter(&metadata), "Rotated photo metadata");
        PROPVARIANT value{};
        value.vt = VT_UI2; value.uiVal = 6;
        ok(metadata->SetMetadataByName(L"/app1/ifd/{ushort=274}", &value), "Rotated photo EXIF orientation");
    }
    std::vector<std::uint8_t> row(std::size_t(w) * 3);
    for (UINT x = 0; x < w; ++x) { row[x * 3] = x < w / 2 ? 0 : 255; row[x * 3 + 2] = x < w / 2 ? 255 : 0; }
    for (UINT y = 0; y < h; ++y) ok(frame->WritePixels(1, w * 3, UINT(row.size()), row.data()), "Rotated photo rows");
    ok(frame->Commit(), "Rotated photo frame commit");
    ok(encoder->Commit(), "Rotated photo commit");
    STATSTG stat{};
    ok(stream->Stat(&stat, STATFLAG_NONAME), "Rotated photo size");
    std::vector<std::uint8_t> out(static_cast<std::size_t>(stat.cbSize.QuadPart));
    LARGE_INTEGER zero{};
    ok(stream->Seek(zero, STREAM_SEEK_SET, nullptr), "Rotated photo rewind");
    ULONG read{};
    ok(stream->Read(out.data(), ULONG(out.size()), &read), "Rotated photo bytes");
    if (read != out.size()) throw std::runtime_error("Whole rotated photo");
    if (!png) return out;
    // An eXIf chunk (a little-endian TIFF IFD with Orientation = 6) after IHDR.
    const std::vector<std::uint8_t> tiff{'I', 'I', 42, 0, 8, 0, 0, 0, 1, 0, 0x12, 0x01, 3, 0, 1, 0, 0, 0, 6, 0, 0, 0, 0, 0, 0, 0};
    std::vector<std::uint8_t> chunk;
    const auto big = [&](std::uint32_t v) { for (int k = 3; k >= 0; --k) chunk.push_back(static_cast<std::uint8_t>(v >> (8 * k))); };
    big(static_cast<std::uint32_t>(tiff.size()));
    chunk.insert(chunk.end(), {'e', 'X', 'I', 'f'});
    chunk.insert(chunk.end(), tiff.begin(), tiff.end());
    std::uint32_t crc = 0xffffffffu;
    for (std::size_t n = 4; n < chunk.size(); ++n) { crc ^= chunk[n]; for (unsigned k = 0; k < 8; ++k) crc = (crc & 1) ? 0xedb88320u ^ (crc >> 1) : crc >> 1; }
    big(crc ^ 0xffffffffu);
    constexpr std::size_t afterHeader = 8 + 4 + 4 + 13 + 4; // signature, IHDR length, type, data, CRC
    out.insert(out.begin() + afterHeader, chunk.begin(), chunk.end());
    return out;
}
// The upright photo has red over blue: sample the middle column at 30% and
// 70% of its height in a straight or premultiplied 8-bit image whose red and
// blue bytes sit at `red` and `blue` within each 4-byte pixel.
inline bool rotatedPhotoUpright(const std::uint8_t* pixels, unsigned width, unsigned height, unsigned red, unsigned blue) {
    const auto at = [&](double y) { return pixels + (std::size_t(height * y) * width + width / 2) * 4; };
    const auto* top = at(.3);
    const auto* bottom = at(.7);
    return top[red] > top[blue] + 100 && bottom[blue] > bottom[red] + 100;
}
} // namespace endfield::tests
#endif
