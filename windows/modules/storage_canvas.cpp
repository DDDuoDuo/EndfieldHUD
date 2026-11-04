// StorageCanvas (Sources/TelemetryCanvases.swift:67-266), statement by
// statement (line numbers cite it).
#include "modules/storage_canvas.hpp"
#include "modules/telemetry_format.hpp"
#include <cmath>

namespace endfield::modules {
namespace d = design;

StorageCanvas::StorageCanvas(d::Context& context, StorageController& controller) : Canvas(core::Module::storage, context), controller_(&controller) {}
StorageCanvas::~StorageCanvas() { if (active_) controller_->deactivate(); }

// MARK: Lifecycle (:104-136)
void StorageCanvas::didActivate() {
    active_ = true;
    lastError_ = controller_->snapshot().error;
    controller_->activate(now());
    changed();
}
void StorageCanvas::stopReading() {
    active_ = false; pendingRefreshToast_ = false;
    controller_->deactivate();
}
void StorageCanvas::controllerChanged(double now) {
    if (!active_) return;                  // the observer exists only between didActivate and stopReading
    env().now = now;
    changed();
}
void StorageCanvas::wake(double now) {
    if (!active_) return;
    controller_->wake(now);
    changed();
}
void StorageCanvas::changed() {
    if (!renderedLanguage() || switchingVolume_) return;
    applyReadings();
    if (!isActive()) return;
    const auto& snapshot = controller_->snapshot();
    if (pendingRefreshToast_ && !snapshot.isLoading) {
        pendingRefreshToast_ = false;
        if (!snapshot.error) toast(d::Icon::refresh, "Readings updated", "读数已更新", volumeTitle());
    }
    if (!snapshot.isLoading && snapshot.error && snapshot.error != lastError_)
        toast(d::Icon::warn, "Storage unavailable", "存储不可用", localizedError(snapshot.error, env().language));
    if (!snapshot.isLoading) lastError_ = snapshot.error;
}

// MARK: Actions (:140-175)
bool StorageCanvas::perform(std::string_view actionID, double now) {
    env().now = now;
    if (actionID == "storage:settings") { openSettings(); return true; }
    if (actionID == "storage:refresh") { refresh(); return true; }
    return false;
}
void StorageCanvas::openSettings() {
    toast(d::Icon::ext, "Storage settings", "系统设置");
    if (onOpenSystemStorage) onOpenSystemStorage();
}
void StorageCanvas::refresh() {
    if (!isActive()) return;
    pendingRefreshToast_ = true;
    controller_->refresh(now());
    changed();
}
void StorageCanvas::openVolumeMenu(const d::Dropdown& anchor) {
    if (menuOpen()) { closeMenu(); return; }
    const auto* current = controller_->currentVolume();
    std::vector<d::Menu::Item> items;
    for (const auto& volume : controller_->volumes()) {
        d::Menu::Item item; item.id = volume.id; item.title = volume.name(); item.selected = current && volume.id == current->id;
        items.push_back(std::move(item));
    }
    if (items.empty()) return;
    openMenu("volume", std::move(items), anchor, std::nullopt, [this](const d::Menu::Item& item) {
        const auto* current = controller_->currentVolume();
        if (current && item.id == current->id) return;
        // The chosen disk re-enters from below with its listed numbers; its
        // fresh reading then updates that page in place.
        switchingVolume_ = true;
        const bool chosen = controller_->selectVolume(item.id, now());
        switchingVolume_ = false;
        if (chosen) navigate(0, 1);
    });
}

// MARK: Page (:178-256)
std::string StorageCanvas::volumeTitle() const {
    if (const auto* current = controller_->currentVolume()) return current->name();
    if (const auto& capacity = controller_->snapshot().capacity) return capacity->volumeName;
    return text("Startup disk", "启动磁盘");
}
void StorageCanvas::buildPage(d::Page& page) {
    auto& volume = page.make<d::Dropdown>("volume", text("Disk", "磁盘"), volumeTitle());
    volume.onActivate = [this, &volume] { openVolumeMenu(volume); };
    auto& refreshButton = page.make<d::IconButton>("refresh", text("Refresh storage", "刷新存储"), d::Icon::refresh);
    refreshButton.onActivate = [this] { refresh(); };
    header().configure({"STORAGE"}, "SYS-10", title("Storage", "存储空间"), std::string(telemetry::missing), false, {&volume, &refreshButton});

    const auto& p = env().palette;
    const double width = d::metric::boxWidth;
    d::Layer& readoutRow = page.makeLayer("storage.readout");
    d::Layer& availableLabel = d::makeText(page.builder(), "storage.available.label", text("Available", "可用"), d::type::label(p), {0, 0, 300, 10});
    d::Layer& usedLabel = d::makeText(page.builder(), "storage.used.label", text("Used", "已用"), d::type::label(p), {width - 300, 49, 300, 10}, d::TextAlignment::right);
    auto& cells = page.own<d::Cells>("storage.cells", 40, width, true, 30);
    const auto frames = d::StatPlate::gridFrames(3, width);
    Live live;
    for (std::size_t index = 0; index < live.plates.size(); ++index) live.plates[index] = &page.own<d::StatPlate>("storage.plate." + std::to_string(index), frames[0].width);
    d::Layer& readoutFrame = page.makeLayer("storage.readings");
    readoutFrame.setFrame({0, 81, width, 92});
    live.readout = &page.builder().controls.make<d::Readout>(env(), "storage.readings", volumeTitle(), "", readoutFrame);
    live.volume = &volume;
    live.available = &page.own<d::BigNumeral>("storage.available", 84);
    live.used = &page.own<d::BigNumeral>("storage.used", 30);
    live.cells = &cells;
    readoutRow.setFrame({0, 81, width, 92});
    // Pinned at their outer corners: the readings change their widths.
    live.available->layer().setAnchorPoint({0, 0}); live.available->layer().setPosition({0, 16});
    live.used->layer().setAnchorPoint({1, 0}); live.used->layer().setPosition({width, 65});
    for (d::Layer* child : {&availableLabel, &usedLabel, &live.available->layer(), &live.used->layer()}) readoutRow.add(*child);
    cells.layer().setOrigin({0, 189});
    for (std::size_t index = 0; index < live.plates.size(); ++index) {
        auto frame = frames[index]; frame.y += 233;
        live.plates[index]->layer().setFrame(frame);
    }
    page.layer().add(readoutRow); page.layer().add(cells.layer());
    for (auto* plate : live.plates) page.layer().add(plate->layer());
    page.layer().add(readoutFrame);
    auto& settings = page.make<d::Button>("settings", text("Storage Settings", "存储设置"), d::Icon::ext, d::Button::Kind::primary, d::Button::Height::large);
    settings.place({std::round((width - settings.preferredWidth()) / 2), 304});
    settings.onActivate = [this] { openSettings(); };
    page.layer().add(settings.layer());
    page.add(*live.readout); page.add(settings);
    page.stagger(readoutRow); page.stagger(cells.layer());
    for (auto* plate : live.plates) page.stagger(plate->layer());
    page.stagger(settings.layer());
    live_ = live;
    applyReadings(false);
}

// applyReadings (:221-256): readings, counter and disk name, in place.
void StorageCanvas::applyReadings(bool animated) {
    if (!live_) return;
    auto& live = *live_;
    const auto& capacity = controller_->snapshot().capacity;
    const auto usedPercent = capacity ? std::optional(telemetry::percent(capacity->usedFraction() * 100)) : std::nullopt;
    header().setCounter(usedPercent.value_or(std::string(telemetry::missing)));
    live.volume->setValue(volumeTitle());
    const auto available = capacity ? std::optional(telemetry::storage(capacity->availableBytes)) : std::nullopt;
    live.available->set(available ? available->number : std::string(telemetry::missing), available ? available->unit : "");
    live.used->set(usedPercent ? usedPercent->substr(0, usedPercent->size() - 1) : std::string(telemetry::missing), usedPercent ? "%" : "");
    const int filled = capacity ? int(std::round(capacity->usedFraction() * 40)) : 0;
    const bool low = capacity && capacity->isLowOnSpace();
    live.cells->set(filled, low ? std::optional(d::Cells::Tone{d::rgb(0xFF6B6D), d::rgb(0xB8262A)}) : std::nullopt, false, animated);
    cellFill_ = {filled, low};
    const std::array<std::string, 3> labels{text("Total", "总计"), text("Used", "已用"), text("Available", "可用")};
    std::array<std::string, 3> values;
    const std::array<std::optional<std::int64_t>, 3> bytes{capacity ? std::optional(capacity->totalBytes) : std::nullopt,
        capacity ? std::optional(capacity->usedBytes()) : std::nullopt, capacity ? std::optional(capacity->availableBytes) : std::nullopt};
    for (std::size_t index = 0; index < values.size(); ++index) {
        if (!bytes[index]) { values[index] = std::string(telemetry::missing); continue; }
        const auto value = telemetry::storage(*bytes[index]);
        values[index] = value.number + " " + value.unit;
    }
    for (std::size_t index = 0; index < live.plates.size(); ++index) live.plates[index]->set(labels[index], values[index], std::nullopt, index == 1);
    live.readout->label = volumeTitle();
    if (usedPercent) {
        std::string value;
        for (std::size_t index = 0; index < labels.size(); ++index) value += (index ? ", " : "") + labels[index] + " " + values[index];
        live.readout->value = value + ", " + *usedPercent;
    } else {
        live.readout->value = controller_->snapshot().isLoading ? text("Refreshing", "正在刷新") : localizedError(controller_->snapshot().error, env().language);
    }
}

// localizedError (:259-265): the controller's two messages, also for none.
std::string StorageCanvas::localizedError(const std::optional<std::string>& value, core::Language language) {
    if (value && *value == "The startup filesystem is unavailable.") return core::localized("The startup filesystem is unavailable.", "启动文件系统不可用。", language);
    if (!value || *value == "Storage capacity is unavailable.") return core::localized("Storage capacity is unavailable.", "无法读取存储容量。", language);
    return *value;
}
} // namespace endfield::modules
