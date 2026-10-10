#pragma once
#include "modules/hypergryph_account_controller.hpp"
#ifdef _WIN32
#include "app/utility_executor.hpp"
#include "native/hypergryph_account_transport.hpp"

// HypergryphAvatarLoader for Windows: public, allowlisted HTTPS avatar images
// only (bbs.hycdn.cn, assets.skland.com, assets.skport.com, static.skport.com,
// web-static.hg-cdn.com); no credential, cookie or account header is sent.
// One transient request (4 MiB cap, 10 s), then a WIC decode/downsample on the
// shared utility queue to a metadata-free PNG of at most 512 px (EXIF
// orientation applied), mirroring CGImageSourceCreateThumbnailAtIndex.
namespace endfield::native {
inline constexpr std::size_t avatarMaximumBytes=4*1024*1024;
inline constexpr unsigned avatarMaximumDimension=8192,avatarThumbnailSize=512;
inline constexpr std::size_t avatarMaximumPixels=16'000'000;
// Thread-agnostic (initializes COM on the calling thread if needed). Throws
// std::invalid_argument for undecodable/oversized images.
std::vector<std::uint8_t> downsampleAvatar(std::span<const std::uint8_t> encoded);
class WinHttpAvatarLoader final:public modules::hypergryph::AccountAvatarLoader {
public:
    WinHttpAvatarLoader(WinHttpAccountTransport&,app::UtilityExecutor&);
    ~WinHttpAvatarLoader() override;
    modules::hypergryph::RequestHandle load(const std::string& url,std::function<void(std::optional<std::vector<std::uint8_t>>)>) override;
private:
    struct Impl;std::shared_ptr<Impl> impl_;
};
}
#endif
