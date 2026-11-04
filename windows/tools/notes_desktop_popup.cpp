// A desktop note's formatting pop-up (see the header). Line references are
// Sources/NotesFormattingControls.swift's unless marked HUDNotesInteraction.
//
// Removed with the v1.2.0 menus (DOCS-246, DOCS-250): NotesRetainedMenu's
// font / size / colour / special menus (242 x 184 and 242 x 48 panels, seven
// 24 px rows, 18 px scroll steps, the 166 px wheel applying on every move, the
// "Current color" swatch) and the retained media source chooser and shelf
// picker (tools/notes_format_menu.*, tools/notes_media_menu.*).
#include "tools/notes_desktop_popup.hpp"
#ifdef _WIN32
#include <algorithm>
#include <cmath>
#include <set>

namespace endfield::tools {
namespace {
namespace d = modules::design;
namespace gpu = native;
using d::Point; using d::Rect;
void advance(d::Environment& env, double time) { if (std::isfinite(time) && time > env.now) env.now = time; }
bool inside(Rect r, Point p) noexcept { return p.x >= r.x && p.y >= r.y && p.x < r.x + r.width && p.y < r.y + r.height; }
std::string sizeTitle(double size) { return std::to_string(static_cast<long long>(std::round(size))); }
}

struct NotesDesktopPopup::Impl {
    struct Open {
        Kind kind; std::string noteID;
        d::LayerArena arena; d::ControlSet set; d::Builder builder;
        d::Layer* chrome;                                // notes.desktopPopup
        d::Layer* panel{};                               // the menu, the pop-over or the style bar
        std::unique_ptr<d::Menu> menu;
        std::unique_ptr<d::Popover> popover;
        std::vector<d::Control*> controls;
        d::Router router;
        gpu::NativeDesignScene scene;
        Point origin;                                    // workspace position of the chrome
        std::uint64_t synced{}; bool posed{};
        std::optional<double> fadeEnd;                   // leaving: composed until then
        Open(Kind k, std::string note, d::Environment& env, gpu::LayerRasterizer& r, const gpu::LayerRasterOptions& o, std::uint64_t serial)
            : kind(k), noteID(std::move(note)), arena("notes.desk." + std::to_string(serial), &env.deadlines), builder{env, arena, set},
              chrome(&arena.make("notes.desktopPopup")),
              scene(r, o, "notes.desk." + std::to_string(serial), gpu::DesignImages{{std::string(modules::notesColorWheelImageKey), modules::notesColorWheelArtwork()}}) {
            chrome->setAnchorPoint({0, 0}); chrome->setAnimatable(); chrome->setPartRoot(true); chrome->setZPosition(2'000'000);
        }
        std::uint64_t revision() const noexcept {
            return arena.revision() + (menu ? menu->layer().arena().revision() << 16 : 0) + (popover ? popover->layer().arena().revision() << 16 : 0);
        }
        d::Size size() const noexcept { return panel ? panel->bounds() : d::Size{}; }
    };
    gpu::LayerRasterizer& raster; gpu::LayerRasterOptions options;
    d::Environment* env{};
    NotesDesktopPopup& owner;
    std::unique_ptr<Open> current;
    std::vector<std::unique_ptr<Open>> retiring;
    std::vector<gpu::LayerCompositionEntry> composed;
    std::uint64_t serial{}, revision{};
    Impl(gpu::LayerRasterizer& r, gpu::LayerRasterOptions o, NotesDesktopPopup& p) : raster(r), options(std::move(o)), owner(p) {}
    // layoutFormatting (HUDNotesInteraction :425-438): at the note's
    // bottom-left, 4 px below it, clamped into the workspace.
    static void place(Open& o, core::Rect note, core::Rect ws) {
        const auto s = o.size();
        o.origin = {std::min(ws.x + ws.width - s.width, std::max(ws.x, note.x)), std::min(ws.y + ws.height - s.height, std::max(ws.y, note.y + note.height + 4))};
    }
    void finish(Open& o, core::Rect note, core::Rect ws, double time) {
        // The panel at the chrome's origin (the chrome takes the workspace position).
        const auto size = o.panel->bounds();
        o.panel->setFrame({0, 0, size.width, size.height});
        o.chrome->add(*o.panel);
        o.router.set(o.controls);
        place(o, note, ws);
        // playEntrance (:585): the redesign's top-down wipe.
        if (o.menu) o.menu->playEntrance(env->clock());
        else if (o.popover) o.popover->playEntrance(env->clock());
        else { o.panel->setAnimatable(); d::motion::wipe(*o.panel, d::WipeOrigin::top, d::duration::menuWipe, env->clock(), d::easing::standard, 0, .4); }
        (void)time;
        ++revision;
    }
    // dismiss (HUDNotesInteraction :442-455): input stops at once, the
    // artwork fades to 0 over .14 s ease (at once under Reduce Motion).
    void retire(double time) {
        if (!current) return;
        auto& o = *current;
        o.router.cancel();
        for (d::Control* c : o.controls) { if (auto* slider = dynamic_cast<d::Slider*>(c)) slider->cancelDrag(); c->clearInteractionState(); }
        if (!env->reduceMotion) {
            d::motion::animate(*o.chrome, d::Channel::opacity, 0, .14, env->clock(), d::easing::ease);
            o.fadeEnd = time + .14;
        } else o.fadeEnd = time;
        retiring.push_back(std::move(current));
        ++revision;
    }
    void pick(const std::string& value) {
        if (!current) return;
        if (current->kind == Kind::font) { core::notes::FormatChange c; c.kind = core::notes::FormatKind::font; c.fontName = value; if (owner.onChange) owner.onChange(c); }
        else if (current->kind == Kind::size) {
            char* end{}; const double size = std::strtod(value.c_str(), &end);
            if (end != value.c_str() && std::isfinite(size)) { core::notes::FormatChange c; c.kind = core::notes::FormatKind::size; c.fontSize = size; if (owner.onChange) owner.onChange(c); }
        } else if (owner.onPick) owner.onPick(value);
        if (owner.onClose) owner.onClose();
    }
    d::Control* control(std::string_view id) const {
        if (!current) return nullptr;
        for (d::Control* c : current->controls) if (c->id() == id) return c;
        return nullptr;
    }
};

NotesDesktopPopup::NotesDesktopPopup(gpu::LayerRasterizer& r, gpu::LayerRasterOptions o) : impl_(std::make_unique<Impl>(r, std::move(o), *this)) {}
NotesDesktopPopup::~NotesDesktopPopup() = default;
void NotesDesktopPopup::setEnvironment(d::Environment* env) { impl_->env = env; }

// NotesDesktopPopup.init (:496-553).
void NotesDesktopPopup::openFormat(Format f, core::Rect note, core::Rect ws, double time) {
    auto& i = *impl_;
    if (!i.env) return;
    advance(*i.env, time);
    i.retire(time);
    auto& env = *i.env;
    auto o = std::make_unique<Impl::Open>(f.kind, std::move(f.noteID), env, i.raster, i.options, ++i.serial);
    auto* raw = o.get();
    const auto close = [&i] { if (i.owner.onClose) i.owner.onClose(); };
    switch (f.kind) {
    case Kind::font: case Kind::size: {
        std::vector<std::string> values;
        std::string selected;
        if (f.kind == Kind::font) { values = std::move(f.families); selected = std::move(f.selectedFamily); }
        else {
            std::set<int> unique(sizes.begin(), sizes.end());
            unique.insert(int(std::round(f.style.fontSize)));
            for (const int v : unique) values.push_back(std::to_string(v));
            selected = sizeTitle(f.style.fontSize);
        }
        std::vector<d::Menu::Item> items;
        for (const auto& v : values) { d::Menu::Item item; item.id = item.title = v; item.selected = v == selected; items.push_back(std::move(item)); }
        o->menu = std::make_unique<d::Menu>(env, f.kind == Kind::font ? "notes.desk.font" : "notes.desk.size", std::move(items), f.kind == Kind::font ? 200 : 96, o->arena.token());
        if (const auto at = std::find(values.begin(), values.end(), selected); at != values.end())
            o->menu->setScroll(double(at - values.begin() - 3) * d::Menu::rowHeight);
        o->menu->onPick = [&i, raw](const d::Menu::Item& item) { if (i.current.get() == raw) i.pick(item.id); };
        o->menu->onClose = [&i, raw, close] { if (i.current.get() == raw) close(); };
        o->panel = &o->menu->layer();
        o->controls = o->menu->controls();
        break;
    }
    case Kind::color: {
        auto picker = modules::notesColorPicker(env, "notes.desk.color", std::move(f.colorTitle), f.color, o->chrome, [&i, raw](std::uint32_t hex) {
            if (i.current.get() != raw || !i.owner.onChange) return;
            core::notes::FormatChange c; c.kind = core::notes::FormatKind::color; c.color = modules::notesFormat::rgba(hex); i.owner.onChange(c);
        });
        o->popover = std::move(picker.popover);
        o->popover->onClose = [&i, raw, close] { if (i.current.get() == raw) close(); };
        o->controls = std::move(picker.controls);
        if (auto* x = o->popover->closeButton()) o->controls.push_back(x);
        o->panel = &o->popover->layer();
        break;
    }
    case Kind::special: case Kind::menu: {
        // The style bar (:523-543).
        o->panel = &o->builder.layer("notes.styleBar");
        constexpr double side = buttonSide, pitch = buttonPitch, inset = (pitch - side) / 2 + 2;
        const auto t = [&](std::string_view en, std::string_view zh) { return core::localized(en, zh, env.language); };
        struct Trait { modules::NotesFormatToggle::Glyph glyph; std::string label; bool on; core::notes::FormatKind kind; };
        const std::array<Trait, 4> traits{{{modules::NotesFormatToggle::Glyph::bold, t("Bold", "粗体"), f.style.bold, core::notes::FormatKind::bold},
            {modules::NotesFormatToggle::Glyph::italic, t("Italic", "斜体"), f.style.italic, core::notes::FormatKind::italic},
            {modules::NotesFormatToggle::Glyph::underline, t("Underline", "下划线"), f.style.underline, core::notes::FormatKind::underline},
            {modules::NotesFormatToggle::Glyph::strikethrough, t("Strikethrough", "删除线"), f.style.strikethrough, core::notes::FormatKind::strikethrough}}};
        d::applyPanelChrome(o->builder, *o->panel, {inset * 2 + 4 * pitch + side, inset * 2 + side});
        for (std::size_t n = 0; n < traits.size(); ++n) {
            auto& toggle = o->builder.make<modules::NotesFormatToggle>("notes.desk.trait:" + std::to_string(n), traits[n].label, traits[n].glyph, traits[n].on, d::Size{side, side});
            const auto kind = traits[n].kind;
            auto* pointer = &toggle;
            toggle.onActivate = [&i, raw, pointer, kind] {
                pointer->setOn(!pointer->isOn());
                if (i.current.get() == raw && i.owner.onChange) { core::notes::FormatChange c; c.kind = kind; i.owner.onChange(c); }
            };
            o->controls.push_back(&toggle);
        }
        auto& x = o->builder.make<d::IconButton>("notes.desk.close", t("Close", "关闭"), d::Icon::x, d::IconButton::Style::dark, false, false, side, 9);
        x.onActivate = [&i, raw, close] { if (i.current.get() == raw) close(); };
        o->controls.push_back(&x);
        for (std::size_t n = 0; n < o->controls.size(); ++n) { o->controls[n]->layer().setFrame({inset + double(n) * pitch, inset, side, side}); o->panel->add(o->controls[n]->layer()); }
        break;
    }
    }
    i.current = std::move(o);
    i.finish(*i.current, note, ws, time);
}
// The Windows relink sources and the File Shelf list: the same menu.
void NotesDesktopPopup::openMenu(std::string noteID, std::string id, std::vector<d::Menu::Item> items, double minimumWidth, core::Rect note, core::Rect ws, double time) {
    auto& i = *impl_;
    if (!i.env) return;
    advance(*i.env, time);
    i.retire(time);
    auto o = std::make_unique<Impl::Open>(Kind::menu, std::move(noteID), *i.env, i.raster, i.options, ++i.serial);
    auto* raw = o.get();
    o->menu = std::make_unique<d::Menu>(*i.env, std::move(id), std::move(items), minimumWidth, o->arena.token());
    o->menu->onPick = [&i, raw](const d::Menu::Item& item) { if (i.current.get() == raw) i.pick(item.id); };
    o->menu->onClose = [&i, raw] { if (i.current.get() == raw && i.owner.onClose) i.owner.onClose(); };
    o->panel = &o->menu->layer();
    o->controls = o->menu->controls();
    i.current = std::move(o);
    i.finish(*i.current, note, ws, time);
}
void NotesDesktopPopup::close(double time) { auto& i = *impl_; if (!i.env || !i.current) return; advance(*i.env, time); i.retire(time); }
bool NotesDesktopPopup::open() const noexcept { return impl_->current != nullptr; }
std::optional<NotesDesktopPopup::Kind> NotesDesktopPopup::kind() const noexcept { return impl_->current ? std::optional(impl_->current->kind) : std::nullopt; }
const std::string& NotesDesktopPopup::noteID() const noexcept { static const std::string none; return impl_->current ? impl_->current->noteID : none; }
void NotesDesktopPopup::place(core::Rect note, core::Rect ws) { if (impl_->current) Impl::place(*impl_->current, note, ws); }

bool NotesDesktopPopup::contains(core::Point p) const noexcept {
    const auto* o = impl_->current.get();
    if (!o) return false;
    const auto s = o->size();
    return inside({o->origin.x, o->origin.y, s.width, s.height}, {p.x, p.y});
}
// mouseDown / mouseDragged / mouseUp (:609-622): the router and the highlight pass.
bool NotesDesktopPopup::pointerDown(core::Point p, double time) {
    auto& i = *impl_;
    if (!i.current || !contains(p)) return false;
    advance(*i.env, time);
    auto& o = *i.current;
    const Point local{p.x - o.origin.x, p.y - o.origin.y};
    o.router.pointerDown(local, *o.chrome);
    if (i.current.get() == &o) d::updateHighlights(*o.chrome, local, true);
    return true;
}
void NotesDesktopPopup::pointerDragged(core::Point p, double time) {
    auto& i = *impl_;
    if (!i.current) return;
    advance(*i.env, time);
    auto& o = *i.current;
    o.router.pointerDragged({p.x - o.origin.x, p.y - o.origin.y}, *o.chrome);
}
bool NotesDesktopPopup::pointerUp(core::Point p, double time) {
    auto& i = *impl_;
    if (!i.current) return false;
    advance(*i.env, time);
    auto& o = *i.current;
    const bool tracked = o.router.tracking();
    const Point local{p.x - o.origin.x, p.y - o.origin.y};
    d::updateHighlights(*o.chrome, local, false);
    o.router.pointerUp(local, *o.chrome);   // may close (and retire) this pop-up
    return tracked;
}
void NotesDesktopPopup::hover(std::optional<core::Point> p, bool pressed, double time) {
    auto& i = *impl_;
    if (!i.current) return;
    advance(*i.env, time);
    auto& o = *i.current;
    d::updateHighlights(*o.chrome, p ? std::optional<Point>(Point{p->x - o.origin.x, p->y - o.origin.y}) : std::nullopt, pressed && p.has_value());
}
bool NotesDesktopPopup::tracking() const noexcept { return impl_->current && impl_->current->router.tracking(); }
// scrollWheel (:623-625).
bool NotesDesktopPopup::wheel(double delta, double time) {
    auto& i = *impl_;
    if (!i.current) return false;
    advance(*i.env, time);
    if (i.current->menu) i.current->menu->scroll(delta);
    return true;
}
// keyDown (:626-630): the menu's keys; Esc closes the others.
bool NotesDesktopPopup::key(const d::KeyInput& k, double time) {
    auto& i = *impl_;
    if (!i.current) return false;
    advance(*i.env, time);
    if (i.current->menu) return i.current->menu->handleKey(k);
    if (k.virtualKey == d::key::escape) { if (i.owner.onClose) i.owner.onClose(); return true; }
    return false;
}

void NotesDesktopPopup::pose(const core::Matrix4& world, float opacity, double time) {
    auto& i = *impl_;
    const auto poseOne = [&](Impl::Open& o) {
        const auto key = o.revision();
        if (o.synced != key) { if (o.scene.syncContent(*o.chrome, i.env->text, key, i.options.pixelsPerPoint, time)) ++i.revision; o.synced = key; }
        o.scene.updatePose({world * core::Matrix4::translation(o.origin.x, o.origin.y), opacity, time, {}, {}});
        o.posed = true;
    };
    for (auto& o : i.retiring) if (o->fadeEnd && time < *o->fadeEnd) poseOne(*o); else if (o->posed) { o->posed = false; ++i.revision; }
    if (i.current) poseOne(*i.current);
}
bool NotesDesktopPopup::requiresFrames(double time) const {
    const auto& i = *impl_;
    for (const auto& o : i.retiring) if (o->posed && o->fadeEnd && time < *o->fadeEnd) return true;
    if (!i.current || !i.current->posed) return false;
    const auto busy = [&](const d::Layer& l, const auto& self) -> bool {
        if (l.animating(time)) return true;
        for (const d::Layer* c : l.children()) if (self(*c, self)) return true;
        return false;
    };
    return busy(*i.current->chrome, busy) || i.current->scene.requiresFrames(time);
}
std::span<const gpu::LayerCompositionEntry> NotesDesktopPopup::entries() const noexcept {
    auto& i = *impl_;
    i.composed.clear();
    for (const auto& o : i.retiring) if (o->posed) for (const auto& e : o->scene.entries()) i.composed.push_back(e);
    if (i.current && i.current->posed) for (const auto& e : i.current->scene.entries()) i.composed.push_back(e);
    return i.composed;
}
std::uint64_t NotesDesktopPopup::compositionRevision() const noexcept { return impl_->revision; }
void NotesDesktopPopup::upload(gpu::Renderer& r) {
    auto& i = *impl_;
    for (auto& o : i.retiring) if (o->posed && o->scene.uploadAnimations(r)) ++i.revision;
    if (i.current && i.current->posed && i.current->scene.uploadAnimations(r)) ++i.revision;
}
void NotesDesktopPopup::collected(gpu::Renderer& r) {
    auto& i = *impl_;
    if (i.current) (void)i.current->scene.collectRetired(r);
    for (auto it = i.retiring.begin(); it != i.retiring.end();) {
        auto& o = **it;
        (void)o.scene.collectRetired(r);
        if (!o.posed && o.scene.releaseResources(r)) it = i.retiring.erase(it); else ++it;
    }
}
bool NotesDesktopPopup::release(gpu::Renderer& r) {
    auto& i = *impl_;
    bool done = true;
    if (i.current) done = i.current->scene.releaseResources(r) && done;
    for (auto& o : i.retiring) done = o->scene.releaseResources(r) && done;
    return done;
}

// HUDAccessibilityStandIns (:563-575): perform activates, adjust sets the slider.
void NotesDesktopPopup::automationElements(std::vector<gpu::NotesAutomationElement>& out, const std::function<std::optional<core::Point>(core::Point)>& project) const {
    const auto* o = impl_->current.get();
    if (!o) return;
    std::vector<d::AccessibilityItem> items;
    o->router.accessibilityItems(*o->chrome, items);
    for (const auto& item : items) {
        const core::Rect local{item.rect.x + o->origin.x, item.rect.y + o->origin.y, item.rect.width, item.rect.height};
        const auto bounds = gpu::notesAutomationBounds(local, project);
        if (!bounds) continue;
        gpu::NotesAutomationElement e;
        e.id = "notes.desk:" + item.id; e.label = item.label; e.help = item.help; e.rect = *bounds; e.enabled = item.enabled; e.selected = item.on;
        if (item.role == d::AccessibilityRole::slider) {
            e.role = gpu::NotesAutomationRole::slider; e.value = item.number; e.minimum = item.minimum; e.maximum = item.maximum; e.smallChange = e.largeChange = item.step;
        }
        out.push_back(std::move(e));
    }
}
bool NotesDesktopPopup::perform(std::string_view id, double time) {
    auto& i = *impl_;
    if (!id.starts_with("notes.desk:") || !i.current) return false;
    advance(*i.env, time);
    auto* c = i.control(id.substr(11));
    if (!c || !c->enabled()) return false;
    c->activate();
    return true;
}
bool NotesDesktopPopup::setValue(std::string_view id, double value, double time) {
    auto& i = *impl_;
    if (!id.starts_with("notes.desk:") || !i.current || !std::isfinite(value)) return false;
    advance(*i.env, time);
    auto* slider = dynamic_cast<d::Slider*>(i.control(id.substr(11)));
    if (!slider) return false;
    slider->setValue(value);
    if (slider->onChange) slider->onChange(slider->value(), true);
    return true;
}
} // namespace endfield::tools
#endif
