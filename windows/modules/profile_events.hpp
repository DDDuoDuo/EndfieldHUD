#pragma once
#include <optional>
#include <string_view>
#include <utility>

namespace endfield::modules {
// SystemEventRecorder.receiveProfileCrop. Feed committed profile snapshots
// (ProfileState::profile(), never slider drafts or pointer motion). The first
// snapshot is a baseline; a later change returns the profileCropChanged
// metadata target ("background", "thumbnail" or "both"). Only which crop
// changed is recorded: zoom values and user content are never logged.
class ProfileCropRecorder final {
public:
    std::optional<std::string_view>receive(double backgroundZoom,double thumbnailZoom)noexcept;
private:
    std::optional<std::pair<double,double>>crop_;
};
}
