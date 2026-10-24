#include "modules/projection_canvas.hpp"
#include "modules/notes_format_bar.hpp"
#include <cmath>
#include <cstdio>

namespace endfield::modules {
namespace d = design;
namespace {
std::string hexName(std::uint32_t hex) { char s[8]; std::snprintf(s, sizeof(s), "%06X", unsigned(hex & 0xFFFFFFu)); return s; }
}

ProjectionCanvas::ProjectionCanvas(d::Context& context, ProjectionPenSettings& settings, std::function<void()> onOpen)
    : Canvas(core::Module::projection, context), settings_(&settings), onOpen_(std::move(onOpen)) {}

// didActivate (:20-23): the page shows the workspace's live values.
void ProjectionCanvas::didActivate() {
    settings_->capture();
    refresh();
}
std::string ProjectionCanvas::counter() const { return settings_->values().background ? "BG ON" : "BG OFF"; }
void ProjectionCanvas::refresh() {
    for (const auto& binder : std::vector(binders_)) binder();
    if (renderedLanguage()) header().setCounter(counter());
}
void ProjectionCanvas::change(const std::function<void(ProjectionPenSettings::Values&)>& update) {
    settings_->update(update);
    refresh();
}

// --- Page (:39-99) ---------------------------------------------------------------
void ProjectionCanvas::buildPage(d::Page& page) {
    binders_.clear();
    // The first palette's theme colour is the untouched pen's colour (ProjectionSettings.themeColor).
    settings_->setThemeColor(notesFormat::hex(env().palette.yellow()));
    const double width = d::metric::boxWidth;
    double y = d::metric::headerHeight;
    auto& preview = makePreview(page);
    preview.setOrigin({0, y});
    page.layer().add(preview);
    page.stagger(preview);
    y += previewSize.height + 10;

    auto& toolbar = makeToolbar(page);
    toolbar.setOrigin({0, y});
    page.layer().add(toolbar);
    page.stagger(toolbar);
    y += toolbar.bounds().height + 10;

    auto& widthRow = page.make<d::RowPlate>("row:penWidth", text("Pen width", "画笔粗细"), std::nullopt, d::RowPlate::Variant::slider, "›", width);
    const auto area = widthRow.controlArea();
    auto& slider = page.make<d::Slider>("penWidth", text("Pen width", "画笔粗细"), std::min(ProjectionPenSettings::maximumWidth, settings_->values().width),
        d::Slider::Range{ProjectionPenSettings::minimumWidth, ProjectionPenSettings::maximumWidth, 1, std::nullopt}, d::ValueFormat::pixels, area.width);
    slider.layer().setOrigin({area.x, area.y});
    widthRow.layer().add(slider.layer());
    hostBubble(slider);
    slider.onChange = [this](double value, bool final) {
        if (final) {
            change([value](ProjectionPenSettings::Values& v) { v.width = value; });
            if (context().onInputStateChange) context().onInputStateChange();
        } else if (strokeWidthChanged_) strokeWidthChanged_(value);
    };
    binders_.push_back([this, &slider] { if (!slider.dragging()) slider.setValue(settings_->values().width); });
    page.add(widthRow); page.add(slider);

    auto& backgroundRow = page.make<d::RowPlate>("row:dotted", text("Dotted background", "点阵背景"), std::nullopt, d::RowPlate::Variant::standard, "›", width);
    auto& toggle = page.make<d::Toggle>("dotted", text("Dotted background", "点阵背景"), settings_->values().background);
    toggle.onChange = [this](bool on) { change([on](ProjectionPenSettings::Values& v) { v.background = on; }); };
    binders_.push_back([this, &toggle] { toggle.setOn(settings_->values().background, true); });
    backgroundRow.setCluster({&toggle.layer()});
    page.add(backgroundRow); page.add(toggle);

    for (d::RowPlate* row : {&widthRow, &backgroundRow}) {
        row->layer().setOrigin({0, y});
        page.layer().add(row->layer());
        page.stagger(row->layer());
        y += row->layer().bounds().height + d::metric::rowGap;
    }

    auto& open = page.make<d::Button>("open", text("Open projection", "打开投影"), d::Icon::ext, d::Button::Kind::primary, d::Button::Height::large);
    open.place({std::round((width - open.preferredWidth()) / 2), y - d::metric::rowGap + 12});
    open.onActivate = [this] {
        if (onOpen_) onOpen_();
        toast(d::Icon::ext, "Projection", "投影已打开");
    };
    page.layer().add(open.layer());
    page.add(open); page.stagger(open.layer());

    header().configure({"PROJECTION"}, "MOD-09", title("Projection", "投影"), counter(), false);
}

// The workspace background (dots, or the checkerboard when it is off) with
// a sample stroke in the pen colour and width (:102-133). The dots and the
// stroke are their own surfaces: Background toggles only hide the dots, a
// width drag repaints only the stroke.
d::Layer& ProjectionCanvas::makePreview(d::Page& page) {
    const auto size = previewSize;
    const d::Rect bounds{0, 0, size.width, size.height};
    const auto& p = env().palette;
    auto& root = page.makeLayer("projection.preview");
    root.setBounds(size);
    root.setMasksToBounds(true);
    auto& checker = page.makeLayer("projection.checker");
    checker.setFrame(bounds); d::setBackground(checker, d::rgb(0x1C1C1C));
    auto& checkerTiles = page.makeLayer("projection.checker.tiles");
    checkerTiles.setFrame(bounds); d::setPath(checkerTiles, checkerPath(size), d::rgb(0x232323));
    checker.add(checkerTiles);
    auto& dots = page.makeLayer("projection.dots");
    dots.setFrame(bounds); dots.setAnimatable();
    d::setBackground(dots, d::rgb(0x141414));
    auto& dotMarks = page.makeLayer("projection.dots.marks");
    dotMarks.setFrame(bounds); d::setPath(dotMarks, dotPath(size), d::rgb(0x555555));
    dots.add(dotMarks);
    auto& stroke = page.makeLayer("projection.stroke");
    stroke.setFrame(bounds); stroke.setAnimatable();
    // CALayer draws its border above its sublayers: a ring layer on top.
    auto& ring = page.makeLayer("projection.preview.ring");
    ring.setFrame(bounds); d::setBorder(ring, std::nullopt, p.token(d::ThemeToken::ringField));
    root.add(checker); root.add(dots); root.add(stroke); root.add(ring);
    const auto render = [this, &dots, &stroke, bounds](double width) {
        const auto& values = settings_->values();
        dots.setHidden(!values.background);
        d::StrokeStyle style; style.width = width / 4 + 1; style.cap = "round"; style.join = "round";
        stroke.setContent(d::shape(stroke.id(), bounds, samplePath(), std::nullopt, d::rgb(values.color), style));
    };
    render(settings_->values().width);
    binders_.push_back([this, render] { render(settings_->values().width); });
    strokeWidthChanged_ = render;
    return root;
}

// The pen toolbar (spec 3.14 item 2, :135-194): swatches, divider, tools, Clear all.
d::Layer& ProjectionCanvas::makeToolbar(d::Page& page) {
    const auto& p = env().palette;
    const double height = 42;
    auto& bar = page.makeLayer("projection.toolbar");
    bar.setBounds({d::metric::boxWidth, height});
    d::setBorder(bar, p.token(d::ThemeToken::panel), p.token(d::ThemeToken::ringField));
    double x = 6;
    for (const auto hex : settings_->palette()) {
        const auto name = hexName(hex);
        auto& swatch = page.make<d::Swatch>("color:" + name, text("Pen color", "画笔颜色") + " #" + name, d::rgb(hex), 22.0);
        swatch.onActivate = [this, hex] { change([hex](ProjectionPenSettings::Values& v) { v.color = hex; v.erasing = false; }); };
        binders_.push_back([this, &swatch, hex] { swatch.setSelected(settings_->values().color == hex); });
        swatch.layer().setOrigin({x, (height - 22) / 2});
        bar.add(swatch.layer());
        page.add(swatch);
        x += 22 + 6;
    }
    // Any other colour: the shared custom-colour pop-over (wheel + brightness).
    auto& custom = page.make<d::Swatch>("color:custom", text("Custom color", "自定义颜色"), std::nullopt, 22.0);
    custom.onActivate = [this, &custom] { openColorPicker(custom); };
    binders_.push_back([this, &custom] {
        const auto palette = settings_->palette();
        custom.setSelected(std::find(palette.begin(), palette.end(), settings_->values().color) == palette.end());
    });
    custom.layer().setOrigin({x, (height - 22) / 2});
    bar.add(custom.layer());
    page.add(custom);
    x += 22 + 6;
    auto& divider = page.makeLayer("projection.divider");
    divider.setFrame({x + 4, (height - 22) / 2, 1, 22});
    d::setBackground(divider, p.token(d::ThemeToken::ringIcon));
    bar.add(divider);
    x += 4 + 1 + 4 + 6;
    struct Tool { const char* id; std::string label; d::Icon icon; std::function<bool(const ProjectionPenSettings::Values&)> on; std::function<void()> action; };
    const std::array<Tool, 4> tools{{
        {"pen", text("Pen", "画笔"), d::Icon::pen, [](const auto& v) { return !v.erasing; }, [this] { change([](auto& v) { v.erasing = false; }); }},
        {"eraser", text("Eraser", "橡皮擦"), d::Icon::eraser, [](const auto& v) { return v.erasing; }, [this] { change([](auto& v) { v.erasing = true; }); }},
        {"background", text("Background", "背景"), d::Icon::grid, [](const auto& v) { return v.background; }, [this] { change([](auto& v) { v.background = !v.background; }); }},
        {"media", text("Insert media", "插入媒体"), d::Icon::image, [](const auto&) { return false; }, [this] { insertMedia(); }}}};
    for (const auto& tool : tools) {
        auto& button = page.make<d::IconButton>(tool.id, tool.label, tool.icon);
        button.onActivate = tool.action;
        binders_.push_back([this, &button, on = tool.on] { button.setOn(on(settings_->values())); });
        button.layer().setOrigin({x, (height - 30) / 2});
        bar.add(button.layer());
        page.add(button);
        x += 30 + 6;
    }
    auto& clear = page.make<d::Button>("clear", text("Clear all", "全部清除"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    clear.place({d::metric::boxWidth - 6 - clear.preferredWidth(), (height - 26) / 2});
    clear.onActivate = [this] { confirmClear(); };
    binders_.push_back([this, &clear] { clear.setEnabled(settings_->hasDrawing()); });
    bar.add(clear.layer());
    page.add(clear);
    for (const auto& binder : std::vector(binders_)) binder();
    return bar;
}

// :196-203: a second press while any pop-over is open closes it; each
// committed pick sets the colour and returns to the pen.
void ProjectionCanvas::openColorPicker(const d::Control& anchor) {
    if (popoverOpen()) { closePopover(); return; }
    auto picker = notesColorPicker(env(), "penColor", text("PEN COLOR", "画笔颜色"), settings_->values().color, &top(), [this](std::uint32_t hex) {
        change([hex](ProjectionPenSettings::Values& v) { v.color = hex; v.erasing = false; });
    });
    openPopover(std::move(picker.popover), &anchor, std::nullopt, std::move(picker.controls));
}
void ProjectionCanvas::insertMedia() {
    settings_->requestMediaInsert();
    if (onOpen_) onOpen_();
}
// :210-219: the session drawing is cleared on the retained model (the
// background and the pen stay), also while the workspace is closed.
void ProjectionCanvas::confirmClear() {
    d::ConfirmRequest request;
    request.id = "projectionClear";
    request.title = text("Clear the projection canvas?", "清空投影画布？");
    request.message = text("Source files stay intact.", "源文件保持不变。");
    request.confirmTitle = text("Clear all", "全部清除");
    request.danger = true; request.icon = d::Icon::trash;
    confirm(std::move(request), [this] {
        settings_->clearDrawing();
        refresh();
        toast(d::Icon::trash, "Canvas cleared", "画布已清空");
    });
}

// --- Artwork (:221-259) -----------------------------------------------------------------
// `M40 110 C120 20, 200 140, 290 60 S 450 40, 540 100` in the 590 x 150 space.
d::PathBuilder ProjectionCanvas::samplePath() {
    return d::PathBuilder().move({40, 110}).cubic({120, 20}, {200, 140}, {290, 60}).cubic({380, -20}, {450, 40}, {540, 100});
}
// radial-gradient(circle, #555 1px, transparent 1.5px) 0 0/14px 14px.
d::PathBuilder ProjectionCanvas::dotPath(d::Size size) {
    d::PathBuilder path;
    for (double y = 7; y < size.height; y += 14)
        for (double x = 7; x < size.width; x += 14) path.ellipse({x - 1.25, y - 1.25, 2.5, 2.5});
    return path;
}
// repeating-conic-gradient(#1c1c1c 0 25%, #232323 0 50%) 0 0/20px 20px: the
// top-left and bottom-right quarter of every 20 px tile are lighter.
d::PathBuilder ProjectionCanvas::checkerPath(d::Size size) {
    d::PathBuilder path;
    for (double y = 0; y < size.height; y += 20)
        for (double x = 0; x < size.width; x += 20) path.rect({x, y, 10, 10}).rect({x + 10, y + 10, 10, 10});
    return path;
}
} // namespace endfield::modules
