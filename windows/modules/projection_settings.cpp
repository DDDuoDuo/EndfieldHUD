#include "modules/projection_settings.hpp"
#include <algorithm>
#include <cmath>

namespace endfield::modules {
// init(defaults:) (:31-41): a missing field keeps its default; the store
// already masked the colour and clamped the width.
ProjectionPenSettings::ProjectionPenSettings(const ehud::data::ProjectionSettings& stored, std::uint32_t themeColor,
    std::function<void(const ehud::data::ProjectionSettings&)> persist)
    : theme_(themeColor & 0xFFFFFFu), persist_(std::move(persist)) { load(stored); }
void ProjectionPenSettings::load(const ehud::data::ProjectionSettings& stored) {
    if (stored.color) { values_.color = *stored.color & 0xFFFFFFu; resolved_ = true; }
    else if (!resolved_) values_.color = theme_;
    values_.width = std::isfinite(stored.width) ? std::clamp(stored.width, 1., 80.) : 12;
    values_.erasing = stored.erasing;
    values_.background = stored.background;
    if (model_) apply(*model_);
}
std::array<std::uint32_t, 6> ProjectionPenSettings::palette(std::uint32_t theme) noexcept { return {theme & 0xFFFFFFu, 0xFAFAFA, 0x22A6F2, 0xE5383B, 0x8CE03C, 0x111111}; }
void ProjectionPenSettings::setThemeColor(std::uint32_t hex) noexcept {
    theme_ = hex & 0xFFFFFFu;
    if (!std::exchange(resolved_, true)) values_.color = theme_;
}
void ProjectionPenSettings::attach(ProjectionModel* model) {
    model_ = model;
    if (model_) apply(*model_);
}
// update (:49-58).
void ProjectionPenSettings::update(const std::function<void(Values&)>& change) {
    auto next = values_;
    change(next);
    next.color &= 0xFFFFFFu;
    next.width = std::isfinite(next.width) ? std::clamp(next.width, 1., 80.) : values_.width;
    resolved_ = true;
    if (next == values_) return;
    values_ = next;
    // save (:98-101): the whole dictionary, the colour included.
    if (persist_) persist_({values_.color, values_.width, values_.erasing, values_.background});
    if (model_) apply(*model_);
}
// capture (:61-72).
void ProjectionPenSettings::capture() {
    if (!model_) return;
    const auto& model = *model_;
    update([&](Values& v) {
        v.color = hex(model.color());
        v.width = model.brushWidth();
        v.erasing = model.erasing();
        v.background = model.backgroundEnabled();
    });
}
bool ProjectionPenSettings::hasDrawing() const noexcept { return model_ && (!model_->drawing().strokes().empty() || !model_->media().empty()); }
bool ProjectionPenSettings::clearDrawing() { return model_ && model_->clearContent(); }
bool ProjectionPenSettings::takeMediaInsertRequest() noexcept { return std::exchange(mediaInsertRequested_, false); }
NotesColor ProjectionPenSettings::color(std::uint32_t hex) noexcept {
    return {double((hex >> 16) & 255) / 255, double((hex >> 8) & 255) / 255, double(hex & 255) / 255, 1};
}
std::uint32_t ProjectionPenSettings::hex(const NotesColor& c) noexcept {
    const auto component = [](double v) { return std::uint32_t(std::round(std::clamp(std::isfinite(v) ? v : 0., 0., 1.) * 255)); };
    return component(c[0]) << 16 | component(c[1]) << 8 | component(c[2]);
}
// apply (:91-96).
void ProjectionPenSettings::apply(ProjectionModel& model) const {
    model.setColor(color(values_.color));
    model.setBrushWidth(values_.width);
    model.setErasing(values_.erasing);
    model.setBackgroundEnabled(values_.background);
}
} // namespace endfield::modules
