// MediaAssemblyCanvas (Sources/MediaAssemblyCanvas.swift), statement by
// statement; line numbers cite that file.
#include "modules/media_assembly_canvas.hpp"
#include "modules/media_assembly_export.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace endfield::modules {
namespace d = design;
namespace {
using d::Point; using d::Rect;
// CGRect.contains: half-open, empty rects contain nothing.
bool inside(Rect r, Point p) { return r.width > 0 && r.height > 0 && p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
Rect offset(Rect r, Point by) { return {r.x + by.x, r.y + by.y, r.width, r.height}; }
std::string fixed(const char* format, double value) { char text[48]; std::snprintf(text, sizeof text, format, value); return text; }
std::string lastPathComponent(const std::string& path) {
    const auto slash = path.find_last_of("/\\");
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
}

MediaAssemblyCanvas::MediaAssemblyCanvas(d::Context& context, MediaAssemblyController& controller, MediaAssemblyCanvasServices services)
    : Canvas(core::Module::mediaAssembly, context), controller_(&controller), services_(std::move(services)) {}
MediaAssemblyCanvas::~MediaAssemblyCanvas() {
    trackArena(nullptr);
    if (panel_ && services_.cancelPanel) services_.cancelPanel();
}

// MARK: Presentation (:80-106)
// While presented the session renders previews; hidden, the controller drops
// its decoders and the stage keeps its last artwork for the exit.
void MediaAssemblyCanvas::setPresented(bool value) {
    if (value == presented_) return;
    presented_ = value;
    if (!value) {
        drag_.reset(); stopExportTicker();
        if (closeWork_) { cancel(closeWork_); finishClose(); }
        if (panel_ && services_.cancelPanel) services_.cancelPanel();
        panel_.reset();
    }
    controller_->setActive(value, now());
    if (value) controllerChanged();
}
void MediaAssemblyCanvas::show(double now) {
    env().now = now;
    setPresented(true);
    Canvas::show(now);
}
void MediaAssemblyCanvas::willLeave() { setPresented(false); }
void MediaAssemblyCanvas::didActivate() {
    // The design owner keeps a canvas shown across a HUD close (no second
    // makeContent): activation after willLeave presents it again, so the
    // session reactivates and renders one fresh preview.
    setPresented(true);
    const auto& session = controller_->session();
    if (session.exporting() && !exportShown_) exportShown_ = session.progress();
    if (exportShown_) startExportTicker();
    refreshInPlace();
}
void MediaAssemblyCanvas::didDeactivate() { drag_.reset(); stage().setHovered(std::nullopt); stopExportTicker(); }

// MARK: Controller changes (:110-177)
MediaAssemblyCanvas::Structure MediaAssemblyCanvas::structure() const {
    const auto& session = controller_->session();
    const auto& document = session.document();
    return {env().language, document ? document->id : std::string(), document && document->video, session.busy(), isExporting(), closing_, tab_, tool_};
}
bool MediaAssemblyCanvas::isExporting() const noexcept { return controller_->session().exporting() || exportShown_.has_value(); }
// Edits need a loaded source, no import, export or close in progress.
bool MediaAssemblyCanvas::isEditable() const noexcept {
    const auto& session = controller_->session();
    return session.document() && !session.busy() && !isExporting() && !closing_;
}
void MediaAssemblyCanvas::controllerChanged() {
    if (services_.now) env().now = std::max(env().now, services_.now());
    if (!presented_) return;
    if (handlingChange_) { changePending_ = true; return; }
    handlingChange_ = true;
    do { changePending_ = false; processChange(); } while (changePending_);
    handlingChange_ = false;
}
void MediaAssemblyCanvas::processChange() {
    const auto& document = controller_->session().document();
    if ((document ? std::optional(document->id) : std::nullopt) != sourceID_) sourceChanged();
    // A pending page swap builds from the latest state anyway.
    else if (!quiet_ && !transitioning() && renderedLanguage() && structure() != renderedStructure_) rerender();
    else refreshInPlace();
    reportError();
}
// A new (or no) source: every view edit resets and the page enters from
// below (spec §3.10 subnav(0, 1)); an overwrite's reload stays put.
void MediaAssemblyCanvas::sourceChanged() {
    const bool reload = reloadingAfterOverwrite_;
    reloadingAfterOverwrite_ = false;
    const auto& document = controller_->session().document();
    sourceID_ = document ? std::optional(document->id) : std::nullopt;
    viewport_.reset(); selectedSticker_.clear(); drag_.reset();
    cancel(pendingPreview_); pendingPreview_ = 0; geometryAnimationEnd_ = 0;
    if (tool_ == MediaAssemblyTool::crop) quietly([&] { controller_->setCropPreview(false, now()); });
    tool_.reset();
    shownGeometry_ = targetGeometry();
    if (!reload) { clearOnBuild_ = true; revealOnPreview_ = true; }
    if (!renderedLanguage()) return;
    if (document && !reload && isActive()) navigate(0, 1); else rerender();
}
void MediaAssemblyCanvas::refreshInPlace() {
    if (!renderedLanguage()) return;
    header().setCounter(counterText());
    for (const auto& binder : binders_) binder();
    layoutStage();
}
void MediaAssemblyCanvas::reportError() {
    const auto& error = controller_->session().error();
    std::optional<std::string> text;
    if (error && *error != MediaAssemblyError::cancelled) text = mediaAssemblyErrorMessage(*error, env().language);
    if (text == lastError_) return;
    lastError_ = text;
    if (text && isActive()) toast(d::Icon::warn, "Notice", "提示", *text);
}
std::string MediaAssemblyCanvas::counterText() const {
    const auto& session = controller_->session();
    const auto& document = session.document();
    if (!document) return "EMPTY";
    if (const auto progress = exportShown_ ? exportShown_ : (session.exporting() ? std::optional(session.progress()) : std::nullopt))
        return std::to_string(int(std::floor(std::min(1., std::max(0., *progress)) * 100))) + "%";
    return document->video ? "VID 01" : "IMG 01";
}

// MARK: Page (:181-250)
// Built when first needed (the Mac's lazy stage) and again for a new palette.
MediaAssemblyStage& MediaAssemblyCanvas::stage() {
    if (!stage_ || stage_->paletteGeneration() != env().palette.generation()) {
        auto next = std::make_unique<MediaAssemblyStage>(env(), "media.stage." + std::to_string(env().palette.generation()), services_.stickerImage);
        // The replaced stage stays until no page shows it (its layer left its page).
        if (stage_) { next->adopt(*stage_); retiredStages_.push_back(std::move(stage_)); }
        stage_ = std::move(next);
        wire(*stage_);
        trackArena(&stage_->arena());
    }
    return *stage_;
}
void MediaAssemblyCanvas::buildPage(d::Page& page) {
    binders_.clear(); parameterRows_.clear();
    std::erase_if(retiredStages_, [](const auto& stage) { return stage->layer().parent() == nullptr; });
    renderedStructure_ = structure();
    const auto& session = controller_->session();
    const MediaAssemblyDocumentInfo* document = session.document().get();
    auto& stage = this->stage();
    if (clearOnBuild_) { clearOnBuild_ = false; stage.setImage(nullptr, false); stage.setContentOpacity(1, false); }
    buildHeader(page, document);
    // The stage outlives pages: it joins this page when it is installed (didInstall).
    stage.layer().setOrigin(stageOrigin);
    stagePage_ = &page;
    page.stagger(stage.layer());
    if (auto* seek = stage.configurePlayback(page.builder(), document && document->video ? std::optional(document->duration) : std::nullopt,
            text("Playback position", "播放进度"))) {
        seek->onChange = [this](double value, bool final) {
            if (final) controller_->seek(value, now());
            else stage_->setPlayback(true, controller_->session().view().playing, value, duration());
        };
        hostBubble(*seek);
    }
    relabelStageControls();
    page.add(stage.controls());
    auto& panel = page.makeLayer("media.panel");
    panel.setFrame(panelFrame);
    panel.setPartRoot(true);
    page.layer().add(panel);
    page.stagger(panel);
    panel.setValue(d::Channel::opacity, !document || isExporting() ? .35 : 1);
    buildPanel(page, panel);
    layoutStage();
}
void MediaAssemblyCanvas::didInstall(d::Page& page) {
    if (&page != stagePage_) return;
    page.layer().insert(stage().layer(), 0);
    stage().installPlayback();
    stagePage_ = nullptr;
    // The installed page's seek slider takes the current position.
    layoutStage();
}
void MediaAssemblyCanvas::buildHeader(d::Page& page, const MediaAssemblyDocumentInfo* document) {
    const bool exporting = isExporting();
    const auto& session = controller_->session();
    auto& open = page.make<d::Button>("open", text("Open", "打开"), d::Icon::open, d::Button::Kind::secondary, d::Button::Height::compact);
    open.place({0, 0});
    open.setEnabled(!exporting && !closing_);
    open.onActivate = [this, &open] { openSourceMenu(open); };
    auto& close = page.make<d::IconButton>("close", text("Close source", "关闭源文件"), d::Icon::x);
    close.setEnabled(document && !exporting && !closing_);
    close.onActivate = [this] { confirmClose(); };
    auto& exportButton = page.make<d::Button>("export", exporting ? text("Cancel", "取消") : text("Export", "导出"),
        exporting ? d::Icon::x : d::Icon::export_, d::Button::Kind::primary, d::Button::Height::compact);
    exportButton.place({0, 0});
    exportButton.setEnabled(session.exporting() || (!exporting && document && !session.busy() && !closing_));
    exportButton.onActivate = [this, &exportButton] { exportPressed(exportButton); };
    header().configure({"MEDIA ASSEMBLY"}, "MOD-05", title("Media Assembly", "影像加工"), counterText(), false, {&open, &close, &exportButton});
}
void MediaAssemblyCanvas::wire(MediaAssemblyStage& stage) {
    stage.zoomOut().onActivate = [this] { zoomStep(.8); };
    stage.zoomIn().onActivate = [this] { zoomStep(1.25); };
    stage.zoomValue().onActivate = [this] { resetZoom(); };
    stage.play().onActivate = [this] { controller_->togglePlayback(now()); };
    stage.pause().onActivate = [this] { controller_->togglePlayback(now()); };
    stage.open().onActivate = [this] { openSourceMenu(stage_->open()); };
}
// Stage controls outlive pages; their labels follow the UI language.
void MediaAssemblyCanvas::relabelStageControls() {
    auto& stage = this->stage();
    stage.zoomOut().label = text("Zoom out", "缩小"); stage.zoomIn().label = text("Zoom in", "放大");
    stage.zoomValue().label = text("Reset zoom", "重置缩放");
    stage.play().label = text("Play", "播放"); stage.pause().label = text("Pause", "暂停");
    for (d::Control* control : stage.controls()) if (control->id() == "seek") control->label = text("Playback position", "播放进度");
    stage.open().setTitle(text("Open", "打开"));
    stage.open().layer().setOrigin({std::round((MediaAssemblyStage::size.width - stage.open().layer().bounds().width) / 2), stage.open().layer().frame().y});
    const auto& document = controller_->session().document();
    const bool playable = isEditable() && document && document->video && tool_ != MediaAssemblyTool::crop;
    stage.play().setEnabled(playable); stage.pause().setEnabled(playable);
    for (d::Control* control : stage.controls()) if (control->id() == "seek") control->setEnabled(playable && tool_ != MediaAssemblyTool::trim);
}

// MARK: Panel (:254-409)
void MediaAssemblyCanvas::buildPanel(d::Page& page, d::Layer& panel) {
    const bool enabled = isEditable();
    auto& tabs = page.makeTabs("mediaTab", {{text("Adjust", "调整"), std::nullopt}, {text("Filters", "滤镜"), std::nullopt}, {text("Stickers", "贴纸"), std::nullopt}},
        int(tab_), panelFrame.width, false, true);
    tabs.onSelect = [this](int index, double direction) { selectTab(index == 1 ? Tab::filters : index == 2 ? Tab::stickers : Tab::adjust, direction); };
    for (d::Control* control : tabs.controls()) control->setEnabled(enabled);
    tabs.layer().setOrigin({0, 0}); panel.add(tabs.layer());
    page.add(tabs.controls());
    if (enabled) page.tabs = &tabs;
    const auto& document = controller_->session().document();
    switch (tab_) {
    case Tab::adjust:
        if (!tool_) buildTiles(page, panel, enabled);
        else if (*tool_ == MediaAssemblyTool::crop) buildCropPage(page, panel, enabled);
        else buildSliderPage(*tool_, page, panel, enabled);
        break;
    case Tab::filters: buildFilters(page, panel, enabled); break;
    case Tab::stickers: buildStickers(page, panel, enabled && !(document && document->video)); break;
    }
}
void MediaAssemblyCanvas::buildTiles(d::Page& page, d::Layer& panel, bool enabled) {
    const double width = (panelFrame.width - 6) / 2;
    struct Item { std::string name, title; d::Icon icon; Rect frame; };
    std::vector<Item> tiles;
    const std::array<std::tuple<const char*, std::string, d::Icon>, 6> grid{{
        {"crop", mediaAssemblyToolTitle(MediaAssemblyTool::crop, env().language), d::Icon::grid}, {"rotate", text("Rotate", "旋转"), d::Icon::rotate},
        {"mirror", text("Mirror", "镜像"), d::Icon::mirror}, {"adjust", mediaAssemblyToolTitle(MediaAssemblyTool::adjust, env().language), d::Icon::gear},
        {"curves", mediaAssemblyToolTitle(MediaAssemblyTool::curves, env().language), d::Icon::lyric},
        {"levels", mediaAssemblyToolTitle(MediaAssemblyTool::levels, env().language), d::Icon::apps}}};
    for (std::size_t index = 0; index < grid.size(); ++index) {
        const auto& [name, title, icon] = grid[index];
        tiles.push_back({name, title, icon, {double(index % 2) * (width + 6), 42 + double(index / 2) * 68, width, 62}});
    }
    double y = 42 + 3 * 68;
    std::vector<std::tuple<std::string, std::string, d::Icon>> wide{{"reset", text("Reset all", "全部重置"), d::Icon::reset}};
    const auto& document = controller_->session().document();
    if (document && document->video) wide.emplace(wide.begin(), "trim", mediaAssemblyToolTitle(MediaAssemblyTool::trim, env().language), d::Icon::timer);
    for (auto& [name, title, icon] : wide) {
        tiles.push_back({name, title, icon, {0, y, panelFrame.width, 36}});
        y += 42;
    }
    for (auto& item : tiles) {
        auto& tile = page.make<MediaAssemblyToolTile>("tool:" + item.name, item.title, item.icon, d::Size{item.frame.width, item.frame.height});
        tile.setEnabled(enabled);
        tile.onActivate = [this, name = item.name] { toolTile(name); };
        tile.layer().setOrigin({item.frame.x, item.frame.y}); panel.add(tile.layer());
        page.add(tile); page.stagger(tile.layer());
        if (item.name == "mirror") {
            tile.setOn(controller_->session().adjustments().mirrored);
            binders_.push_back([this, &tile] { tile.setOn(controller_->session().adjustments().mirrored); });
        }
    }
}
// Back, the tool's name and (except Crop) an icon-only reset (spec §3.10 .thd).
void MediaAssemblyCanvas::buildToolHeader(MediaAssemblyTool tool, d::Page& page, d::Layer& panel, bool enabled) {
    auto& back = page.make<d::IconButton>("toolBack", text("Back", "返回"), d::Icon::back, d::IconButton::Style::back);
    back.onActivate = [this] { closeTool(); };
    const bool resettable = tool != MediaAssemblyTool::crop;
    const auto& p = env().palette;
    d::Layer& name = d::makeText(page.builder(), "media.tool.name", mediaAssemblyToolTitle(tool, env().language),
        {13, d::FontWeight::bold, p.token(d::ThemeToken::titleText), 1, .06, true}, {38, 50.5, panelFrame.width - 38 - (resettable ? 54 : 0), 13});
    back.layer().setOrigin({0, 42});
    panel.add(back.layer()); panel.add(name);
    back.setEnabled(enabled);
    page.add(back);
    if (!resettable) return;
    auto& reset = page.make<d::Button>("toolReset", "", d::Icon::reset, d::Button::Kind::secondary, d::Button::Height::compact);
    reset.label = text("Reset", "重置");
    reset.place({panelFrame.width - reset.preferredWidth(), 44});
    reset.setEnabled(enabled);
    reset.onActivate = [this, tool] { resetTool(tool); };
    panel.add(reset.layer());
    page.add(reset);
}
void MediaAssemblyCanvas::buildCropPage(d::Page& page, d::Layer& panel, bool enabled) {
    buildToolHeader(MediaAssemblyTool::crop, page, panel, enabled);
    auto& reset = page.make<d::Button>("cropReset", text("Reset crop", "重置裁剪"), d::Icon::reset, d::Button::Kind::secondary);
    reset.place({0, 78}, panelFrame.width);
    reset.onActivate = [this] { auto value = controller_->session().adjustments(); value.crop = {}; updateAdjustments(std::move(value)); };
    auto& done = page.make<d::Button>("cropDone", text("Done", "完成"), d::Icon::check, d::Button::Kind::primary);
    done.place({0, 116}, panelFrame.width);
    done.onActivate = [this] { closeTool(); };
    for (d::Button* button : {&reset, &done}) {
        button->setEnabled(enabled);
        panel.add(button->layer());
        page.add(*button); page.stagger(button->layer());
    }
}
void MediaAssemblyCanvas::buildSliderPage(MediaAssemblyTool tool, d::Page& page, d::Layer& panel, bool enabled) {
    buildToolHeader(tool, page, panel, enabled);
    auto& scroll = page.scrollArea("media.tool." + std::string(mediaAssemblyToolID(tool)), {0, 78, panelFrame.width, 330}, &panel);
    const auto parameters = MediaAssemblyParameter::parameters(tool, duration(), env().language);
    const auto& adjustments = controller_->session().adjustments();
    for (std::size_t index = 0; index < parameters.size(); ++index) {
        auto& row = page.own<MediaAssemblyParameterRow>(parameters[index], parameters[index].read(adjustments), panelFrame.width - 6);
        row.layer().setOrigin({0, double(index) * (MediaAssemblyParameterRow::height + 2)}); scroll.content().add(row.layer());
        hostBubble(row.slider());
        row.slider().setEnabled(enabled);
        row.slider().onChange = [this, &row](double value, bool final) { parameterChanged(row, value, final); };
        page.add(row.slider()); page.stagger(row.layer());
        parameterRows_.push_back(&row);
    }
    scroll.setContentHeight(double(parameters.size()) * (MediaAssemblyParameterRow::height + 2) - 2);
    page.restoreOffset(scroll);
    binders_.push_back([this] {
        for (auto* row : parameterRows_) row->setValue(row->parameter().read(controller_->session().adjustments()));
    });
}
void MediaAssemblyCanvas::buildFilters(d::Page& page, d::Layer& panel, bool enabled) {
    auto& scroll = page.scrollArea("media.filters", {0, 42, panelFrame.width, 366}, &panel);
    const d::Size size{69, 72};
    std::vector<std::pair<MediaAssemblyFilter, MediaAssemblyPresetCell*>> cells;
    const auto filters = mediaAssemblyFilters();
    for (std::size_t index = 0; index < filters.size(); ++index) {
        const auto filter = *mediaAssemblyFilter(filters[index].id);
        const auto title = mediaAssemblyFilterTitle(filter, env().language);
        auto& cell = page.make<MediaAssemblyPresetCell>("filter:" + std::string(filters[index].id), title, title,
            services_.filterThumbnail ? services_.filterThumbnail(filter) : std::nullopt, size);
        cell.setSelected(controller_->session().adjustments().filter == filter, false);
        cell.setEnabled(enabled);
        cell.onActivate = [this, filter] { selectFilter(filter); };
        cell.layer().setOrigin({3 + double(index % 2) * (size.width + 6), 3 + double(index / 2) * (size.height + 6)});
        scroll.content().add(cell.layer());
        page.add(cell); page.stagger(cell.layer());
        cells.emplace_back(filter, &cell);
    }
    const auto rows = (cells.size() + 1) / 2;
    scroll.setContentHeight(6 + double(rows) * (size.height + 6) - 6);
    page.restoreOffset(scroll);
    binders_.push_back([this, cells] {
        for (const auto& [filter, cell] : cells) cell->setSelected(controller_->session().adjustments().filter == filter);
    });
}
void MediaAssemblyCanvas::buildStickers(d::Page& page, d::Layer& panel, bool enabled) {
    auto& scroll = page.scrollArea("media.stickers", {0, 42, panelFrame.width, 366}, &panel);
    // Video exports carry no stickers: the grid stays visible but inert.
    const auto& document = controller_->session().document();
    if (document && document->video) scroll.layer().setValue(d::Channel::opacity, .35);
    const d::Size size{44, 52};
    const auto kinds = mediaAssemblyStickers();
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto kind = *mediaAssemblySticker(kinds[index].id);
        auto& cell = page.make<MediaAssemblyPresetCell>("sticker:" + std::string(kinds[index].id), mediaAssemblyStickerTitle(kind, env().language), std::nullopt,
            services_.stickerThumbnail ? services_.stickerThumbnail(kind) : std::nullopt, size);
        cell.setEnabled(enabled);
        cell.onActivate = [this, kind] { addSticker(kind); };
        cell.layer().setOrigin({3 + double(index % 3) * (size.width + 6), 3 + double(index / 3) * (size.height + 6)});
        scroll.content().add(cell.layer());
        page.add(cell); page.stagger(cell.layer());
    }
    scroll.setContentHeight(6 + double((kinds.size() + 2) / 3) * (size.height + 6) - 6);
    page.restoreOffset(scroll);
}

// MARK: Panel actions (:413-521)
void MediaAssemblyCanvas::selectTab(Tab next, double direction) {
    if (next == tab_) return;
    quietly([&] { leaveTool(); });
    tab_ = next;
    navigate(direction);
}
void MediaAssemblyCanvas::toolTile(std::string_view name) {
    if (!isEditable()) return;
    if (name == "rotate") {
        auto value = controller_->session().adjustments(); value.rotationQuarterTurns = (value.rotationQuarterTurns + 1) % 4;
        animateNextLayout_ = true; updateAdjustments(std::move(value)); animateNextLayout_ = false;
    } else if (name == "mirror") {
        auto value = controller_->session().adjustments(); value.mirrored = !value.mirrored;
        animateNextLayout_ = true; updateAdjustments(std::move(value)); animateNextLayout_ = false;
    } else if (name == "reset") confirmReset();
    else if (const auto next = mediaAssemblyTool(name)) openTool(*next);
}
// Crop, Adjust, Curves, Levels and Trim open forward (subnav(1)).
void MediaAssemblyCanvas::openTool(MediaAssemblyTool next) {
    if (!isEditable() || tool_) return;
    if (next == MediaAssemblyTool::crop) quietly([&] { controller_->setCropPreview(true, now()); });
    tool_ = next;
    navigate(1);
}
// Back / Done / Esc return to the tiles (subnav(-1)).
void MediaAssemblyCanvas::closeTool() {
    if (!tool_) return;
    quietly([&] { leaveTool(); });
    navigate(-1);
}
void MediaAssemblyCanvas::leaveTool() {
    if (tool_ == MediaAssemblyTool::crop) controller_->setCropPreview(false, now());
    tool_.reset();
}
void MediaAssemblyCanvas::resetTool(MediaAssemblyTool tool) {
    if (!isEditable()) return;
    const auto parameters = MediaAssemblyParameter::parameters(tool, duration(), env().language);
    const auto& document = controller_->session().document();
    if (tool == MediaAssemblyTool::trim && document) { updateTrim(0, document->duration, 0); return; }
    auto value = controller_->session().adjustments();
    for (const auto& parameter : parameters) parameter.write(value, parameter.defaultValue);
    updateAdjustments(std::move(value));
}
void MediaAssemblyCanvas::parameterChanged(MediaAssemblyParameterRow& row, double value, bool final) {
    if (!isEditable()) return;
    const auto& session = controller_->session();
    auto next = session.adjustments();
    row.parameter().write(next, value);
    row.showDragValue(row.parameter().read(next));
    const auto& document = session.document();
    if (tool_ == MediaAssemblyTool::trim && document) {
        const double start = next.trimStart, end = next.trimEnd.value_or(document->duration);
        if (start != session.adjustments().trimStart || end != session.adjustments().trimEnd.value_or(document->duration))
            updateTrim(start, end, row.parameter().id == "trimStart" ? start : end);
    } else if (next != session.adjustments()) {
        updateAdjustments(std::move(next));
    }
    if (final) row.setValue(row.parameter().read(controller_->session().adjustments()));
}
// Double-clicking a tool slider resets that value (spec §3.10).
bool MediaAssemblyCanvas::pointerDown(Point point, const d::PointerInput& input, double now) {
    env().now = now;
    if (input.clickCount == 2 && isActive() && !menuOpen() && !popoverOpen() && !transitioning())
        for (auto* row : parameterRows_)
            if (hits(row->slider(), point)) { parameterChanged(*row, row->parameter().defaultValue, true); return true; }
    return Canvas::pointerDown(point, input, now);
}
bool MediaAssemblyCanvas::hits(const d::Control& control, Point point) const {
    if (!control.enabled() || !d::isVisible(control.layer(), root()) || !d::isUnclipped(control.layer(), point, root())) return false;
    const auto local = d::convert(point, root(), control.layer());
    return local && control.contains(*local);
}
void MediaAssemblyCanvas::selectFilter(MediaAssemblyFilter filter) {
    if (!isEditable()) return;
    auto value = controller_->session().adjustments(); value.filter = filter;
    updateAdjustments(std::move(value));
}
// A sticker joins at the centre (.25 of the image, unrotated) and is selected.
void MediaAssemblyCanvas::addSticker(MediaAssemblyStickerKind kind) {
    const auto& document = controller_->session().document();
    if (!isEditable() || !document || document->video) return;
    if (controller_->session().adjustments().stickers.size() >= 16) { toast(d::Icon::x, "Sticker limit", "最多 16 个贴纸"); return; }
    MediaAssemblySticker item;
    item.id = services_.uuid ? services_.uuid() : std::string(); item.kind = kind; item.x = .5; item.y = .5; item.size = .25; item.rotation = 0;
    if (item.id.empty()) return;
    selectedSticker_ = item.id;
    auto value = controller_->session().adjustments(); value.stickers.push_back(std::move(item));
    updateAdjustments(std::move(value));
}
void MediaAssemblyCanvas::confirmReset() {
    d::ConfirmRequest request;
    request.id = "mediaReset"; request.title = text("Reset all edits?", "重置所有编辑？");
    request.message = text("Filters, adjustments, crop and stickers.", "滤镜、调整、裁剪与贴纸。");
    request.confirmTitle = text("Reset", "重置"); request.icon = d::Icon::reset;
    confirm(std::move(request), [this] {
        if (!isEditable()) return;
        selectedSticker_.clear();
        controller_->reset(now());
        toast(d::Icon::reset, "Edits reset", "已重置");
    });
}

// MARK: Source: open, close, drops (:525-595)
void MediaAssemblyCanvas::openSourceMenu(const d::Control& anchor) {
    if (isExporting()) return;
    std::vector<d::Menu::Item> items(2);
    items[0].id = "finder"; items[0].title = std::string(core::translated(core::chooseInFileExplorer, env().language));
    items[1].id = "shelf"; items[1].title = text("Choose from Shelf", "从暂存架选择");
    openMenu("open", std::move(items), anchor, std::nullopt, [this, anchor = anchor.id()](const d::Menu::Item& item) {
        if (item.id == "finder") { chooseFile(); return; }
        // The anchor held by id (the Mac's weak anchor): the header's Open or the stage's.
        const d::Control* control{};
        for (d::Control* candidate : page().controls()) if (candidate->id() == anchor) control = candidate;
        for (d::Control* candidate : header().controls()) if (candidate->id() == anchor) control = candidate;
        if (control) openShelfMenu(control);
    });
}
// The shelf's images and videos; files that went missing are listed disabled.
void MediaAssemblyCanvas::openShelfMenu(const d::Control* anchor) {
    std::vector<MediaAssemblyShelfChoice> choices;
    if (services_.shelfChoices) for (auto& choice : services_.shelfChoices()) if (choice.supported) choices.push_back(std::move(choice));
    std::vector<d::Menu::Item> items;
    if (choices.empty()) { d::Menu::Item item; item.id = "empty"; item.title = text("Shelf is empty", "暂存架为空"); item.enabled = false; items.push_back(std::move(item)); }
    std::vector<std::string> ids;
    for (const auto& choice : choices) { d::Menu::Item item; item.id = choice.id; item.title = choice.title; item.enabled = choice.available; items.push_back(std::move(item)); ids.push_back(choice.id); }
    openMenu("shelf", std::move(items), *anchor, std::nullopt, [this, ids = std::move(ids)](const d::Menu::Item& item) {
        if (std::find(ids.begin(), ids.end(), item.id) == ids.end() || isExporting()) return;
        try {
            if (auto access = services_.shelfAccess ? services_.shelfAccess(item.id) : std::nullopt)
                controller_->importPath(std::move(access->path), std::move(access->lease), true, now());
        } catch (const MediaAssemblyFailure& failure) { controller_->report(failure.error()); }
        catch (...) { controller_->report(MediaAssemblyError::unavailable); }
    });
}
void MediaAssemblyCanvas::chooseFile() {
    if (!services_.chooseFile || !services_.chooseFile()) return;
    panel_ = Panel{false, {}};
}
void MediaAssemblyCanvas::panelFinished(std::optional<std::string> path, double now) {
    env().now = std::max(env().now, now);
    if (!panel_) return;
    const auto panel = std::move(*panel_);
    panel_.reset();
    if (!panel.save) { if (presented_ && path) importFiles(std::span(&*path, 1), now); return; }
    // The save dialog already asked before replacing an existing file.
    const auto& document = controller_->session().document();
    if (path && document && document->id == panel.document)
        startExport(*path, services_.fileExists && services_.fileExists(*path), *document, false);
}
void MediaAssemblyCanvas::importFiles(std::span<const std::string> paths, double now) {
    env().now = std::max(env().now, now);
    if (isExporting() || paths.empty()) return;
    closePopups();
    controller_->importPath(paths.front(), nullptr, true, this->now());
}
// Closing always asks first; with edits it warns that they are discarded. A
// module switch or HUD close dismisses the bar and keeps the session.
void MediaAssemblyCanvas::confirmClose() {
    const auto& document = controller_->session().document();
    if (!document || isExporting() || closing_) return;
    const bool edited = controller_->session().adjustments() != MediaAssemblyAdjustments{};
    d::ConfirmRequest request;
    request.id = "mediaClose"; request.title = text("Close this file?", "关闭此文件？");
    request.message = edited ? text("Unexported edits are discarded.", "未导出的编辑将被丢弃。") : document->name;
    request.confirmTitle = text("Close", "关闭"); request.danger = edited; request.icon = d::Icon::x;
    confirm(std::move(request), [this, id = document->id] {
        const auto& current = controller_->session().document();
        if (!current || current->id != id) return;
        closeSource();
    });
}
// The picture fades (.16 s), then the empty stage shows (spec §3.10 Close).
void MediaAssemblyCanvas::closeSource() {
    if (!controller_->session().document() || isExporting() || closing_) return;
    closePopups();
    closing_ = true; drag_.reset();
    stage().setContentOpacity(0, true);
    rerender();
    closeWork_ = timerGroup();
    after(env().reduceMotion ? 0 : .16, closeWork_, [this] { finishClose(); });
}
void MediaAssemblyCanvas::finishClose() {
    closeWork_ = 0; closing_ = false;
    if (!controller_->closeDocument(now()) && renderedLanguage()) rerender();
}

// MARK: Export (:599-691)
void MediaAssemblyCanvas::exportPressed(const d::Control& anchor) {
    const auto& session = controller_->session();
    if (session.exporting()) { controller_->cancelExport(); return; }
    const auto& document = session.document();
    if (!document || session.busy() || isExporting()) return;
    std::vector<d::Menu::Item> items(1);
    items[0].id = "saveAs"; items[0].title = document->animated() ? text("Save first frame as…", "将首帧另存为…") : text("Save As…", "另存为…");
    if (document->kind != MediaAssemblyKind::gif) { d::Menu::Item item; item.id = "overwrite"; item.title = text("Overwrite original", "覆盖原文件"); items.push_back(std::move(item)); }
    openMenu("export", std::move(items), anchor, std::nullopt, [this, id = document->id](const d::Menu::Item& item) {
        const auto& current = controller_->session().document();
        if (!current || current->id != id) return;
        if (item.id == "saveAs") saveAs(*current); else confirmOverwrite(*current);
    });
}
void MediaAssemblyCanvas::saveAs(const MediaAssemblyDocumentInfo& document) {
    if (!services_.saveAs) return;
    if (!services_.saveAs(services_.folderOf ? services_.folderOf(document.path) : std::string(), document.suggestedFilename(), controller_->exportExtensions())) return;
    panel_ = Panel{true, document.id};
}
void MediaAssemblyCanvas::confirmOverwrite(const MediaAssemblyDocumentInfo& document) {
    d::ConfirmRequest request;
    request.id = "mediaOverwrite"; request.title = text("Replace the original file?", "覆盖原文件？");
    request.message = document.name; request.confirmTitle = text("Overwrite", "覆盖"); request.danger = true; request.icon = d::Icon::export_;
    confirm(std::move(request), [this, id = document.id, path = document.path] {
        const auto& current = controller_->session().document();
        if (current && current->id == id) startExport(path, true, *current, true);
    });
}
void MediaAssemblyCanvas::startExport(const std::string& path, bool overwrite, const MediaAssemblyDocumentInfo& document, bool original) {
    const auto& session = controller_->session();
    if (!presented_ || session.busy() || isExporting() || !session.document() || session.document()->id != document.id) return;
    exportShown_ = 0; exportOutcome_.reset();
    const auto name = lastPathComponent(path);
    const auto id = document.id;
    controller_->exportTo(path, overwrite, id, now(), [this, name, original](const std::optional<std::string>& target, std::optional<MediaAssemblyError> error) {
        exportFinished(target, error.has_value() || !target, name, original);
    });
    if (!controller_->session().exporting()) exportShown_.reset();
    startExportTicker();
}
void MediaAssemblyCanvas::exportFinished(std::optional<std::string> target, bool failed, std::string name, bool original) {
    if (services_.now) env().now = std::max(env().now, services_.now());
    if (!failed && target) {
        const auto& document = controller_->session().document();
        if (document && services_.samePath && services_.samePath(*target, document->path)) reloadingAfterOverwrite_ = true;
        exportOutcome_ = original ? ExportOutcome::overwritten : ExportOutcome::saved;
        exportName_ = std::move(name);
        if (!exportShown_ || !isActive()) finishExportDisplay(); else startExportTicker();
        return;
    }
    exportShown_.reset(); exportOutcome_.reset(); stopExportTicker();
    if (renderedLanguage()) rerender();
}
// The gauge eases toward the real progress on the demo's 90 ms cadence, so
// even an instant export reads EXPORTING n% before the toast.
void MediaAssemblyCanvas::startExportTicker() {
    if (exportTicker_ || !isActive() || !exportShown_) return;
    exportTicker_ = timerGroup();
    scheduleExportTick();
}
// Repeating (Timer in the common run-loop mode) while the ticker runs.
void MediaAssemblyCanvas::scheduleExportTick() {
    const auto token = exportTicker_;
    after(.09, token, [this, token] {
        if (exportTicker_ != token) return;
        exportTick();
        if (exportTicker_ == token) scheduleExportTick();
    });
}
void MediaAssemblyCanvas::stopExportTicker() { if (exportTicker_) cancel(exportTicker_); exportTicker_ = 0; }
void MediaAssemblyCanvas::exportTick() {
    if (!exportShown_) { stopExportTicker(); return; }
    double shown = *exportShown_;
    const auto& session = controller_->session();
    const double target = session.exporting() ? session.progress() : (exportOutcome_ ? 1 : shown);
    if (env().reduceMotion) shown = std::max(shown, target);
    else if (target > shown) shown = std::min(target, shown + std::max(.07, (target - shown) * .35));
    exportShown_ = shown;
    header().setCounter(counterText());
    stage().setExport(shown, text("Exporting", "正在导出"));
    if (!session.exporting() && exportOutcome_ && shown >= 1) finishExportDisplay();
}
void MediaAssemblyCanvas::finishExportDisplay() {
    stopExportTicker();
    const auto outcome = exportOutcome_;
    exportShown_.reset(); exportOutcome_.reset();
    if (renderedLanguage()) rerender();
    if (!isActive() || !outcome) return;
    if (*outcome == ExportOutcome::saved) toast(d::Icon::export_, "Exported", "已导出", exportName_);
    else toast(d::Icon::export_, "Exported", "已覆盖原文件");
}

// MARK: Stage (:695-794)
MediaAssemblyGeometry MediaAssemblyCanvas::targetGeometry() const {
    return MediaAssemblyGeometry::of(controller_->session().adjustments(), controller_->session().cropPreview());
}
double MediaAssemblyCanvas::duration() const { const auto& document = controller_->session().document(); return document ? document->duration : 0; }
// The size of what the stage shows, before the transition transform.
core::Point MediaAssemblyCanvas::shownSize() const {
    if (stage_ && stage_->image()) return {double(stage_->image()->width), double(stage_->image()->height)};
    const auto& document = controller_->session().document();
    if (!document) return {MediaAssemblyStage::size.width, MediaAssemblyStage::size.height};
    const auto& crop = shownGeometry_.crop;
    const core::Point size{document->pixels.x * crop.width, document->pixels.y * crop.height};
    return shownGeometry_.quarterTurns % 2 == 0 ? size : core::Point{size.y, size.x};
}
// The picture's frame on the stage, as the edits will show it.
core::Point MediaAssemblyCanvas::layoutSize() const {
    const auto size = shownSize();
    return MediaAssemblyPictureTransform::delta(shownGeometry_, targetGeometry()).turns % 2 == 0 ? size : core::Point{size.y, size.x};
}
Rect MediaAssemblyCanvas::imageRect() const { return viewport_.imageRect(layoutSize(), MediaAssemblyStage::bounds); }
// Stickers live in the output (cropped) frame; while the crop tool shows the
// whole picture they sit inside the crop box.
Rect MediaAssemblyCanvas::stickerBase(Rect image) const {
    if (shownGeometry_.crop != MediaAssemblyCrop{} || controller_->session().adjustments().crop == MediaAssemblyCrop{}) return image;
    const auto crop = displayCropRect();
    return {image.x + crop.x * image.width, image.y + crop.y * image.height, crop.width * image.width, crop.height * image.height};
}
Rect MediaAssemblyCanvas::displayCropRect() const {
    const auto& p = controller_->session().adjustments();
    return MediaAssemblyViewport::displayCrop(p.crop, p.rotationQuarterTurns, p.mirrored);
}
const MediaAssemblySticker* MediaAssemblyCanvas::selectedItem() const { return sticker(selectedSticker_); }
const MediaAssemblySticker* MediaAssemblyCanvas::sticker(std::string_view id) const {
    if (id.empty()) return nullptr;
    for (const auto& item : controller_->session().adjustments().stickers) if (item.id == id) return &item;
    return nullptr;
}
// Takes a newly rendered preview, unless a rotate / mirror transition is still
// turning the old one: then it waits for the transition's end.
void MediaAssemblyCanvas::takePreview() {
    const auto preview = controller_->previewImage();
    if (!preview || preview == stage().image()) return;
    const double wait = geometryAnimationEnd_ - now();
    if (wait > .005) {
        if (pendingPreview_) return;
        pendingPreview_ = timerGroup();
        after(wait, pendingPreview_, [this] { pendingPreview_ = 0; layoutStage(); });
        return;
    }
    const bool sameLook = shownGeometry_ == targetGeometry();
    shownGeometry_ = targetGeometry();
    clearOnBuild_ = false;
    stage().setImage(preview, sameLook && !revealOnPreview_);
    if (revealOnPreview_) { revealOnPreview_ = false; stage().setContentOpacity(0, false); stage().setContentOpacity(1, true); }
}
void MediaAssemblyCanvas::releasePreview() noexcept { if (stage_) stage_->releaseImages(); }
// Every stage layer from the current state, in place.
void MediaAssemblyCanvas::layoutStage() {
    if (!renderedLanguage()) return;
    auto& stage = this->stage();
    const auto& session = controller_->session();
    const auto document = session.document();
    stage.setEmpty(!document && !closing_, text("Open an image or video", "打开图片或视频"));
    relabelStageControls();
    if (!document) {
        stage.setStickers({}, {}); stage.setSelection(nullptr, {}); stage.setCrop(std::nullopt, {});
        stage.setExport(std::nullopt, ""); return;
    }
    if (presented_) takePreview();
    const auto delta = MediaAssemblyPictureTransform::delta(shownGeometry_, targetGeometry());
    const auto transform = stage.transform().nearest(delta.turns, delta.reflected);
    const bool animated = animateNextLayout_ && transform != stage.transform() && !env().reduceMotion;
    const auto rect = imageRect();
    stage.layoutPicture(rect, transform, animated);
    if (animated) geometryAnimationEnd_ = now() + .25;
    const auto base = stickerBase(rect);
    const auto& adjustments = session.adjustments();
    stage.setStickers(adjustments.stickers, base);
    stage.setSelection(tool_ == MediaAssemblyTool::crop ? nullptr : selectedItem(), base);
    stage.setCrop(tool_ == MediaAssemblyTool::crop ? std::optional(displayCropRect()) : std::nullopt, rect);
    stage.setChips(document->name, adjustments.filter == MediaAssemblyFilter::none ? std::nullopt : std::optional(mediaAssemblyFilterTitle(adjustments.filter, env().language)),
        adjustments.stickers.size());
    stage.setZoom(viewport_.zoom());
    const bool playing = session.view().playing;
    stage.setPlayer(playing);
    stage.setPlayback(document->video && !isExporting() && tool_ != MediaAssemblyTool::trim, playing, session.currentTime(), document->duration);
    stage.setExport(isExporting() ? std::optional(exportShown_.value_or(session.progress())) : std::nullopt, text("Exporting", "正在导出"));
}
Point MediaAssemblyCanvas::stagePoint(Point point) { return {point.x - stageOrigin.x, point.y - stageOrigin.y}; }
void MediaAssemblyCanvas::zoomStep(double factor) {
    if (!controller_->session().document()) return;
    viewport_.zoomBy(factor, layoutSize(), MediaAssemblyStage::bounds);
    layoutStage();
}
bool MediaAssemblyCanvas::zoom(double factor, std::optional<Point> at) {
    if (!controller_->session().document()) return false;
    if (at) viewport_.magnify(factor, *at, layoutSize(), MediaAssemblyStage::bounds);
    else viewport_.zoomBy(factor, layoutSize(), MediaAssemblyStage::bounds);
    layoutStage();
    return true;
}
void MediaAssemblyCanvas::resetZoom() { viewport_.reset(); layoutStage(); }

// MARK: Stage gestures, box px (:798-900)
bool MediaAssemblyCanvas::handlePointerDown(Point point, const d::PointerInput&) {
    if (!inside(hitBounds(), point)) return false;
    const auto local = stagePoint(point);
    if (!inside(MediaAssemblyStage::bounds, local) || !controller_->session().document() || closing_) return true;
    beginDrag(local);
    if (drag_ && context().onLockParallax) context().onLockParallax();
    return true;
}
void MediaAssemblyCanvas::beginDrag(Point local) {
    const auto rect = imageRect(), base = stickerBase(rect);
    const auto& document = controller_->session().document();
    const bool editable = isEditable() && document && !document->video;
    if (const auto* item = selectedItem(); editable && tool_ != MediaAssemblyTool::crop && item) {
        if (const auto handle = MediaAssemblyViewport::handle(local, MediaAssemblyViewport::stickerRect(*item, base), item->rotation)) {
            switch (handle->kind) {
            case MediaAssemblyHandle::Kind::remove: removeSticker(item->id); break;
            case MediaAssemblyHandle::Kind::rotate: drag_ = Drag{Drag::Kind::rotate, {}, item->id, {}, 0}; break;
            case MediaAssemblyHandle::Kind::corner: drag_ = Drag{Drag::Kind::scale, {}, item->id, {}, 0}; break;
            }
            return;
        }
    }
    if (isEditable() && tool_ == MediaAssemblyTool::crop) {
        const auto origins = MediaAssemblyViewport::cropHandleOrigins(displayCropRect(), rect, {MediaAssemblyStage::size.width, MediaAssemblyStage::size.height});
        for (std::size_t corner = 0; corner < origins.size(); ++corner)
            if (inside({origins[corner].x - 5, origins[corner].y - 5, 24, 24}, local)) { drag_ = Drag{Drag::Kind::crop, {}, {}, {}, int(corner)}; return; }
    }
    if (editable) {
        const auto& stickers = controller_->session().adjustments().stickers;
        for (auto it = stickers.rbegin(); it != stickers.rend(); ++it) {
            const auto frame = MediaAssemblyViewport::stickerRect(*it, base);
            if (!inside(frame, MediaAssemblyViewport::unrotate(local, {frame.x + frame.width / 2, frame.y + frame.height / 2}, it->rotation))) continue;
            const auto item = *it;
            select(item.id);
            drag_ = Drag{Drag::Kind::move, local, item.id, item, 0};
            return;
        }
    }
    if (!selectedSticker_.empty()) select({});
    if (tool_ != MediaAssemblyTool::crop) drag_ = Drag{Drag::Kind::pan, local, {}, {}, 0};
}
void MediaAssemblyCanvas::handlePointerDragged(Point point) {
    if (!drag_) return;
    const auto local = stagePoint(point);
    const auto rect = imageRect(), base = stickerBase(rect);
    auto& drag = *drag_;
    switch (drag.kind) {
    case Drag::Kind::pan:
        viewport_.move({local.x - drag.start.x, local.y - drag.start.y}, layoutSize(), MediaAssemblyStage::bounds);
        drag.start = local;
        layoutStage();
        break;
    case Drag::Kind::move: {
        auto item = drag.original;
        item.x = std::min(1., std::max(0., item.x + (local.x - drag.start.x) / std::max(1., base.width)));
        item.y = std::min(1., std::max(0., item.y + (local.y - drag.start.y) / std::max(1., base.height)));
        replaceSticker(drag.id, item);
        break;
    }
    case Drag::Kind::scale: {
        const auto* found = sticker(drag.id);
        if (!found) return;
        auto item = *found;
        const Point centre{base.x + base.width * item.x, base.y + base.height * item.y};
        item.size = std::min(1., std::max(.05, std::hypot(local.x - centre.x, local.y - centre.y) * std::sqrt(2.) / std::max(1., std::min(base.width, base.height))));
        replaceSticker(drag.id, item);
        break;
    }
    case Drag::Kind::rotate: {
        const auto* found = sticker(drag.id);
        if (!found) return;
        auto item = *found;
        const Point centre{base.x + base.width * item.x, base.y + base.height * item.y};
        item.rotation = std::round(std::atan2(local.y - centre.y, local.x - centre.x) * 180 / std::numbers::pi + 90);
        replaceSticker(drag.id, item);
        break;
    }
    case Drag::Kind::crop: {
        const Point at{(local.x - rect.x) / std::max(1., rect.width), (local.y - rect.y) / std::max(1., rect.height)};
        const auto display = MediaAssemblyViewport::dragCrop(displayCropRect(), drag.corner, at);
        auto value = controller_->session().adjustments();
        value.crop = MediaAssemblyViewport::sourceCrop(display, value.rotationQuarterTurns, value.mirrored);
        updateAdjustments(std::move(value));
        break;
    }
    }
}
void MediaAssemblyCanvas::handlePointerUp(Point) {
    if (!drag_) return;
    drag_.reset();
    if (context().onInputStateChange) context().onInputStateChange();
}
void MediaAssemblyCanvas::handlePointerMoved(std::optional<Point> point) {
    const auto* item = selectedItem();
    if (drag_ || tool_ == MediaAssemblyTool::crop || !point || !item) { stage().setHovered(std::nullopt); return; }
    const auto base = stickerBase(imageRect());
    stage().setHovered(MediaAssemblyViewport::handle(stagePoint(*point), MediaAssemblyViewport::stickerRect(*item, base), item->rotation));
}
// Mouse wheel: x1.1 per notch about the pointer (1-20); touchpad scrolling
// pans, with Alt (the Mac's ⌘ / ⌥) it zooms.
bool MediaAssemblyCanvas::handleScroll(Point point, Point delta) {
    const auto local = stagePoint(point);
    if (!inside(MediaAssemblyStage::bounds, local) || !controller_->session().document()) return false;
    if (wheelPrecise_ && !wheelModifier_) {
        viewport_.move({-delta.x, -delta.y}, layoutSize(), MediaAssemblyStage::bounds);
        layoutStage(); return true;
    }
    if (delta.y == 0) return true;
    const double factor = wheelPrecise_ ? std::exp(-delta.y * .01) : (delta.y < 0 ? 1.1 : 1 / 1.1);
    return zoom(factor, local);
}
bool MediaAssemblyCanvas::handleMagnify(Point point, double magnification) {
    const auto local = stagePoint(point);
    if (!inside(MediaAssemblyStage::bounds, local)) return false;
    return zoom(1 + magnification, local);
}

// MARK: Stickers (:904-931)
void MediaAssemblyCanvas::select(std::string id) { selectedSticker_ = std::move(id); layoutStage(); }
void MediaAssemblyCanvas::replaceSticker(std::string id, MediaAssemblySticker item) {
    auto value = controller_->session().adjustments();
    for (auto& existing : value.stickers) if (existing.id == id) existing = item;
    updateAdjustments(std::move(value));
}
void MediaAssemblyCanvas::removeSticker(std::string id) {
    if (!isEditable()) return;
    if (selectedSticker_ == id) selectedSticker_.clear();
    auto value = controller_->session().adjustments();
    std::erase_if(value.stickers, [&](const auto& item) { return item.id == id; });
    updateAdjustments(std::move(value));
}
// Keyboard / accessibility nudges of the selected sticker.
bool MediaAssemblyCanvas::adjustSelectedSticker(double dx, double dy, double size, double rotation) {
    const auto* found = selectedItem();
    if (!isEditable() || !found) return false;
    auto item = *found;
    item.x = std::min(1., std::max(0., item.x + dx)); item.y = std::min(1., std::max(0., item.y + dy));
    item.size = std::min(1., std::max(.05, item.size + size));
    item.rotation = std::fmod(item.rotation + rotation, 360.);
    replaceSticker(item.id, item);
    return true;
}
void MediaAssemblyCanvas::updateAdjustments(MediaAssemblyAdjustments value) { controller_->update(std::move(value), std::nullopt, now()); }
void MediaAssemblyCanvas::updateTrim(double start, double end, double scrubTo) {
    if (!std::isfinite(scrubTo)) return;
    auto value = controller_->session().adjustments(); value.trimStart = start; value.trimEnd = end;
    controller_->update(std::move(value), scrubTo, now());
}

// MARK: Keyboard (:935-966)
bool MediaAssemblyCanvas::handleKey(const d::KeyInput& key) {
    // ⌘ and ⌃ (Win, Ctrl) are not handled.
    if (key.win || key.control) return false;
    if (key.virtualKey == d::key::escape) {
        if (tool_) { closeTool(); return true; }
        if (!selectedSticker_.empty()) { select({}); return true; }
        return false;
    }
    const auto& session = controller_->session();
    const auto& document = session.document();
    if (!document) return false;
    const bool option = key.alt, shift = key.shift;
    const double step = shift ? .05 : .01;
    if (!selectedSticker_.empty()) {
        switch (key.virtualKey) {
        case d::key::back: case d::key::deleteForward: removeSticker(selectedSticker_); return true;
        case d::key::left: return option ? adjustSelectedSticker(0, 0, 0, shift ? -15 : -3) : adjustSelectedSticker(-step, 0, 0, 0);
        case d::key::right: return option ? adjustSelectedSticker(0, 0, 0, shift ? 15 : 3) : adjustSelectedSticker(step, 0, 0, 0);
        case d::key::down: return option ? adjustSelectedSticker(0, 0, -step, 0) : adjustSelectedSticker(0, step, 0, 0);
        case d::key::up: return option ? adjustSelectedSticker(0, 0, step, 0) : adjustSelectedSticker(0, -step, 0, 0);
        default: break;
        }
    }
    if (key.character == U'+' || key.character == U'=') { zoomStep(1.25); return true; }
    if (key.character == U'-') { zoomStep(.8); return true; }
    const bool plain = !key.shift && !key.alt && !key.control && !key.win;
    if (!document->video || !plain || !isEditable() || tool_ == MediaAssemblyTool::crop) return false;
    switch (key.virtualKey) {
    case d::key::space: controller_->togglePlayback(now()); return true;
    case d::key::left: case d::key::right: controller_->seek(session.currentTime() + (key.virtualKey == d::key::left ? -5 : 5), now()); return true;
    default: return false;
    }
}

// MARK: Accessibility (:970-1029)
bool MediaAssemblyCanvas::perform(std::string_view id, double now) {
    env().now = now;
    settleSwap();
    const bool done = performAccessibilityAction(id, now);
    settleSwap();
    return done;
}
// The selected sticker and the crop box are adjustable without a pointer.
void MediaAssemblyCanvas::accessibilityItems(std::vector<d::AccessibilityItem>& out) const {
    const auto before = out.size();
    Canvas::accessibilityItems(out);
    if (out.size() == before || !isEditable()) return;
    const auto slider = [](std::string id, std::string label, double value, double minimum, double maximum, double step, std::string description, Rect rect) {
        d::AccessibilityItem item;
        item.id = std::move(id); item.label = std::move(label); item.role = d::AccessibilityRole::slider;
        item.number = value; item.minimum = minimum; item.maximum = maximum; item.step = step; item.value = std::move(description); item.rect = rect;
        return item;
    };
    if (tool_ == MediaAssemblyTool::crop) {
        const auto crop = displayCropRect();
        const auto frame = offset(imageRect(), stageOrigin);
        const std::array<std::pair<const char*, double>, 4> edges{{{"X₁", crop.x}, {"Y₁", crop.y}, {"X₂", crop.x + crop.width}, {"Y₂", crop.y + crop.height}}};
        for (std::size_t index = 0; index < edges.size(); ++index)
            out.push_back(slider("crop.edge" + std::to_string(index), text("Crop", "裁剪") + " " + edges[index].first, edges[index].second, 0, 1, .01,
                fixed("%.2f", edges[index].second), frame));
    } else if (const auto* item = selectedItem()) {
        const auto frame = offset(MediaAssemblyViewport::stickerRect(*item, stickerBase(imageRect())), stageOrigin);
        const auto sticker = text("Sticker", "贴纸");
        out.push_back(slider("sticker.x", sticker + " X", item->x, 0, 1, .01, fixed("%.2f", item->x), frame));
        out.push_back(slider("sticker.y", sticker + " Y", item->y, 0, 1, .01, fixed("%.2f", item->y), frame));
        out.push_back(slider("sticker.size", text("Size", "大小"), item->size, .05, 1, .01, fixed("%.2f", item->size), frame));
        out.push_back(slider("sticker.rotation", text("Rotate", "旋转"), item->rotation, -360, 360, 3, fixed("%.0f°", item->rotation), frame));
    }
}
bool MediaAssemblyCanvas::adjustAccessibilitySlider(std::string_view id, double value, double now) {
    env().now = now;
    if (!std::isfinite(value)) return false;
    if (const auto* item = selectedItem(); id.starts_with("sticker.") && item) {
        if (id == "sticker.x") adjustSelectedSticker(value - item->x, 0, 0, 0);
        else if (id == "sticker.y") adjustSelectedSticker(0, value - item->y, 0, 0);
        else if (id == "sticker.size") adjustSelectedSticker(0, 0, value - item->size, 0);
        else adjustSelectedSticker(0, 0, 0, value - item->rotation);
        return true;
    }
    if (id.starts_with("crop.edge") && id.size() == 10 && id[9] >= '0' && id[9] <= '9' && isEditable()) {
        auto crop = displayCropRect();
        const double v = std::min(1., std::max(0., value));
        const double maxX = crop.x + crop.width, maxY = crop.y + crop.height;
        switch (id[9] - '0') {
        case 0: { const double x = std::min(v, maxX - .05); crop = {x, crop.y, maxX - x, crop.height}; break; }
        case 1: { const double y = std::min(v, maxY - .05); crop = {crop.x, y, crop.width, maxY - y}; break; }
        case 2: crop.width = std::max(.05, v - crop.x); break;
        default: crop.height = std::max(.05, v - crop.y); break;
        }
        auto adjustments = controller_->session().adjustments();
        adjustments.crop = MediaAssemblyViewport::sourceCrop(crop, adjustments.rotationQuarterTurns, adjustments.mirrored);
        updateAdjustments(std::move(adjustments));
        return true;
    }
    return Canvas::adjustAccessibilitySlider(id, value, now);
}
} // namespace endfield::modules
