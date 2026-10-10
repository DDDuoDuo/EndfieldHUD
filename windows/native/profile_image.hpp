#pragma once
#include "app/utility_executor.hpp"
#include "modules/profile_artwork.hpp"
#include "modules/profile_state.hpp"
#include <filesystem>
#include <memory>
#include <span>
#include <string>
#ifdef _WIN32
namespace endfield::native {
struct ProfileDecodedImage {
    modules::ProfileImage image; // straight sRGB RGBA8, top-left rows
    int orientation{1};          // EXIF 1...8, still to apply (avatars only)
    unsigned sourceWidth{},sourceHeight{}; // stored pixel size before any reduction
};
// UserProfileStore.importImage on a worker thread (COM initialized per call):
// a regular file of at most 128 MB that WIC can decode. Avatars keep their
// original encoded bytes as <UUID>.image after a 1...32768 px per side, <=128
// megapixel check; backgrounds become an oriented PNG of at most 2048 px as
// <UUID>.png. The file is written atomically in imagesDirectory and its name
// returned. Throws ProfileError(image | imageTooLarge | imageDimensions |
// persistence). The extension never decides the codec.
std::string importProfileImage(const std::filesystem::path& source,modules::ProfileImageKind,const std::filesystem::path& imagesDirectory);
// The same import for encoded bytes already in memory (the account's
// downsampled game avatar, which the source writes to a temporary file and
// imports). At most 128 MB; the codec is chosen from content.
std::string importProfileImageBytes(std::span<const std::uint8_t>encoded,modules::ProfileImageKind,const std::filesystem::path& imagesDirectory);
// UserProfileStore.loadImage: avatars report their EXIF orientation and are
// reduced to at most maximumDimension px for the crop renderer; backgrounds
// are oriented and at most 2048 px. Throws ProfileError(image | imageDimensions).
ProfileDecodedImage decodeProfileImage(const std::filesystem::path& managed,modules::ProfileImageKind,unsigned maximumDimension=4096);

struct ProfileImageCallbacks {
    std::function<void(modules::ProfileImageKind,std::string filename)>imported;
    std::function<void(modules::ProfileImageKind,modules::ProfileFailure,std::string detail)>importFailed;
    std::function<void(const std::string& filename,std::shared_ptr<const ProfileDecodedImage>)>decoded;
    std::function<void(const std::string& filename,modules::ProfileFailure)>decodeFailed;
};
// Owner-thread binding of profile image work to the app's shared utility
// executor: one import at a time, coalesced decodes and a bounded decoded
// cache (two images: the current avatar and background). No thread, timer or
// picker is created here. Callbacks run inside the executor's owner drain.
class ProfileImageService final {
public:
    ProfileImageService(std::filesystem::path imagesDirectory,app::UtilityExecutor&,ProfileImageCallbacks={});
    ~ProfileImageService();
    ProfileImageService(const ProfileImageService&)=delete;
    ProfileImageService&operator=(const ProfileImageService&)=delete;
    bool import(std::filesystem::path source,modules::ProfileImageKind); // false while an import is pending
    void request(const std::string& filename,modules::ProfileImageKind);
    std::shared_ptr<const ProfileDecodedImage>cached(std::string_view filename)const;
    void forget(std::string_view filename);
    // Remove an imported file whose profile commit failed (Mac importImage rollback).
    void discard(const std::string& filename);
    void queueCapacityAvailable();
    bool busy()const noexcept;
private:struct Impl;std::shared_ptr<Impl>impl_;
};
}
#endif
