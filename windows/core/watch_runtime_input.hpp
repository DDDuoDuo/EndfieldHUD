#pragma once
#include "core/source_button_motion.hpp"
#include "core/source_camera.hpp"
#include "core/source_selectable_color.hpp"
#include "core/source_watch_frame.hpp"
#include <filesystem>
#include <functional>
#include <memory>

namespace endfield::core::source {
// Compact build output, independent of the oracle Package. Sections are hashed,
// parsed and consumed one at a time. Returned model addresses remain stable for
// the session lifetime; this owner is deliberately nonmovable.
class WatchRuntimeInput final {
public:
    using Stage = std::function<void(std::string_view)>;
    explicit WatchRuntimeInput(std::filesystem::path explicitRoot, Stage stage = {});
    ~WatchRuntimeInput();
    WatchRuntimeInput(const WatchRuntimeInput&) = delete;
    WatchRuntimeInput& operator=(const WatchRuntimeInput&) = delete;
    const SceneDefinition& scene() const noexcept;
    const MountedLayoutDocument& document() const noexcept;
    const Library& library() const noexcept;
    const SourceCamera& camera() const noexcept;
    const SourceWatchFrameResources& resources() const noexcept;
    const SourceDesktopFrameSettings& desktopSettings() const noexcept;
    const std::optional<DesktopHoverProfile>& profile() const noexcept;
    const std::vector<AnimatorBinding>& animators() const noexcept;
    const Json& controllerTransitions() const noexcept;
    const Json& nativeButtons() const noexcept;
    const Json& nativeTop() const noexcept;
    const Json& nativeBottom() const noexcept;
    const Json& chrome() const noexcept;
    const std::filesystem::path& assetRoot() const noexcept;
    // Exact originating shell-packet manifest digest; retained after setup JSON
    // release so compiled GPU resources can be checked against the same source.
    const std::string& sourceManifestSHA256() const noexcept;
    // Call only after the session/native bindings/content/chrome have copied
    // their setup inputs. Typed models and already verified raster files remain.
    void releaseSetupJSON() noexcept;
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace endfield::core::source
