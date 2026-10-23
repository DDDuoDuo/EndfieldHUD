#include "modules/clipboard_canvas.hpp"
#include "modules/hud_design_badges.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <map>

namespace endfield::modules {
namespace d = design;
namespace {
constexpr double cardPitch = d::ListCard::height + 6;
bool space(char c) noexcept { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\v' || c == '\f'; }
std::string trimmed(std::string_view s) {
    while (!s.empty() && space(s.front())) s.remove_prefix(1);
    while (!s.empty() && space(s.back())) s.remove_suffix(1);
    return std::string(s);
}
// URL(string:)?.host of the trimmed preview (:324): the authority's host
// after "scheme://", without user info, port or IPv6 brackets; nil without
// an authority. The preview never holds inner whitespace for a detected link.
std::optional<std::string> host(std::string_view text) {
    const std::string value = trimmed(text);
    const auto colon = value.find(':');
    if (colon == std::string::npos || colon == 0 || !std::isalpha(static_cast<unsigned char>(value[0]))) return std::nullopt;
    for (std::size_t n = 1; n < colon; ++n) {
        const char c = value[n];
        if (!std::isalnum(static_cast<unsigned char>(c)) && c != '+' && c != '-' && c != '.') return std::nullopt;
    }
    if (value.compare(colon + 1, 2, "//") != 0) return std::nullopt;
    std::string_view authority(value);
    authority.remove_prefix(colon + 3);
    authority = authority.substr(0, authority.find_first_of("/?#"));
    if (const auto at = authority.rfind('@'); at != std::string_view::npos) authority.remove_prefix(at + 1);
    std::string_view result;
    if (!authority.empty() && authority.front() == '[') {
        const auto close = authority.find(']');
        if (close == std::string_view::npos) return std::nullopt;
        result = authority.substr(1, close - 1);
    } else result = authority.substr(0, authority.find(':'));
    if (result.empty()) return std::nullopt;
    return std::string(result);
}
std::string number(std::int64_t v) { return std::to_string(v); }
}

ClipboardCanvas::ClipboardCanvas(d::Context& context, ClipboardActions actions, std::function<double()> wallClock)
    : Canvas(core::Module::clipboard, context), actions_(std::move(actions)), wallClock_(std::move(wallClock)) {
    if (actions_.snapshot) snapshot_ = actions_.snapshot();
    lastStatus_ = snapshot_.status;
    ageTimer_ = timerGroup();
}

// --- Lifecycle (:65-81) -------------------------------------------------------------
void ClipboardCanvas::didActivate() {
    lastStatus_ = snapshot_.status;
    if (!renderedStructure_ || structure() != *renderedStructure_) rerender(); else refreshAges();
    scheduleAges();
}
// The Mac's 30 s repeating Timer (tolerance 5 s) while active.
void ClipboardCanvas::scheduleAges() {
    cancel(ageTimer_);
    after(30, ageTimer_, [this] { refreshAges(); scheduleAges(); });
}
void ClipboardCanvas::didDeactivate() { cancel(ageTimer_); }
void ClipboardCanvas::recordingModeChanged() {
    Canvas::recordingModeChanged();
    // `root.superlayer != nil`: the canvas is the shown module.
    if (env().recordingMode && isActive()) {
        d::Layer* layers[]{&page().layer()};
        d::redaction::playEntrance(layers, clock());
    }
}

ClipboardCanvas::Structure ClipboardCanvas::structure() const {
    Structure s;
    for (const auto* record : ordered()) { s.records.push_back(record->id); s.pins.push_back(record->pinned); }
    for (const auto* record : visibleItems()) s.visible.push_back(record->id);
    s.capacity = snapshot_.capacity; s.filter = filter_;
    return s;
}
// Pinned records first; each group keeps the store's order (newest capture
// first), so unpinning returns a record to its place (:91).
std::vector<const ClipboardRecord*> ClipboardCanvas::ordered() const {
    std::vector<const ClipboardRecord*> out;
    for (const auto& r : snapshot_.items) if (r.pinned) out.push_back(&r);
    for (const auto& r : snapshot_.items) if (!r.pinned) out.push_back(&r);
    return out;
}
// The records under the current filter, in list order (:94, Filter.matches :21-29).
std::vector<const ClipboardRecord*> ClipboardCanvas::visibleItems() const {
    auto out = ordered();
    std::erase_if(out, [&](const ClipboardRecord* r) {
        switch (filter_) {
        case Filter::all: return false;
        case Filter::text: return r->kind != ClipboardRecordKind::text;
        case Filter::link: return r->kind != ClipboardRecordKind::url;
        case Filter::image: return r->kind != ClipboardRecordKind::image;
        case Filter::file: return r->kind != ClipboardRecordKind::files;
        }
        return false;
    });
    return out;
}
const ClipboardRecord* ClipboardCanvas::item(std::uint64_t id) const {
    const auto it = std::find_if(snapshot_.items.begin(), snapshot_.items.end(), [&](const auto& r) { return r.id == id; });
    return it == snapshot_.items.end() ? nullptr : &*it;
}

// A hidden canvas rebuilds when it is next shown, not on every capture (:97-100).
void ClipboardCanvas::show(double now) {
    if (renderedLanguage() && renderedStructure_ && structure() != *renderedStructure_) { env().now = now; rerender(); }
    Canvas::show(now);
}

void ClipboardCanvas::storeChanged() {
    if (actions_.snapshot) snapshot_ = actions_.snapshot();
    if (focusedID_ && !item(*focusedID_)) focusedID_.reset();
    if (isActive() && (!renderedStructure_ || structure() != *renderedStructure_)) {
        std::map<std::uint64_t, double> before;
        for (const auto& [id, card] : cards_) before.emplace(id, card->layer().frame().y);
        rerender();
        // A pin or unpin slides the cards that moved to their new rows (0.3 s E).
        for (const auto& [id, card] : cards_) {
            const auto it = before.find(id);
            const double y = card->layer().frame().y;
            if (it == before.end() || it->second == y) continue;
            card->layer().setValue(d::Channel::translateY, it->second - y);
            d::motion::animate(card->layer(), d::Channel::translateY, 0, .3, clock());
        }
    }
    const auto status = snapshot_.status;
    const auto previous = std::exchange(lastStatus_, status);
    if (!isActive() || copying_ || !status || status == previous) return;
    toast(d::Icon::warn, "Notice", "提示", *status);
}

// --- Page (:122-185) ----------------------------------------------------------------
void ClipboardCanvas::buildPage(d::Page& page) {
    const auto language = env().language;
    renderedStructure_ = structure();
    cards_.clear();
    const auto visible = visibleItems();
    auto& clear = page.make<d::Button>("clipboard.clear", text("Clear", "清空"), d::Icon::trash, d::Button::Kind::secondary, d::Button::Height::compact);
    clear.place({0, 0});
    clear.setEnabled(std::any_of(snapshot_.items.begin(), snapshot_.items.end(), [](const auto& r) { return !r.pinned; }));
    clear.onActivate = [this] { confirmClear(); };
    header().configure({"CLIPBOARD"}, "MOD-03", title("Clipboard", "剪贴板缓存"),
        d::format::pad2(std::int64_t(snapshot_.items.size())) + "/" + d::format::pad2(std::int64_t(snapshot_.capacity)), false, {&clear});

    auto& tabs = page.makeTabs("clipboard.filter", {{text("All", "全部"), std::nullopt}, {text("Text", "文字"), std::nullopt},
        {text("Link", "链接"), std::nullopt}, {text("Image", "图片"), std::nullopt}, {text("File", "文件"), std::nullopt}},
        int(filter_), d::metric::boxWidth);
    tabs.onSelect = [this](int index, double direction) {
        if (index < 0 || index >= int(filterCount)) return;
        filter_ = Filter(index);
        navigate(direction);
    };
    tabs.layer().setOrigin({0, d::metric::headerHeight});
    page.layer().add(tabs.layer());
    page.tabs = &tabs;
    page.add(tabs.controls());
    page.stagger(tabs.layer());

    // `.rows.scroll`: at most 350 tall, 58 px cards 6 apart, 6 px right padding.
    const double top = d::metric::headerHeight + 34 + 10;
    const double contentHeight = visible.empty() ? 250 : double(visible.size()) * cardPitch - 6;
    const double height = std::min(350.0, contentHeight);
    auto& scroll = page.scrollArea("clipboard.rows", {0, top, d::metric::boxWidth, height});
    list_ = &scroll;
    const double width = d::metric::boxWidth - 6 - (contentHeight > height ? 4 : 0);
    if (visible.empty()) {
        auto& empty = d::emptyState::bilingual(page.builder(), "clipboard.empty", "NO RECORDS", "剪贴板为空", {width, 250});
        empty.setOrigin({0, 0});
        scroll.content().add(empty);
        page.stagger(empty);
    }
    const double date = wallClock_ ? wallClock_() : 0;
    const bool recording = env().recordingMode;
    for (std::size_t index = 0; index < visible.size(); ++index) {
        const ClipboardRecord& record = *visible[index];
        const std::uint64_t id = record.id;
        // Recording mode keeps the title from VoiceOver too.
        const std::string spoken = recording ? text("Redacted", "已隐藏") : recordTitle(record, language);
        const std::string prefix = "clip:" + std::to_string(index);
        // Row actions (light 24 px icon buttons): pin is lit while pinned; delete is off for a pinned record.
        auto& pin = page.make<d::IconButton>(prefix + ":pin", (record.pinned ? text("Unpin: ", "取消固定：") : text("Pin: ", "固定：")) + spoken,
            d::Icon::pin, d::IconButton::Style::light, false, false, 24.0, 13.0);
        pin.setOn(record.pinned);
        pin.onActivate = [this, id] { togglePin(id); };
        auto& del = page.make<d::IconButton>(prefix + ":delete", text("Delete: ", "删除：") + spoken, d::Icon::trash,
            d::IconButton::Style::light, false, false, 24.0, 13.0);
        del.setEnabled(!record.pinned);
        del.onActivate = [this, id] { remove(id); };
        auto& card = page.make<d::ListCard>(prefix, cardKind(record.kind), recordTitle(record, language), meta(record, language),
            age(record.createdAt, date, language), record.pinned, width, recording ? std::nullopt : record.thumbnail,
            text("COPY", "复制"), recording, std::vector<d::Control*>{&pin, &del});
        card.label = text("Copy: ", "复制：") + spoken;
        card.setFocused(focusedID_ == id);
        card.onActivate = [this, id] { copy(id); };
        // As on the Mac (:176-178), frame.origin is set after the focus lift
        // (translation.x 3): the frame keeps x 0, so a card built focused
        // shows unlifted and sits 3 px left once the focus leaves.
        card.layer().setOrigin({-card.layer().value(d::Channel::translateX), double(index) * cardPitch});
        scroll.content().add(card.layer());
        page.add(card.controls());
        page.stagger(card.layer());
        cards_.emplace_back(id, &card);
    }
    scroll.setContentHeight(contentHeight);
    page.restoreOffset(scroll);
}

void ClipboardCanvas::refreshAges() {
    const double date = wallClock_ ? wallClock_() : 0;
    for (const auto& [id, card] : cards_)
        if (const auto* record = item(id)) card->setAge(age(record->createdAt, date, env().language));
}

// --- Actions (:197-243) -------------------------------------------------------------
void ClipboardCanvas::copy(std::uint64_t id) {
    if (!item(id)) return;
    setFocus(id);
    copying_ = true;
    bool success = false;
    try { success = actions_.copy && actions_.copy(id); } catch (...) { copying_ = false; throw; }
    copying_ = false;
    // The store reports synchronously on the Mac; here the status is read back now.
    if (actions_.snapshot) snapshot_.status = actions_.snapshot().status;
    lastStatus_ = snapshot_.status;
    if (success) {
        for (const auto& [cardID, card] : cards_) if (cardID == id) { card->flashCopied(); break; }
        toast(d::Icon::copy, "Copied", "已复制到剪贴板");
    } else {
        toast(d::Icon::warn, "Notice", "提示", snapshot_.status.value_or(text("Could not restore this item", "无法恢复此项目")));
    }
}
void ClipboardCanvas::togglePin(std::uint64_t id) {
    if (actions_.togglePin && actions_.togglePin(id)) storeChanged();
}
// Deletes one unpinned record (pinned records are protected, :214-223).
void ClipboardCanvas::remove(std::uint64_t id) {
    const auto* record = item(id);
    if (!record || record->pinned) return;
    const auto visible = visibleItems();
    if (focusedID_ == id) {
        const auto at = std::find_if(visible.begin(), visible.end(), [&](const auto* r) { return r->id == id; });
        if (at != visible.end()) {
            const auto index = std::size_t(at - visible.begin());
            std::vector<const ClipboardRecord*> rest;
            for (const auto* r : visible) if (r->id != id) rest.push_back(r);
            if (rest.empty()) focusedID_.reset(); else focusedID_ = rest[std::min(index, rest.size() - 1)]->id;
        }
    }
    if (actions_.remove && actions_.remove(id)) storeChanged();
}
void ClipboardCanvas::confirmClear() {
    const auto removable = std::count_if(snapshot_.items.begin(), snapshot_.items.end(), [](const auto& r) { return !r.pinned; });
    const auto pinned = std::int64_t(snapshot_.items.size()) - removable;
    if (removable <= 0) return;
    const std::string count = number(removable), kept = number(pinned);
    const std::array<std::string_view, 2> args{count, kept};
    const std::string message = pinned > 0 ? text("{0} records / {1} pinned kept", "{0} 条记录 / 保留 {1} 条固定记录", args)
                                           : text("{0} records", "{0} 条记录", std::span(args.data(), 1));
    d::ConfirmRequest request;
    request.id = "clipboard.clear"; request.title = text("Clear clipboard history?", "清空剪贴板记录？"); request.message = message;
    request.confirmTitle = text("Clear", "清空"); request.danger = true; request.icon = d::Icon::trash;
    confirm(std::move(request), [this] {
        if (!actions_.clearUnpinned || !actions_.clearUnpinned()) return;
        storeChanged();
        toast(d::Icon::trash, "History cleared", "剪贴板记录已清空");
    });
}
// Selects a filter tab the way the tab bar does (verification and tests, :238-243).
void ClipboardCanvas::selectFilter(Filter next) {
    if (next == filter_) return;
    const double direction = int(next) > int(filter_) ? 1 : -1;
    filter_ = next;
    navigate(direction);
}

// --- Keyboard and context menu (:247-302) -------------------------------------------
bool ClipboardCanvas::handleKey(const d::KeyInput& key) {
    if (key.commandModifiers() || key.shift) return false;
    switch (key.virtualKey) {
    case d::key::down: moveFocus(1); return true;
    case d::key::up: moveFocus(-1); return true;
    case d::key::enter: if (focusedID_) copy(*focusedID_); return true;          // Return and keypad Enter
    case d::key::back: case d::key::deleteForward: if (focusedID_) remove(*focusedID_); return true;
    default:
        if (key.character < U'1' || key.character > U'6') return false;
        copyVisible(std::size_t(key.character - U'1'));
        return true;
    }
}
void ClipboardCanvas::moveFocus(int step) {
    const auto visible = visibleItems();
    if (visible.empty()) return;
    std::optional<int> current;
    if (focusedID_) for (std::size_t n = 0; n < visible.size(); ++n) if (visible[n]->id == *focusedID_) current = int(n);
    const int last = int(visible.size()) - 1;
    const int index = std::min(last, std::max(0, current ? *current + step : (step < 0 ? last : 0)));
    setFocus(visible[std::size_t(index)]->id);
    if (!list_) return;
    for (const auto& [id, card] : cards_) if (id == focusedID_) { list_->reveal(card->layer().frame()); break; }
}
// 1-6 copy the records currently in view, top to bottom (:272-278).
void ClipboardCanvas::copyVisible(std::size_t index) {
    if (!list_) return;
    const auto bounds = list_->layer().bounds();
    const d::Rect viewport{0, list_->offset(), bounds.width, bounds.height};
    std::vector<std::uint64_t> shown;
    for (const auto& [id, card] : cards_) {
        const d::Rect f = card->layer().frame();
        // CGRect.intersects: a positive-area overlap.
        if (f.x < viewport.x + viewport.width && viewport.x < f.x + f.width && f.y < viewport.y + viewport.height && viewport.y < f.y + f.height)
            shown.push_back(id);
    }
    if (index < shown.size()) copy(shown[index]);
}
void ClipboardCanvas::setFocus(std::optional<std::uint64_t> id) {
    focusedID_ = id;
    for (const auto& [cardID, card] : cards_) card->setFocused(cardID == id);
}
bool ClipboardCanvas::handleRightPointerDown(d::Point point) {
    std::optional<std::uint64_t> hit;
    for (const auto& [id, card] : cards_) {
        const auto& layer = card->layer();
        if (!d::isVisible(layer, root()) || !d::isUnclipped(layer, point, root())) continue;
        const auto local = d::convert(point, root(), layer);
        const auto b = layer.bounds();
        if (local && local->x >= 0 && local->y >= 0 && local->x < b.width && local->y < b.height) { hit = id; break; }
    }
    const auto* record = hit ? item(*hit) : nullptr;
    if (!record) return false;
    const std::uint64_t id = *hit;
    setFocus(id);
    std::vector<d::Menu::Item> items;
    items.push_back({.id = "copy", .title = text("Copy", "复制")});
    items.push_back({.id = "pin", .title = record->pinned ? text("Unpin", "取消固定") : text("Pin", "固定")});
    items.push_back({.id = "delete", .title = text("Delete", "删除"), .danger = true, .separatorBefore = true, .enabled = !record->pinned});
    openMenu("clipboard.menu", std::move(items), d::Rect{point.x, point.y, 1, 1}, nullptr, 150.0, [this, id](const d::Menu::Item& pick) {
        if (pick.id == "copy") copy(id);
        else if (pick.id == "pin") togglePin(id);
        else remove(id);
    });
    return true;
}

// --- Formatting (spec 2.14, 3.8; :306-347) --------------------------------------------
d::ListCard::Kind ClipboardCanvas::cardKind(ClipboardRecordKind kind) noexcept {
    switch (kind) {
    case ClipboardRecordKind::text: return d::ListCard::Kind::text;
    case ClipboardRecordKind::url: return d::ListCard::Kind::link;
    case ClipboardRecordKind::image: return d::ListCard::Kind::image;
    case ClipboardRecordKind::files: return d::ListCard::Kind::file;
    }
    return d::ListCard::Kind::text;
}
std::string ClipboardCanvas::recordTitle(const ClipboardRecord& record, core::Language language) {
    return record.kind == ClipboardRecordKind::image ? core::localized("Image", "图片", language) : record.preview;
}
// The meta line keeps its triple spaces: `Notes   28 chars`, `Safari`,
// `PNG   2880 × 1872`, `Finder   48.2 MB`.
std::string ClipboardCanvas::meta(const ClipboardRecord& record, core::Language language) {
    const auto& details = record.details;
    std::vector<std::optional<std::string>> parts;
    const auto count = [&](std::string_view en, std::string_view zh, std::int64_t n) {
        const std::string value = number(n);
        const std::array<std::string_view, 1> args{value};
        return core::localized(en, zh, language, args);
    };
    switch (record.kind) {
    case ClipboardRecordKind::text:
        parts = {details.source, details.characters ? std::optional(count("{0} chars", "{0} 字符", *details.characters)) : std::nullopt};
        break;
    case ClipboardRecordKind::url: parts = {details.source ? details.source : host(record.preview)}; break;
    case ClipboardRecordKind::image:
        parts = {details.format, details.pixelWidth && details.pixelHeight
            ? std::optional(number(*details.pixelWidth) + " × " + number(*details.pixelHeight)) : std::nullopt};
        break;
    case ClipboardRecordKind::files: {
        std::string extent;
        if (details.fileCount && *details.fileCount > 1) extent = count("{0} items", "{0} 项", *details.fileCount);
        else if (details.byteCount) extent = fileSize(*details.byteCount);
        else extent = core::localized("Folder", "文件夹", language);
        parts = {details.source, extent};
        break;
    }
    }
    std::string out;
    for (const auto& part : parts) {
        if (!part || part->empty()) continue;
        if (!out.empty()) out += "   ";
        out += *part;
    }
    return out;
}
// `NOW`, `2 MIN`, `1 H`, `3 D`.
std::string ClipboardCanvas::age(double createdAt, double now, core::Language language) {
    const double elapsed = now - createdAt;
    const std::int64_t seconds = std::isfinite(elapsed) && elapsed > 0 ? std::int64_t(std::min(elapsed, 9.2e18)) : 0;
    if (seconds < 60) return core::localized("NOW", "刚刚", language);
    const auto value = [&](std::string_view en, std::string_view zh, std::int64_t n) {
        const std::string text = number(n);
        const std::array<std::string_view, 1> args{text};
        return core::localized(en, zh, language, args);
    };
    const std::int64_t minutes = seconds / 60;
    if (minutes < 60) return value("{0} MIN", "{0} 分钟", minutes);
    const std::int64_t hours = minutes / 60;
    if (hours < 24) return value("{0} H", "{0} 小时", hours);
    return value("{0} D", "{0} 天", hours / 24);
}
// ByteCountFormatter (.file, adaptive, en_US): 1000-based; bytes as words,
// KB without fraction digits, MB with 1, GB and larger with 2; trailing
// fraction zeros dropped; the shown digit rounds half up (1500 -> 2 KB,
// 2500 -> 3 KB; Mac oracle clipboardFormat.fileSizes), and a value that
// rounds to 1000 moves to the next unit (999,500 -> 1 MB).
std::string ClipboardCanvas::fileSize(std::int64_t bytes) {
    if (bytes == 0) return "Zero KB";
    if (bytes == 1) return "1 byte";
    if (bytes > -1000 && bytes < 1000) return std::to_string(bytes) + " bytes";
    static constexpr std::array<const char*, 7> units{"KB", "MB", "GB", "TB", "PB", "EB", "ZB"};
    static constexpr std::array<int, 7> digits{0, 1, 2, 2, 2, 2, 2};
    double value = double(bytes) / 1000;
    std::size_t unit = 0;
    while (unit + 1 < units.size() && std::abs(value) >= 1000) { value /= 1000; ++unit; }
    const double scale = std::pow(10.0, digits[unit]);
    double shown = std::round(value * scale) / scale;
    if (std::abs(shown) >= 1000 && unit + 1 < units.size()) { value /= 1000; ++unit; shown = std::round(value * std::pow(10.0, digits[unit])) / std::pow(10.0, digits[unit]); }
    char text[64]{};
    std::snprintf(text, sizeof(text), "%.*f", digits[unit], shown);
    std::string out = text;
    if (out.find('.') != std::string::npos) {
        while (out.back() == '0') out.pop_back();
        if (out.back() == '.') out.pop_back();
    }
    return out + " " + units[unit];
}

// --- Test hooks ----------------------------------------------------------------------------
std::vector<std::string> ClipboardCanvas::cardTitlesForTesting() const {
    std::vector<std::string> out;
    for (const auto& [id, card] : cards_) out.push_back(card->label);
    return out;
}
std::optional<std::size_t> ClipboardCanvas::focusedCardIndexForTesting() const {
    for (std::size_t n = 0; n < cards_.size(); ++n) if (cards_[n].second->focused()) return n;
    return std::nullopt;
}
} // namespace endfield::modules
